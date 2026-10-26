#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRgba64>
#include <cstring>
#include "core/gpu/GpuEngine.h"
#include "core/preview/PreviewTasks.h"
#include "core/pipeline/BrushMaskTileRaster.h"

namespace {
AdjustmentState painted() {
    AdjustmentState state;state.exposure=-.6;state.saturation=11;
    state.geometry.straighten=.7;state.geometry.crop={.01,.02,.97,.95};
    state.look.mode="manual";state.look.code="FL";state.look.parameters={{"contrast",2},{"clarity",3},{"sharpness",4}};
    BrushMaskGeometry::Stroke stroke;stroke.style={.018,.43,.71};
    for(int i=0;i<17;++i)stroke.dabs.push_back({{.21+i*.031,.32+i*.013},.6+(i%3)*.1});
    state.brushMasks.layers={{"preview",true,.9,{stroke}}};return state;
}
QByteArray fileHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))return {};return hash.result().toHex();
}
QByteArray imageHash(const QImage &image) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y=0;y<image.height();++y)hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),qsizetype(image.width())*image.depth()/8));
    return hash.result().toHex();
}
QImage independentStrips(const QImage &source,const ProcessingPlan &plan) {
    QImage image(source.size(),QImage::Format_RGBA64);if(image.isNull())return {};
    image.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<source.height();y+=128) {
        const auto strip=ImagePipeline::processRegion(source,plan,{0,y,source.width(),std::min(128,source.height()-y)});
        if(strip.isNull())return {};
        for(int row=0;row<strip.height();++row)std::memcpy(image.scanLine(y+row),strip.constScanLine(row),std::size_t(source.width())*8);
    }
    return image;
}
}

class BrushGpuPreviewTests final:public QObject {
    Q_OBJECT
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<GpuEngine> engine;
    quint64 revision=0;
    struct Result {QImage image;ScopesResult histogram;QJsonObject fingerprint;QString error;};
    Result render(const QImage &source,const ProcessingPlan &plan,std::shared_ptr<BrushRenderBudget> budget) {
        QRhiCommandBuffer *cb=nullptr;if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return {};
        Result result;QByteArray bins;
        const auto callback=[&](quint64,QByteArray data,quint64){bins=std::move(data);};
        const bool ok=engine->processAdmittedBrush(cb,source,plan,++revision,callback,true,&result.error,std::move(budget));
        QRhiReadbackResult pixels;bool done=false;
        if(ok) {
            if(!engine->captureWorkingFrame(cb,revision,[&](QJsonObject value){result.fingerprint=std::move(value);},&result.error)) {
                rhi->endOffscreenFrame();rhi->finish();return result;
            }
            pixels.completed=[&]{done=true;};auto *updates=rhi->nextResourceUpdateBatch();
            updates->readBackTexture(QRhiReadbackDescription(engine->outputTexture()),&pixels);cb->resourceUpdate(updates);
        }
        rhi->endOffscreenFrame();rhi->finish();
        if(!ok||!done||pixels.data.size()!=qint64(source.width())*source.height()*16)return result;
        QImage borrowed(reinterpret_cast<const uchar *>(pixels.data.constData()),source.width(),source.height(),source.width()*16,QImage::Format_RGBA32FPx4);
        result.image=ImagePipeline::rgba64Source(borrowed);
        result.image.setColorSpace(ColorManagement::colorSpace(plan.output));
        result.histogram=ScopesEngine::fromGpu(bins,quint64(source.width())*source.height());return result;
    }
    bool ordinaryFrame(const QImage &source) {
        QRhiCommandBuffer *cb=nullptr;if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return false;
        const auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto);
        const bool ok=engine->process(cb,source,plan,++revision,{},false);
        rhi->endOffscreenFrame();rhi->finish();return ok;
    }
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;rhi.reset(QRhi::create(QRhi::D3D11,&params,QRhi::PreferSoftwareRenderer));
#elif defined(Q_OS_MACOS)
        QRhiMetalInitParams params;rhi.reset(QRhi::create(QRhi::Metal,&params));
