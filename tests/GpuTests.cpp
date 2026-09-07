#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QRgba64>
#include <random>
#include <cstring>
#include "core/gpu/GpuEngine.h"
#include "core/scopes/ScopesEngine.h"

class GpuTests : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<GpuEngine> engine;
    quint64 revision=0;
    struct Result { QImage image; ScopesResult histogram; };
    Result render(const QImage &input,const AdjustmentState &state,bool statistics=true) {
        QRhiCommandBuffer *cb=nullptr;
        if (rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess) return {};
        QByteArray counts;
        const auto floatImage=input.format()==QImage::Format_RGBA32FPx4 ? input : input.convertToFormat(QImage::Format_RGBA32FPx4);
        const auto callback=statistics ? GpuEngine::HistogramReady([&](quint64,QByteArray data,quint64){ counts=std::move(data); }) : GpuEngine::HistogramReady{};
        const bool ok=engine->process(cb,floatImage,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto),++revision,callback,true);
        QRhiReadbackResult readback; bool done=false;
        if (ok) {
            readback.completed=[&] { done=true; };
            auto *updates=rhi->nextResourceUpdateBatch();
            updates->readBackTexture(QRhiReadbackDescription(engine->outputTexture()),&readback);
            cb->resourceUpdate(updates);
        }
        rhi->endOffscreenFrame(); rhi->finish();
        if (!ok) { qWarning()<<engine->error(); return {}; }
        if (!done || readback.data.size()!=input.width()*input.height()*16) return {};
        QImage image(reinterpret_cast<const uchar *>(readback.data.constData()),input.width(),input.height(),input.width()*16,QImage::Format_RGBA32FPx4);
        return {image.copy().convertToFormat(QImage::Format_RGBA64),ScopesEngine::fromGpu(counts,quint64(input.width())*input.height())};
    }
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;
        rhi.reset(QRhi::create(QRhi::D3D11,&params,QRhi::PreferSoftwareRenderer));
#elif defined(Q_OS_MACOS)
        QRhiMetalInitParams params;
        rhi.reset(QRhi::create(QRhi::Metal,&params));
#else
        QRhiGles2InitParams params;
        params.format=QSurfaceFormat::defaultFormat();
        surface.reset(QRhiGles2InitParams::newFallbackSurface(params.format));
        params.fallbackSurface=surface.get();
        rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
#endif
        if (!rhi || !rhi->isFeatureSupported(QRhi::Compute)) {
            if (qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")) QFAIL("A working compute backend is required");
            QSKIP("No compute GPU backend available: this is NOT a GPU test pass");
        }
        engine=std::make_unique<GpuEngine>(rhi.get());
        qInfo()<<engine->backendName();
    }
    void cleanupTestCase() { engine.reset(); rhi.reset(); surface.reset(); }
    void pixelsAndHistogramMatchCpu() {
        QImage image(131,137,QImage::Format_RGBA64);
        std::mt19937 random(652819);
        for(int y=0;y<image.height();++y) {
            auto *line=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<image.width();++x) line[x]=QRgba64::fromRgba64(random()%60000,random()%60000,random()%60000,65535);
        }
        for(int mode=0;mode<4;++mode) {
            AdjustmentState state;
            if(mode==1) { state.exposure=.8;state.temperature=30;state.tint=-25;state.highlights=-20;state.shadows=10;state.contrast=12;state.highlightRecovery=30; }
            if(mode==2) { state.hue=-23;state.saturation=18;state.vibrance=22;state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8; }
            if(mode==3) { state.exposure=3.0;state.saturation=-20;state.blacks=-50;state.whites=60; }
            const auto actual=render(image,state);
            QVERIFY2(!actual.image.isNull(),qPrintable(engine->error()));
            const auto reference=ImagePipeline::process(image,state,ImagePipeline::InputEncoding::LinearProPhoto);
            int maximum=0;quint64 absolute=0;
            for(int y=0;y<image.height();++y) {
                const auto *a=reinterpret_cast<const QRgba64 *>(actual.image.constScanLine(y));
                const auto *b=reinterpret_cast<const QRgba64 *>(reference.constScanLine(y));
                for(int x=0;x<image.width();++x) {
                    for(int difference:{std::abs(int(a[x].red())-int(b[x].red())),std::abs(int(a[x].green())-int(b[x].green())),std::abs(int(a[x].blue())-int(b[x].blue()))}) { maximum=std::max(maximum,difference);absolute+=difference; }
                }
            }
            qInfo()<<"CPU/GPU mode"<<mode<<"max 16-bit error"<<maximum<<"mean"<<double(absolute)/(image.width()*image.height()*3);
            QVERIFY2(maximum<=40,qPrintable(QString::number(maximum)));
            // Exact histogram of the pixels actually produced by the GPU, not
            // of slightly different floating-point CPU arithmetic.
            const auto expectedHistogram=ScopesEngine::analyze(actual.image);
            QCOMPARE(actual.histogram.pixelCount,quint64(image.width())*image.height());
            QCOMPARE(actual.histogram.red,expectedHistogram.red);
            QCOMPARE(actual.histogram.green,expectedHistogram.green);
            QCOMPARE(actual.histogram.blue,expectedHistogram.blue);
            QCOMPARE(actual.histogram.luma,expectedHistogram.luma);
            QCOMPARE(actual.histogram.shadowClipPercent,expectedHistogram.shadowClipPercent);
            QCOMPARE(actual.histogram.highlightClipPercent,expectedHistogram.highlightClipPercent);
        }
    }
    void displayPassHasCorrectOrientation() {
        QImage source(16,16,QImage::Format_RGBA64);
        for(int y=0;y<16;++y) {
            auto *line=reinterpret_cast<QRgba64 *>(source.scanLine(y));
            for(int x=0;x<16;++x) line[x]=QRgba64::fromRgba64(y<8?30000:2000,2000,y<8?2000:30000,65535);
        }
        auto computed=render(source,{},false);QVERIFY(!computed.image.isNull());
        std::unique_ptr<QRhiTexture> color(rhi->newTexture(QRhiTexture::RGBA8,source.size(),1,QRhiTexture::RenderTarget|QRhiTexture::UsedAsTransferSource));
        QVERIFY(color->create());
        std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());QVERIFY(target->create());
        QRhiCommandBuffer *cb=nullptr;QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        QVERIFY2(engine->draw(cb,target.get()),qPrintable(engine->error()));
        QRhiReadbackResult pixels;bool done=false;pixels.completed=[&]{done=true;};
        auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(color.get()),&pixels);cb->resourceUpdate(updates);
        rhi->endOffscreenFrame();rhi->finish();QVERIFY(done);
        QImage actual(reinterpret_cast<const uchar *>(pixels.data.constData()),16,16,64,QImage::Format_RGBA8888);
        actual=rhi->isYUpInFramebuffer()?actual.mirrored():actual.copy();
        const auto reference=computed.image.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(actual.pixelColor(3,3),reference.pixelColor(3,3));
        QCOMPARE(actual.pixelColor(3,12),reference.pixelColor(3,12));
    }
    void parameterChangesDoNotUploadSource() {
        QImage input(67,73,QImage::Format_RGBA32FPx4);input.fill(QColor::fromRgbF(.2,.3,.4));
        auto first=render(input,{});QVERIFY(!first.image.isNull());
        const auto uploads=engine->sourceUploads();
        AdjustmentState state;state.exposure=.5;
        auto second=render(input,state);QVERIFY(!second.image.isNull());
        QCOMPARE(engine->sourceUploads(),uploads);
        QVERIFY(first.image!=second.image);
    }
};
int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    GpuTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "GpuTests.moc"
