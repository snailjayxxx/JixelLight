#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QScopeGuard>
#include <QRgba64>
#include <random>
#include <cstring>
#include <cmath>
#include <limits>
#include "core/gpu/GpuEngine.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/pipeline/FloatFrameFingerprint.h"
#include "core/pipeline/FloatSceneResampler.h"
#include "core/color/MonitorColorTransform.h"
#include "core/scopes/ScopesEngine.h"
#include "core/look/LookProfiles.h"
#include "core/raw/RawDecoder.h"
#include "core/cache/SourceCache.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QFile>
#include <QScopeGuard>
#include <QDir>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <QtConcurrent>
#include "core/gpu/brush/NativeBrushFrame.h"

class NativeBrushLuminanceGpuTests : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<GpuEngine> engine;
    static AdjustmentState nativeLuminanceState(double exposure,bool inverted) {
        AdjustmentState state;state.exposure=.17;state.temperature=37;state.tint=-12;
        state.luminanceMaskEnabled=true;state.luminanceMaskExposure=exposure;state.luminanceMaskInverted=inverted;
        state.luminanceMaskLower=.12;state.luminanceMaskUpper=.6;state.luminanceMaskFeather=.04;
        state.linearMaskEnabled=true;state.linearMaskExposure=.15;
        state.radialMaskEnabled=true;state.radialMaskExposure=-.2;
        state.vignetteAmount=-.6;
        BrushMaskGeometry::Stroke paint;paint.style={.31,.42,.8};paint.dabs={{{.28,.37},.5},{{.69,.62},.9}};
        auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.3,.38},.3}};
        auto repaint=paint;repaint.dabs={{{.34,.4},.6}};
        state.brushMasks.layers.push_back({"range-paint-erase",true,.7,{paint,erase,repaint}});
        state.brushMasks.layers.push_back({"range-negative",true,-.45,{paint}});
        return state;
    }
    void verifyNativeLuminance(const QImage &image,const AdjustmentState &state,ImagePipeline::InputEncoding encoding,
                               ColorManagement::OutputSpace output,bool raw,const QRectF &frame={0,0,1,1},bool pointMustChange=false) {
        namespace N=NativeBrush;
        auto budget=std::make_shared<BrushRenderBudget>();
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();
        QVERIFY(caps.active());auto plan=ProcessingPlan::compile(state,encoding,output,raw,raw?.35f:0.f);plan.setFrameRect(frame);
        struct Prepared {N::CanonicalSource::Ptr source;N::UploadAdmission::Ptr upload;N::FrameAdmission::Ptr frame;BrushImageLease reference;bool pointChanged=false;N::FrameFailure why=N::FrameFailure::None;};
        Prepared prepared;
        {
            QFutureWatcher<Prepared> watcher;QSignalSpy finished(&watcher,&QFutureWatcher<Prepared>::finished);
            QVERIFY(finished.isValid());watcher.setFuture(QtConcurrent::run([=] {
                Prepared result;result.source=N::CanonicalSource::capture(image,{"range-owned",encoding,raw,raw?.35f:0.f,frame},caps.threads(),budget);
                if(!result.source)return result;
                result.upload=N::UploadAdmission::prepare(result.source,caps);
                result.frame=N::FrameAdmission::prepare(result.source,caps,plan,{"range-catalog","range-input","range-version",1},1,&result.why);
                const auto request=result.source->exactRequest(plan);if(request)result.reference=request->render({});
                if(pointMustChange&&result.reference) {
                    auto inactive=state;inactive.pointColorEnabled=false;
                    auto off=ProcessingPlan::compile(inactive,encoding,output,raw,raw?.35f:0.f);off.setFrameRect(frame);
                    const auto withoutPoint=result.source->exactRequest(off);
                    if(withoutPoint) {
                        const auto unchanged=withoutPoint->render({});
                        result.pointChanged=unchanged&&unchanged.image()!=result.reference.image();
                    }
                }
                return result;
            }));
            if(!watcher.isFinished())QVERIFY(finished.wait(10000));
            QVERIFY(watcher.isFinished());prepared=watcher.result();
        }
        const auto context=QString("native luminance: format=%1 encoding=%2 output=%3 RAW=%4 EV=%5 invert=%6 refusal=%7")
            .arg(int(image.format())).arg(int(encoding)).arg(int(output)).arg(raw).arg(state.luminanceMaskExposure).arg(state.luminanceMaskInverted).arg(int(prepared.why));
        QVERIFY2(prepared.upload&&prepared.frame&&prepared.reference,qPrintable(context));
        if(pointMustChange)QVERIFY2(prepared.pointChanged,"Point Color fixture must visibly change the independent CPU reference");
        auto input=domain->attach(prepared.upload);QVERIFY(bool(input));QRhiCommandBuffer *cb=nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QVERIFY(domain->upload(input,cb));rhi->endOffscreenFrame();rhi->finish();domain->collect();QVERIFY(domain->ready(input));
        QVERIFY(domain->activate(prepared.frame->stamp()));auto handle=domain->attachFrame(input,prepared.frame);QVERIFY(bool(handle));
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QVERIFY(domain->compute(handle,cb));rhi->endOffscreenFrame();rhi->finish();domain->collect();QVERIFY(domain->publishable(handle));
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QRhiReadbackResult readback;bool done=false;readback.completed=[&]{done=true;};
        auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(domain->texture(handle)),&readback);cb->resourceUpdate(updates);
        QVERIFY(domain->histogram(handle,cb));rhi->endOffscreenFrame();rhi->finish();domain->collect();QVERIFY(done);
        QCOMPARE(readback.pixelSize,image.size());QCOMPARE(readback.format,QRhiTexture::RGBA32F);QCOMPARE(readback.data.size(),image.width()*image.height()*16);
        const auto original=ImagePipeline::floatSource(image);const auto *values=reinterpret_cast<const float *>(readback.data.constData());
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            const auto *actual=values+4*(y*image.width()+x),*source=reinterpret_cast<const float *>(original.constScanLine(y))+4*x;
            for(int channel=0;channel<4;++channel)QVERIFY2(std::isfinite(actual[channel]),qPrintable(context));
            QCOMPARE(actual[3],source[3]);
        }
        const QImage view(reinterpret_cast<const uchar *>(readback.data.constData()),image.width(),image.height(),image.width()*16,QImage::Format_RGBA32FPx4);
        const auto actual=ImagePipeline::rgba64Source(view);const auto &expected=prepared.reference.image();int maximum=0;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            const auto a=reinterpret_cast<const QRgba64 *>(actual.constScanLine(y))[x],b=reinterpret_cast<const QRgba64 *>(expected.constScanLine(y))[x];
            maximum=std::max({maximum,std::abs(int(a.red())-int(b.red())),std::abs(int(a.green())-int(b.green())),std::abs(int(a.blue())-int(b.blue()))});QCOMPARE(a.alpha(),b.alpha());
        }
        qInfo().noquote()<<context<<"maximum RGB16 error"<<maximum;QVERIFY2(maximum<=40,qPrintable(context+QString(" max=%1").arg(maximum)));
        auto replies=domain->takeHistogramReplies();QCOMPARE(replies.size(),std::size_t(1));QCOMPARE(replies[0]->stamp,prepared.frame->stamp());
        const auto histogram=ScopesEngine::fromGpu(replies[0]->counts,replies[0]->pixels),oracle=ScopesEngine::analyze(actual);
        QCOMPARE(histogram.pixelCount,quint64(image.width())*image.height());QCOMPARE(histogram.red,oracle.red);QCOMPARE(histogram.green,oracle.green);QCOMPARE(histogram.blue,oracle.blue);QCOMPARE(histogram.luma,oracle.luma);
        QCOMPARE(histogram.shadowClipPercent,oracle.shadowClipPercent);QCOMPARE(histogram.highlightClipPercent,oracle.highlightClipPercent);
        const auto trace=domain->trace();QCOMPARE(trace.sourceUploads,quint64(1));QCOMPARE(trace.coverageDispatches,quint64(1));QCOMPARE(trace.colourDispatches,quint64(1));QCOMPARE(trace.histogramDispatches,quint64(1));
        QCOMPARE(trace.controlReadbackBytes,quint64(8));QCOMPARE(trace.histogramReadbackBytes,quint64(16400));QCOMPARE(domain->normalFinishCalls(),quint64(0));
        domain->release(handle);domain->release(input);domain->collect();domain->shutdown();QCOMPARE(domain->teardownFinishCalls(),quint64(0));
        replies.clear();prepared={};domain.reset();QCOMPARE(budget->snapshot().charged,qint64(0));
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
    void nativeBrushLuminanceMatchesReferenceAcrossSources() {
        QImage integer(37,29,QImage::Format_RGBA64),floating(37,29,QImage::Format_RGBA32FPx4);
        for(int y=0;y<29;++y)for(int x=0;x<37;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,quint16(((x+y)%4)*21845));
            auto *p=reinterpret_cast<float *>(floating.scanLine(y))+4*x;p[0]=.02f+2.f*x/36.f;p[1]=.01f+1.5f*y/28.f;p[2]=.1f+1.2f*((x+y)%17)/16.f;p[3]=float((x+y)%4)/3.f;
        }
        for(bool fp32:{false,true})for(int source=0;source<3;++source)for(int output=0;output<4;++output)
        for(double exposure:{-2.1,1.1})for(bool inverted:{false,true}) {
            const auto state=nativeLuminanceState(exposure,inverted);
            verifyNativeLuminance(fp32?floating:integer,state,source==0?ImagePipeline::InputEncoding::SRgb:ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),source==2);
            if(QTest::currentTestFailed())return;
        }
    }
    void nativeBrushLuminanceNarrowShouldersHdrAndAlpha() {
        QImage image(37,13,QImage::Format_RGBA32FPx4);
        const float levels[]{-.03f,0.f,.1989999f,.199f,.1995f,.2f,.2005f,.201f,.3f,.699f,.7f,.7005f,.701f,.8f,1.999f,2.f,2.0005f,2.001f,3.5f,65504.f};
        const float alpha[]{0.f,1.e-30f,.375f,1.f};
        for(int y=0;y<13;++y)for(int x=0;x<37;++x) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;p[0]=p[1]=p[2]=levels[(x+y)%std::size(levels)];p[3]=alpha[(x+2*y)%4];
        }
        for(const auto range: {std::array<double,3>{0,0,.001},{.2,.2,.001},{.2,.7,.001},{.2,.7,1},{2,2,.001}})
        for(double exposure:{-3.,3.})for(bool inverted:{false,true})for(int output=0;output<3;++output) {
            auto state=nativeLuminanceState(exposure,inverted);state.luminanceMaskLower=range[0];state.luminanceMaskUpper=range[1];state.luminanceMaskFeather=range[2];
            state.grainAmount=output==2?0:19;
            verifyNativeLuminance(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,{.125,.25,.5,.5});
            if(QTest::currentTestFailed())return;
        }
    }
    void nativeBrushLuminanceRetainsAdmissionPolicies() {
        namespace N=NativeBrush;QImage image(19,13,QImage::Format_RGBA32FPx4);image.fill(QColor::fromRgbF(.2,.3,.4,.375));
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();QVERIFY(caps.active());
        const bool restricted=rhi->backend()==QRhi::D3D11||rhi->backend()==QRhi::OpenGLES2;
        QFutureWatcher<QVector<bool>> watcher;watcher.setFuture(QtConcurrent::run([=] {
            QVector<bool> checks;auto budget=std::make_shared<BrushRenderBudget>();auto state=nativeLuminanceState(1.4,false);
            const auto source=N::CanonicalSource::capture(image,{"range-policy",ImagePipeline::InputEncoding::LinearProPhoto,false,0,{0,0,1,1}},caps.threads(),budget);
            if(!source)return checks;
            const auto attempt=[&](const ProcessingPlan &plan,bool accepted,N::FrameFailure expected=N::FrameFailure::CPUPolicy) {
                N::FrameFailure why=N::FrameFailure::None;const auto frame=N::FrameAdmission::prepare(source,caps,plan,{"policy-catalog","policy-input","policy-version",1},1,&why);
                qInfo()<<"native luminance policy"<<checks.size()<<"admitted"<<bool(frame)<<"refusal"<<int(why);
                checks.push_back(accepted?bool(frame)&&why==N::FrameFailure::None:!frame&&why==expected);
            };
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
            attempt(plan,true);
            attempt(ProcessingPlan::compile(state,plan.encoding,plan.output,true,0),false,N::FrameFailure::InvalidSource);
            auto malformed=plan;malformed.data[ProcessingPlan::LuminanceMask].w=0;attempt(malformed,false,N::FrameFailure::CPUPlan);
            malformed=plan;malformed.data[ProcessingPlan::LuminanceMaskOptions].x=2;attempt(malformed,false,N::FrameFailure::CPUPlan);
            auto point=state;point.pointColorEnabled=true;point.pointColorHue=23;point.pointColorHueRange=5;attempt(ProcessingPlan::compile(point,plan.encoding,plan.output,false,0),false);
            auto color=state;color.colorMaskEnabled=true;color.colorMaskExposure=.5;attempt(ProcessingPlan::compile(color,plan.encoding,plan.output,false,0),true);
            auto grain=state;grain.grainAmount=19;attempt(ProcessingPlan::compile(grain,plan.encoding,ColorManagement::OutputSpace::AdobeRgb,false,0),false);
            auto bright=state;bright.exposure=.6;bright.saturation=21;attempt(ProcessingPlan::compile(bright,plan.encoding,plan.output,false,0),!restricted);
            return checks;
        }));
        QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(),10000);const auto checks=watcher.result();QCOMPARE(checks.size(),8);
        for(int i=0;i<checks.size();++i)QVERIFY2(checks[i],qPrintable(QString("native luminance admission policy %1").arg(i)));
        domain->shutdown();
    }
    void nativeBrushColorMaskMatchesSourcesAndLuminance() {
        QImage integer(37,29,QImage::Format_RGBA64),floating(37,29,QImage::Format_RGBA32FPx4);
        for(int y=0;y<29;++y)for(int x=0;x<37;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,quint16(((x+y)%4)*21845));
            auto *p=reinterpret_cast<float *>(floating.scanLine(y))+4*x;p[0]=.02f+2.f*x/36.f;p[1]=.01f+1.5f*y/28.f;p[2]=.1f+1.2f*((x+y)%17)/16.f;p[3]=float((x+y)%4)/3.f;
        }
        for(bool fp32:{false,true})for(int source=0;source<3;++source)for(int output:{0,1,3})
        for(double exposure:{-2.1,1.1})for(bool inverted:{false,true}) {
            auto state=nativeLuminanceState(.4,!inverted);state.colorMaskEnabled=true;state.colorMaskExposure=exposure;state.colorMaskInverted=inverted;
            state.colorMaskLab={.6,.02,.01};state.colorMaskHueRange=90;state.colorMaskChromaRange=.12;state.colorMaskLightnessRange=.4;
            qInfo()<<"native colour broad"<<fp32<<source<<output<<exposure<<inverted;
            verifyNativeLuminance(fp32?floating:integer,state,source==0?ImagePipeline::InputEncoding::SRgb:ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),source==2);
            if(QTest::currentTestFailed())return;
        }
    }
    void nativeBrushColorMaskHueSeamsAndNeutrals() {
        constexpr float Pi=3.14159265358979323846f;
        for(const auto center:{PointColor::Lab{.62f,.08f,359.f},PointColor::Lab{.035f,.012f,179.f},PointColor::Lab{.6f,0,0}})
        for(const auto range:{std::array<float,3>{45,.003f,.003f},{90,.02f,.12f},{180,.12f,.4f}}) {
            const float rH=range[0],rC=range[1],rL=range[2],angle=center.b*(Pi/180.f);QImage image(31,29,QImage::Format_RGBA32FPx4);
            for(int y=0;y<29;++y)for(int x=0;x<31;++x) {
                const float t=float((x*7+y*11)%101)/100.f,L=center.L+rL*(t*2-1),C=std::max(0.f,center.a+rC*(float(x)/30.f*2-1));
                const float h=(center.b+rH*(float(y)/28.f*2-1))*(Pi/180.f);
                const auto rgb=PointColor::toEncodedSrgb({L,C*std::cos(h),C*std::sin(h)});auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
                for(int c=0;c<3;++c)p[c]=rgb[c];const float alpha[]{0,1.e-9f,1.f/65535.f,.8f,1};p[3]=alpha[(x+y)%5];
            }
            for(int output:{0,1,3})for(double exposure:{-3.,3.})for(bool inverted:{false,true}) {
                auto state=nativeLuminanceState(-.35,!inverted);state.colorMaskEnabled=true;state.colorMaskExposure=exposure;state.colorMaskInverted=inverted;
                state.colorMaskLab={center.L,center.a*std::cos(angle),center.a*std::sin(angle)};state.colorMaskHueRange=rH;state.colorMaskChromaRange=rC;state.colorMaskLightnessRange=rL;state.grainAmount=19;
                qInfo()<<"native colour seam"<<center.L<<center.a<<center.b<<rH<<rC<<rL<<output<<exposure<<inverted;
                verifyNativeLuminance(image,state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output),false,{.125,.25,.5,.5});
                if(QTest::currentTestFailed())return;
            }
        }
    }
    void nativeBrushColorMaskRetainsCpuPolicies() {
        namespace N=NativeBrush;QImage image(19,13,QImage::Format_RGBA32FPx4);image.fill(QColor::fromRgbF(.2,.3,.4,.375));
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();QVERIFY(caps.active());
        const bool restricted=rhi->backend()==QRhi::D3D11||rhi->backend()==QRhi::OpenGLES2;
        QFutureWatcher<QVector<bool>> watcher;watcher.setFuture(QtConcurrent::run([=] {
            QVector<bool> checks;auto budget=std::make_shared<BrushRenderBudget>();auto state=nativeLuminanceState(.4,false);
            state.colorMaskEnabled=true;state.colorMaskExposure=1.1;state.colorMaskLab={.6,.02,.01};state.colorMaskHueRange=90;state.colorMaskChromaRange=.12;state.colorMaskLightnessRange=.4;
            auto source=N::CanonicalSource::capture(image,{"colour-policy",ImagePipeline::InputEncoding::LinearProPhoto,false,0,{0,0,1,1}},caps.threads(),budget);if(!source)return checks;
            const auto attempt=[&](const ProcessingPlan &plan,bool accepted,N::FrameFailure expected=N::FrameFailure::CPUPolicy) {
                N::FrameFailure why=N::FrameFailure::None;const auto frame=N::FrameAdmission::prepare(source,caps,plan,{"colour-catalog","colour-input","colour-version",1},1,&why);
                qInfo()<<"native colour policy"<<checks.size()<<"admitted"<<bool(frame)<<"refusal"<<int(why);checks.push_back(accepted?bool(frame)&&why==N::FrameFailure::None:!frame&&why==expected);
            };
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);attempt(plan,true);
            attempt(ProcessingPlan::compile(state,plan.encoding,plan.output,true,0),false,N::FrameFailure::InvalidSource);
            auto malformed=plan;malformed.data[ProcessingPlan::ColorMaskRange].y=0;attempt(malformed,false,N::FrameFailure::CPUPlan);
            malformed=plan;malformed.data[ProcessingPlan::ColorMaskRange].w=2;attempt(malformed,false,N::FrameFailure::CPUPlan);
            auto narrow=state;narrow.colorMaskHueRange=44.9;attempt(ProcessingPlan::compile(narrow,plan.encoding,plan.output,false,0),false);
            narrow=state;narrow.colorMaskChromaRange=.001;attempt(ProcessingPlan::compile(narrow,plan.encoding,plan.output,false,0),false);
            narrow=state;narrow.colorMaskLightnessRange=.001;attempt(ProcessingPlan::compile(narrow,plan.encoding,plan.output,false,0),false);
            attempt(ProcessingPlan::compile(state,plan.encoding,ColorManagement::OutputSpace::AdobeRgb,false,0),false);
            auto point=state;point.pointColorEnabled=true;point.pointColorHue=23;point.pointColorHueRange=5;attempt(ProcessingPlan::compile(point,plan.encoding,plan.output,false,0),false);
            auto bright=state;bright.saturation=21;attempt(ProcessingPlan::compile(bright,plan.encoding,plan.output,false,0),!restricted);
            return checks;
        }));
        QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(),10000);const auto checks=watcher.result();QCOMPARE(checks.size(),10);
        for(int i=0;i<checks.size();++i)QVERIFY2(checks[i],qPrintable(QString("native colour admission policy %1").arg(i)));domain->shutdown();
    }
    void nativeBrushPointColorMatchesSourcesAndMasks() {
        QImage integer(37,29,QImage::Format_RGBA64),floating(37,29,QImage::Format_RGBA32FPx4);
        for(int y=0;y<29;++y)for(int x=0;x<37;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,quint16(((x+y)%4)*21845));
            auto *p=reinterpret_cast<float *>(floating.scanLine(y))+4*x;
            p[0]=.02f+2.f*x/36.f;p[1]=.01f+1.5f*y/28.f;p[2]=.1f+1.2f*((x+y)%17)/16.f;p[3]=float((x+y)%4)/3.f;
        }
        for(bool fp32:{false,true})for(int source=0;source<3;++source)for(int output=0;output<4;++output)
        for(const auto adjustment:{std::array<double,3>{-33,27,12},{42,-32,-18},{0,55,21}}) {
            const auto &image=fp32?floating:integer;
            const auto encoding=source==0?ImagePipeline::InputEncoding::SRgb:ImagePipeline::InputEncoding::LinearProPhoto;
            const auto space=ColorManagement::OutputSpace(output);const bool raw=source==2;
            auto state=nativeLuminanceState(.4,false);
            state.colorMaskEnabled=space!=ColorManagement::OutputSpace::AdobeRgb;state.colorMaskExposure=.25;
            state.colorMaskHueRange=90;state.colorMaskChromaRange=.12;state.colorMaskLightnessRange=.4;
            const auto selected=ImagePipeline::samplePointColor(image,ProcessingPlan::compile(state,encoding,space,raw,raw?.35f:0.f),{18,13});
            QVERIFY(selected.has_value());state.pointColorLab={selected->L,selected->a,selected->b};
            state.pointColorEnabled=true;state.pointColorHue=adjustment[0];state.pointColorSaturation=adjustment[1];state.pointColorLuminance=adjustment[2];
            state.pointColorHueRange=180;state.pointColorChromaRange=1;state.pointColorLightnessRange=1;
            qInfo()<<"native point sources"<<fp32<<source<<output<<adjustment[0]<<adjustment[1]<<adjustment[2];
            verifyNativeLuminance(image,state,encoding,space,raw,{0,0,1,1},true);
            if(QTest::currentTestFailed())return;
        }
    }
    void nativeBrushPointColorHueSeamsAndNeutrals() {
        constexpr float Pi=3.14159265358979323846f;
        for(const auto center:{PointColor::Lab{.62f,.08f,359.f},PointColor::Lab{.035f,.012f,179.f},PointColor::Lab{.6f,0,0}})
        for(const auto range:{std::array<float,3>{45,.003f,.003f},{90,.02f,.12f},{180,.12f,.4f}}) {
            const float rH=range[0],rC=range[1],rL=range[2],angle=center.b*(Pi/180.f);QImage image(31,29,QImage::Format_RGBA32FPx4);
            for(int y=0;y<29;++y)for(int x=0;x<31;++x) {
                const float L=center.L+rL*(float((x*7+y*11)%101)/50.f-1),C=std::max(0.f,center.a+rC*(float(x)/30.f*2-1));
                const float hue=(center.b+rH*(float(y)/28.f*2-1))*(Pi/180.f);
                const auto rgb=PointColor::toEncodedSrgb({L,C*std::cos(hue),C*std::sin(hue)});
                auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;for(int c=0;c<3;++c)p[c]=rgb[c];
                const float alpha[]{0,1.e-30f,1.e-9f,.8f,1};p[3]=alpha[(x+y)%5];
            }
            for(int output=0;output<4;++output)for(double shift:{-87.,87.}) {
                auto state=nativeLuminanceState(-.35,false);
                state.pointColorEnabled=true;state.pointColorLab={center.L,center.a*std::cos(angle),center.a*std::sin(angle)};
                state.pointColorHue=shift;state.pointColorSaturation=shift<0?-73:73;state.pointColorLuminance=shift<0?-37:37;
                state.pointColorHueRange=rH;state.pointColorChromaRange=rC;state.pointColorLightnessRange=rL;
                state.grainAmount=ColorManagement::OutputSpace(output)==ColorManagement::OutputSpace::AdobeRgb?0:19;
                qInfo()<<"native point seam"<<center.L<<center.a<<center.b<<rH<<rC<<rL<<output<<shift;
                verifyNativeLuminance(image,state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output),false,{.125,.25,.5,.5});
                if(QTest::currentTestFailed())return;
            }
        }
    }
    void nativeBrushPointColorRealSonyCropMatchesReference() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixtures configured");}
        const auto loaded=loadSourceFresh(QDir(folder).filePath("sony_a7_iv_07.arw"),{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));
        const QRect crop((loaded.image.width()-128)/2,(loaded.image.height()-96)/2,128,96);
        const auto image=loaded.image.copy(crop);
        const QRectF frame(double(crop.x())/loaded.image.width(),double(crop.y())/loaded.image.height(),double(crop.width())/loaded.image.width(),double(crop.height())/loaded.image.height());
        for(int output=0;output<4;++output)for(double shift:{-87.,87.}) {
            auto state=nativeLuminanceState(-.35,false);const auto space=ColorManagement::OutputSpace(output);
            auto picking=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,true,.35f);picking.setFrameRect(frame);
            const auto selected=ImagePipeline::samplePointColor(image,picking,{64,48});QVERIFY(selected.has_value());
            state.pointColorEnabled=true;state.pointColorLab={selected->L,selected->a,selected->b};
            state.pointColorHue=shift;state.pointColorSaturation=41;state.pointColorLuminance=shift<0?-19:19;
            state.pointColorHueRange=180;state.pointColorChromaRange=1;state.pointColorLightnessRange=1;
            qInfo()<<"native point Sony full-decode/cropped-export"<<output<<shift;
            verifyNativeLuminance(image,state,ImagePipeline::InputEncoding::LinearProPhoto,space,true,frame,true);
            if(QTest::currentTestFailed())return;
        }
    }
    void nativeBrushPointColorRetainsCpuPolicies() {
        namespace N=NativeBrush;QImage image(19,13,QImage::Format_RGBA32FPx4);image.fill(QColor::fromRgbF(.2,.3,.4,.375));
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();QVERIFY(caps.active());
        QFutureWatcher<QVector<bool>> watcher;QSignalSpy finished(&watcher,&QFutureWatcher<QVector<bool>>::finished);QVERIFY(finished.isValid());
        watcher.setFuture(QtConcurrent::run([=] {
            QVector<bool> checks;auto budget=std::make_shared<BrushRenderBudget>();auto state=nativeLuminanceState(.4,false);
            state.pointColorEnabled=true;state.pointColorHue=23;state.pointColorSaturation=37;state.pointColorLuminance=-19;
            state.pointColorHueRange=90;state.pointColorChromaRange=.12;state.pointColorLightnessRange=.4;
            const auto source=N::CanonicalSource::capture(image,{"point-policy",ImagePipeline::InputEncoding::LinearProPhoto,false,0,{0,0,1,1}},caps.threads(),budget);if(!source)return checks;
            const auto attempt=[&](const ProcessingPlan &plan,bool accepted,N::FrameFailure expected=N::FrameFailure::CPUPolicy) {
                N::FrameFailure why=N::FrameFailure::None;const auto frame=N::FrameAdmission::prepare(source,caps,plan,{"point-catalog","point-input","point-version",1},1,&why);
                qInfo()<<"native point policy"<<checks.size()<<"admitted"<<bool(frame)<<"refusal"<<int(why);
                checks.push_back(accepted?bool(frame)&&why==N::FrameFailure::None:!frame&&why==expected);
            };
            const auto compile=[&](const AdjustmentState &s,ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb){return ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0);};
            const auto plan=compile(state);attempt(plan,true);
            attempt(ProcessingPlan::compile(state,plan.encoding,plan.output,true,0),false,N::FrameFailure::InvalidSource);
            auto malformed=plan;malformed.data[ProcessingPlan::PointColorRange].y=0;attempt(malformed,false,N::FrameFailure::CPUPlan);
            malformed=plan;malformed.data[ProcessingPlan::PointColorAdjust].x=2;attempt(malformed,false,N::FrameFailure::CPUPlan);
            auto narrow=state;narrow.pointColorHueRange=44.9;attempt(compile(narrow),false);
            narrow=state;narrow.pointColorChromaRange=.001;attempt(compile(narrow),false);
            narrow=state;narrow.pointColorLightnessRange=.001;attempt(compile(narrow),false);
            auto boundary=state;boundary.pointColorHueRange=45;boundary.pointColorChromaRange=.003;boundary.pointColorLightnessRange=.003;attempt(compile(boundary),true);
            attempt(compile(state,ColorManagement::OutputSpace::AdobeRgb),true);
            auto color=state;color.colorMaskEnabled=true;color.colorMaskExposure=.5;attempt(compile(color),true);
            attempt(compile(color,ColorManagement::OutputSpace::AdobeRgb),false);
            auto disabled=state;disabled.pointColorEnabled=false;disabled.pointColorHueRange=1;attempt(compile(disabled),true);
            auto grain=state;grain.grainAmount=19;attempt(compile(grain,ColorManagement::OutputSpace::AdobeRgb),false);
            auto sharpen=state;sharpen.sharpenAmount=73;attempt(compile(sharpen),false);
            return checks;
        }));
        if(!watcher.isFinished())QVERIFY(finished.wait(10000));QVERIFY(watcher.isFinished());
        const auto checks=watcher.result();QCOMPARE(checks.size(),14);
        for(int i=0;i<checks.size();++i)QVERIFY2(checks[i],qPrintable(QString("native Point Color admission policy %1").arg(i)));domain->shutdown();
    }
};
int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    NativeBrushLuminanceGpuTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "NativeBrushLuminanceGpuTests.moc"
