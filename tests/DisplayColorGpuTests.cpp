#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QRgba64>
#include <cmath>
#include <memory>

#include "core/gpu/GpuEngine.h"

namespace {
constexpr int LutSize=33;
QImage invertedDisplayLut() {
    QImage atlas(LutSize*LutSize,LutSize,QImage::Format_RGBA32FPx4);
    if(atlas.isNull())return {};
    for(int b=0;b<LutSize;++b)for(int g=0;g<LutSize;++g) {
        auto *row=reinterpret_cast<float *>(atlas.scanLine(g));
        for(int r=0;r<LutSize;++r) {
            const int x=b*LutSize+r;
            row[x*4+0]=1.0f-float(r)/float(LutSize-1);
            row[x*4+1]=1.0f-float(g)/float(LutSize-1);
            row[x*4+2]=1.0f-float(b)/float(LutSize-1);
            row[x*4+3]=1.0f;
        }
    }
    return atlas;
}
}

class DisplayColorGpuTests final : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<GpuEngine> engine;
    quint64 revision=0;

    QByteArray readOutput() {
        QRhiCommandBuffer *cb=nullptr;
        if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return {};
        QRhiReadbackResult readback;bool done=false;readback.completed=[&]{done=true;};
        auto *updates=rhi->nextResourceUpdateBatch();
        updates->readBackTexture(QRhiReadbackDescription(engine->outputTexture()),&readback);
        cb->resourceUpdate(updates);
        rhi->endOffscreenFrame();rhi->finish();
        return done?readback.data:QByteArray{};
    }

    bool process(const QImage &input) {
        QRhiCommandBuffer *cb=nullptr;
        if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return false;
        const QImage source=input.format()==QImage::Format_RGBA32FPx4?input:input.convertToFormat(QImage::Format_RGBA32FPx4);
        const auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,
                                                ColorManagement::OutputSpace::SRgb,false,0.0f);
        const bool ok=engine->process(cb,source,plan,++revision,{},false);
        rhi->endOffscreenFrame();rhi->finish();
        return ok;
    }

    QImage drawToRgba8(QSize size) {
        std::unique_ptr<QRhiTexture> color(rhi->newTexture(QRhiTexture::RGBA8,size,1,
            QRhiTexture::RenderTarget|QRhiTexture::UsedAsTransferSource));
        if(!color->create())return {};
        std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());if(!target->create())return {};
        QRhiCommandBuffer *cb=nullptr;if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return {};
        if(!engine->draw(cb,target.get())){rhi->endOffscreenFrame();return {};}
        QRhiReadbackResult pixels;bool done=false;pixels.completed=[&]{done=true;};
        auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(color.get()),&pixels);cb->resourceUpdate(updates);
        rhi->endOffscreenFrame();rhi->finish();if(!done||pixels.data.size()!=size.width()*size.height()*4)return {};
        QImage actual(reinterpret_cast<const uchar *>(pixels.data.constData()),size.width(),size.height(),size.width()*4,QImage::Format_RGBA8888);
        return rhi->isYUpInFramebuffer()?actual.mirrored():actual.copy();
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
        if(!rhi||!rhi->isFeatureSupported(QRhi::Compute)) {
            if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU"))QFAIL("A working compute backend is required");
            QSKIP("No compute GPU backend available");
        }
        engine=std::make_unique<GpuEngine>(rhi.get());
    }

    void monitorLutChangesPresentationButNotProcessedOutput() {
        QImage source(16,16,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y) {
            auto *row=reinterpret_cast<float *>(source.scanLine(y));
            for(int x=0;x<source.width();++x) {
                row[x*4+0]=0.04f+0.45f*float(x)/15.0f;
                row[x*4+1]=0.08f+0.35f*float(y)/15.0f;
                row[x*4+2]=0.12f+0.20f*float(x+y)/30.0f;
                row[x*4+3]=1.0f;
            }
        }
        QVERIFY2(process(source),qPrintable(engine->error()));
        const QByteArray before=readOutput();QVERIFY(!before.isEmpty());
        QImage beforeFloat(reinterpret_cast<const uchar *>(before.constData()),16,16,16*16,QImage::Format_RGBA32FPx4);
        const QImage reference=beforeFloat.copy().convertToFormat(QImage::Format_RGBA8888);

        const QImage lut=invertedDisplayLut();QVERIFY(!lut.isNull());
        engine->setDisplayColorLut(lut,QStringLiteral("test-invert"),QStringLiteral("synthetic inverse monitor"));
        const QImage presented=drawToRgba8(source.size());
        QVERIFY2(!presented.isNull(),qPrintable(engine->error()));

        // The monitor transform is display-only: processed output remains byte
        // identical and therefore scopes/export would see the same image.
        const QByteArray after=readOutput();
        QCOMPARE(after,before);

        for(const QPoint p:{QPoint(2,3),QPoint(8,7),QPoint(13,12)}) {
            const QColor src=reference.pixelColor(p);
            const QColor dst=presented.pixelColor(p);
            QVERIFY2(std::abs(dst.red()-(255-src.red()))<=3,
                     qPrintable(QStringLiteral("R %1 -> %2").arg(src.red()).arg(dst.red())));
            QVERIFY(std::abs(dst.green()-(255-src.green()))<=3);
            QVERIFY(std::abs(dst.blue()-(255-src.blue()))<=3);
        }
    }
};

int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    DisplayColorGpuTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "DisplayColorGpuTests.moc"
