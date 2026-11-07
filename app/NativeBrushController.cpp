#include "app/NativeBrushController.h"
#include "app/PhotoController.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>

namespace N=NativeBrush;
NativeBrushController::NativeBrushController(PhotoController *controller)
    :m_controller(controller),m_instance(QUuid::createUuid().toString(QUuid::WithoutBraces)),m_sources(
        [](const SourceRequest &r,const CancelToken &cancel){
            SourceResult result;if(cancelled(cancel)||!r.context||!r.context->load(std::memory_order_acquire))return result;
            result.reused=bool(r.cached);result.source=r.cached?r.cached:N::CanonicalSource::capture(r.stableImage,r.description,r.caps.threads(),r.budget,&result.failure,cancel);
            if(result.source&&!cancelled(cancel))result.upload=N::UploadAdmission::prepare(result.source,r.caps,&result.failure,cancel);
            return result;
        },[this](const SourceRequest &r,SourceResult result){
            if(m_controller->m_closing||r.context!=m_view.context||!r.context->load(std::memory_order_acquire)
                ||r.target!=m_view.target||r.qtKey!=m_view.qtSourceKey||r.caps.generation()!=m_caps.generation())return;
            if(result.source){if(result.reused)++m_sourceCacheHits;else ++m_sourceCaptures;m_cached=result.source;m_cachedQtKey=r.qtKey;m_cachedDescription=r.description;m_view.source=result.source;}
            if(!result.upload){if(result.failure==N::SourceFailure::Budget)retry();else if(result.failure==N::SourceFailure::Stale){m_caps={};m_owner={};m_view.busy=true;emit m_controller->gpuFrameChanged();}else refuse();return;}
            m_view.upload=std::move(result.upload);m_view.busy=false;m_retries=0;requestFrame();emit m_controller->gpuFrameChanged();
        }),m_frames(
        [](const FrameRequest &r,const CancelToken &cancel){FrameResult result;
            if(!r.context||!r.context->load(std::memory_order_acquire))return result;
            result.frame=N::FrameAdmission::prepare(r.source,r.caps,r.plan,r.target,r.revision,&result.failure,cancel);return result;
        },[this](const FrameRequest &r,FrameResult result){
            if(m_controller->m_closing||r.context!=m_view.context||!r.context->load(std::memory_order_acquire)
                ||r.target!=m_view.target||r.caps.generation()!=m_caps.generation())return;
            if(r.revision!=m_controller->m_requestedRevision){requestFrame();return;}
            m_view.busy=false;
            if(!result.frame){if(result.failure==N::FrameFailure::Budget)retry();else if(result.failure==N::FrameFailure::Stale){m_caps={};m_owner={};m_view.busy=true;emit m_controller->gpuFrameChanged();}else refuse();return;}
            m_view.frame=std::move(result.frame);m_retries=0;emit m_controller->gpuFrameChanged();
        }) {
    m_retry.setSingleShot(true);m_retry.setInterval(50);
    QObject::connect(&m_retry,&QTimer::timeout,controller,[this]{refresh();});
    for(auto signal:{&PhotoController::gpuFrameChanged,&PhotoController::backendChanged,&PhotoController::previewGeometryChanged,
                     &PhotoController::currentIndexChanged,&PhotoController::projectChanged})
        QObject::connect(controller,signal,controller,[this]{refresh();});
}
NativeBrushController::~NativeBrushController(){invalidate(true);}
NativeBrushView NativeBrushController::view()const{
    auto value=m_view;value.guiThread=m_guiThread;value.sourceCacheHits=m_sourceCacheHits;value.sourceCaptures=m_sourceCaptures;const auto*c=m_controller;
    if(c->hasImage()){const auto&p=c->m_photos[c->m_currentIndex];value.target={m_instance+"/"+c->m_project.projectPath()+"/catalog:"+QString::number(c->m_catalogEpoch),p.sourceKey.isEmpty()?p.path:p.sourceKey,p.storageKey(),c->m_photoEpoch};}
    value.revision=c->m_requestedRevision;value.qtSourceKey=c->m_preparing?0:c->m_gpuSource.cacheKey();return value;
}

