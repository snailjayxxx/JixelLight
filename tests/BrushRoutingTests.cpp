#include <QtTest>
#include <QColorSpace>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QRgba64>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <cstring>
#include "core/cache/RenderedPreviewCache.h"
#include "core/export/ExportQueue.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/pipeline/StageGraph.h"
#include "core/look/LookProfiles.h"

namespace {
QImage sourceImage() {
    QImage image(2203,1207,QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(
            4000+x*17,6000+y*23,8000+(x+y)*9,65535);
    return image;
}
AdjustmentState painted() {
    AdjustmentState state;state.exposure=-.4;state.saturation=11;
    state.look.mode="manual";state.look.code="FL";state.look.parameters={{"clarity",3},{"sharpness",4}};
    BrushMaskGeometry::Stroke stroke;stroke.style={.025,.43,.71};
    for(int i=0;i<13;++i)stroke.dabs.push_back({{.21+i*.041,.32+i*.019},.6+(i%3)*.1});
    state.brushMasks.layers={{"route",true,.8,{stroke}}};return state;
}
QImage reference(const QImage &source,const ProcessingPlan &plan) {
    QImage result(source.size(),QImage::Format_RGBA64);result.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<source.height();y+=128) {
        const auto strip=ImagePipeline::processRegion(source,plan,{0,y,source.width(),std::min(128,source.height()-y)});
        if(strip.isNull())return {};
        for(int row=0;row<strip.height();++row)std::memcpy(result.scanLine(y+row),strip.constScanLine(row),std::size_t(source.width())*8);
    }
    return result;
}
QByteArray bytes(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
QByteArray fileHash(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);return hash.addData(&file)?hash.result().toHex():QByteArray{};}
}
class BrushRoutingTests final:public QObject {
    Q_OBJECT
private slots:
    void cpuPreviewCachePreservesIntentAndLeasesAcrossHitsEvictionAndRefusal() {
        const auto source=sourceImage();auto state=painted();state.geometry.straighten=.7;state.geometry.crop={.02,.03,.93,.91};
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::DisplayP3,false,0);
        const auto expected=reference(source,plan);QVERIFY(!expected.isNull());QVERIFY(ImagePipeline::processWithPlan(source,plan).isNull());
        auto budget=std::make_shared<BrushRenderBudget>();QImage retained;QString error;
        {
            RenderedPreviewCache cache(expected.sizeInBytes()*3/2,budget);
            auto first=cache.render(source,plan,{},&error);QVERIFY2(!first.isNull(),qPrintable(error));QCOMPARE(first,expected);QCOMPARE(first.colorSpace(),expected.colorSpace());
            QCOMPARE(budget->snapshot().charged,first.sizeInBytes());auto hit=cache.render(source,plan,{},&error);
            QCOMPARE(hit.constBits(),first.constBits());QCOMPARE(cache.snapshot()["hits"].toInteger(),1);
            auto changed=plan;changed.state.geometry.flipHorizontal=true;
            QVERIFY(cache.render(source,changed,{},&error).isNull());QVERIFY(error.contains("original intent"));QCOMPARE(cache.snapshot()["hits"].toInteger(),1);
            auto next=state;next.exposure=.3;const auto nextPlan=ProcessingPlan::compile(next,plan.encoding,plan.output,false,0);
            auto second=cache.render(source,nextPlan,{},&error);QVERIFY2(!second.isNull(),qPrintable(error));QCOMPARE(second,reference(source,nextPlan));
            QCOMPARE(cache.snapshot()["entries"].toInt(),1);QCOMPARE(cache.snapshot()["evictions"].toInteger(),1);
            QCOMPARE(budget->snapshot().charged,first.sizeInBytes()+second.sizeInBytes());retained=first;
        }
        QCOMPARE(budget->snapshot().charged,retained.sizeInBytes());QCOMPARE(retained,expected);retained={};QCOMPARE(budget->snapshot().charged,0);
        auto deniedBudget=std::make_shared<BrushRenderBudget>(1);RenderedPreviewCache denied(64*1024*1024,deniedBudget);
        QVERIFY(denied.render(source,plan,{},&error).isNull());QVERIFY(error.contains("memory budget"));QCOMPARE(denied.snapshot()["entries"].toInt(),0);QCOMPARE(deniedBudget->snapshot().charged,0);
        auto lut=std::make_shared<LookLut>(*LookLut::identity(2));lut->rgb.reserve(262144);
        auto calibrated=state;calibrated.look.mode="calibrated";calibrated.look.lut=lut;
        const auto lutPlan=ProcessingPlan::compile(calibrated,plan.encoding,plan.output,false,0);
        {RenderedPreviewCache withLut(64*1024*1024,budget);QVERIFY(!withLut.render(source,lutPlan,{},&error).isNull());
            const auto charged=(expected.sizeInBytes()+qint64(lut->rgb.capacity())*sizeof(float)+1023)/1024*1024;
            QCOMPARE(withLut.snapshot()["charged_bytes"].toInteger(),charged);}
        QCOMPARE(budget->snapshot().charged,0);
    }
    void queuedRealRawAsShotLoadsSourceAndMatchesIndependentTiffPixels() {
        const auto fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()) {
            if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required public Sony RAW unavailable");
            QSKIP("No public Sony dataset configured; not real queued RAW acceptance");
        }
        const auto path=QDir(fixtures).filePath("sony_a7_iv_07.arw");
        const QByteArray hash="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";QCOMPARE(fileHash(path),hash);
        const auto source=loadSourceFresh(path,{});QVERIFY2(!source.image.isNull(),qPrintable(source.error));QCOMPARE(source.image.size(),QSize(7028,4688));QVERIFY(source.fullResolution);
        const auto before=StageGraph::outputFingerprint(source.image);auto state=painted();state.look={};state.look.mode="as-shot";
        state.geometry.crop={.07,.05,.5,.5};state.geometry.straighten=.7;state.geometry.quarterTurns=1;
        const auto resolved=LookProfiles::resolveAsShot(state,source.metadata,true);
        double base=source.metadata.value("rawBaseExposureStops",source.metadata.value("rawBaselineExposure",0.0)).toDouble();if(!std::isfinite(base))base=0;base=std::clamp(base,-8.,8.);
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto target=dir.filePath("owned-raw.tif");
        auto budget=std::make_shared<BrushRenderBudget>();auto cache=std::make_shared<SourceCache>(32*1024*1024,dir.filePath("cache"));
        ExportQueue queue(cache,nullptr,budget);QSignalSpy done(&queue,&ExportQueue::finished),files(&queue,&ExportQueue::fileFinished);
        QVERIFY(queue.start({{path,target,{},state,ColorManagement::OutputSpace::DisplayP3,100,true}}));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,90000);QCOMPARE(done[0][0].toInt(),1);QCOMPARE(done[0][1].toInt(),0);QVERIFY2(files[0][2].toBool(),qPrintable(files[0][3].toString()));
        const auto prepared=state.geometry.apply(source.image);QVERIFY(!prepared.isNull());
        const auto expected=reference(prepared,ProcessingPlan::compile(resolved,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::DisplayP3,true,float(base)));
        const QImage actual(target);QCOMPARE(actual.size(),expected.size());QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);QCOMPARE(actual.colorSpace(),expected.colorSpace());
        QCOMPARE(budget->snapshot().charged,0);QCOMPARE(fileHash(path),hash);QCOMPARE(StageGraph::outputFingerprint(source.image),before);
        qInfo().noquote()<<"BRUSH_ROUTING_EVIDENCE "<<QJsonDocument(QJsonObject{
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"case","queued-real-raw-as-shot-tiff"},
            {"source_override",false},{"raw_sha256",QString::fromLatin1(hash)},{"source_pixels",qint64(source.image.width())*source.image.height()},
            {"output_pixels",qint64(actual.width())*actual.height()},{"output_space","display-p3"},{"look_mode",resolved.look.mode},{"look_code",resolved.look.code},
            {"raw_base_ev",base},{"exact_pixels",true},{"icc_exact",true},{"source_unchanged",true},
            {"operation_end_charged_bytes",budget->snapshot().charged},{"operation_peak_reserved_bytes",budget->snapshot().peak},
            {"native_float_raw_decoder",false},{"native_gpu_brush",false},{"producer","cpu-reference"}}).toJson(QJsonDocument::Compact);
    }
    void queuedEnabledBrushExportsAllFormatsWithFrozenStateAndIcc() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto source=sourceImage();auto state=painted();
        const auto original=dir.filePath("source.png");QVERIFY(source.save(original,"PNG"));const auto before=bytes(original);
        QVector<ExportRequest> requests;QStringList extensions{"png","tif","webp","jpg"};
        const std::array spaces{ColorManagement::OutputSpace::ProPhotoRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::SRgb};
        for(int i=0;i<4;++i)requests.append({original,dir.filePath("rendered."+extensions[i]),source,state,spaces[std::size_t(i)],100,true});
        auto budget=std::make_shared<BrushRenderBudget>();ExportQueue queue(std::make_shared<SourceCache>(),nullptr,budget);
        QSignalSpy done(&queue,&ExportQueue::finished),files(&queue,&ExportQueue::fileFinished);QVERIFY(queue.start(requests));
        requests[0].state.exposure=4;state.brushMasks.layers.clear();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);QCOMPARE(done[0][0].toInt(),4);QCOMPARE(done[0][1].toInt(),0);QVERIFY(!done[0][2].toBool());QCOMPARE(files.count(),4);
        for(int i=0;i<4;++i) {
            QVERIFY2(files[i][2].toBool(),qPrintable(files[i][3].toString()));const auto actual=QImage(dir.filePath("rendered."+extensions[i]));QVERIFY(!actual.isNull());
            const auto expected=reference(source,ProcessingPlan::compile(painted(),ImagePipeline::InputEncoding::LinearProPhoto,spaces[std::size_t(i)],false,0));
            QCOMPARE(actual.colorSpace(),expected.colorSpace());
            if(i<2)QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);
            else if(i==2)QCOMPARE(actual.convertToFormat(QImage::Format_RGBA8888),expected.convertToFormat(QImage::Format_RGBA8888));
            else {
                const auto a=actual.convertToFormat(QImage::Format_RGB888),b=expected.convertToFormat(QImage::Format_RGB888);int maximum=0;
                for(int y=0;y<a.height();++y)for(int x=0;x<a.width()*3;++x)maximum=std::max(maximum,std::abs(int(a.constScanLine(y)[x])-int(b.constScanLine(y)[x])));
                QVERIFY2(maximum<=8,qPrintable(QString::number(maximum)));
            }
        }
        QCOMPARE(budget->snapshot().charged,0);QCOMPARE(bytes(original),before);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{});
    }
    void queueBudgetRefusalAndRealStripCancelPreserveTargetThenRecover() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto source=sourceImage();const auto state=painted();QString error;
        BrushRenderRequest::Spec spec;auto measured=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(measured,qPrintable(error));
        auto needed=measured->reservedBytes();measured.reset();auto streamedSpec=spec;streamedSpec.layout=BrushRenderRequest::Layout::Stripes128;
        measured=BrushRenderRequest::create(source,state,streamedSpec,{},&error);QVERIFY2(measured,qPrintable(error));needed=std::max(needed,measured->reservedBytes());measured.reset();
        auto budget=std::make_shared<BrushRenderBudget>(needed);
        auto blocker=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(blocker,qPrintable(error));
        ExportQueue queue(std::make_shared<SourceCache>(),nullptr,budget);QSignalSpy done(&queue,&ExportQueue::finished),files(&queue,&ExportQueue::fileFinished);
        const auto target=dir.filePath("owned-output.png");const QByteArray sentinel="existing-owned-output";
        {QFile file(target);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(sentinel),sentinel.size());}
        ExportRequest request{dir.filePath("owned-source.png"),target,source,state,ColorManagement::OutputSpace::SRgb,100,false};
        QVERIFY(queue.start({request}));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);QCOMPARE(done[0][1].toInt(),1);QVERIFY(files[0][3].toString().contains("memory budget"));QCOMPARE(bytes(target),sentinel);
        blocker.reset();QCOMPARE(budget->snapshot().charged,0);done.clear();files.clear();
        QVERIFY(queue.start({request}));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);QCOMPARE(done[0][0].toInt(),1);QVERIFY(bytes(target)!=sentinel);QCOMPARE(budget->snapshot().charged,0);
        const auto jpeg=dir.filePath("owned-output.jpg");{QFile file(jpeg);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(sentinel),sentinel.size());}
        request.destination=jpeg;done.clear();files.clear();int cancelledAt=0;
        const auto connection=connect(&queue,&ExportQueue::progress,this,[&](int,int,int percent,const QString &){if(!cancelledAt&&percent>0&&percent<100){cancelledAt=percent;queue.cancel();}});
        QVERIFY(queue.start({request}));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);disconnect(connection);
        QVERIFY(cancelledAt>0&&cancelledAt<100);QVERIFY(done[0][2].toBool());QCOMPARE(bytes(jpeg),sentinel);QCOMPARE(budget->snapshot().charged,0);
        done.clear();files.clear();QVERIFY(queue.start({request}));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);QCOMPARE(done[0][0].toInt(),1);QVERIFY(bytes(jpeg)!=sentinel);QCOMPARE(budget->snapshot().charged,0);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{});
    }
};
QTEST_GUILESS_MAIN(BrushRoutingTests)
#include "BrushRoutingTests.moc"
