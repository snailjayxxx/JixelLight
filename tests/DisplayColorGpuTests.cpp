#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QRgba64>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
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

QImage alphaDisplayLut(int mode) {
    if(mode==1)return invertedDisplayLut();
    QImage atlas(LutSize*LutSize,LutSize,QImage::Format_RGBA32FPx4);
    for(int b=0;b<LutSize;++b)for(int g=0;g<LutSize;++g)for(int r=0;r<LutSize;++r) {
        float *pixel=reinterpret_cast<float *>(atlas.scanLine(g))+(b*LutSize+r)*4;
        const float rr=r/float(LutSize-1),gg=g/float(LutSize-1),bb=b/float(LutSize-1);
        pixel[0]=mode==0?rr:1-bb;
        pixel[1]=mode==0?gg:rr;
        pixel[2]=mode==0?bb:1-gg;
        pixel[3]=1;
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
        const QImage source=ImagePipeline::floatSource(input);
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

    void sceneGraphPremultipliesAfterMonitorWithoutChangingWorkingPixels() {
        QImage source(16,16,QImage::Format_RGBA32FPx4);
        const float alpha[]{0.f,.25f,.64f,1.f};
        for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
            float *p=reinterpret_cast<float *>(source.scanLine(y))+x*4;
            p[0]=.04f+.45f*x/15;p[1]=.08f+.35f*y/15;
            p[2]=.12f+.20f*(x+y)/30;p[3]=alpha[x%4];
        }
        QVERIFY2(process(source),qPrintable(engine->error()));
        const QByteArray before=readOutput();
        QCOMPARE(before.size(),16*16*4*int(sizeof(float)));
        // Compute pixels follow the top-left source convention. Only the
        // framebuffer readback below needs the backend's framebuffer Y flip.
        const QImage reference=QImage(reinterpret_cast<const uchar *>(before.constData()),
            16,16,16*4*int(sizeof(float)),QImage::Format_RGBA32FPx4).copy();

        std::unique_ptr<QRhiTexture> color(rhi->newTexture(QRhiTexture::RGBA8,source.size(),1,
            QRhiTexture::RenderTarget|QRhiTexture::UsedAsTransferSource));
        QVERIFY(color->create());
        std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());QVERIFY(target->create());
        const auto drawSameTarget=[&](bool premultiplied) -> QImage {
            QRhiCommandBuffer *cb=nullptr;
            if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return {};
            const bool ok=premultiplied?engine->drawForSceneGraph(cb,target.get()):engine->draw(cb,target.get());
            if(!ok){rhi->endOffscreenFrame();return {};}
            QRhiReadbackResult pixels;bool done=false;pixels.completed=[&]{done=true;};
            auto *updates=rhi->nextResourceUpdateBatch();
            updates->readBackTexture(QRhiReadbackDescription(color.get()),&pixels);cb->resourceUpdate(updates);
            rhi->endOffscreenFrame();rhi->finish();
            if(!done||pixels.data.size()!=16*16*4)return {};
            QImage actual(reinterpret_cast<const uchar *>(pixels.data.constData()),16,16,16*4,QImage::Format_RGBA8888);
            return rhi->isYUpInFramebuffer()?actual.mirrored():actual.copy();
        };
        int maximum=0,draws=0;QJsonArray cases;
        for(int mode=0;mode<3;++mode) {
            engine->setDisplayColorLut(alphaDisplayLut(mode),QStringLiteral("alpha-%1").arg(mode),
                QStringLiteral("independent analytic monitor LUT"));
            // Exercise pipeline-cache changes on one stable target/pass,
            // including consecutive uses of the same premultiplied pipeline.
            for(bool premultiplied:{false,true,false,true,true,false}) {
                const QImage actual=drawSameTarget(premultiplied);
                QVERIFY2(!actual.isNull(),qPrintable(engine->error()));int error=0;
                for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
                    const float *p=reinterpret_cast<const float *>(reference.constScanLine(y))+x*4;
                    QVERIFY(std::abs(p[3]-alpha[x%4])<.000001f);
                    const float r=std::clamp(p[0],0.f,1.f),g=std::clamp(p[1],0.f,1.f),b=std::clamp(p[2],0.f,1.f);
                    const float rgb[]{mode==0?r:mode==1?1-r:1-b,mode==0?g:mode==1?1-g:r,mode==0?b:mode==1?1-b:1-g};
                    const auto c=actual.pixelColor(x,y);const int got[]{c.red(),c.green(),c.blue(),c.alpha()};
                    for(int k=0;k<4;++k) {
                        const float expected=k==3?p[3]:rgb[k]*(premultiplied?p[3]:1.f);
                        error=std::max(error,std::abs(got[k]-int(std::lround(expected*255))));
                    }
                }
                QCOMPARE(readOutput(),before);
                QVERIFY2(error<=1,qPrintable(QStringLiteral("LUT %1, premultiplied %2: %3/255").arg(mode).arg(premultiplied).arg(error)));
                maximum=std::max(maximum,error);++draws;
                cases.append(QJsonObject{{"lut",mode},{"premultiplied",premultiplied},{"max_channel_error_255",error}});
            }
        }
        const QJsonObject evidence{{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"draws_same_target",draws},{"pixels_each",256},{"alpha_levels",QJsonArray{0,.25,.64,1}},
            {"analytic_monitor_luts",3},{"max_channel_error_255",maximum},{"error_limit_255",1},
            {"working_output_bytes_unchanged",true},{"straight_draw_unchanged",true},
            {"monitor_transform_before_premultiplication",true},{"same_target_pipeline_switch",true},
            {"hidden_rgb_preserved_in_straight_draw",true},{"transparent_device_rgb_zero",true},
            {"opaque_device_rgb_unchanged",true},{"cases",cases}};
        qInfo().noquote()<<"DISPLAY_ALPHA_EVIDENCE"<<QJsonDocument(evidence).toJson(QJsonDocument::Compact);
    }
};

int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    DisplayColorGpuTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "DisplayColorGpuTests.moc"