void NativeBrushController::invalidate(bool clearCanonical){
    if(m_view.context)m_view.context->store(false,std::memory_order_release);
    m_owner.retire();m_caps={};m_owner={};m_sources.cancel();m_frames.cancel();m_retry.stop();
    if(m_view.upload&&m_view.upload->ready())m_view.upload->discard();m_view={};
    if(clearCanonical){m_cached.reset();m_cachedQtKey=0;m_cachedDescription={};}
}
void NativeBrushController::suspend(bool value){if(m_suspended==value)return;m_suspended=value;if(value)invalidate(false);else refresh();}
void NativeBrushController::refresh(){
    auto *c=m_controller;if(c->m_closing)return;
    const bool wanted=!m_suspended&&c->m_gpuEnabled&&!c->m_preparing&&!c->m_gpuSource.isNull()&&c->hasImage()
        &&BrushMaskTileRaster::anyEnabled(c->currentState().brushMasks);
    if(!wanted){if(m_view.context)invalidate(!c->m_gpuEnabled||!c->hasImage());return;}
    const auto &photo=c->m_photos[c->m_currentIndex];
    const N::DeliveryTarget target{m_instance+"/"+c->m_project.projectPath()+"/catalog:"+QString::number(c->m_catalogEpoch),
        photo.sourceKey.isEmpty()?photo.path:photo.sourceKey,photo.storageKey(),c->m_photoEpoch};
    const auto plan=c->gpuPlan();
    const auto inputBytes=QJsonDocument(QJsonObject{{"source_family",target.photo},{"loaded_key",c->m_loadedKey},{"geometry",c->m_preparedGeometry}}).toJson(QJsonDocument::Compact);
    const N::SourceDescription description{QString::fromLatin1(QCryptographicHash::hash(inputBytes,QCryptographicHash::Sha256).toHex()),
        plan.encoding,plan.rawSource,plan.baseExposureStops,c->m_previewFrameRect};
    bool notify=false;
    if(!m_view.context||target!=m_view.target||m_view.qtSourceKey!=c->m_gpuSource.cacheKey()
        ||m_view.prepareGeneration!=c->m_prepareGeneration||description!=m_description){
        invalidate(false);m_view.context=std::make_shared<std::atomic_bool>(true);m_view.facts=std::make_shared<NativeBrushTelemetry>();m_view.target=target;notify=true;
        m_view.qtSourceKey=c->m_gpuSource.cacheKey();m_view.prepareGeneration=c->m_prepareGeneration;m_description=description;m_retries=0;
    }
    if(m_view.revision!=c->m_requestedRevision){m_view.revision=c->m_requestedRevision;m_view.refused=false;m_retries=0;}
    m_view.wanted=true;
    const auto detail=plan.data[ProcessingPlan::LookDetail];bool hard=false;
    for(const auto &l:plan.state.brushMasks.layers)if(l.enabled&&l.exposureEv!=0)for(const auto &s:l.strokes)
        hard=hard||(s.style.opacity>0&&s.style.hardness>=.999);
    if(detail.x||detail.y||detail.z||detail.w||plan.state.look.lut||hard){refuse();return;}
    if(m_view.refused||!m_caps.active()){if(notify)emit c->gpuFrameChanged();return;}
    if(!m_view.source||!m_view.upload)requestSource();else requestFrame();
    if(notify)emit c->gpuFrameChanged();
}
void NativeBrushController::rhiReady(const std::shared_ptr<std::atomic_bool> &context,N::SourceCapabilities caps,N::DomainRetirement owner){
    if(!context||context!=m_view.context||!context->load(std::memory_order_acquire)||!m_view.wanted||m_view.refused){owner.retire();return;}
    if(!caps.active()){owner.retire();refuse();return;}
    if(m_caps.generation()==caps.generation())return;
    m_sources.cancel();m_frames.cancel();if(m_view.upload&&m_view.upload->ready())m_view.upload->discard();
    m_caps=std::move(caps);m_owner=std::move(owner);m_view.upload.reset();m_view.frame.reset();m_view.source.reset();requestSource();
}
void NativeBrushController::requestSource(){
    if(!m_caps.active()||m_view.refused||!m_view.context)return;
    if(m_sources.containsCurrentRequest([&](const SourceRequest&r){return r.context==m_view.context&&r.caps.generation()==m_caps.generation();}))return;
    const bool cached=m_cached&&m_cachedQtKey==m_view.qtSourceKey&&m_cachedDescription==m_description;
    SourceRequest request{m_view.context,m_view.target,m_description,m_caps,cached?m_cached:nullptr,
        cached?QImage{}:m_controller->m_gpuSource,m_controller->m_brushBudget,m_view.qtSourceKey};
    m_view.busy=true;m_sources.submit(std::move(request));
}
void NativeBrushController::requestFrame(){
    if(!m_view.context||!m_caps.active()||!m_view.source||!m_view.upload||m_view.refused)return;
    const auto revision=m_controller->m_requestedRevision;
    if(m_view.frame&&m_view.frame->stamp().revision==revision)return;
    if(m_frames.containsCurrentRequest([&](const FrameRequest&r){return r.context==m_view.context&&r.revision==revision&&r.caps.generation()==m_caps.generation();}))return;
    m_view.busy=true;m_frames.submit({m_view.context,m_caps,m_view.source,m_view.target,m_controller->gpuPlan(),revision});
}
void NativeBrushController::refuse(){
    if(m_view.refused)return;
    m_view.refused=true;m_view.busy=false;m_owner.retire();m_caps={};m_sources.cancel();m_frames.cancel();m_retry.stop();
    if(m_view.upload&&m_view.upload->ready())m_view.upload->discard();m_view.upload.reset();m_view.frame.reset();emit m_controller->gpuFrameChanged();
}
void NativeBrushController::retry(){m_view.busy=true;if(++m_retries>61){refuse();return;}if(!m_retry.isActive())m_retry.start();}
bool NativeBrushController::matches(const std::shared_ptr<std::atomic_bool> &context,const N::FrameStamp &stamp,quint64 generation) const {
    return context&&context==m_view.context&&context->load(std::memory_order_acquire)&&!m_controller->m_closing
        &&stamp.target==m_view.target&&stamp.revision==m_controller->m_requestedRevision
        &&m_view.qtSourceKey==m_controller->m_gpuSource.cacheKey()&&m_view.source&&stamp.sourceKey==m_view.source->key()
        &&m_caps.active()&&generation==m_caps.generation();
}
void NativeBrushController::failure(const std::shared_ptr<std::atomic_bool>&context,const QString&reason){if(context==m_view.context&&context&&context->load(std::memory_order_acquire)){m_trace={{"producer","native-refused"},{"reason",reason}};refuse();}}
void NativeBrushController::trace(QJsonObject value){m_trace=std::move(value);}
QJsonObject NativeBrushController::trace()const{auto value=m_trace;value["current_requested_revision"]=qint64(m_controller->m_requestedRevision);
    const auto actual=view();value["current_delivery_matches"]=!m_controller->m_closing&&!m_controller->m_preparing
        &&value.value("parameter_revision").toInteger()==qint64(m_controller->m_requestedRevision)
        &&value.value("catalog").toString()==actual.target.catalog&&value.value("photo").toString()==actual.target.photo
        &&value.value("version").toString()==actual.target.version&&value.value("owner_epoch").toInteger()==qint64(actual.target.ownerEpoch)
        &&value.value("source_cache_key").toString()==QString::number(m_controller->m_gpuSource.cacheKey());
    if(m_view.facts)value["delivery_facts"]=m_view.facts->snapshot();
    value["current_frame_native"]=m_view.wanted&&!m_view.refused&&m_view.context&&m_view.context->load(std::memory_order_acquire)&&m_caps.active()
        &&value.value("parameter_revision").toInteger()==qint64(m_controller->m_requestedRevision)
        &&value.value("rhi_generation").toInteger()==qint64(m_caps.generation())
        &&value.value("catalog").toString()==m_view.target.catalog&&value.value("photo").toString()==m_view.target.photo
        &&value.value("version").toString()==m_view.target.version&&value.value("owner_epoch").toInteger()==qint64(m_view.target.ownerEpoch)
        &&value.value("source_cache_key").toString()==QString::number(m_controller->m_gpuSource.cacheKey())
        &&value.value("producer").toString()=="native-brush-gpu-compute";return value;}

