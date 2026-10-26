#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include "core/pipeline/BrushRenderRequest.h"
#include "core/cache/SourceCache.h"
#include "core/export/JpegExporter.h"
#include "core/export/RasterExporter.h"
#include "core/scopes/ScopesEngine.h"
#include "diagnostics/PerformanceRecorder.h"

namespace {
constexpr auto SonyHash="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";
QByteArray fileHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()){const auto chunk=file.read(1024*1024);if(chunk.isEmpty())return {};hash.addData(chunk);}return hash.result().toHex();
}
QByteArray imageHash(const QImage &image) {
    QCryptographicHash hash(QCryptographicHash::Sha256);const auto visible=qsizetype(image.width())*(image.format()==QImage::Format_RGBA32FPx4?16:8);
    for(int y=0;y<image.height();++y)hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),visible));return hash.result().toHex();
}
struct MemorySample {
    std::atomic_bool stop{false};std::atomic<qint64> peak{0};
    std::thread worker{[this]{while(!stop.load(std::memory_order_relaxed)){
        const auto rss=PerformanceRecorder::residentBytes();auto seen=peak.load(std::memory_order_relaxed);
        while(seen<rss&&!peak.compare_exchange_weak(seen,rss,std::memory_order_relaxed)){}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }}};
    qint64 finish(){stop.store(true,std::memory_order_relaxed);if(worker.joinable())worker.join();return peak.load();}
    ~MemorySample(){finish();}
};
AdjustmentState painted(bool detail=true) {
    AdjustmentState state;state.exposure=-.55;state.highlights=-12;state.saturation=9;
    BrushMaskGeometry::Stroke one;one.style={.008,.43,.71};
    for(int i=0;i<19;++i)one.dabs.push_back({{.17+i*.031,.31+i*.009},.6+(i%3)*.1});
    BrushMaskGeometry::Stroke two;two.style={.013,.27,.51};two.dabs={{{.41,.52},.9},{{.42,.53},.7},{{.45,.55},.8}};
    state.brushMasks.layers={{"dodge",true,.85,{one}},{"burn",true,-.65,{two}}};
    if(detail){state.look.mode="manual";state.look.code="FL";state.look.parameters={{"sharpness",4},{"clarity",3}};}
    return state;
}
ProcessingPlan planFor(const AdjustmentState &state,const BrushRenderRequest::Spec &spec) {
    auto plan=ProcessingPlan::compile(state,spec.encoding,spec.output,spec.rawSource,spec.rawBaseExposureStops);plan.setFrameRect(spec.normalizedFrame);return plan;
}
bool compareLegacyStrips(const QImage &actual,const QImage &input,const ProcessingPlan &plan,const QRect &region,QString *error) {
    if(actual.size()!=region.size()||actual.format()!=QImage::Format_RGBA64){*error="Output size/format mismatch";return false;}
    for(int y=0;y<region.height();y+=128) {
        const auto expected=ImagePipeline::processRegion(input,plan,{region.x(),region.y()+y,region.width(),std::min(128,region.height()-y)});
        if(expected.isNull()||expected.colorSpace()!=actual.colorSpace()){*error="Independent bounded reference refused or ICC differs";return false;}
        for(int row=0;row<expected.height();++row)if(std::memcmp(expected.constScanLine(row),actual.constScanLine(y+row),std::size_t(region.width())*8)){
            *error=QString("Independent strip pixels differ at row %1").arg(y+row);return false;
        }
    }
    return true;
}
QImage legacyEncoded(const QImage &input,const ProcessingPlan &plan,QImage::Format format) {
    QImage output(input.size(),format);if(output.isNull())return {};
    output.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<input.height();y+=128) {
        auto strip=ImagePipeline::processRegion(input,plan,{0,y,input.width(),std::min(128,input.height()-y)});
        if(strip.isNull())return {};
        if(strip.format()!=format)strip=strip.convertToFormat(format);
        const auto bytes=qsizetype(input.width())*output.depth()/8;
        for(int row=0;row<strip.height();++row)std::memcpy(output.scanLine(y+row),strip.constScanLine(row),std::size_t(bytes));
    }
    return output;
}
void report(QJsonObject data,const BrushRenderBudget &budget,MemorySample &memory,QElapsedTimer &timer) {
    const auto snapshot=budget.snapshot();data["elapsed_ms"]=timer.elapsed();data["operation_budget_bytes"]=snapshot.limit;
    data["operation_peak_reserved_bytes"]=snapshot.peak;data["operation_end_charged_bytes"]=snapshot.charged;
    data["sampled_process_rss_peak_bytes"]=memory.finish();data["rss_sampling_ms"]=5;data["timing_gate"]=false;
    data["source_commit"]=QStringLiteral(JIXELLIGHT_GIT_COMMIT);data["engine"]=ProcessingPlan::EngineVersion;
    data["source_snapshot"]="deep-pixel-copy";
    data["native_float_raw_decoder"]=false;data["producer"]="cpu-reference";
    data["native_gpu_brush"]=false;data["gpu_upload_executed"]=false;
    qInfo().noquote()<<"BRUSH_LARGE_EVIDENCE "<<QJsonDocument(data).toJson(QJsonDocument::Compact);
}
}

