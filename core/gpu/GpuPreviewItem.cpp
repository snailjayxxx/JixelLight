#include "core/gpu/GpuPreviewItem.h"
#include "core/gpu/GpuEngine.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/color/MonitorColorTransform.h"
#include "app/PhotoController.h"
#include <QQuickWindow>
#include <QPointer>
#include <QScreen>
#include <QRunnable>
#include <QJsonObject>
#include "diagnostics/PerformanceRecorder.h"

namespace {
namespace N=NativeBrush;
// Only the real RHI owner holds this object strongly. GUI callbacks carry a
// weak owner and an atomic retirement token, never GPU object ownership.
struct RendererOwner final {
    struct Retired {std::unique_ptr<N::RenderDomain> domain;std::unique_ptr<GpuEngine> engine;std::shared_ptr<NativeBrushTelemetry> facts;};
    QRhi *rhi=nullptr;
    std::unique_ptr<GpuEngine> engine;
    std::unique_ptr<N::RenderDomain> domain;
    std::shared_ptr<NativeBrushTelemetry> facts;
    std::vector<Retired> retired;
    QImage lut;QString lutKey,profile;
    void ensure(QRhi *value){if(rhi!=value){shutdown();rhi=value;}if(rhi&&!engine){engine=std::make_unique<GpuEngine>(rhi);engine->setDisplayColorLut(lut,lutKey,profile);}}
    void monitor(QImage value,QString key,QString name){lut=std::move(value);lutKey=std::move(key);profile=std::move(name);if(engine)engine->setDisplayColorLut(lut,lutKey,profile);}
    void retire(){if(!domain)return;domain->retirement().retire();retired.push_back({std::move(domain),std::move(engine),std::move(facts)});ensure(rhi);}
    void collect(){if(domain)domain->collect();for(auto it=retired.begin();it!=retired.end();){it->domain->collect();if(!it->domain->pending()&&!it->engine->hasPendingReadback()){it->domain->shutdown();it->domain.reset();it->engine.reset();if(it->facts)it->facts->retiredOwnersReleased.fetch_add(1,std::memory_order_release);it=retired.erase(it);}else ++it;}}
    bool pending()const{if(domain&&domain->pending())return true;if(engine&&engine->hasPendingReadback())return true;for(const auto&r:retired)if(r.domain->pending()||r.engine->hasPendingReadback())return true;return false;}
    void shutdown(){if(domain){domain->retirement().retire();domain->shutdown();domain.reset();if(facts)facts->retiredOwnersReleased.fetch_add(1,std::memory_order_release);facts.reset();}engine.reset();for(auto&r:retired){r.domain->shutdown();r.domain.reset();r.engine.reset();if(r.facts)r.facts->retiredOwnersReleased.fetch_add(1,std::memory_order_release);}retired.clear();rhi=nullptr;}
    ~RendererOwner(){shutdown();}
};
QString threadKey(Qt::HANDLE thread){return QString::number(reinterpret_cast<quintptr>(thread),16);}
class Renderer final : public QQuickRhiItemRenderer {
public:
    Renderer():m_owner(std::make_shared<RendererOwner>()){}
    void initialize(QRhiCommandBuffer *) override {
        if(m_owner->rhi!=rhi()||!m_owner->engine){m_owner->ensure(rhi());resetHandles();m_failed=false;m_notified=0;m_fboValid=false;}
    }
    void synchronize(QQuickRhiItem *item) override {
        m_owner->ensure(rhi());
        auto *view=static_cast<GpuPreviewItem *>(item);
        auto controller=qobject_cast<PhotoController *>(view->controller());
        const auto next=controller?controller->nativeBrushView():NativeBrushView{};
        const bool changed=controller!=m_controller||next.target!=m_native.target||next.context!=m_native.context||next.qtSourceKey!=m_native.qtSourceKey;
        if(changed){m_owner->retire();resetHandles();m_fboValid=false;m_notified=0;}
        m_controller=controller;m_native=next;m_source={};
        if(m_covered!=view->presentationCovered()){m_covered=view->presentationCovered();m_redraw=true;}
        m_diagnosticRequest=controller?controller->diagnosticRequestId():0;
        m_diagnosticRevision=controller?controller->diagnosticRevision():0;
        m_diagnosticSourceKey=controller?controller->diagnosticSourceKey():0;
        if(view->displayColorLutKey()!=m_monitorKey){m_redraw=true;m_monitorKey=view->displayColorLutKey();}
        m_owner->monitor(view->displayColorLut(),view->displayColorLutKey(),view->displayColorProfileName());
        if(controller&&controller->gpuEnabled()){
            if(m_failed){m_owner->retire();m_owner->engine.reset();m_owner->ensure(rhi());m_failed=false;m_notified=0;}
            m_source=controller->gpuSource();m_plan=controller->gpuPlan();m_revision=controller->renderRevision();
        }
        if(auto *window=view->window();window&&window!=m_window){
            m_window=window;const std::weak_ptr<RendererOwner> weak=m_owner;
            // Both signals run on the scene-graph owner. AboutToStop also
            // handles hidden windows, whose NoStage jobs may be discarded.
            QObject::connect(window,&QQuickWindow::sceneGraphAboutToStop,window,[weak]{if(auto state=weak.lock())state->shutdown();},Qt::DirectConnection);
            QObject::connect(window,&QQuickWindow::sceneGraphInvalidated,window,[weak]{if(auto state=weak.lock())state->shutdown();},Qt::DirectConnection);
        }
        if(m_native.wanted&&!m_native.refused&&m_native.context&&m_native.context->load(std::memory_order_acquire)&&m_owner->rhi){
            if(!m_owner->domain||!m_owner->domain->capabilities().active()){
                m_owner->retire();resetHandles();m_owner->domain=std::make_unique<N::RenderDomain>(m_owner->rhi,m_owner->engine->nativeBrushShaders());m_owner->facts=m_native.facts;
            }
            auto &domain=*m_owner->domain;const auto generation=domain.generation();
            const std::weak_ptr<RendererOwner> weak=m_owner;
            view->bindNativeRetirement(domain.retirement(),[weak,generation]{if(auto state=weak.lock();state&&state->domain&&state->domain->generation()==generation){state->domain->retirement().retire();state->domain->shutdown();state->domain.reset();state->engine.reset();if(state->facts)state->facts->retiredOwnersReleased.fetch_add(1,std::memory_order_release);state->facts.reset();}});
            if(m_capsSent!=generation){m_capsSent=generation;const auto caps=domain.capabilities();const auto context=m_native.context;const auto token=domain.retirement();
                QMetaObject::invokeMethod(controller,[controller,context,caps,token]{if(controller)controller->nativeBrushRhiReady(context,caps,token);else token.retire();},Qt::QueuedConnection);
            }
        }else if(m_owner->domain){m_owner->retire();resetHandles();}
    }
    void render(QRhiCommandBuffer *cb) override {
        m_owner->collect();
        // Retirement between synchronize() and render() must never turn the
        // obsolete native snapshot into a legacy CPU upload for a new photo.
        if(m_native.context&&!m_native.context->load(std::memory_order_acquire)){
            if(m_native.facts)m_native.facts->renderRetiredBlocked.fetch_add(1,std::memory_order_release);
            m_owner->retire();resetHandles();diagnosticUnavailable(QStringLiteral("Native delivery context was retired"));clear(cb);drive();return;
        }
        if(m_source.isNull()||!m_owner->engine||m_failed){diagnosticUnavailable(QStringLiteral("GPU preview is unavailable for the frozen snapshot"));clear(cb);drive();return;}
        if(qEnvironmentVariableIsSet("JIXELLIGHT_GPU_TIMESTAMPS")){const double seconds=cb->lastCompletedGpuTime();if(seconds>0)PerformanceRecorder::sample("gpu_previous_frame_ms",seconds*1000.,{{"scope",QStringLiteral("whole previous QRhi frame, not one node")}});}
        if(m_native.wanted&&!m_native.refused&&m_native.context&&m_native.context->load(std::memory_order_acquire)&&m_owner->domain){
            native(cb);drive();return;
        }
        legacy(cb);drive();
    }
private:
    void resetHandles(){m_input={};m_pending={};m_current={};m_histogram={};m_stamp={};m_capsSent=0;m_redraw=true;m_presented=0;}
    void clear(QRhiCommandBuffer *cb){cb->beginPass(renderTarget(),Qt::transparent,{1.0f,0});cb->endPass();m_fboValid=false;}
    void drive(){if(m_owner->pending())update();}
    void nativeFailure(const QString &reason){const auto controller=m_controller;const auto context=m_native.context;if(controller)QMetaObject::invokeMethod(controller,[controller,context,reason]{if(controller)controller->nativeBrushFailed(context,reason);},Qt::QueuedConnection);if(m_owner->domain)m_owner->domain->retirement().retire();}
    void native(QRhiCommandBuffer *cb){
        auto &domain=*m_owner->domain;
        if(!domain.capabilities().active()){if(!m_fboValid)clear(cb);return;}
        if(!m_input&&m_native.upload&&m_native.upload->ready()){
            N::SourceFailure why;m_input=domain.attach(m_native.upload,&why);
            if(!m_input||!domain.upload(m_input,cb)){nativeFailure(QStringLiteral("Native source attachment or upload failed"));if(!m_fboValid)clear(cb);return;}
        }
        if(m_input&&!domain.ready(m_input)&&!domain.pending()){nativeFailure(QStringLiteral("Native source completion was invalid"));if(!m_fboValid)clear(cb);return;}
        if(m_input&&domain.ready(m_input)&&m_native.frame&&m_native.frame->ready()
            &&m_native.frame->stamp().target==m_native.target&&m_native.frame->stamp().revision==m_revision){
            const auto stamp=m_native.frame->stamp();
            if(!domain.activate(stamp)){nativeFailure(QStringLiteral("Native frame identity was rejected"));return;}
            N::FrameFailure why;auto frame=domain.attachFrame(m_input,m_native.frame,&why);
            if(!frame||!domain.compute(frame,cb)){nativeFailure(QStringLiteral("Native frame attachment or compute failed"));return;}
            if(m_native.facts){m_native.facts->computeSubmissions.fetch_add(1,std::memory_order_relaxed);m_native.facts->lastSubmittedRevision.store(stamp.revision,std::memory_order_release);}
            if(m_pending)domain.release(m_pending);m_pending=frame;m_stamp=stamp;
            m_frameThread=m_native.frame->preparationThread();
        }
        domain.collect();
        if(m_pending&&domain.ready(m_pending)){if(m_current)domain.release(m_current);m_current=m_pending;m_pending={};m_redraw=true;}
        else if(m_pending&&!domain.pending()){nativeFailure(QStringLiteral("Native compute completion was invalid"));return;}
        if(!m_current||!domain.publishable(m_current)){if(!m_fboValid)clear(cb);return;}
        const bool resized=m_targetSize!=renderTarget()->pixelSize();
        if(m_redraw||resized){
            if(!domain.present(m_current,cb,[&](QRhiTexture*texture){if(m_covered){cb->beginPass(renderTarget(),Qt::transparent,{1.0f,0});cb->endPass();return true;}return m_owner->engine->drawExternalWorkingTexture(cb,renderTarget(),texture);})){nativeFailure(QStringLiteral("Native monitor presentation failed"));return;}
            m_redraw=false;m_targetSize=renderTarget()->pixelSize();m_fboValid=true;
        }
        if(m_histogram!=m_current&&domain.histogram(m_current,cb))m_histogram=m_current;
        for(auto &reply:domain.takeHistogramReplies()){
            const auto controller=m_controller;const auto context=m_native.context;
            QMetaObject::invokeMethod(controller,[controller,context,reply]{if(controller)controller->nativeBrushHistogram(context,reply);},Qt::QueuedConnection);
        }
        if(m_diagnosticRequest&&m_handledDiagnosticRequest!=m_diagnosticRequest){
            if(m_stamp.revision!=m_diagnosticRevision||m_source.cacheKey()!=m_diagnosticSourceKey)diagnosticUnavailable(QStringLiteral("Native preview changed before diagnostic capture"));
            else{const auto controller=m_controller;const auto context=m_native.context;const auto request=m_diagnosticRequest,generation=domain.generation();m_handledDiagnosticRequest=request;
                if(!domain.capture(m_current,cb,m_diagnosticSourceKey,[controller,context,request,generation](N::FrameStamp stamp,QJsonObject output){
                    if(controller)QMetaObject::invokeMethod(controller,[controller,context,request,generation,stamp,output=std::move(output)]()mutable{if(!controller)return;if(!controller->nativeBrushMatches(context,stamp,generation))output={{"available",false},{"error","Native diagnostic delivery target retired"}};controller->gpuDiagnosticReady(request,std::move(output));},Qt::QueuedConnection);
                }))sendDiagnostic({{"available",false},{"error","Native diagnostic readback budget or context rejected"}});
            }
        }
        if(m_presented!=m_stamp.revision){
            m_presented=m_stamp.revision;const auto controller=m_controller;const auto context=m_native.context;const auto stamp=m_stamp;
            const auto generation=domain.generation();const auto trace=domain.trace();
            const auto sourceThread=m_native.source?m_native.source->preparationThread():nullptr;
            QJsonObject facts{{"producer","native-brush-gpu-compute"},{"parameter_revision",qint64(stamp.revision)},
                {"source_cache_key",QString::number(m_source.cacheKey())},{"input_source_key",QString::fromLatin1(stamp.sourceKey.toHex())},
                {"catalog",stamp.target.catalog},{"photo",stamp.target.photo},{"version",stamp.target.version},{"owner_epoch",qint64(stamp.target.ownerEpoch)},
                {"prepare_generation",qint64(m_native.prepareGeneration)},{"rhi_generation",qint64(generation)},
                {"source_capture_serial",qint64(m_native.source?m_native.source->captureSerial():0)},
                {"controller_source_captures",qint64(m_native.sourceCaptures)},{"controller_source_cache_hits",qint64(m_native.sourceCacheHits)},
                {"source_uploads",qint64(trace.sourceUploads)},{"coverage_dispatches",qint64(trace.coverageDispatches)},{"colour_dispatches",qint64(trace.colourDispatches)},
                {"histogram_dispatches",qint64(trace.histogramDispatches)},{"histogram_readback_bytes",qint64(trace.histogramReadbackBytes)},
                {"control_readback_bytes",qint64(trace.controlReadbackBytes)},{"normal_finish_calls",qint64(domain.normalFinishCalls())},
                {"gui_thread",threadKey(m_native.guiThread)},{"rhi_thread",threadKey(QThread::currentThreadId())},
                {"source_worker_thread",threadKey(sourceThread)},{"frame_worker_thread",threadKey(m_frameThread)},
                {"monitor_lut_key",m_monitorKey},{"presentation","original monitor LUT, manual interpolation, scene-graph premultiplication"},
                {"cpu_colour_uploaded",false},{"cpu_mask_uploaded",false}};
            const auto delivery=m_native.facts;
            if(delivery){delivery->enqueueIntents.fetch_add(1,std::memory_order_relaxed);delivery->lastEnqueueRevision.store(stamp.revision,std::memory_order_release);}
            if(controller)QMetaObject::invokeMethod(controller,[controller,context,stamp,generation,delivery,facts=std::move(facts),backend=m_owner->engine->backendName()]{
                if(!controller)return;const bool accepted=controller->nativeBrushMatches(context,stamp,generation);
                if(delivery)(accepted?delivery->guiAccepted:delivery->guiRejected).fetch_add(1,std::memory_order_release);
                controller->nativeBrushPresented(context,stamp,generation,facts,backend);
            },Qt::QueuedConnection);
        }
    }
    void legacy(QRhiCommandBuffer *cb){
        const auto controller=m_controller;
        const auto histogram=GpuEngine::HistogramReady([controller](quint64 revision,QByteArray counts,quint64 pixels){if(controller)QMetaObject::invokeMethod(controller,[controller,revision,counts=std::move(counts),pixels]{if(controller)controller->gpuScopes(revision,counts,pixels);},Qt::QueuedConnection);});
        QString failure;
        const bool processed=BrushMaskTileRaster::anyEnabled(m_plan.state.brushMasks)
            ?m_owner->engine->processAdmittedBrush(cb,m_source,m_plan,m_revision,histogram,false,&failure,controller?controller->brushRenderBudget():nullptr)
            :m_owner->engine->process(cb,m_source,m_plan,m_revision,histogram);
        bool drawn=processed;
        if(processed){if(m_covered){cb->beginPass(renderTarget(),Qt::transparent,{1.0f,0});cb->endPass();}else drawn=m_owner->engine->drawForSceneGraph(cb,renderTarget());}
        if(!drawn){
            if(failure.isEmpty())failure=m_owner->engine->error();diagnosticUnavailable(failure);m_failed=true;
            if(controller)QMetaObject::invokeMethod(controller,[controller,error=failure]{if(controller)controller->gpuFailed(error);},Qt::QueuedConnection);return;
        }
        m_fboValid=true;
        if(m_diagnosticRequest&&m_handledDiagnosticRequest!=m_diagnosticRequest){
            if(m_revision!=m_diagnosticRevision||m_source.cacheKey()!=m_diagnosticSourceKey)diagnosticUnavailable(QStringLiteral("GPU preview changed before diagnostic capture"));
            else{const auto request=m_diagnosticRequest;m_handledDiagnosticRequest=request;QString error;
                if(!m_owner->engine->captureWorkingFrame(cb,m_diagnosticRevision,[controller,request](QJsonObject output){if(controller)QMetaObject::invokeMethod(controller,[controller,request,output=std::move(output)]{if(controller)controller->gpuDiagnosticReady(request,output);},Qt::QueuedConnection);},&error))sendDiagnostic({{"available",false},{"error",error}});
            }
        }
        if(m_notified!=m_revision){m_notified=m_revision;
            const auto target=m_native.target;const auto sourceKey=m_source.cacheKey();const bool cpu=m_owner->engine->lastProcessUsedCpuFallback();
            const QJsonObject facts{{"producer",cpu?"cpu-reference-upload":"gpu-compute"},{"parameter_revision",qint64(m_revision)},
                {"source_cache_key",QString::number(sourceKey)},{"catalog",target.catalog},{"photo",target.photo},{"version",target.version},
                {"owner_epoch",qint64(target.ownerEpoch)},{"cpu_reference_uploaded",cpu},{"pipeline_method",cpu?"original processAdmittedBrush/CPU reference":"original GPU colour pipeline"}};
            if(controller)QMetaObject::invokeMethod(controller,[controller,target,sourceKey,facts,revision=m_revision,backend=m_owner->engine->backendName()]{if(controller)controller->nativeBrushLegacyPresented(revision,sourceKey,target,facts,backend);},Qt::QueuedConnection);
        }
    }
    void sendDiagnostic(QJsonObject output){const auto controller=m_controller;const auto request=m_diagnosticRequest;if(controller)QMetaObject::invokeMethod(controller,[controller,request,output=std::move(output)]{if(controller)controller->gpuDiagnosticReady(request,output);},Qt::QueuedConnection);}
    void diagnosticUnavailable(const QString &error){if(!m_diagnosticRequest||m_handledDiagnosticRequest==m_diagnosticRequest)return;m_handledDiagnosticRequest=m_diagnosticRequest;sendDiagnostic({{"available",false},{"error",error}});}
    std::shared_ptr<RendererOwner> m_owner;
    QPointer<PhotoController> m_controller;
    QPointer<QQuickWindow> m_window;
    NativeBrushView m_native;
    QImage m_source;ProcessingPlan m_plan;
    N::InputHandle m_input;N::FrameHandle m_pending,m_current,m_histogram;
    N::FrameStamp m_stamp;
    Qt::HANDLE m_frameThread=nullptr;
    quint64 m_revision=0,m_notified=0,m_capsSent=0,m_presented=0;
    quint64 m_diagnosticRequest=0,m_diagnosticRevision=0,m_handledDiagnosticRequest=0;
    qint64 m_diagnosticSourceKey=0;
    QString m_monitorKey;QSize m_targetSize;
    bool m_failed=false,m_fboValid=false,m_redraw=true,m_covered=false;
};
}
GpuPreviewItem::GpuPreviewItem(QQuickItem *parent):QQuickRhiItem(parent) {
    // QQuickRhiItem expects premultiplied content when compositing transparency.
    // The renderer applies this after the monitor transform, at presentation.
    setAlphaBlending(true);
    connect(this,&QQuickItem::visibleChanged,this,[this]{if(m_controller)m_controller->nativeBrushSuspend(!isVisible());if(!isVisible())retireNativeResources();else update();});
    m_histogramRefresh.setInterval(90); m_histogramRefresh.setSingleShot(true);
    connect(&m_histogramRefresh,&QTimer::timeout,this,[this] { update(); });
    connect(this,&QQuickItem::windowChanged,this,[this](QQuickWindow *window) {
        retireNativeResources();
        if (!window) return;
        m_nativeWindow=window;
        connect(window,&QWindow::visibleChanged,this,[this](bool visible){if(m_controller)m_controller->nativeBrushSuspend(!visible);if(!visible)retireNativeResources();else update();});
        connect(window,&QQuickWindow::sceneGraphInitialized,this,&GpuPreviewItem::checkBackend,Qt::QueuedConnection);
        connect(window,&QWindow::screenChanged,this,[this](QScreen *) { refreshDisplayColorManagement(); });
        connect(window,&QWindow::activeChanged,this,[this] {
            if (this->window() && this->window()->isActive()) refreshDisplayColorManagement();
        });
        QTimer::singleShot(0,this,&GpuPreviewItem::checkBackend);
        QTimer::singleShot(0,this,&GpuPreviewItem::refreshDisplayColorManagement);
    });
}
void GpuPreviewItem::setPresentationCovered(bool value){if(value==m_presentationCovered)return;m_presentationCovered=value;emit presentationCoveredChanged();update();}
GpuPreviewItem::~GpuPreviewItem(){retireNativeResources();}
void GpuPreviewItem::bindNativeRetirement(NativeBrush::DomainRetirement token,std::function<void()> reclaim){m_nativeRetirement=std::move(token);m_nativeReclaim=std::move(reclaim);}
void GpuPreviewItem::retireNativeResources(){
    m_nativeRetirement.retire();m_nativeRetirement={};
    if(m_nativeReclaim){auto reclaim=std::move(m_nativeReclaim);if(auto*w=m_nativeWindow.data())w->scheduleRenderJob(QRunnable::create(std::move(reclaim)),QQuickWindow::NoStage);}
}
void GpuPreviewItem::releaseResources(){retireNativeResources();QQuickRhiItem::releaseResources();}
QObject *GpuPreviewItem::controller() const { return m_controller; }
void GpuPreviewItem::setController(QObject *object) {
    auto *controller=qobject_cast<PhotoController *>(object);
    if (controller==m_controller) return;
    retireNativeResources();
    if (m_controller) disconnect(m_controller,nullptr,this,nullptr);
    m_controller=controller;
    if (controller) {
        connect(controller,&PhotoController::gpuFrameChanged,this,[this] { update(); m_histogramRefresh.start(); });
        connect(controller,&PhotoController::backendChanged,this,[this] { update(); });
    }
    emit controllerChanged(); update();
    QTimer::singleShot(0,this,&GpuPreviewItem::checkBackend);
    QTimer::singleShot(0,this,&GpuPreviewItem::refreshDisplayColorManagement);
}
QQuickRhiItemRenderer *GpuPreviewItem::createRenderer() { return new Renderer; }