NativeBrushView PhotoController::nativeBrushView()const{return m_nativeBrush?m_nativeBrush->view():NativeBrushView{};}
void PhotoController::nativeBrushRhiReady(const std::shared_ptr<std::atomic_bool>&context,N::SourceCapabilities caps,N::DomainRetirement owner){if(m_nativeBrush)m_nativeBrush->rhiReady(context,std::move(caps),std::move(owner));else owner.retire();}
void PhotoController::nativeBrushSuspend(bool hidden){if(m_nativeBrush)m_nativeBrush->suspend(hidden);}
void PhotoController::nativeBrushPresented(const std::shared_ptr<std::atomic_bool>&context,const N::FrameStamp&stamp,quint64 generation,QJsonObject trace,const QString&backend){
    if(!m_nativeBrush||!m_nativeBrush->matches(context,stamp,generation))return;m_nativeBrush->trace(std::move(trace));gpuPresented(stamp.revision,backend+QStringLiteral(" · native brush"));}
void PhotoController::nativeBrushHistogram(const std::shared_ptr<std::atomic_bool>&context,const std::shared_ptr<N::HistogramReply>&reply){
    if(reply&&reply->owner.active()&&m_nativeBrush&&m_nativeBrush->matches(context,reply->stamp,reply->generation))gpuScopes(reply->stamp.revision,reply->counts,reply->pixels);}
bool PhotoController::nativeBrushMatches(const std::shared_ptr<std::atomic_bool>&context,const N::FrameStamp&stamp,quint64 generation)const{return m_nativeBrush&&m_nativeBrush->matches(context,stamp,generation);}
QJsonObject PhotoController::nativeBrushTrace()const{return m_nativeBrush?m_nativeBrush->trace():QJsonObject{};}

void PhotoController::nativeBrushFailed(const std::shared_ptr<std::atomic_bool>&context,const QString&reason){if(m_nativeBrush)m_nativeBrush->failure(context,reason);}

void PhotoController::nativeBrushLegacyPresented(quint64 revision,qint64 sourceKey,const N::DeliveryTarget&target,QJsonObject facts,const QString&backend){
    if(!m_nativeBrush||m_closing||m_preparing||!m_gpuEnabled||revision!=m_requestedRevision||sourceKey!=m_gpuSource.cacheKey()||target!=m_nativeBrush->view().target)return;
    m_nativeBrush->trace(std::move(facts));gpuPresented(revision,backend);
}
