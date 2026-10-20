#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QRgba64>
#include <random>
#include <cstring>
#include <cmath>
#include <limits>
#include "core/gpu/GpuEngine.h"
#include "core/pipeline/FloatFrameFingerprint.h"
#include "core/pipeline/FloatSceneResampler.h"
#include "core/color/MonitorColorTransform.h"
#include "core/scopes/ScopesEngine.h"
#include "core/look/LookProfiles.h"
#include "core/raw/RawDecoder.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>

class GpuTests : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<GpuEngine> engine;
    quint64 revision=0;
    struct Result { QImage image; ScopesResult histogram; QJsonObject diagnostic, independentFingerprint; };
    Result render(const QImage &input,const AdjustmentState &state,bool statistics=true,
                  ImagePipeline::InputEncoding encoding=ImagePipeline::InputEncoding::LinearProPhoto,
                  ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb,const QRectF &frameRect={0,0,1,1},bool diagnostic=false) {
        QRhiCommandBuffer *cb=nullptr;
        if (rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess) return {};
        QByteArray counts;
        const auto floatImage=ImagePipeline::floatSource(input);
        const auto callback=statistics ? GpuEngine::HistogramReady([&](quint64,QByteArray data,quint64){ counts=std::move(data); }) : GpuEngine::HistogramReady{};
        auto plan=ProcessingPlan::compile(state,encoding,output); plan.setFrameRect(frameRect);
        const bool ok=engine->process(cb,floatImage,plan,++revision,callback,true);
        QJsonObject captured;
        if (ok && diagnostic && !engine->captureWorkingFrame(cb,revision,[&](QJsonObject value){captured=std::move(value);})) {
            rhi->endOffscreenFrame(); rhi->finish(); return {};
        }
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
        // Check before integer conversion, which could otherwise conceal NaNs.
        const auto *values=reinterpret_cast<const float *>(readback.data.constData());
        for(qsizetype i=0;i<readback.data.size()/qsizetype(sizeof(float));++i)
            if(!std::isfinite(values[i])) { qWarning()<<"Non-finite GPU result at"<<i;return {}; }
        QImage image(reinterpret_cast<const uchar *>(readback.data.constData()),input.width(),input.height(),input.width()*16,QImage::Format_RGBA32FPx4);
        return {ImagePipeline::rgba64Source(image),ScopesEngine::fromGpu(counts,quint64(input.width())*input.height()),
                captured,diagnostic?FloatFrameFingerprint::capture(image):QJsonObject{}};
    }
    void verifyParity(const QImage &input,const AdjustmentState &state,
                      ImagePipeline::InputEncoding encoding,ColorManagement::OutputSpace space,
                      bool statistics,const char *label,const QRectF &frameRect={0,0,1,1}) {
        const auto actual=render(input,state,statistics,encoding,space,frameRect);
        QVERIFY2(!actual.image.isNull(),qPrintable(engine->error()));
        auto plan=ProcessingPlan::compile(state,encoding,space); plan.setFrameRect(frameRect);
        const auto reference=ImagePipeline::processWithPlan(input,plan);
        int maximum=0,worstX=0,worstY=0;
        quint64 absolute=0,overTolerance=0;
        for(int y=0;y<input.height();++y) {
            const auto *a=reinterpret_cast<const QRgba64 *>(actual.image.constScanLine(y));
            const auto *b=reinterpret_cast<const QRgba64 *>(reference.constScanLine(y));
            for(int x=0;x<input.width();++x) {
                const int dr=std::abs(int(a[x].red())-int(b[x].red()));
                const int dg=std::abs(int(a[x].green())-int(b[x].green()));
                const int db=std::abs(int(a[x].blue())-int(b[x].blue()));
                const int delta=std::max({dr,dg,db});
                absolute+=dr+dg+db;
                if(delta>40) ++overTolerance;
                if(delta>maximum) { maximum=delta;worstX=x;worstY=y; }
                QCOMPARE(a[x].alpha(),b[x].alpha());
            }
        }
        const auto original=input.pixelColor(worstX,worstY).rgba64();
        const auto cpu=reference.pixelColor(worstX,worstY).rgba64();
        const auto gpu=actual.image.pixelColor(worstX,worstY).rgba64();
        qInfo()<<label<<"encoding"<<int(encoding)<<"output"<<int(space)
               <<"max"<<maximum<<"mean"<<double(absolute)/(input.width()*input.height()*3)
               <<"pixels over 40"<<overTolerance<<"at"<<worstX<<worstY
               <<"input"<<original.red()<<original.green()<<original.blue()
               <<"CPU"<<cpu.red()<<cpu.green()<<cpu.blue()<<"GPU"<<gpu.red()<<gpu.green()<<gpu.blue();
        // This is the original bound. Do not make platform-specific exceptions.
        QVERIFY2(maximum<=40,qPrintable(QString::number(maximum)));
        if(statistics) {
            const auto expected=ScopesEngine::analyze(actual.image);
            QCOMPARE(actual.histogram.pixelCount,quint64(input.width())*input.height());
            QCOMPARE(actual.histogram.red,expected.red);
            QCOMPARE(actual.histogram.green,expected.green);
            QCOMPARE(actual.histogram.blue,expected.blue);
            QCOMPARE(actual.histogram.luma,expected.luma);
            QCOMPARE(actual.histogram.shadowClipPercent,expected.shadowClipPercent);
            QCOMPARE(actual.histogram.highlightClipPercent,expected.highlightClipPercent);
        }
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
    void diagnosticCaptureMatchesIndependentReadbackAndNamesCpuFallback() {
        QImage source(23,19,QImage::Format_RGBA64);
        for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(x*2341,y*3127,12345,(x+y)%2?23456:65535);
        const bool fallback=rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2;
        for (int mode=0;mode<4;++mode) {
            AdjustmentState state; state.exposure=.3; state.vignetteAmount=-1.5;
            if(mode==1){state.hue=-23;state.hslSaturation[5]=-25;}
            if(mode>=2){state.look.mode="manual";state.look.code=mode==2?"FL":"ST";state.look.parameters={{"sharpness",3},{"clarity",2}};}
            const auto space=mode>=2?ColorManagement::OutputSpace::DisplayP3:ColorManagement::OutputSpace::SRgb;
            const auto result=render(source,state,true,ImagePipeline::InputEncoding::LinearProPhoto,space,{.1,.2,.7,.6},true);
            QVERIFY(!result.image.isNull()); QVERIFY(result.diagnostic["available"].toBool());
            for(auto it=result.independentFingerprint.begin();it!=result.independentFingerprint.end();++it)
                QCOMPARE(result.diagnostic[it.key()],it.value());
            QCOMPARE(result.diagnostic["parameter_revision"].toInteger(),qint64(revision));
            QCOMPARE(result.diagnostic["output_space"].toString(),ColorManagement::key(space));
            // FL compiles HSL band changes, so it uses the same existing
            // D3D11/OpenGL safety fallback. ST proves native spatial compute.
            QCOMPARE(result.diagnostic["producer"].toString(),QString((mode==1||mode==2)&&fallback?"cpu-reference-upload":"gpu-compute"));
            QVERIFY(!result.diagnostic["monitor_icc"].toBool());
            QCOMPARE(result.histogram.pixelCount,quint64(source.width()*source.height()));
        }
    }
    void diagnosticRefusalsDoNotPoisonRenderingOrUploadSource() {
        QImage source(17,13,QImage::Format_RGBA32FPx4);source.fill(QColor::fromRgbF(.2,.3,.4));
        auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb);
        QRhiCommandBuffer *cb=nullptr;QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        QVERIFY(engine->process(cb,source,plan,++revision,{}));
        const auto uploads=engine->sourceUploads();QString error;QJsonObject output;
        QVERIFY(!engine->captureWorkingFrame(cb,revision+1,[&](QJsonObject){},&error));QVERIFY(!error.isEmpty());
        QVERIFY(!engine->captureWorkingFrame(cb,revision,[&](QJsonObject){},&error,17*13*16-1));
        QVERIFY(!engine->captureWorkingFrame(nullptr,revision,[&](QJsonObject){},&error));
        QVERIFY(engine->captureWorkingFrame(cb,revision,[&](QJsonObject value){output=std::move(value);},&error));QVERIFY(error.isEmpty());
        QVERIFY(!engine->captureWorkingFrame(cb,revision,[&](QJsonObject){},&error));
        QCOMPARE(engine->sourceUploads(),uploads);QVERIFY(engine->error().isEmpty());
        rhi->endOffscreenFrame();rhi->finish();QVERIFY(output["available"].toBool());QVERIFY(!engine->hasPendingReadback());
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QByteArray counts;
        QVERIFY(engine->process(cb,source,plan,revision,[&](quint64,QByteArray bytes,quint64){counts=std::move(bytes);},true));
        rhi->endOffscreenFrame();rhi->finish();QCOMPARE(counts.size(),HistogramCounts::GpuWords*int(sizeof(quint32)));
        QCOMPARE(engine->sourceUploads(),uploads);
    }
    void diagnosticReadbackSurvivesTeardownAndIgnoresDisplayLut() {
        QImage source(11,7,QImage::Format_RGBA32FPx4);source.fill(QColor::fromRgbF(.3,.4,.5));
        auto first=render(source,{},false,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,{0,0,1,1},true);
        QVERIFY(first.diagnostic["available"].toBool());
        QImage black(33*33,33,QImage::Format_RGBA32FPx4);black.fill(Qt::black);
        engine->setDisplayColorLut(black,"diagnostic-black-monitor","test monitor");
        std::unique_ptr<QRhiTexture> color(rhi->newTexture(QRhiTexture::RGBA8,source.size(),1,QRhiTexture::RenderTarget|QRhiTexture::UsedAsTransferSource));
        QVERIFY(color->create());
        std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());QVERIFY(target->create());
        QRhiCommandBuffer *cb=nullptr;QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb);
        QVERIFY(engine->process(cb,source,plan,++revision,{}));QJsonObject second;
        QVERIFY(engine->draw(cb,target.get()));
        QVERIFY(engine->captureWorkingFrame(cb,revision,[&](QJsonObject value){second=std::move(value);}));
        rhi->endOffscreenFrame();engine.reset(); // Destruction must drain a still-owned result.
        QVERIFY(second["available"].toBool());QCOMPARE(second["pixel_sha256"],first.diagnostic["pixel_sha256"]);
        engine=std::make_unique<GpuEngine>(rhi.get());
        QImage resized(5,3,QImage::Format_RGBA32FPx4);resized.fill(Qt::gray);
        QVERIFY(!render(resized,{}).image.isNull());
    }
    void numericSafetyFallbackIsScoped() {
        QImage image(32,24,QImage::Format_RGBA64);
        image.fill(QColor(96,128,160));
        AdjustmentState neutral;
        QVERIFY(!render(image,neutral,false).image.isNull());
        QVERIFY(!engine->lastProcessUsedCpuFallback());
        const bool backendNeedsFallback = rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2;
        AdjustmentState hsl; hsl.hue=-23; hsl.hslSaturation[5]=-25;
        QVERIFY(!render(image,hsl,false).image.isNull());
        QCOMPARE(engine->lastProcessUsedCpuFallback(), backendNeedsFallback);
        AdjustmentState bright; bright.exposure=3.0; bright.saturation=-20; bright.blacks=-50; bright.whites=60;
        QVERIFY(!render(image,bright,false).image.isNull());
        QCOMPARE(engine->lastProcessUsedCpuFallback(), backendNeedsFallback);
        AdjustmentState ordinary; ordinary.exposure=.8; ordinary.saturation=18; ordinary.vibrance=22;
        QVERIFY(!render(image,ordinary,false).image.isNull());
        QVERIFY(!engine->lastProcessUsedCpuFallback());
        ordinary.exposure=.3; ordinary.vignetteAmount=3;
        QVERIFY(!render(image,ordinary,false).image.isNull()); QCOMPARE(engine->lastProcessUsedCpuFallback(),backendNeedsFallback);
        ordinary.vignetteAmount=-3;
        QVERIFY(!render(image,ordinary,false).image.isNull()); QVERIFY(!engine->lastProcessUsedCpuFallback());
    }
    void fp32DownscaledPreviewCpuGpuParityAllSpacesAndFallbacks() {
        QImage scene(321,259,QImage::Format_RGBA32FPx4);
        for(int y=0;y<scene.height();++y) for(int x=0;x<scene.width();++x) {
            auto *p=reinterpret_cast<float *>(scene.scanLine(y))+4*x;
            p[0]=float(2.1+.004*x+.002*y);
            p[1]=float(.22+.001*x+.0005*y);
            p[2]=float(-.07+.0004*x);
            p[3]=(x+y)%19==0?0.0f:(x+y)%11==0?.375f:1.0f;
        }
        auto scaled=FloatSceneResampler::scale(scene,{101,79});
        QVERIFY(!scaled.isNull());
        QCOMPARE(scaled.format(),QImage::Format_RGBA32FPx4);
        QVERIFY(ImagePipeline::validFloatSource(scaled));
        const auto *mid=reinterpret_cast<const float *>(scaled.constScanLine(39))+4*50;
        QVERIFY(mid[0]>2.0f);
        QVERIFY(mid[2]<0.1f);
        for(int mode=0;mode<2;++mode) for(int output=0;output<4;++output) {
            AdjustmentState state;state.exposure=-2;
            if(mode==1) state.hslSaturation[5]=-22;
            verifyParity(scaled,state,ImagePipeline::InputEncoding::LinearProPhoto,
                         ColorManagement::OutputSpace(output),true,"FP32 antialiased preview");
            if(QTest::currentTestFailed()) return;
            const bool fallback=mode==1 && (rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);
        }
    }
    void fp32GeometryStraightenProjectiveCaMatchesCpuGpuAndScopes() {
        // Exercise the actual scene image returned by spatial geometry,
        // including >1 highlights, negative channels and premultiplied-alpha
        // resampling. GPU compute and numeric CPU fallback must see the exact
        // same FP32 source; no integer proxy is allowed.
        QImage scene(113,89,QImage::Format_RGBA32FPx4);
        for(int y=0;y<scene.height();++y) for(int x=0;x<scene.width();++x) {
            auto *p=reinterpret_cast<float *>(scene.scanLine(y))+4*x;
            p[0]=float(2.1+.006*x+.003*y);
            p[1]=float(.16+.004*x+.001*y);
            p[2]=float(-.22+.002*x+.002*y);
            p[3]=(x+y)%9==0?.75f:1.0f;
        }
        GeometryState geometry;
        geometry.straighten=8.5;
        geometry.perspectiveHorizontal=.13;
        geometry.perspectiveVertical=-.09;
        geometry.distortion=-.12;
        geometry.redCa=1.5;
        geometry.blueCa=-.9;
        geometry.crop={.1,.1,.8,.8};
        geometry.quarterTurns=1;
        geometry.flipHorizontal=true;
        const auto corrected=geometry.apply(scene);
        QVERIFY(!corrected.isNull());
        QCOMPARE(corrected.format(),QImage::Format_RGBA32FPx4);
        QVERIFY(ImagePipeline::validFloatSource(corrected));
        QVERIFY(reinterpret_cast<const float *>(corrected.constScanLine(corrected.height()/2))[4*(corrected.width()/2)]>1.0f);
        const auto fingerprint=FloatFrameFingerprint::capture(corrected);
        QVERIFY(fingerprint.value("available").toBool());
        QVERIFY(fingerprint.value("ranges_rgba").toArray()[0].toArray()[1].toDouble()>2.0);
        for(int mode=0;mode<2;++mode) for(int space=0;space<4;++space) {
            AdjustmentState state;
            state.exposure=-2.0;
            state.temperature=mode==0?0:9;
            state.highlightRecovery=mode==0?0:22;
            if(mode==1) state.hslSaturation[5]=-27; // verified CPU fallback on D3D/OpenGL
            const auto output=ColorManagement::OutputSpace(space);
            verifyParity(corrected,state,ImagePipeline::InputEncoding::LinearProPhoto,
                         output,true,"FP32 lens/CA/projective/straighten");
            if(QTest::currentTestFailed()) return;
            const bool expectedFallback=mode==1 &&
                (rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),expectedFallback);
        }
    }
    void fp32SceneOverRangeCpuGpuAndCpuSafetyFallback() {
        QImage scene(53,47,QImage::Format_RGBA32FPx4);
        const std::array<quint16,4> alpha{0,1,12345,65535};
        for(int y=0;y<scene.height();++y) {
            auto *row=reinterpret_cast<float *>(scene.scanLine(y));
            for(int x=0;x<scene.width();++x) {
                row[4*x]=.19f+float((x*11+y*3)%43)*.071f; // reaches >3
                row[4*x+1]=.04f+float((x*7+y*5)%43)*.029f;
                row[4*x+2]=(x+y)%8==0?-.012f:(.07f+float((x*5+y*9)%43)*.042f);
                row[4*x+3]=float(alpha[(x+y)%4])/65535.0f;
            }
        }
        QVERIFY(ImagePipeline::validFloatSource(scene));
        QCOMPARE(ImagePipeline::floatSource(scene).cacheKey(),scene.cacheKey());
        const auto clipped=ImagePipeline::rgba64Source(scene);
        for(int mode=0;mode<3;++mode) for(int space=0;space<4;++space) {
            AdjustmentState state; state.exposure=-1.5;
            if(mode==1) { state.temperature=18;state.highlightRecovery=25;state.vignetteAmount=-.9; }
            if(mode==2) { state.hue=-15;state.hslSaturation[5]=-20; }
            const auto output=ColorManagement::OutputSpace(space);
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0);
            if(mode==0 && space==0) {
                const auto full=ImagePipeline::processWithPlan(scene,plan);
                const auto earlyClipped=ImagePipeline::processWithPlan(clipped,plan);
                QVERIFY(full!=earlyClipped); // no pre-tone clipping in the CPU reference
            }
            verifyParity(scene,state,ImagePipeline::InputEncoding::LinearProPhoto,output,true,"FP32 scene > 1");
            if(QTest::currentTestFailed()) return;
            const bool fallback=mode==2 && (rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);
        }
        // Capture a real output fingerprint from the frozen FP32 source rather
        // than a preclipped RGBA64 proxy.
        AdjustmentState reference; reference.exposure=-2.0;
        auto result=render(scene,reference,true,ImagePipeline::InputEncoding::LinearProPhoto,
                           ColorManagement::OutputSpace::SRgb,{},true);
        QVERIFY(!result.image.isNull());
        QVERIFY(result.diagnostic.value("available").toBool());
        QCOMPARE(result.diagnostic.value("producer").toString(),QString("gpu-compute"));
    }
    void invalidFloatSceneIsRefusedBeforeGpuUpload() {
        QImage scene(4,4,QImage::Format_RGBA32FPx4);
        for(int y=0;y<scene.height();++y) {
            auto *row=reinterpret_cast<float *>(scene.scanLine(y));
            for(int x=0;x<scene.width();++x) {
                row[4*x]=.35f;row[4*x+1]=.1f;row[4*x+2]=.5f;row[4*x+3]=1.0f;
            }
        }
        reinterpret_cast<float *>(scene.scanLine(1))[4]=std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!ImagePipeline::validFloatSource(scene));
        QRhiCommandBuffer *cb=nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        bool accepted=false;
        QString error;
        quint64 uploads=99,rendered=99;
        auto guarded=std::make_unique<GpuEngine>(rhi.get());
        const auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,
                                                ColorManagement::OutputSpace::SRgb,false,0);
        accepted=guarded->process(cb,scene,plan,1,{});
        error=guarded->error();
        uploads=guarded->sourceUploads();
        rendered=guarded->renderedRevision();
        rhi->endOffscreenFrame();rhi->finish();
        guarded.reset();
        QVERIFY(!accepted);
        QVERIFY(error.contains("Invalid") && error.contains("FP32"));
        QCOMPARE(uploads,quint64(0));
        QCOMPARE(rendered,quint64(0));
    }
    void colorGradingCpuGpuParityOnEveryOutputAndThreeTones() {
        QImage source(59,67,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x) {
            const quint16 r=quint16((x*1039+y*1043+300)%65536);
            const quint16 g=quint16((x*2539+y*2381+4000)%65536);
            const quint16 b=quint16((x*4763+y*779+2700)%65536);
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(r,g,b,alpha[(x+y)%4]);
        }
        for(int mode=0;mode<3;++mode) for(int encoding=0;encoding<2;++encoding) for(int space=0;space<4;++space) {
            AdjustmentState state; state.gradingEnabled=true;
            state.gradingHue={20,132,246};
            state.gradingSaturation={90,35,75};
            state.gradingBalance=mode==0 ? -70 : mode==1 ? 0 : 70;
            if(mode==1) { state.blackWhite=true; state.bwMix[0]=50; }
            if(mode==2) { state.look.mode="manual"; state.look.code="ST"; state.exposure=.3; }
            verifyParity(source,state,ImagePipeline::InputEncoding(encoding),
                         ColorManagement::OutputSpace(space),true,"three-way perceptual grade");
            if(QTest::currentTestFailed()) return;
            // Grading requires no CPU upload fallback on the existing tested backends.
            QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void blackWhiteMixCpuGpuParityAndNativeCompute() {
        QImage source(47,39,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) {
            const quint16 r=quint16((x*9421+y*1283)%65536);
            const quint16 g=quint16((x*2837+y*7431)%65536);
            const quint16 b=quint16((x*5213+y*6151)%65536);
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(r,g,b,alpha[(x+y)%4]);
        }
        for(bool sony : {false,true}) for(int encoding=0;encoding<2;++encoding)
            for(int space=0;space<4;++space) {
                AdjustmentState state; state.blackWhite=true; state.exposure=.15;
                state.bwMix={75,-30,45,-25,15,-55,30,65};
                if(sony){state.look.mode="manual";state.look.code="BW";}
                verifyParity(source,state,ImagePipeline::InputEncoding(encoding),
                             ColorManagement::OutputSpace(space),true,"eight-band B&W mix");
                if(QTest::currentTestFailed()) return;
                QVERIFY(!engine->lastProcessUsedCpuFallback());
            }
    }
    // In-memory research-only brush plans cannot be saved or enabled in the
    // user interface. Until the QRhi brush shader is validated, ALL backends
    // must use the existing CPU reference upload and preserve scopes/alpha.
    void researchBrushCpuGpuFallbackFullViewportAndHdrParity() {
        QImage source(47,39,QImage::Format_RGBA32FPx4);
        const std::array<float,4> alphas{0.0f,1.0f/65535.0f,.5f,1.0f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.21f+float(x)*.031f; // HDR scene samples >1
            p[1]=-.03f+float(y)*.016f;
            p[2]=.4f+float((x+y)%9)*.06f;
            p[3]=alphas[(x+y)%4];
        }
        const QImage original=source.copy();
        BrushMaskDocument::Layer oval;
        oval.id="cpu_research";oval.enabled=true;oval.exposureEv=1.7;
        oval.strokes={BrushMaskGeometry::Stroke{
            BrushMaskGeometry::Style{.19,.4,.85},
            {{{.44,.51},1.},{{.56,.47},.65}}}};
        BrushMaskDocument::Layer darker;
        darker.id="shadow_research";darker.enabled=true;darker.exposureEv=-.85;
        darker.strokes={BrushMaskGeometry::Stroke{
            BrushMaskGeometry::Style{.13,.72,.65},{{{.59,.41},1.}}}};
        AdjustmentState brush;brush.brushMasks.layers={oval,darker};
        QVERIFY(BrushMaskDocument::validDocument(brush.brushMasks));
        QVERIFY(!AdjustmentState::validBrushMaskJson(brush.toJson()));
        brush.linearMaskEnabled=true;brush.linearMaskExposure=-.4;brush.linearMaskAngle=22;
        brush.vignetteAmount=-.4;
        for(const auto frame : {QRectF(0,0,1,1),QRectF(.18,.13,.61,.58)})
            for(const auto output : {ColorManagement::OutputSpace::SRgb,
                                     ColorManagement::OutputSpace::DisplayP3,
                                     ColorManagement::OutputSpace::ProPhotoRgb}) {
                verifyParity(source,brush,ImagePipeline::InputEncoding::LinearProPhoto,
                             output,true,"M4.4A research CPU brush GPU fallback",frame);
                if (QTest::currentTestFailed()) return;
                QVERIFY(engine->lastProcessUsedCpuFallback());
            }
        // The sixth argument is the explicit full-image ROI. {} is an EMPTY
        // QRectF, not the default {0,0,1,1}; it correctly fails the strict
        // research mask viewport check and poisons this test engine.
        auto withDiagnostic=render(source,brush,true,ImagePipeline::InputEncoding::LinearProPhoto,
                                   ColorManagement::OutputSpace::SRgb,QRectF(0,0,1,1),true);
        QVERIFY(!withDiagnostic.image.isNull());
        QVERIFY(engine->lastProcessUsedCpuFallback());
        QCOMPARE(withDiagnostic.diagnostic.value("producer").toString(),
                 QString("cpu-reference-upload"));
        QCOMPARE(source,original);
        for(auto &layer:brush.brushMasks.layers) layer.enabled=false;
        auto disabled=render(source,brush,false);
        QVERIFY(!disabled.image.isNull());
        QVERIFY(!engine->lastProcessUsedCpuFallback()); // ordinary GPU compute
    }
    void radialMaskCpuGpuParityInvertedEllipseViewportAndSourceAlpha() {
        QImage source(73,91,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*911+y*107)%65536,(x*487+y*311)%65536,(x*1177+y*59)%65536,alpha[(x+y)%4]);
        for(int mode=0;mode<4;++mode) {
            AdjustmentState state;state.radialMaskEnabled=true;
            state.radialMaskExposure=mode==0?1.5:mode==1?-2.5:mode==2?2.0:-1.2;
            state.radialMaskCenterX=mode==0?.5:mode==1?.25:mode==2?.78:.4;
            state.radialMaskCenterY=mode==0?.5:mode==1?.68:mode==2?.3:.6;
            state.radialMaskRadiusX=mode==1?.12:mode==2?.7:.35;
            state.radialMaskRadiusY=mode==1?.68:mode==2?.17:.4;
            state.radialMaskFeather=mode==1?.02:mode==2?.9:.33;
            state.radialMaskInverted=mode>=2;
            state.radialMaskAngle=mode==0?0:mode==1?45:mode==2?90:-125;
            if(mode==3) {state.linearMaskEnabled=true;state.linearMaskExposure=.65;
                          state.linearMaskAngle=32;state.vignetteAmount=-.5;}
            for(const auto &frame : {QRectF(0,0,1,1),QRectF(.17,.23,.46,.4)})
                for(int encoding=0;encoding<2;++encoding)
                    for(int space=0;space<4;++space) {
                        verifyParity(source,state,ImagePipeline::InputEncoding(encoding),
                                     ColorManagement::OutputSpace(space),true,
                                     "independent radial mask",frame);
                        if(QTest::currentTestFailed())return;
                    }
        }
    }
    void linearMaskCpuGpuParityIncludesFullImageViewportAndAlpha() {
        QImage source(71,93,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*941+y*113)%65536,(x*433+y*277)%65536,(x*1531+y*71)%65536,alpha[(x+y)%4]);
        for(double angle : {0.0,90.0,-45.0,180.0})
            for(const auto &frame : {QRectF(0,0,1,1),QRectF(.13,.18,.52,.43)})
                for(int encoding=0;encoding<2;++encoding)
                    for(const auto space : {ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::ProPhotoRgb}) {
                        AdjustmentState mask;mask.linearMaskEnabled=true;mask.linearMaskExposure=-2;
                        mask.linearMaskCenter=.4;mask.linearMaskFeather=.35;mask.linearMaskAngle=angle;
                        mask.vignetteAmount=-.3;
                        verifyParity(source,mask,ImagePipeline::InputEncoding(encoding),space,true,"graduated linear mask",frame);
                        if(QTest::currentTestFailed())return;
                    }
    }
    void vignetteCpuGpuParityIncludesFullFrameAndViewportCoordinates() {
        QImage source(71,93,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64((x*941+y*113)%65536,(x*433+y*277)%65536,(x*1531+y*71)%65536,alpha[(x+y)%4]);
        const std::array<std::array<double,3>,5> cases{{{{0,.5,1}},{{-3,0,1}},{{-1.5,.4,.7}},{{3,0,.8}},{{2,.95,.01}}}};
        for (const auto &v : cases) for (const auto &frame : {QRectF(0,0,1,1),QRectF(.13,.18,.52,.43)})
            for (int encoding=0;encoding<2;++encoding) for (int space=0;space<4;++space) for (bool sony : {false,true}) {
                AdjustmentState state; state.exposure=.3; state.vignetteAmount=v[0]; state.vignetteMidpoint=v[1]; state.vignetteFeather=v[2];
                if (sony) { state.look.mode="manual"; state.look.code="FL"; state.look.parameters={{"sharpness",3},{"clarity",2}}; }
                verifyParity(source,state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(space),true,"vignette",frame);
            }
    }
    void cpuCorrectedGeometryFeedsGpuWithoutRelaxingParity() {
        QImage source(131,97,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(2000+x*301,1000+y*503,3000+x*179+y*137,65535);
        for (int mode=0;mode<5;++mode) {
            AdjustmentState state; state.exposure=.4; state.temperature=15; state.saturation=12;
            auto &g=state.geometry;
            if (mode==0) g.perspectiveHorizontal=.15;
            if (mode==1) g.perspectiveVertical=-.12;
            if (mode==2) g.distortion=.2;
            if (mode==3) { g.redCa=1.2; g.blueCa=-1.1; }
            if (mode==4) { g.perspectiveHorizontal=.1; g.perspectiveVertical=-.08; g.distortion=-.15; g.redCa=.7; g.blueCa=-.6; g.straighten=5; g.crop={.1,.2,.7,.6}; g.quarterTurns=1; }
            const auto prepared=g.apply(source); QVERIFY(!prepared.isNull());
            for (auto space : {ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::AdobeRgb,ColorManagement::OutputSpace::ProPhotoRgb})
                verifyParity(prepared,state,ImagePipeline::InputEncoding::LinearProPhoto,space,true,"CPU manual geometry -> GPU color");
        }
    }
    void cleanupTestCase() { engine.reset(); rhi.reset(); surface.reset(); }
    void pixelsAndHistogramMatchCpu() {
        // Retain the historical fixture as well as the explicitly ordered new
        // fixtures below. Function-argument RNG order differs across compilers.
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
            qInfo()<<"CPU/GPU mode"<<mode;
            verifyParity(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"historical fixture");
            if(QTest::currentTestFailed()) return;
        }
    }
    void sonyLooksDetailsAndLuts() {
        QImage image(83,95,QImage::Format_RGBA64);std::mt19937 rng(81138);
        for(int y=0;y<image.height();++y){auto *p=reinterpret_cast<QRgba64*>(image.scanLine(y));for(int x=0;x<image.width();++x){
            const quint16 r=rng()%58000,g=rng()%58000,b=rng()%58000;p[x]=QRgba64::fromRgba64(r,g,b,65535);}}
        for(const auto &v:LookProfiles::catalog()) {
            AdjustmentState s;s.look.mode="manual";s.look.code=v.toMap()["code"].toString();
            verifyParity(image,s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"Sony approximate preset");
            if(QTest::currentTestFailed())return;
            s.look.parameters={{"contrast",3},{"highlights",-2},{"shadows",2},{"fade",2},{"saturation",-1},{"sharpness",8},{"sharpnessRange",5},{"clarity",7}};
            verifyParity(image,s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"Sony fine adjustments + detail");
            if(QTest::currentTestFailed())return;
        }
        for(int n:{2,17,33})for(int space=0;space<4;++space) {
            auto lut=std::make_shared<LookLut>(*LookLut::identity(n));
            for(int i=0;i<lut->rgb.size();i+=3){lut->rgb[i]=.04f+.86f*lut->rgb[i];lut->rgb[i+2]=.02f+.92f*lut->rgb[i+2];}
            lut->updateDigest();AdjustmentState s;s.look.mode="calibrated";s.look.lut=lut;s.look.strength=.7;
            s.look.parameters={{"sharpness",6},{"clarity",4}};
            verifyParity(image,s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"LUT and details across output spaces");
            if(QTest::currentTestFailed())return;
        }
    }
    void realSonyEmpiricalProfileParity() {
        const QString fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony dataset configured; not a real-profile pass");}
        const QString path=qEnvironmentVariable("JIXELLIGHT_TEST_SONY_PROFILE",QDir::currentPath()+"/sony-real-reports/dataset/empirical.jlook.json");
        QFile file(path);QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(path));
        const auto json=QJsonDocument::fromJson(file.readAll()).object();
        const auto look=LookState::fromJson(json["look"].toObject());QVERIFY2(look.lut,qPrintable(look.error));
        QVERIFY(look.lut->evidence["independentValidationPassed"].toBool());
        QString error;auto source=RawDecoder::decode(fixtures+"/sony_a7_iv_07.arw",&error);
        QVERIFY2(!source.isNull(),qPrintable(error));source=source.scaled(257,257,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        for(int space=0;space<4;++space){AdjustmentState state;state.look=look;
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"Real Sony held-out RAW + empirical LUT");
            if(QTest::currentTestFailed())return;
            state.look.strength=.7;state.look.parameters={{"sharpness",4},{"clarity",1}};
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"Real Sony LUT strength and details");
            if(QTest::currentTestFailed())return;
        }
    }
    void denseColorParity_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("mixed-hsl")<<2;
        QTest::newRow("three-stop-highlight-boundary")<<3;
    }
    void denseColorParity() {
        QFETCH(int,mode);
        QImage image(1024,1024,QImage::Format_RGBA64);
        std::mt19937 random(652819);
        for(int y=0;y<image.height();++y) {
            auto *line=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<image.width();++x) {
                const quint16 r=random()%60000, g=random()%60000, b=random()%60000;
                line[x]=QRgba64::fromRgba64(r,g,b,65535);
            }
        }
        AdjustmentState state;
        if(mode==2) {
            state.hue=-23;state.saturation=18;state.vibrance=22;
            state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8;
        } else {state.exposure=3;state.saturation=-20;state.blacks=-50;state.whites=60;}
        verifyParity(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,"dense parity");
    }
    void knownBoundaryNeighborhoods() {
        constexpr quint16 seeds[][3]={{16492,36979,31231},{18743,43453,32369},{20306,54764,34044},
                                       {28853,59865,40358},{22323,46996,38591}};
        QImage image(257,15,QImage::Format_RGBA64);
        for(int y=0;y<15;++y) {
            auto *line=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<257;++x) {
                int channels[]{seeds[y/3][0],seeds[y/3][1],seeds[y/3][2]};
                channels[y%3]+=x-128;
                line[x]=QRgba64::fromRgba64(channels[0],channels[1],channels[2],65535);
            }
        }
        AdjustmentState state;state.exposure=3;state.saturation=-20;state.blacks=-50;state.whites=60;
        verifyParity(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"boundary neighborhoods");
    }
    void encodingsAndOutputSpaces_data() {
        QTest::addColumn<int>("encoding");QTest::addColumn<int>("output");
        for(int e=0;e<2;++e) for(int s=0;s<4;++s)
            QTest::newRow(qPrintable(QString("encoding-%1-output-%2").arg(e).arg(s)))<<e<<s;
    }
    void encodingsAndOutputSpaces() {
        QFETCH(int,encoding);QFETCH(int,output);
        QImage image(97,73,QImage::Format_RGBA64);
        std::mt19937 rng(993781);
        for(int y=0;y<image.height();++y) {
            auto *row=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<image.width();++x) {
                const quint16 r=rng()%65536,g=rng()%65536,b=rng()%65536;
                row[x]=QRgba64::fromRgba64(r,g,b,65535);
            }
        }
        AdjustmentState state;state.temperature=30;state.tint=-20;state.exposure=.4;state.contrast=10;state.highlights=-20;
        const auto enc=encoding==0?ImagePipeline::InputEncoding::SRgb:ImagePipeline::InputEncoding::LinearProPhoto;
        verifyParity(image,state,enc,ColorManagement::OutputSpace(output),true,"encoding/output parity");
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