#else
        QRhiGles2InitParams params;params.format=QSurfaceFormat::defaultFormat();
        surface.reset(QRhiGles2InitParams::newFallbackSurface(params.format));params.fallbackSurface=surface.get();
        rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
#endif
        if(!rhi||!rhi->isFeatureSupported(QRhi::Compute)) {
            if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU"))QFAIL("Mandatory brush preview GPU unavailable");
            QSKIP("No GPU available; not admitted brush display acceptance");
        }
        engine=std::make_unique<GpuEngine>(rhi.get());qInfo()<<engine->backendName();
    }
    void admissionRefusalDoesNotUploadOrPoisonEngineAndLeaseSurvivesUpload() {
        auto state=painted();state.geometry={};auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        QImage tooWide(4097,1,QImage::Format_RGBA32FPx4);tooWide.fill(QColor::fromRgbF(.4,.2,.1));
        auto denied=render(tooWide,plan,std::make_shared<BrushRenderBudget>());QVERIFY(denied.image.isNull());
        QVERIFY(denied.error.contains("texture budget"));QVERIFY(engine->error().isEmpty());QCOMPARE(engine->sourceUploads(),quint64(0));QVERIFY(!engine->outputTexture());
        QImage source(79,53,QImage::Format_RGBA32FPx4);source.fill(QColor::fromRgbF(.4,.2,.1,.73));
        auto shortBudget=std::make_shared<BrushRenderBudget>(1);denied=render(source,plan,shortBudget);
        QVERIFY(denied.image.isNull());QVERIFY(denied.error.contains("memory budget"));QCOMPARE(shortBudget->snapshot().charged,0);
        QVERIFY(engine->error().isEmpty());QCOMPARE(engine->sourceUploads(),quint64(0));QVERIFY(!engine->outputTexture());
        auto budget=std::make_shared<BrushRenderBudget>();const auto good=render(source,plan,budget);
        QVERIFY2(!good.image.isNull(),qPrintable(good.error));QCOMPARE(good.image,independentStrips(source,plan));
        QCOMPARE(budget->snapshot().charged,source.sizeInBytes());QVERIFY(engine->lastProcessUsedCpuFallback());
        QCOMPARE(good.fingerprint["producer"].toString(),QString("cpu-reference-upload"));
        const auto uploads=engine->sourceUploads(),rendered=engine->renderedRevision();
        auto changed=plan;changed.state.hue+=1e-12;
        denied=render(source,changed,budget);QVERIFY(denied.image.isNull());QVERIFY(denied.error.contains("original intent"));
        QCOMPARE(engine->sourceUploads(),uploads);QCOMPARE(engine->renderedRevision(),rendered);QVERIFY(engine->error().isEmpty());
        QVERIFY(ordinaryFrame(source));QVERIFY(!engine->lastProcessUsedCpuFallback());QCOMPARE(budget->snapshot().charged,0);
        QVERIFY(!render(source,plan,budget).image.isNull());QCOMPARE(budget->snapshot().charged,source.sizeInBytes());
        engine.reset();QCOMPARE(budget->snapshot().charged,0);engine=std::make_unique<GpuEngine>(rhi.get());
    }
    void realSonyPreparedPreview_data() {
        QTest::addColumn<bool>("panned");QTest::newRow("fit")<<false;QTest::newRow("pan100")<<true;
    }
    void preparedHdrRetainsSignedRgbHiddenColorAndFractionalAlpha() {
        QImage source(2057,1033,QImage::Format_RGBA32FPx4);
        const std::array<float,4> alphas{0.f,1.f/65535.f,.375f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=1.07f+float(x%127)*.003f;p[1]=-.13f+float(y%97)*.006f;
            p[2]=.23f+float((x+y)%89)*.008f;p[3]=alphas[(x+y)%4];
        }
        const auto unchanged=imageHash(source);auto state=painted();
        auto budget=std::make_shared<BrushRenderBudget>();
        for(const auto output:{ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::ProPhotoRgb}) {
            auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0);
            plan.setFrameRect({.19,.11,.63,.71});const auto expected=independentStrips(source,plan);QVERIFY(!expected.isNull());
            const auto actual=render(source,plan,budget);QVERIFY2(!actual.image.isNull(),qPrintable(actual.error));
            int maximum=0;
            for(int y=0;y<source.height();++y) {
                const auto *a=reinterpret_cast<const QRgba64 *>(actual.image.constScanLine(y)),*b=reinterpret_cast<const QRgba64 *>(expected.constScanLine(y));
                for(int x=0;x<source.width();++x) {
                    maximum=std::max({maximum,std::abs(int(a[x].red())-int(b[x].red())),std::abs(int(a[x].green())-int(b[x].green())),std::abs(int(a[x].blue())-int(b[x].blue()))});
                    QCOMPARE(a[x].alpha(),b[x].alpha());
                }
            }
            QVERIFY2(maximum<=40,qPrintable(QString::number(maximum)));
            QCOMPARE(actual.fingerprint["producer"].toString(),QString("cpu-reference-upload"));
            QCOMPARE(actual.histogram.pixelCount,quint64(source.width())*source.height());QCOMPARE(budget->snapshot().charged,source.sizeInBytes());
            const auto bins=ScopesEngine::analyze(actual.image);QCOMPARE(actual.histogram.red,bins.red);QCOMPARE(actual.histogram.green,bins.green);QCOMPARE(actual.histogram.blue,bins.blue);QCOMPARE(actual.histogram.luma,bins.luma);
            qInfo().noquote()<<"ADMITTED_BRUSH_GPU_EVIDENCE "<<QJsonDocument(QJsonObject{
                {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"case","prepared-signed-hdr-alpha"},
                {"prepared_pixels",qint64(source.width())*source.height()},{"output",ColorManagement::key(output)},
                {"max_channel_error",maximum},{"gpu_limit",40},{"alpha_exact",true},{"histogram_pixels",qint64(actual.histogram.pixelCount)},
                {"producer","cpu-reference-upload"},{"native_gpu_brush",false},{"gpu_upload_executed",true},
                {"budget_bytes",budget->snapshot().limit},{"peak_reserved_bytes",budget->snapshot().peak},
                {"backend",engine->backendName()}}).toJson(QJsonDocument::Compact);
        }
        QVERIFY(ordinaryFrame(source));QCOMPARE(budget->snapshot().charged,0);QCOMPARE(imageHash(source),unchanged);
    }
    void realSonyPreparedPreview() {
        QFETCH(bool,panned);const auto fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()) {
            if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required public Sony fixture unavailable");
            QSKIP("Public Sony fixtures not configured; not native RAW preview acceptance");
        }
        const auto path=QDir(fixtures).filePath("sony_a7_iv_07.arw");
        const QByteArray expectedHash="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";
        QCOMPARE(fileHash(path),expectedHash);auto source=loadSourceFresh(path,{});QVERIFY2(!source.image.isNull(),qPrintable(source.error));
        QCOMPARE(source.image.size(),QSize(7028,4688));QCOMPARE(source.image.format(),QImage::Format_RGBA64);
        const auto sourceHash=imageHash(source.image);const auto state=painted();
        PrepareRequest prepare;prepare.image=source.image;prepare.geometry=state.geometry;prepare.fullResolution=true;
        prepare.viewport=panned?QSize(2049,1293):QSize(2400,1600);prepare.zoom=panned?1:0;prepare.centerX=.63;prepare.centerY=.41;
        const auto prepared=preparePreview(prepare,{});QVERIFY2(!prepared.gpu.isNull(),qPrintable(prepared.error));
        QCOMPARE(prepared.viewportOnly,panned);QVERIFY(qint64(prepared.gpu.width())*prepared.gpu.height()>qint64(BrushMaskTileRaster::MaxResearchFramePixels));
        if(panned){QCOMPARE(prepared.gpu.size(),prepare.viewport);QVERIFY(prepared.frameRect.width()<.5);QVERIFY(prepared.frameRect.x()>0);}
        const auto preparedHash=imageHash(prepared.gpu);auto budget=std::make_shared<BrushRenderBudget>();
        for(const auto output:{ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::ProPhotoRgb}) {
            auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output,true,.35f);plan.setFrameRect(prepared.frameRect);
            QVERIFY(ImagePipeline::processWithPlan(prepared.gpu,plan).isNull()); // old research API stays bounded
            const auto expected=independentStrips(prepared.gpu,plan);QVERIFY(!expected.isNull());
            const auto actual=render(prepared.gpu,plan,budget);QVERIFY2(!actual.image.isNull(),qPrintable(actual.error));
            int maximum=0;for(int y=0;y<expected.height();++y) {
                const auto *a=reinterpret_cast<const QRgba64 *>(actual.image.constScanLine(y)),*b=reinterpret_cast<const QRgba64 *>(expected.constScanLine(y));
                for(int x=0;x<expected.width();++x) {
                    maximum=std::max({maximum,std::abs(int(a[x].red())-int(b[x].red())),std::abs(int(a[x].green())-int(b[x].green())),std::abs(int(a[x].blue())-int(b[x].blue()))});
                    QCOMPARE(a[x].alpha(),b[x].alpha());
                }
            }
            QVERIFY2(maximum<=40,qPrintable(QString::number(maximum)));
            const auto bins=ScopesEngine::analyze(actual.image);QCOMPARE(actual.histogram.pixelCount,quint64(expected.width())*expected.height());
            QCOMPARE(actual.histogram.red,bins.red);QCOMPARE(actual.histogram.green,bins.green);QCOMPARE(actual.histogram.blue,bins.blue);QCOMPARE(actual.histogram.luma,bins.luma);
            QVERIFY(actual.fingerprint["available"].toBool());QCOMPARE(actual.fingerprint["producer"].toString(),QString("cpu-reference-upload"));
            QVERIFY(!actual.fingerprint["monitor_icc"].toBool());QCOMPARE(budget->snapshot().charged,prepared.gpu.sizeInBytes());
            qInfo().noquote()<<"ADMITTED_BRUSH_GPU_EVIDENCE "<<QJsonDocument(QJsonObject{
                {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"case",panned?"sony-pan100":"sony-fit"},
                {"raw_sha256",QString::fromLatin1(expectedHash)},{"raw_pixels",qint64(source.image.width())*source.image.height()},
                {"prepared_pixels",qint64(prepared.gpu.width())*prepared.gpu.height()},
                {"frame",QJsonArray{prepared.frameRect.x(),prepared.frameRect.y(),prepared.frameRect.width(),prepared.frameRect.height()}},
                {"output",ColorManagement::key(output)},{"max_channel_error",maximum},{"gpu_limit",40},
                {"histogram_pixels",qint64(actual.histogram.pixelCount)},{"producer","cpu-reference-upload"},{"native_gpu_brush",false},
                {"native_float_raw_decoder",false},{"gpu_upload_executed",true},{"budget_bytes",budget->snapshot().limit},
                {"peak_reserved_bytes",budget->snapshot().peak},{"backend",engine->backendName()}}).toJson(QJsonDocument::Compact);
        }
        QVERIFY(ordinaryFrame(prepared.gpu));QCOMPARE(budget->snapshot().charged,0);
        QCOMPARE(imageHash(prepared.gpu),preparedHash);QCOMPARE(imageHash(source.image),sourceHash);QCOMPARE(fileHash(path),expectedHash);
    }
};
int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);BrushGpuPreviewTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "BrushGpuPreviewTests.moc"
