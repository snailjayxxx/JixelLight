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
                               ColorManagement::OutputSpace output,bool raw,const QRectF &frame={0,0,1,1}) {
        namespace N=NativeBrush;
        auto budget=std::make_shared<BrushRenderBudget>();
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();
        QVERIFY(caps.active());auto plan=ProcessingPlan::compile(state,encoding,output,raw,raw?.35f:0.f);plan.setFrameRect(frame);
        struct Prepared {N::CanonicalSource::Ptr source;N::UploadAdmission::Ptr upload;N::FrameAdmission::Ptr frame;BrushImageLease reference;N::FrameFailure why=N::FrameFailure::None;};
        Prepared prepared;
        {
            QFutureWatcher<Prepared> watcher;watcher.setFuture(QtConcurrent::run([=] {
                Prepared result;result.source=N::CanonicalSource::capture(image,{"range-owned",encoding,raw,raw?.35f:0.f,frame},caps.threads(),budget);
                if(!result.source)return result;
                result.upload=N::UploadAdmission::prepare(result.source,caps);
                result.frame=N::FrameAdmission::prepare(result.source,caps,plan,{"range-catalog","range-input","range-version",1},1,&result.why);
                const auto request=result.source->exactRequest(plan);if(request)result.reference=request->render({});return result;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(),10000);prepared=watcher.result();
        }
        const auto context=QString("native luminance: format=%1 encoding=%2 output=%3 RAW=%4 EV=%5 invert=%6 refusal=%7")
            .arg(int(image.format())).arg(int(encoding)).arg(int(output)).arg(raw).arg(state.luminanceMaskExposure).arg(state.luminanceMaskInverted).arg(int(prepared.why));
        QVERIFY2(prepared.upload&&prepared.frame&&prepared.reference,qPrintable(context));
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
            auto point=state;point.pointColorEnabled=true;point.pointColorHue=23;attempt(ProcessingPlan::compile(point,plan.encoding,plan.output,false,0),false);
            auto color=state;color.colorMaskEnabled=true;color.colorMaskExposure=.5;attempt(ProcessingPlan::compile(color,plan.encoding,plan.output,false,0),false);
            auto grain=state;grain.grainAmount=19;attempt(ProcessingPlan::compile(grain,plan.encoding,ColorManagement::OutputSpace::AdobeRgb,false,0),false);
            auto bright=state;bright.exposure=.6;bright.saturation=21;attempt(ProcessingPlan::compile(bright,plan.encoding,plan.output,false,0),!restricted);
            return checks;
        }));
        QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(),10000);const auto checks=watcher.result();QCOMPARE(checks.size(),8);
        for(int i=0;i<checks.size();++i)QVERIFY2(checks[i],qPrintable(QString("native luminance admission policy %1").arg(i)));
        domain->shutdown();
    }
};
int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    NativeBrushLuminanceGpuTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "NativeBrushLuminanceGpuTests.moc"