void GpuPreviewItem::checkBackend() {
    if (window() && m_controller && window()->rendererInterface()->graphicsApi()==QSGRendererInterface::Software)
        m_controller->gpuFailed(QStringLiteral("Qt Quick software scene graph"));
}

void GpuPreviewItem::refreshDisplayColorManagement() {
    constexpr int LutSize = 33;
    const MonitorColorProfile profile=MonitorColorTransform::profileForScreen(window()?window()->screen():nullptr);
    const QString requestedKey=profile.valid ? QStringLiteral("icc:")+profile.key : QStringLiteral("identity-srgb");
    if (requestedKey==m_displayColorLutKey && !m_displayColorLut.isNull()) {
        if(m_controller)m_controller->setDisplayColorLut(m_displayColorLut,m_displayColorLutKey);
        return;
    }

    QString error;
    QImage lut=profile.valid ? MonitorColorTransform::srgbToMonitorLut(profile.icc,LutSize,&error)
                             : MonitorColorTransform::identityLut(LutSize);
    QString effectiveKey=requestedKey;
    QString name=profile.valid ? profile.description : QStringLiteral("sRGB identity fallback");
    if (lut.isNull()) {
        lut=MonitorColorTransform::identityLut(LutSize);
        effectiveKey=QStringLiteral("identity-after-icc-error:")+profile.key;
        name=QStringLiteral("sRGB identity fallback (%1)").arg(error);
        PerformanceRecorder::value("display_monitor_icc_error",error);
    }
    if (lut.isNull()) return;

    m_displayColorLut=std::move(lut);
    m_displayColorLutKey=effectiveKey;
    m_displayColorProfileName=name;
    if(m_controller)m_controller->setDisplayColorLut(m_displayColorLut,m_displayColorLutKey);
    PerformanceRecorder::value("display_color_management",
        profile.valid && error.isEmpty()
            ? QStringLiteral("shared sRGB -> monitor ICC 33^3 LUT for GPU and QML image-provider display")
            : QStringLiteral("shared identity-sRGB display LUT"));
    PerformanceRecorder::value("display_monitor_profile",name);
    if (profile.valid) PerformanceRecorder::value("display_monitor_profile_path",profile.sourcePath);
    update();
}