class LargeBrushTests final:public QObject {
    Q_OBJECT
    QString m_checkedSony;
    bool sony(SourceData *source) {
        const auto path=QDir(qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES")).filePath("sony_a7_iv_07.arw");
        if(!QFileInfo::exists(path)) {
            if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QTest::qFail("Required pinned Sony fixture is absent",__FILE__,__LINE__);
            else QTest::qSkip("Sony fixtures not configured",__FILE__,__LINE__);
            return false;
        }
        if(fileHash(path)!=QByteArray(SonyHash)){QTest::qFail("Sony SHA-256 does not match the repository manifest",__FILE__,__LINE__);return false;}
        m_checkedSony=path;*source=loadSourceFresh(path,{});
        if(source->image.size()!=QSize(7028,4688)||source->image.format()!=QImage::Format_RGBA64||!source->fullResolution) {
            QTest::qFail(qPrintable(QString("Real Sony bitmap %1x%2 format%3 full%4: %5").arg(source->image.width()).arg(source->image.height()).arg(source->image.format()).arg(source->fullResolution).arg(source->error)),__FILE__,__LINE__);return false;
        }
        return true;
    }
    BrushRenderRequest::Spec sonySpec(const SourceData &source,ColorManagement::OutputSpace space=ColorManagement::OutputSpace::SRgb) {
        BrushRenderRequest::Spec spec;spec.rawSource=true;spec.output=space;spec.rawBaseExposureStops=float(source.metadata.value("rawBaseExposureStops",0).toDouble());return spec;
    }
private slots:
    void cleanup(){if(!m_checkedSony.isEmpty()){QCOMPARE(fileHash(m_checkedSony),QByteArray(SonyHash));m_checkedSony.clear();}}
    void float24MpGeometryWholeAndPannedUnevenRoiMatchIndependentStrips() {
        MemorySample memory;QElapsedTimer timer;timer.start();QImage source(6000,4000,QImage::Format_RGBA32FPx4);QVERIFY(!source.isNull());
        source.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
        for(int y=0;y<source.height();++y){auto *row=reinterpret_cast<float *>(source.scanLine(y));for(int x=0;x<source.width();++x){
            row[4*x]=float((x*17+y*7)%4096)/1800.f-.21f;row[4*x+1]=float((x*11+y*23)%4096)/1500.f;
            row[4*x+2]=float((x*3+y*19)%4096)/2100.f-.08f;row[4*x+3]=(x+y)%37?.71f:0.f;
        }}
        const auto before=imageHash(source);auto state=painted();state.geometry.straighten=.7;state.geometry.crop={.01,.02,.98,.96};state.geometry.quarterTurns=1;
        auto budget=std::make_shared<BrushRenderBudget>();BrushRenderRequest::Spec spec;spec.output=ColorManagement::OutputSpace::DisplayP3;QString error;
        auto whole=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(whole,qPrintable(error));auto output=whole->render({},&error);QVERIFY2(output,qPrintable(error));
        const auto input=state.geometry.apply(source);QVERIFY(!input.isNull());const auto pixels=quint64(output.image().width())*output.image().height();
        QVERIFY2(compareLegacyStrips(output.image(),input,planFor(state,spec),input.rect(),&error),qPrintable(error));
        output={};whole.reset();QCOMPARE(budget->snapshot().charged,0);
        spec.sourceStage=BrushRenderRequest::SourceStage::Prepared;spec.region={137,233,2511,1437};spec.normalizedFrame={.13,.07,.71,.81};
        auto roi=BrushRenderRequest::create(input,state,spec,{},&error,budget);QVERIFY2(roi,qPrintable(error));auto cropped=roi->render({},&error);QVERIFY2(cropped,qPrintable(error));
        QVERIFY2(compareLegacyStrips(cropped.image(),input,planFor(state,spec),spec.region,&error),qPrintable(error));
        spec.layout=BrushRenderRequest::Layout::Stripes128;auto streamed=BrushRenderRequest::create(input,state,spec,{},&error,budget);QVERIFY2(streamed,qPrintable(error));
        QVERIFY2(streamed->visitStrips([&](const BrushImageLease &strip,int y){for(int row=0;row<strip.image().height();++row)
            if(std::memcmp(strip.image().constScanLine(row),cropped.image().constScanLine(y+row),std::size_t(spec.region.width())*8))return false;return true;},{},&error),qPrintable(error));
        cropped={};roi.reset();streamed.reset();QCOMPARE(budget->snapshot().charged,0);QCOMPARE(imageHash(source),before);
        report({{"case","float24mp"},{"source_pixels",24000000},{"source_bytes",source.sizeInBytes()},{"rendered_pixels",qint64(pixels)},
            {"roi_pixels",qint64(spec.region.width())*spec.region.height()},{"max_channel_error",0},{"source_unchanged",true},{"float_signed_hdr_alpha",true}},*budget,memory,timer);
    }
    void sony33MpWholeAndRoiMatchIndependentStrips_data() {
        QTest::addColumn<QString>("space");QTest::newRow("srgb")<<"srgb";QTest::newRow("p3")<<"display-p3";QTest::newRow("prophoto")<<"prophoto-rgb";
    }
    void sony33MpWholeAndRoiMatchIndependentStrips() {
        QFETCH(QString,space);MemorySample memory;QElapsedTimer timer;timer.start();SourceData source;if(!sony(&source))return;
        const auto before=imageHash(source.image);const auto state=painted();auto spec=sonySpec(source,ColorManagement::fromKey(space));auto budget=std::make_shared<BrushRenderBudget>();QString error;
        auto whole=BrushRenderRequest::create(source.image,state,spec,{},&error,budget);QVERIFY2(whole,qPrintable(error));auto rendered=whole->render({},&error);QVERIFY2(rendered,qPrintable(error));
        QVERIFY2(compareLegacyStrips(rendered.image(),source.image,planFor(state,spec),source.image.rect(),&error),qPrintable(error));
        const QRect region(373,257,2901,1459);spec.region=region;spec.normalizedFrame={.11,.08,.73,.81};
        rendered={};whole.reset();auto roi=BrushRenderRequest::create(source.image,state,spec,{},&error,budget);QVERIFY2(roi,qPrintable(error));auto cropped=roi->render({},&error);QVERIFY2(cropped,qPrintable(error));
        QVERIFY2(compareLegacyStrips(cropped.image(),source.image,planFor(state,spec),region,&error),qPrintable(error));
        cropped={};roi.reset();QCOMPARE(budget->snapshot().charged,0);QCOMPARE(imageHash(source.image),before);
        report({{"case","sony33mp-render"},{"space",space},{"source_pixels",32947264},{"source_bytes",source.image.sizeInBytes()},
            {"raw_sha256",SonyHash},{"max_channel_error",0},{"source_unchanged",true},{"roi_pixels",qint64(region.width())*region.height()}},*budget,memory,timer);
    }
    void sony33MpSharedFullScopesAndPlotsMatchIndependentEncodedFrame() {
        MemorySample memory;QElapsedTimer timer;timer.start();SourceData source;if(!sony(&source))return;const auto before=imageHash(source.image);const auto state=painted();
        auto spec=sonySpec(source);spec.layout=BrushRenderRequest::Layout::Stripes128;auto budget=std::make_shared<BrushRenderBudget>();QString error;
        auto request=BrushRenderRequest::create(source.image,state,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));std::array<ScopePlotResult,3> plots;
        const auto histogram=ScopesEngine::analyzeAdmitted(*request,{},&error,&plots);QCOMPARE(histogram.pixelCount,quint64(32947264));
        const auto encoded=legacyEncoded(source.image,planFor(state,spec),QImage::Format_RGBA64);QVERIFY(!encoded.isNull());const auto expected=ScopesEngine::analyze(encoded);
        QCOMPARE(histogram.red,expected.red);QCOMPARE(histogram.green,expected.green);QCOMPARE(histogram.blue,expected.blue);QCOMPARE(histogram.luma,expected.luma);
        QCOMPARE(histogram.shadowClipPercent,expected.shadowClipPercent);QCOMPARE(histogram.highlightClipPercent,expected.highlightClipPercent);
        const auto ink=renderEncodedScopePlots(encoded);for(int i=0;i<3;++i){QCOMPARE(plots[i].pixels,quint64(32947264));QCOMPARE(plots[i].image,ink[i].image);}
        request.reset();QCOMPARE(budget->snapshot().charged,0);QCOMPARE(imageHash(source.image),before);
        report({{"case","sony33mp-scopes"},{"source_pixels",32947264},{"scope_pixels",qint64(histogram.pixelCount)},
            {"all_1024_bins_exact",true},{"all_three_plots_exact",true},{"source_unchanged",true},{"raw_sha256",SonyHash}},*budget,memory,timer);
    }
    void sony33MpRasterExportsMatchIndependentPixelsAndIcc_data() {
        QTest::addColumn<int>("format");QTest::addColumn<QString>("space");
        QTest::newRow("png16")<<int(RasterFormat::Png16)<<"prophoto-rgb";
        QTest::newRow("tiff16")<<int(RasterFormat::Tiff16)<<"display-p3";
        QTest::newRow("webp8")<<int(RasterFormat::WebP8)<<"srgb";
    }
    void sony33MpRasterExportsMatchIndependentPixelsAndIcc() {
        QFETCH(int,format);QFETCH(QString,space);MemorySample memory;QElapsedTimer timer;timer.start();SourceData source;if(!sony(&source))return;
        const auto before=imageHash(source.image);const auto state=painted();const auto raster=RasterFormat(format);auto spec=sonySpec(source,ColorManagement::fromKey(space));
        auto budget=std::make_shared<BrushRenderBudget>();QString error;auto request=BrushRenderRequest::create(source.image,state,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto suffix=raster==RasterFormat::Png16?"png":raster==RasterFormat::Tiff16?"tif":"webp";const auto path=dir.filePath("owned-output."+QString(suffix));
        QVERIFY2(exportRaster(*request,path,raster,100,{},&error),qPrintable(error));const auto fileBytes=QFileInfo(path).size();QVERIFY(fileBytes>0);
        QImageReader reader(path);QCOMPARE(reader.size(),QSize(7028,4688));auto decoded=reader.read();QVERIFY2(!decoded.isNull(),qPrintable(reader.errorString()));
        const auto referenceFormat=raster==RasterFormat::WebP8?QImage::Format_RGB888:QImage::Format_RGBA64;
        const auto expected=legacyEncoded(source.image,planFor(state,spec),referenceFormat);QVERIFY(!expected.isNull());
        QCOMPARE(decoded.colorSpace(),ColorManagement::colorSpace(spec.output));QCOMPARE(decoded.convertToFormat(referenceFormat),expected);
        request.reset();QCOMPARE(budget->snapshot().charged,0);QCOMPARE(imageHash(source.image),before);
        report({{"case","sony33mp-raster"},{"format",suffix},{"space",space},{"source_pixels",32947264},
            {"decoded_pixels",32947264},{"exact_pixels",true},{"icc_exact",true},{"encoded_file_bytes",fileBytes},
            {"source_unchanged",true},{"raw_sha256",SonyHash}},*budget,memory,timer);
    }
    void sony33MpJpegCancellationPreservesTargetAndHealthyOutputMatchesCpu() {
        MemorySample memory;QElapsedTimer timer;timer.start();SourceData source;if(!sony(&source))return;const auto before=imageHash(source.image);const auto state=painted();
        auto spec=sonySpec(source);spec.layout=BrushRenderRequest::Layout::Stripes128;auto budget=std::make_shared<BrushRenderBudget>();QString error;
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("owned-output.jpg");
        {QFile target(path);QVERIFY(target.open(QIODevice::WriteOnly));QCOMPARE(target.write("keep-original-target"),20);}
        auto cancelledRequest=BrushRenderRequest::create(source.image,state,spec,{},&error,budget);QVERIFY2(cancelledRequest,qPrintable(error));
        auto cancel=std::make_shared<std::atomic_bool>(false);int progress=0;
        QVERIFY(!exportJpegTiled(*cancelledRequest,path,100,cancel,&error,[&](int value){progress=value;if(value>0)cancel->store(true);}));
        QVERIFY(progress>0);QVERIFY(error.contains("Cancelled"));
        {QFile target(path);QVERIFY(target.open(QIODevice::ReadOnly));QCOMPARE(target.readAll(),QByteArray("keep-original-target"));}
        QCOMPARE(QDir(dir.path()).entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot),QStringList{"owned-output.jpg"});cancelledRequest.reset();QCOMPARE(budget->snapshot().charged,0);
        auto healthy=BrushRenderRequest::create(source.image,state,spec,{},&error,budget);QVERIFY2(healthy,qPrintable(error));QVERIFY2(exportJpegTiled(*healthy,path,100,{},&error),qPrintable(error));
        const auto fileBytes=QFileInfo(path).size();QImage decoded(path);QVERIFY(!decoded.isNull());QCOMPARE(decoded.size(),source.image.size());QCOMPARE(decoded.colorSpace(),ColorManagement::colorSpace(spec.output));
        decoded=decoded.convertToFormat(QImage::Format_RGB888);const auto expected=legacyEncoded(source.image,planFor(state,spec),QImage::Format_RGB888);QVERIFY(!expected.isNull());
        int maximum=0;
        for(int y=0;y<expected.height();++y){const auto *a=decoded.constScanLine(y),*b=expected.constScanLine(y);for(int x=0;x<expected.width()*3;++x)maximum=std::max(maximum,std::abs(int(a[x])-int(b[x])));}
        QVERIFY2(maximum<=5,qPrintable(QString("JPEG quality100 maximum channel error %1 exceeds 5/255").arg(maximum)));
        healthy.reset();QCOMPARE(budget->snapshot().charged,0);QVERIFY(cancel->load());QCOMPARE(imageHash(source.image),before);
        report({{"case","sony33mp-jpeg"},{"source_pixels",32947264},{"decoded_pixels",32947264},{"icc_exact",true},
            {"jpeg_quality",100},{"max_jpeg_error_255",maximum},{"jpeg_error_limit_255",5},{"cancel_progress_percent",progress},
            {"cancel_preserved_target",true},{"encoded_file_bytes",fileBytes},{"source_unchanged",true},{"raw_sha256",SonyHash}},*budget,memory,timer);
    }
};
QTEST_MAIN(LargeBrushTests)
#include "LargeBrushTests.moc"
