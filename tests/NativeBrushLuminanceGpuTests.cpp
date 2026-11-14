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
#include <optional>
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
#include "core/gpu/brush/NativeBrushPrivate.h"

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
                               ColorManagement::OutputSpace output,bool raw,const QRectF &frame={0,0,1,1},bool pointMustChange=false,
                               bool dehazeMustChange=false,std::optional<SpatialDehaze::Airlight> fullAirlight={},QSize fullSize={},
                               bool precisionCpuExpected=false) {
        namespace N=NativeBrush;
        auto budget=std::make_shared<BrushRenderBudget>();
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();
        const auto compile=[=](const AdjustmentState &adjustments) {
            auto plan=ProcessingPlan::compile(adjustments,encoding,output,raw,raw?.35f:0.f);plan.setFrameRect(frame);
            if(fullAirlight&&plan.dehazeActive()){plan.setReferenceFrameSize(fullSize);plan.setDehazeAirlight(*fullAirlight);}
            return plan;
        };
        QVERIFY(caps.active());const auto plan=compile(state);
        struct Prepared {N::CanonicalSource::Ptr source;N::UploadAdmission::Ptr upload;N::FrameAdmission::Ptr frame;BrushImageLease reference;bool pointChanged=false,dehazeChanged=false;N::FrameFailure why=N::FrameFailure::None;};
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
                    const auto off=compile(inactive);
                    const auto withoutPoint=result.source->exactRequest(off);
                    if(withoutPoint) {
                        const auto unchanged=withoutPoint->render({});
                        result.pointChanged=unchanged&&unchanged.image()!=result.reference.image();
                    }
                }
                if(dehazeMustChange&&result.reference) {
                    auto inactive=state;inactive.dehazeAmount=0;
                    const auto withoutDehaze=result.source->exactRequest(compile(inactive));
                    if(withoutDehaze) {
                        const auto unchanged=withoutDehaze->render({});
                        result.dehazeChanged=unchanged&&unchanged.image()!=result.reference.image();
                    }
                }
                return result;
            }));
            if(!watcher.isFinished())QVERIFY(finished.wait(10000));
            QVERIFY(watcher.isFinished());prepared=watcher.result();
        }
        const auto context=QString("native luminance: format=%1 encoding=%2 output=%3 RAW=%4 EV=%5 invert=%6 refusal=%7")
            .arg(int(image.format())).arg(int(encoding)).arg(int(output)).arg(raw).arg(state.luminanceMaskExposure).arg(state.luminanceMaskInverted).arg(int(prepared.why));
        QVERIFY2(prepared.upload&&prepared.reference,qPrintable(context));
        if(precisionCpuExpected)QVERIFY2(!prepared.frame&&prepared.why==N::FrameFailure::CPUPolicy,qPrintable(context));
        else QVERIFY2(prepared.frame&&prepared.why==N::FrameFailure::None,qPrintable(context));
        if(pointMustChange)QVERIFY2(prepared.pointChanged,"Point Color fixture must visibly change the independent CPU reference");
        if(dehazeMustChange)QVERIFY2(prepared.dehazeChanged,"Negative dehaze fixture must visibly change the independent CPU reference");
        if(precisionCpuExpected) {
            QCOMPARE(prepared.reference.image().size(),image.size());
            QCOMPARE(prepared.reference.image().format(),QImage::Format_RGBA64);
            const auto original=ImagePipeline::floatSource(image);
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                const auto expected=quint16(std::lround(reinterpret_cast<const float *>(original.constScanLine(y))[4*x+3]*65535.f));
                QCOMPARE(reinterpret_cast<const QRgba64 *>(prepared.reference.image().constScanLine(y))[x].alpha(),expected);
            }
            QCOMPARE(ScopesEngine::analyze(prepared.reference.image()).pixelCount,quint64(image.width())*image.height());
            QCOMPARE(domain->trace().sourceUploads,quint64(0));QCOMPARE(domain->trace().colourDispatches,quint64(0));
            QCOMPARE(domain->normalFinishCalls(),quint64(0));domain->shutdown();QCOMPARE(domain->teardownFinishCalls(),quint64(0));
            prepared={};domain.reset();QCOMPARE(budget->snapshot().charged,qint64(0));
            qInfo().noquote()<<context<<"verified owning CPU precision reference";
            return;
        }
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
    struct DiagnosticScene {
        std::shared_ptr<BrushRenderBudget> budget=std::make_shared<BrushRenderBudget>();
        std::unique_ptr<NativeBrush::RenderDomain> domain;
        NativeBrush::InputHandle input;
        NativeBrush::FrameHandle frame;
        NativeBrush::FrameStamp stamp;
        QJsonObject sourceFingerprint,outputFingerprint;
        qint64 qtKey=0;
        QString error;
    };
    DiagnosticScene diagnosticScene() {
        namespace N=NativeBrush;DiagnosticScene scene;
        QImage image(9,7,QImage::Format_RGBA32FPx4);
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){auto *v=reinterpret_cast<float *>(image.scanLine(y))+4*x;v[0]=.125f+float(x%4)/4;v[1]=.25f+float(y%3)/8;v[2]=1.5f;v[3]=1;}
        scene.qtKey=image.cacheKey();scene.sourceFingerprint=FloatFrameFingerprint::capture(image);
        AdjustmentState state;BrushMaskGeometry::Stroke stroke;stroke.style={.3,.45,.7};stroke.dabs={{{.3,.4},.7},{{.6,.6},.9}};
        state.brushMasks.layers.push_back({"diagnostic-lifetime",true,.25,{stroke}});
        auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::DisplayP3,true,.5);plan.setFrameRect({0,0,1,1});
        scene.domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=scene.domain->capabilities();
        if(!caps.active()){scene.error="Inactive native diagnostic fixture";return scene;}
        struct Prepared{N::CanonicalSource::Ptr source;N::UploadAdmission::Ptr upload;N::FrameAdmission::Ptr frame;};Prepared prepared;
        {
            const auto budget=scene.budget;QFutureWatcher<Prepared> watcher;QSignalSpy finished(&watcher,&QFutureWatcher<Prepared>::finished);
            watcher.setFuture(QtConcurrent::run([=]{Prepared p;p.source=N::CanonicalSource::capture(image,{"diagnostic-lifetime",plan.encoding,true,.5f,{0,0,1,1}},caps.threads(),budget);if(p.source){p.upload=N::UploadAdmission::prepare(p.source,caps);p.frame=N::FrameAdmission::prepare(p.source,caps,plan,{"diagnostic-catalog","diagnostic-photo","diagnostic-version",1},1);}return p;}));
            if(!watcher.isFinished()&&!finished.wait(10000)){scene.error="Diagnostic worker timed out";return scene;}
            prepared=watcher.result();
        }
        if(!prepared.source||!prepared.upload||!prepared.frame){scene.error="Native diagnostic fixture admission failed";return scene;}
        auto &domain=*scene.domain;scene.input=domain.attach(prepared.upload);QRhiCommandBuffer *cb=nullptr;
        if(!scene.input||rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess){scene.error="Diagnostic fixture upload attachment failed";return scene;}
        bool ok=domain.upload(scene.input,cb);rhi->endOffscreenFrame();rhi->finish();domain.collect();
        if(!ok||!domain.ready(scene.input)){scene.error="Diagnostic fixture upload failed";return scene;}
        scene.stamp=prepared.frame->stamp();if(!domain.activate(scene.stamp)){scene.error="Diagnostic fixture activation failed";return scene;}
        scene.frame=domain.attachFrame(scene.input,prepared.frame);
        if(!scene.frame||rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess){scene.error="Diagnostic fixture frame attachment failed";return scene;}
        ok=domain.compute(scene.frame,cb);rhi->endOffscreenFrame();rhi->finish();domain.collect();
        if(!ok||!domain.publishable(scene.frame)){scene.error="Diagnostic fixture compute failed";return scene;}
        QRhiReadbackResult readback;bool done=false;readback.completed=[&]{done=true;};
        if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess){scene.error="Independent diagnostic fixture frame failed";return scene;}
        auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(domain.texture(scene.frame)),&readback);cb->resourceUpdate(updates);
        rhi->endOffscreenFrame();rhi->finish();domain.collect();
        if(!done||readback.data.size()!=9*7*16||readback.format!=QRhiTexture::RGBA32F||readback.pixelSize!=image.size()){scene.error="Independent diagnostic fixture readback failed";return scene;}
        const QImage output(reinterpret_cast<const uchar *>(readback.data.constData()),9,7,9*16,QImage::Format_RGBA32FPx4);scene.outputFingerprint=FloatFrameFingerprint::capture(output);
        // Prepared CPU payloads leave this scope: subsequent source diagnostics
        // must use the actual retained GPU input, without a CPU re-upload.
        return scene;
    }
    struct DiagnosticReplies {QVector<QJsonObject> values;QVector<NativeBrush::FrameStamp> stamps;int accepted=0;QString error;};
    DiagnosticReplies captureDiagnostic(DiagnosticScene &scene,qint64 budget,const std::function<void()> &beforeEnd={},bool shutdown=false,int requests=1) {
        DiagnosticReplies replies;QRhiCommandBuffer *cb=nullptr;
        if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess){replies.error="Diagnostic frame failed";return replies;}
        for(int i=0;i<requests;++i)if(scene.domain->capture(scene.frame,cb,scene.qtKey,[&](NativeBrush::FrameStamp stamp,QJsonObject value){replies.stamps.append(std::move(stamp));replies.values.append(std::move(value));},budget))++replies.accepted;
        if(beforeEnd)beforeEnd();rhi->endOffscreenFrame();
        if(shutdown)scene.domain->shutdown();else{rhi->finish();scene.domain->collect();}
        if(scene.domain->pending()){replies.error="Diagnostic did not settle";scene.domain->shutdown();}
        return replies;
    }
    void disposeDiagnostic(DiagnosticScene &scene) {
        scene.domain->release(scene.frame);scene.domain->release(scene.input);scene.domain->shutdown();scene.domain.reset();
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
    void nativeDiagnosticCapturesPreparedSourceUpload() {
        namespace N=NativeBrush;
        constexpr int width=9,height=7,stride=width*16+32;
        QByteArray storage(stride*height,char(0x5a));
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const float pixel[4]={.125f+float(x%4)/4,.25f+float(y%3)/8,1.5f,.75f+float(x%2)/4};
            std::memcpy(storage.data()+y*stride+x*16,pixel,sizeof(pixel));
        }
        const QImage image(reinterpret_cast<const uchar *>(storage.constData()),width,height,stride,QImage::Format_RGBA32FPx4);
        const auto expected=FloatFrameFingerprint::capture(image);
        const QRectF frame{.125,.25,.5,.5};
        AdjustmentState state;
        BrushMaskGeometry::Stroke stroke;stroke.style={.3,.45,.7};stroke.dabs={{{.3,.4},.7},{{.6,.6},.9}};
        state.brushMasks.layers.push_back({"upload-diagnostic",true,.25,{stroke}});
        auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::DisplayP3,true,.5);
        plan.setFrameRect(frame);
        auto budget=std::make_shared<BrushRenderBudget>();
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();QVERIFY(caps.active());
        struct Prepared {N::CanonicalSource::Ptr source;N::UploadAdmission::Ptr upload;N::FrameAdmission::Ptr frame;};
        Prepared prepared;
        {
            QFutureWatcher<Prepared> watcher;QSignalSpy finished(&watcher,&QFutureWatcher<Prepared>::finished);
            QVERIFY(finished.isValid());watcher.setFuture(QtConcurrent::run([=] {
                Prepared p;p.source=N::CanonicalSource::capture(image,{"native-upload-diagnostic",plan.encoding,true,.5f,frame},caps.threads(),budget);
                if(p.source){p.upload=N::UploadAdmission::prepare(p.source,caps);p.frame=N::FrameAdmission::prepare(p.source,caps,plan,{"upload-catalog","upload-photo","upload-version",1},1);}
                return p;
            }));
            if(!watcher.isFinished())QVERIFY(finished.wait(10000));
            QVERIFY(watcher.isFinished());prepared=watcher.result();
        }
        QVERIFY(prepared.source&&prepared.upload&&prepared.frame);
        const auto sourceKey=QString::fromLatin1(prepared.source->key().toHex());
        auto input=domain->attach(prepared.upload);QVERIFY(bool(input));QRhiCommandBuffer *cb=nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QVERIFY(domain->upload(input,cb));
        rhi->endOffscreenFrame();rhi->finish();domain->collect();QVERIFY(domain->ready(input));
        const auto stamp=prepared.frame->stamp();QVERIFY(domain->activate(stamp));auto handle=domain->attachFrame(input,prepared.frame);QVERIFY(bool(handle));
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QVERIFY(domain->compute(handle,cb));
        rhi->endOffscreenFrame();rhi->finish();domain->collect();QVERIFY(domain->publishable(handle));
        const auto before=domain->trace();QJsonObject output;N::FrameStamp replyStamp;int callbacks=0;
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        QVERIFY(domain->capture(handle,cb,image.cacheKey(),[&](N::FrameStamp value,QJsonObject result){++callbacks;replyStamp=std::move(value);output=std::move(result);}));
        QRhiReadbackResult independent;bool done=false;independent.completed=[&]{done=true;};
        auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(domain->texture(handle)),&independent);cb->resourceUpdate(updates);
        rhi->endOffscreenFrame();rhi->finish();domain->collect();QCOMPARE(callbacks,1);QVERIFY(done);QVERIFY(replyStamp==stamp);
        QVERIFY(output["available"].toBool());QCOMPARE(output["producer"].toString(),QString("native-brush-gpu-compute"));
        QCOMPARE(independent.data.size(),width*height*16);
        const QImage actual(reinterpret_cast<const uchar *>(independent.data.constData()),width,height,width*16,QImage::Format_RGBA32FPx4);
        const auto finalFingerprint=FloatFrameFingerprint::capture(actual);
        for(auto it=finalFingerprint.begin();it!=finalFingerprint.end();++it)QCOMPARE(output[it.key()],it.value());
        QCOMPARE(output["diagnostic_samples_rgba16"].toArray().size(),32);
        QCOMPARE(domain->trace().sourceUploads,before.sourceUploads);QCOMPARE(domain->trace().coverageDispatches,before.coverageDispatches);
        QCOMPARE(domain->trace().colourDispatches,before.colourDispatches);QCOMPARE(domain->trace().controlReadbackBytes,before.controlReadbackBytes);
        QCOMPARE(domain->normalFinishCalls(),quint64(0));
        domain->release(handle);domain->release(input);domain->shutdown();prepared={};domain.reset();QCOMPARE(budget->snapshot().charged,qint64(0));
        const auto uploaded=output["source_staging"].toObject();
        QVERIFY2(uploaded["available"].toBool(),"Native brush prepared FP32 upload must have its own diagnostic fingerprint");
        for(auto it=expected.begin();it!=expected.end();++it)QCOMPARE(uploaded[it.key()],it.value());
        QCOMPARE(uploaded["stage"].toString(),QString("prepared-source-upload"));
        QCOMPARE(uploaded["producer"].toString(),QString("gpu-texture-upload"));
        QCOMPARE(uploaded["source_cache_key"].toString(),QString::number(image.cacheKey()));
        QCOMPARE(uploaded["input_source_key"].toString(),sourceKey);QCOMPARE(uploaded["parameter_revision"].toInteger(),qint64(1));
        QCOMPARE(uploaded["encoding"].toString(),QString("linear-prophoto"));
        QVERIFY(uploaded["raw_source"].toBool());QCOMPARE(uploaded["base_exposure_stops"].toDouble(),.5);
        QVERIFY(uploaded["used_for_compute"].toBool());QVERIFY(!uploaded["monitor_icc"].toBool());
    }
    void nativeDiagnosticSharesAggregateAndNativeResourceBudget() {
        auto scene=diagnosticScene();QVERIFY2(scene.error.isEmpty(),qPrintable(scene.error));
        const auto trace=scene.domain->trace();const auto base=scene.budget->snapshot().charged;constexpr qint64 bytes=9*7*16;
        const auto counter=[](){return PerformanceRecorder::snapshot()["counters"].toObject()["diagnostic_gpu_readback_bytes"].toInteger();};
        for(const qint64 budget:{qint64(-1),bytes-1,bytes,2*bytes-1,2*bytes,FloatFrameFingerprint::BudgetBytes+1}) {
            const auto before=counter();auto replies=captureDiagnostic(scene,budget);QVERIFY2(replies.error.isEmpty(),qPrintable(replies.error));
            if(budget<bytes){QCOMPARE(replies.accepted,0);QVERIFY(replies.values.isEmpty());QCOMPARE(counter(),before);}
            else {
                QCOMPARE(replies.accepted,1);QCOMPARE(replies.values.size(),1);QVERIFY(replies.stamps[0]==scene.stamp);
                const auto output=replies.values[0],source=output["source_staging"].toObject();QVERIFY(output["available"].toBool());
                for(auto it=scene.outputFingerprint.begin();it!=scene.outputFingerprint.end();++it)QCOMPARE(output[it.key()],it.value());
                const bool both=budget>=2*bytes;QCOMPARE(source["available"].toBool(),both);
                QCOMPARE(output["readback_budget_bytes"].toInteger(),std::min(budget,FloatFrameFingerprint::BudgetBytes));
                QCOMPARE(output["readback_requested_bytes"].toInteger(),both?2*bytes:bytes);QCOMPARE(output["readback_received_bytes"],output["readback_requested_bytes"]);
                QCOMPARE(counter()-before,output["readback_received_bytes"].toInteger());
                if(both)QCOMPARE(source["pixel_sha256"],scene.sourceFingerprint["pixel_sha256"]);
                else{QCOMPARE(source["reason"].toString(),QString("budget"));QVERIFY(!source.contains("pixel_sha256"));}
            }
            QCOMPARE(scene.budget->snapshot().charged,base);
        }
        // Reserve accounting capacity without allocating physical pixel memory.
        // The original final capture fits exactly; the optional input cannot fit.
        auto occupied=NativeBrush::BudgetAccess::reserve(scene.budget,scene.budget->snapshot().limit-base-(bytes*2+1024*1024));QVERIFY(occupied);
        auto limited=captureDiagnostic(scene,FloatFrameFingerprint::BudgetBytes);QVERIFY(limited.error.isEmpty());QCOMPARE(limited.accepted,1);QCOMPARE(limited.values.size(),1);
        const auto output=limited.values[0],source=output["source_staging"].toObject();QVERIFY(output["available"].toBool());QVERIFY(!source["available"].toBool());
        QCOMPARE(source["reason"].toString(),QString("resource-budget"));QVERIFY(!source.contains("pixel_sha256"));QCOMPARE(output["readback_requested_bytes"].toInteger(),bytes);
        occupied.reset();QCOMPARE(scene.budget->snapshot().charged,base);
        QCOMPARE(scene.domain->trace().sourceUploads,trace.sourceUploads);QCOMPARE(scene.domain->trace().coverageDispatches,trace.coverageDispatches);QCOMPARE(scene.domain->trace().colourDispatches,trace.colourDispatches);
        QCOMPARE(scene.domain->trace().controlReadbackBytes,trace.controlReadbackBytes);QCOMPARE(scene.domain->trace().histogramReadbackBytes,trace.histogramReadbackBytes);QCOMPARE(scene.domain->normalFinishCalls(),quint64(0));
        disposeDiagnostic(scene);QCOMPARE(scene.budget->snapshot().charged,qint64(0));
    }
    void nativeDiagnosticSourceLifetimeRetirementAndShutdown() {
        for(int mode=0;mode<4;++mode) {
            auto scene=diagnosticScene();QVERIFY2(scene.error.isEmpty(),qPrintable(scene.error));const auto trace=scene.domain->trace();
            if(mode==3){scene.domain->release(scene.input);QVERIFY(scene.domain->sourceKey(scene.input).isEmpty());}
            bool heldWhilePending=true;
            const auto beforeEnd=[&]{
                if(mode==1)scene.domain->retirement().retire();
                if(mode==0||mode==1){scene.domain->release(scene.frame);scene.domain->release(scene.input);if(scene.domain->pending())heldWhilePending=!scene.domain->sourceKey(scene.input).isEmpty();}
            };
            const int requests=mode==0?2:1;
            auto replies=captureDiagnostic(scene,FloatFrameFingerprint::BudgetBytes,beforeEnd,mode==2,requests);
            QVERIFY2(replies.error.isEmpty(),qPrintable(replies.error));QCOMPARE(replies.accepted,requests);QCOMPARE(replies.values.size(),requests);QVERIFY(heldWhilePending);
            for(int i=0;i<replies.values.size();++i){
                const auto output=replies.values[i],source=output["source_staging"].toObject();QVERIFY(replies.stamps[i]==scene.stamp);
                if(mode==1||mode==2){QVERIFY(!output["available"].toBool());QVERIFY(!source["available"].toBool());QVERIFY(!output.contains("pixel_sha256"));QVERIFY(!source.contains("pixel_sha256"));}
                else{
                    QVERIFY(output["available"].toBool());for(auto it=scene.outputFingerprint.begin();it!=scene.outputFingerprint.end();++it)QCOMPARE(output[it.key()],it.value());
                    QCOMPARE(output["diagnostic_samples_rgba16"].toArray().size(),32);
                    if(mode==3){QVERIFY(!source["available"].toBool());QCOMPARE(source["reason"].toString(),QString("source-retired"));QCOMPARE(output["readback_requested_bytes"].toInteger(),qint64(9*7*16));}
                    else{QVERIFY(source["available"].toBool());for(auto it=scene.sourceFingerprint.begin();it!=scene.sourceFingerprint.end();++it)QCOMPARE(source[it.key()],it.value());}
                }
            }
            QVERIFY(!scene.domain->pending());QCOMPARE(scene.domain->normalFinishCalls(),quint64(0));QVERIFY(scene.domain->teardownFinishCalls()<=1);
            QCOMPARE(scene.domain->trace().sourceUploads,trace.sourceUploads);QCOMPARE(scene.domain->trace().coverageDispatches,trace.coverageDispatches);QCOMPARE(scene.domain->trace().colourDispatches,trace.colourDispatches);
            disposeDiagnostic(scene);QCOMPARE(scene.budget->snapshot().charged,qint64(0));
        }
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
    void nativeBrushNegativeDehazeMatchesSourcesAndMasks() {
        QImage integer(37,29,QImage::Format_RGBA64),floating(37,29,QImage::Format_RGBA32FPx4);
        for(int y=0;y<29;++y)for(int x=0;x<37;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,quint16(((x+y)%4)*21845));
            auto *p=reinterpret_cast<float *>(floating.scanLine(y))+4*x;
            p[0]=.02f+2.f*x/36.f;p[1]=.01f+1.5f*y/28.f;p[2]=.1f+1.2f*((x+y)%17)/16.f;p[3]=float((x+y)%4)/3.f;
        }
        for(bool fp32:{false,true})for(int source=0;source<3;++source)for(int output=0;output<4;++output)
        for(double amount:{-100.,-37.,-1.}) {
            const auto space=ColorManagement::OutputSpace(output);auto state=nativeLuminanceState(.4,false);
            state.dehazeAmount=amount;state.colorMaskEnabled=space!=ColorManagement::OutputSpace::AdobeRgb;state.colorMaskExposure=.25;
            state.colorMaskHueRange=90;state.colorMaskChromaRange=.12;state.colorMaskLightnessRange=.4;
            state.pointColorEnabled=true;state.pointColorLab={.6,.02,.01};state.pointColorHue=23;state.pointColorSaturation=37;state.pointColorLuminance=-19;
            state.pointColorHueRange=180;state.pointColorChromaRange=1;state.pointColorLightnessRange=1;
            qInfo()<<"native negative dehaze sources"<<fp32<<source<<output<<amount;
            verifyNativeLuminance(fp32?floating:integer,state,source==0?ImagePipeline::InputEncoding::SRgb:ImagePipeline::InputEncoding::LinearProPhoto,space,source==2,{0,0,1,1},true,true,{},{},space==ColorManagement::OutputSpace::AdobeRgb);
            if(QTest::currentTestFailed())return;
        }
    }
    void nativeBrushNegativeDehazeHdrAlphaAndFullContext() {
        for(int sourceMode=0;sourceMode<3;++sourceMode) {
        const bool nearZero=sourceMode==0;
        QImage image(37,13,QImage::Format_RGBA32FPx4);
        const float levels[]{-.03f,0.f,.001f,.2f,.7f,1.f,2.f,3.5f,sourceMode==2?15.f:65504.f},alpha[]{0.f,nearZero?1.e-30f:1.f/65535.f,.375f,1.f};
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            p[0]=levels[(x+y)%std::size(levels)];p[1]=levels[(2*x+y+3)%std::size(levels)];p[2]=levels[(x+3*y+5)%std::size(levels)];p[3]=alpha[(x+2*y)%4];
        }
        for(int output=0;output<3;++output)for(bool raw:{false,true})for(double amount:{-100.,-1.})for(int context=0;context<4;++context) {
            auto state=nativeLuminanceState(.4,raw);state.dehazeAmount=amount;state.grainAmount=output==2?0:19;
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),raw,raw?.35f:0.f);
            const auto airlight=ImagePipeline::estimateDehazeAirlight(image,plan);QVERIFY(airlight.valid);
            QImage input=image;QRectF frame{0,0,1,1};
            if(context==1||context==3) {
                const QRect crop(9,3,19,7);input=image.copy(crop);
                frame={double(crop.x())/image.width(),double(crop.y())/image.height(),double(crop.width())/image.width(),double(crop.height())/image.height()};
            }
            if(context>=2)input=input.scaled(context==2?QSize(19,7):QSize(11,5),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            QCOMPARE(input.format(),QImage::Format_RGBA32FPx4);
            bool inputHasNearZeroAlpha=false,inputHasWideRgbSpan=false;
            for(int y=0;y<input.height();++y)for(int x=0;x<input.width();++x) {
                const auto *p=reinterpret_cast<const float *>(input.constScanLine(y))+4*x;const float a=p[3];
                inputHasNearZeroAlpha=inputHasNearZeroAlpha||(a>0&&a<1.e-18f);
                inputHasWideRgbSpan=inputHasWideRgbSpan||std::max({p[0],p[1],p[2]})-std::min({p[0],p[1],p[2]})>ImagePipeline::GpuChannelSpanLimit;
            }
            QCOMPARE(inputHasNearZeroAlpha,nearZero&&context<2);
            QCOMPARE(inputHasWideRgbSpan,sourceMode<2);
            qInfo()<<"native negative dehaze context"<<output<<raw<<amount<<context<<"source-mode"<<sourceMode;
            verifyNativeLuminance(input,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),raw,frame,false,true,
                                  context==0?std::nullopt:std::optional<SpatialDehaze::Airlight>(airlight),image.size(),sourceMode<2||ColorManagement::OutputSpace(output)==ColorManagement::OutputSpace::AdobeRgb);
            if(QTest::currentTestFailed())return;
        }
        }
        QImage boundary(19,13,QImage::Format_RGBA32FPx4);
        for(int y=0;y<boundary.height();++y)for(int x=0;x<boundary.width();++x) {
            auto *p=reinterpret_cast<float *>(boundary.scanLine(y))+4*x;
            p[0]=16.f*x/18.f;p[1]=16.f*y/12.f;p[2]=16.f*(x+y)/30.f;p[3]=(x+y)%3==0?0.f:.375f;
        }
        for(double amount:{0.,-37.})for(int output=0;output<2;++output) {
            auto state=nativeLuminanceState(.4,false);state.dehazeAmount=amount;
            qInfo()<<"native negative dehaze exact span boundary"<<amount<<output;
            verifyNativeLuminance(boundary,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),false,{0,0,1,1},false,amount<0);
            if(QTest::currentTestFailed())return;
        }
    }
    void nativeBrushNegativeDehazeRealSonyCropMatchesReference() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixtures configured");}
        const auto loaded=loadSourceFresh(QDir(folder).filePath("sony_a7_iv_07.arw"),{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));
        const QRect crop((loaded.image.width()-128)/2,(loaded.image.height()-96)/2,128,96);const auto image=loaded.image.copy(crop);
        const QRectF frame(double(crop.x())/loaded.image.width(),double(crop.y())/loaded.image.height(),double(crop.width())/loaded.image.width(),double(crop.height())/loaded.image.height());
        for(int output=0;output<4;++output)for(double amount:{-100.,-37.}) {
            auto state=nativeLuminanceState(-.35,false);state.dehazeAmount=amount;
            state.pointColorEnabled=true;state.pointColorLab={.6,.02,.01};state.pointColorHue=23;state.pointColorSaturation=37;state.pointColorLuminance=-19;
            state.pointColorHueRange=180;state.pointColorChromaRange=1;state.pointColorLightnessRange=1;
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,.35f);
            const auto airlight=ImagePipeline::estimateDehazeAirlight(loaded.image,plan);QVERIFY(airlight.valid);
            qInfo()<<"native negative dehaze Sony full-decode/cropped-export"<<output<<amount;
            verifyNativeLuminance(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,frame,true,true,airlight,loaded.image.size(),ColorManagement::OutputSpace(output)==ColorManagement::OutputSpace::AdobeRgb);
            if(QTest::currentTestFailed())return;
        }
    }
    void nativeBrushNegativeDehazeRetainsCpuPolicies() {
        namespace N=NativeBrush;QImage image(19,13,QImage::Format_RGBA32FPx4);image.fill(QColor::fromRgbF(.2,.3,.4,.375));
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();QVERIFY(caps.active());
        QFutureWatcher<QVector<bool>> watcher;QSignalSpy finished(&watcher,&QFutureWatcher<QVector<bool>>::finished);QVERIFY(finished.isValid());
        watcher.setFuture(QtConcurrent::run([=] {
            QVector<bool> checks;auto budget=std::make_shared<BrushRenderBudget>();auto state=nativeLuminanceState(.4,false);state.dehazeAmount=-37;
            const auto source=N::CanonicalSource::capture(image,{"veil-policy",ImagePipeline::InputEncoding::LinearProPhoto,false,0,{0,0,1,1}},caps.threads(),budget);if(!source)return checks;
            const auto attempt=[&](const ProcessingPlan &plan,bool accepted,N::FrameFailure expected=N::FrameFailure::CPUPolicy,N::CanonicalSource::Ptr candidate={},const CancelToken &cancel={}) {
                N::FrameFailure why=N::FrameFailure::None;const auto frame=N::FrameAdmission::prepare(candidate?candidate:source,caps,plan,{"veil-catalog","veil-input","veil-version",1},1,&why,cancel);
                qInfo()<<"native negative dehaze policy"<<checks.size()<<"admitted"<<bool(frame)<<"refusal"<<int(why);
                checks.push_back(accepted?bool(frame)&&why==N::FrameFailure::None:!frame&&why==expected);
            };
            const auto compile=[&](const AdjustmentState &s,ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb){return ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0);};
            const auto plan=compile(state);attempt(plan,true);
            auto changed=state;changed.dehazeAmount=-.01;attempt(compile(changed),true);
            changed=state;changed.dehazeAmount=-100;attempt(compile(changed),true);
            changed=state;changed.dehazeAmount=1;attempt(compile(changed),false);
            changed=state;changed.dehazeAmount=100;attempt(compile(changed),false);
            auto malformed=plan;malformed.data[ProcessingPlan::Dehaze].x=-1.01;attempt(malformed,false,N::FrameFailure::CPUPlan);
            malformed=plan;malformed.data[ProcessingPlan::DehazeAirlight]={std::numeric_limits<float>::quiet_NaN(),1,1,1};attempt(malformed,false,N::FrameFailure::CPUPlan);
            attempt(ProcessingPlan::compile(state,plan.encoding,plan.output,true,0),false,N::FrameFailure::InvalidSource);
            changed=state;changed.sharpenAmount=73;attempt(compile(changed),false);
            changed=state;changed.denoiseLuma=37;attempt(compile(changed),false);
            changed=state;changed.textureAmount=37;attempt(compile(changed),false);
            changed=state;changed.clarityAmount=37;attempt(compile(changed),false);
            changed=state;changed.pointColorEnabled=true;changed.pointColorHue=23;changed.pointColorHueRange=5;attempt(compile(changed),false);
            changed=state;changed.colorMaskEnabled=true;changed.colorMaskExposure=.5;attempt(compile(changed,ColorManagement::OutputSpace::AdobeRgb),false);
            auto resolved=plan;const auto facts=ImagePipeline::estimateDehazeAirlight(image,plan);if(!facts.valid)return checks;
            resolved.setDehazeAirlight(facts);attempt(resolved,true);
            const QRect crop(4,3,9,6);const auto cropped=image.copy(crop);
            const QRectF frame(double(crop.x())/image.width(),double(crop.y())/image.height(),double(crop.width())/image.width(),double(crop.height())/image.height());
            const auto cropSource=N::CanonicalSource::capture(cropped,{"veil-crop-policy",plan.encoding,false,0,frame},caps.threads(),budget);if(!cropSource)return checks;
            auto partial=plan;partial.setFrameRect(frame);partial.setReferenceFrameSize(image.size());attempt(partial,false,N::FrameFailure::CPUPolicy,cropSource);
            partial.setDehazeAirlight(facts);attempt(partial,true,N::FrameFailure::None,cropSource);
            const auto cancel=std::make_shared<std::atomic_bool>(true);attempt(plan,false,N::FrameFailure::Cancelled,{},cancel);
            attempt(compile(state,ColorManagement::OutputSpace::AdobeRgb),false);
            QImage tiny=image.copy();
            for(int y=0;y<tiny.height();++y)for(int x=0;x<tiny.width();++x)reinterpret_cast<float *>(tiny.scanLine(y))[4*x+3]=1.e-30f;
            const auto tinySource=N::CanonicalSource::capture(tiny,{"veil-tiny-policy",plan.encoding,false,0,{0,0,1,1}},caps.threads(),budget);if(!tinySource)return checks;
            attempt(plan,false,N::FrameFailure::CPUPolicy,tinySource);
            QImage wide=image.copy();auto *w=reinterpret_cast<float *>(wide.scanLine(0));w[0]=65504;w[1]=.2f;w[2]=-.03f;w[3]=0;
            const auto wideSource=N::CanonicalSource::capture(wide,{"wide-RGB-policy",plan.encoding,false,0,{0,0,1,1}},caps.threads(),budget);if(!wideSource)return checks;
            attempt(plan,false,N::FrameFailure::CPUPolicy,wideSource);
            auto inactive=state;inactive.dehazeAmount=0;attempt(compile(inactive),false,N::FrameFailure::CPUPolicy,wideSource);
            QImage boundary=image.copy();auto *b=reinterpret_cast<float *>(boundary.scanLine(0));b[0]=ImagePipeline::GpuChannelSpanLimit;b[1]=b[2]=b[3]=0;
            const auto boundarySource=N::CanonicalSource::capture(boundary,{"span-boundary-policy",plan.encoding,false,0,{0,0,1,1}},caps.threads(),budget);if(!boundarySource)return checks;
            attempt(plan,true,N::FrameFailure::None,boundarySource);
            auto above=boundary.copy();reinterpret_cast<float *>(above.scanLine(0))[0]=std::nextafter(ImagePipeline::GpuChannelSpanLimit,std::numeric_limits<float>::infinity());
            const auto aboveSource=N::CanonicalSource::capture(above,{"above-span-policy",plan.encoding,false,0,{0,0,1,1}},caps.threads(),budget);if(!aboveSource)return checks;
            attempt(plan,false,N::FrameFailure::CPUPolicy,aboveSource);
            return checks;
        }));
        if(!watcher.isFinished())QVERIFY(finished.wait(10000));QVERIFY(watcher.isFinished());
        const auto checks=watcher.result();QCOMPARE(checks.size(),24);
        for(int i=0;i<checks.size();++i)QVERIFY2(checks[i],qPrintable(QString("native negative dehaze admission policy %1").arg(i)));domain->shutdown();
    }
};
int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    NativeBrushLuminanceGpuTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "NativeBrushLuminanceGpuTests.moc"
