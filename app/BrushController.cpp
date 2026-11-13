#include "app/PhotoController.h"
#include "core/commands/CommandRegistry.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/image/ProcessedImageProvider.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QUuid>
#include <limits>

namespace {
enum class BrushPaintRefusal { None, Invalid, Missing, Disabled, Layers, Strokes, Dabs };
struct BrushCapacityFacts {
    bool valid=false;
    int dabs=0;
};
BrushCapacityFacts capacityFacts(const BrushMaskDocument::Document &document) {
    BrushCapacityFacts result;result.valid=BrushMaskDocument::validDocument(document);
    if(result.valid)for(const auto &layer:document.layers)for(const auto &stroke:layer.strokes)result.dabs+=int(stroke.dabs.size());
    return result;
}
// At exactly one remaining document dab, a zero-coverage endpoint at the
// same center as the only positive dab can be removed without changing any
// pixel or the geometric selection path. All other strokes remain unchanged.
void compactCoincidentZeroEndpointAtDocumentLimit(const BrushMaskDocument::Document &document,BrushMaskGeometry::Stroke &stroke) {
    const auto facts=capacityFacts(document);auto &dabs=stroke.dabs;
    if(!facts.valid||std::size_t(facts.dabs)+1!=BrushMaskDocument::MaxTotalDabs||dabs.size()!=2)return;
    const auto &a=dabs[0];const auto &b=dabs[1];
    if(a.center.x!=b.center.x||a.center.y!=b.center.y)return;
    if(a.pressure==0&&b.pressure>0)dabs.erase(dabs.begin());
    else if(b.pressure==0&&a.pressure>0)dabs.pop_back();
}
BrushPaintRefusal paintRefusal(const BrushMaskDocument::Document &document,const BrushCapacityFacts &facts,const QString &id) {
    if(!facts.valid)return BrushPaintRefusal::Invalid;
    if(id.isEmpty()) {
        if(document.layers.size()>=BrushMaskDocument::MaxLayers)return BrushPaintRefusal::Layers;
    }else {
        const auto layer=std::find_if(document.layers.begin(),document.layers.end(),[&](const auto &layer){return layer.id==id;});
        if(layer==document.layers.end())return BrushPaintRefusal::Missing;
        if(!layer->enabled)return BrushPaintRefusal::Disabled;
        if(layer->strokes.size()>=BrushMaskGeometry::MaxStrokes)return BrushPaintRefusal::Strokes;
    }
    if(std::size_t(facts.dabs)>=BrushMaskDocument::MaxTotalDabs)return BrushPaintRefusal::Dabs;
    return BrushPaintRefusal::None;
}
QString refusalKey(BrushPaintRefusal reason) {
    switch(reason) {
    case BrushPaintRefusal::Invalid:return QStringLiteral("invalid");
    case BrushPaintRefusal::Missing:return QStringLiteral("missing");
    case BrushPaintRefusal::Disabled:return QStringLiteral("disabled");
    case BrushPaintRefusal::Layers:return QStringLiteral("layers");
    case BrushPaintRefusal::Strokes:return QStringLiteral("strokes");
    case BrushPaintRefusal::Dabs:return QStringLiteral("dabs");
    case BrushPaintRefusal::None:return {};
    }
    return QStringLiteral("invalid");
}
}
QVariantMap PhotoController::brushCapacity() const {
    const BrushMaskDocument::Document empty;
    const auto &document=hasImage()?m_photos[m_currentIndex].state.brushMasks:empty;
    const auto facts=capacityFacts(document);const auto reason=paintRefusal(document,facts,{});
    // Count availability is separate from source/display readiness and work admission.
    return {{"layers",int(document.layers.size())},{"dabs",facts.dabs},
        {"layer_limit",int(BrushMaskDocument::MaxLayers)},{"stroke_limit",int(BrushMaskGeometry::MaxStrokes)},
        {"dab_limit",int(BrushMaskDocument::MaxTotalDabs)},{"new_layer_allowed",hasImage()&&reason==BrushPaintRefusal::None},
        {"new_layer_block",hasImage()?refusalKey(reason):QStringLiteral("unavailable")}};
}
QVariantList PhotoController::brushLayers() const {
    QVariantList result;if(!hasImage())return result;
    const auto &document=m_photos[m_currentIndex].state.brushMasks;const auto facts=capacityFacts(document);
    for(const auto &layer:document.layers) {
        int dabs=0;QVariantList modes;
        for(const auto &stroke:layer.strokes) {
            dabs+=int(stroke.dabs.size());
            modes.append(stroke.mode==BrushMaskGeometry::StrokeMode::Erase?QStringLiteral("erase"):QStringLiteral("paint"));
        }
        const auto reason=paintRefusal(document,facts,layer.id);
        result.append(QVariantMap{{"id",layer.id},{"enabled",layer.enabled},{"exposure",layer.exposureEv},
            {"strokes",int(layer.strokes.size())},{"dabs",dabs},{"stroke_modes",modes},
            {"append_allowed",reason==BrushPaintRefusal::None},{"paint_block",refusalKey(reason)}});
    }
    return result;
}
QSize PhotoController::brushFrameSize() const {
    return !hasImage()||m_fullSource.isNull()?QSize{}:
        BrushRenderRequest::geometrySize(m_fullSource.size(),m_photos[m_currentIndex].state.geometry);
}
bool PhotoController::brushReady() const {
    return !m_closing&&!xmpMergeBusy()&&currentSourceAvailable()&&m_sourceIsFull&&!m_fullSource.isNull()&&previewReady()
        &&!m_loading&&!m_preparing&&!m_rendering&&!m_interacting&&!m_cropEditing
        &&qint64(m_fullSource.width())*m_fullSource.height()<=BrushRenderRequest::MaxSourcePixels
        &&!brushFrameSize().isEmpty();
}
QString PhotoController::brushStatus() const {
    if(!hasImage())return uiText("请先打开照片。","Open a photograph first.");
    if(qint64(m_fullSource.width())*m_fullSource.height()>BrushRenderRequest::MaxSourcePixels)
        return uiText("原图超过 32Mi 像素，暂不能绘制笔刷。","The source exceeds the 32Mi-pixel brush limit.");
    if(m_cropEditing)return uiText("应用或取消裁剪后可绘制。","Apply or cancel the crop before painting.");
    if(!brushReady())return uiText("图像处理中，完成后可绘制。","Wait for image processing before painting.");
    return uiText("拖动绘制，松开应用 · Esc 取消当前笔触","Drag to paint, release to apply · Esc cancels the stroke");
}
bool PhotoController::brushCapacityWarning() const {
    return m_brushGesture&&m_brushGesture->capacityWarned;
}
void PhotoController::updateBrushCapacityWarning() {
    if(m_brushGesture&&!m_brushGesture->capacityWarned&&m_brushGesture->capacityHadPositivePressure
        &&m_brushGesture->builder->acceptedDabCount()>=m_brushGesture->capacityWarningAt) {
        m_brushGesture->capacityWarned=true;emit brushCapacityWarningChanged();
    }
}
bool PhotoController::brushGestureCurrent() const {
    return m_brushGesture&&brushGestureCurrent(*m_brushGesture);
}
bool PhotoController::brushGestureCurrent(const BrushGesture &g) const {
    if(!hasImage()||!currentSourceAvailable()||m_closing||m_loading||!m_sourceIsFull)return false;
    return g.catalog==m_catalogEpoch&&g.photoKey==m_photos[m_currentIndex].storageKey()&&g.epoch==m_photoEpoch
        &&g.generation==m_prepareGeneration&&g.revision==m_requestedRevision
        &&g.sourceKey==m_fullSource.cacheKey()&&!m_preparing&&!m_cropEditing
        &&g.geometry==m_photos[m_currentIndex].state.geometry.toJson();
}
bool PhotoController::brushRecoveryAvailable() const {
    if(!m_brushRecovery||!brushGestureCurrent(*m_brushRecovery))return false;
    try { return m_photos[m_currentIndex].state.toJson()==m_brushRecovery->initial.toJson(); }
    catch(...){return false;}
}
void PhotoController::discardBrushRecovery() {
    if(m_brushRecovery){m_brushRecovery.reset();emit brushRecoveryChanged();}
}
void PhotoController::cancelBrushStroke() {
    if(QThread::currentThread()!=thread())return;
    const QPointer<PhotoController> guard(this);const auto catalog=m_catalogEpoch,epoch=m_photoEpoch,revision=m_requestedRevision;const bool closing=m_closing;
    const auto current=[this,guard,catalog,epoch,revision,closing]{return guard&&closing==m_closing&&catalog==m_catalogEpoch&&epoch==m_photoEpoch&&revision==m_requestedRevision;};
    discardBrushRecovery();if(!current())return;
    const bool drawing=bool(m_brushGesture),warned=brushCapacityWarning();m_brushGesture.reset();clearBrushPreview();if(!current())return;
    if(drawing){emit brushDrawingChanged();if(!current())return;}if(warned)emit brushCapacityWarningChanged();
}
void PhotoController::initializeBrushPreview() {
    // Coalesce only preparation of the live frame; every input still enters
    // the sampler synchronously and the pending callback reads its latest state.
    m_brushPreviewScheduleTimer.setSingleShot(true);m_brushPreviewScheduleTimer.setInterval(0);
    connect(&m_brushPreviewScheduleTimer,&QTimer::timeout,this,&PhotoController::requestBrushPreview);
    m_brushPreviewRetryTimer.setSingleShot(true);m_brushPreviewRetryTimer.setInterval(40);
    connect(&m_brushPreviewRetryTimer,&QTimer::timeout,this,&PhotoController::requestBrushPreview);
    const auto budget=m_brushBudget;
    const auto ownerThread=QThread::currentThreadId();
    m_brushPreviewJob=std::make_unique<LatestFrameJob<BrushPreviewRequest,RenderedFrame>>(
        [budget,ownerThread](const BrushPreviewRequest &request,const CancelToken &cancel) {
            RenderedFrame result;BrushAdmissionFailure reason=BrushAdmissionFailure::None;
            QElapsedTimer timing;timing.start();
            const bool allowed=BrushRenderRequest::preflightGesture(request.fullSourceSize,request.plan,cancel,&result.error);
            PerformanceRecorder::sample("brush_live_full_preflight",timing.nsecsElapsed()/1.0e6,
                {{"worker_thread",QThread::currentThreadId()!=ownerThread},
                 {"thread_id",QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()))},
                 {"owner_thread_id",QString::number(reinterpret_cast<quintptr>(ownerThread))},
                 {"gesture",QString::number(request.gesture)},{"sample",QString::number(request.sample)},
                 {"source_cache_key",QString::number(request.source.cacheKey())},
                 {"full_source_width",request.fullSourceSize.width()},{"full_source_height",request.fullSourceSize.height()},
                 {"preview_frame_x",request.previewFrameRect.x()},{"preview_frame_y",request.previewFrameRect.y()},
                 {"preview_frame_width",request.previewFrameRect.width()},{"preview_frame_height",request.previewFrameRect.height()},
                 {"admission_passed",allowed},{"cancelled",cancelled(cancel)},{"reason",result.error}});
            if(!allowed)return result;
            auto plan=request.plan;plan.setFrameRect(request.previewFrameRect);
            auto admitted=BrushRenderRequest::createPrepared(request.source,plan,
                BrushRenderRequest::Layout::Whole,cancel,&result.error,budget,&reason);
            if(admitted){auto lease=admitted->render(cancel,&result.error);if(lease)result.image=lease.sharedImage();}
            result.retryableBudget=reason==BrushAdmissionFailure::BudgetUnavailable;return result;
        },[this](const BrushPreviewRequest &request,RenderedFrame result) {
            if(!brushGestureCurrent()||request.gesture!=m_brushGesture->gesture
                ||request.sample>m_brushGesture->sample||request.sample<=m_brushPreviewSample)return;
            if(result.image.isNull()) {
                // Keep the last compatible frame. Retry only bounded admission
                // contention; new pointer samples reset the retry allowance.
                if(result.retryableBudget&&++m_brushPreviewRetries<=61&&!m_brushPreviewRetryTimer.isActive())
                    m_brushPreviewRetryTimer.start();
                return;
            }
            const auto id=QStringLiteral("brush-live/%1").arg(++m_brushPreviewSerial);
            if(!m_provider||!m_provider->setBrushPreview(id,result.image))return;
            m_brushPreviewRetries=0;m_brushPreviewRetryTimer.stop();
            m_brushPreviewSample=request.sample;m_brushPreviewId=id;emit brushPreviewChanged();
        });
}
void PhotoController::clearBrushPreview() {
    m_brushPreviewScheduleTimer.stop();m_brushPreviewRetryTimer.stop();m_brushPreviewRetries=0;
    if(m_brushPreviewJob)m_brushPreviewJob->cancel();
    if(m_provider)m_provider->clearBrushPreview();
    const bool changed=!m_brushPreviewId.isEmpty();m_brushPreviewId.clear();m_brushPreviewSample=0;
    if(changed)emit brushPreviewChanged();
}
bool PhotoController::brushCandidate(const BrushGesture &g,BrushMaskGeometry::Stroke stroke,AdjustmentState *candidate) const {
    if(!candidate)return false;*candidate=g.initial;
    if(g.layer.isEmpty())candidate->brushMasks.layers.push_back({g.newLayer,true,g.exposure,{std::move(stroke)}});
    else {
        auto &layers=candidate->brushMasks.layers;
        auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==g.layer;});
        if(found==layers.end()||!found->enabled)return false;
        found->strokes.push_back(std::move(stroke));
    }
    return BrushMaskDocument::validDocument(candidate->brushMasks);
}
void PhotoController::requestBrushPreview() {
    if(!m_provider||!m_brushPreviewJob||!brushGestureCurrent()||m_previewSource.isNull()
        ||m_brushPreviewSample==m_brushGesture->sample)return;
    if(m_brushPreviewJob->containsCurrentRequest([&](const auto &request){
        return request.gesture==m_brushGesture->gesture&&request.sample==m_brushGesture->sample;
    }))return;
    try {
        // Completing a copy includes its current tail without consuming the
        // live sampler or changing the final stroke's event spacing.
        auto copy=*m_brushGesture->builder;auto stroke=copy.finish();if(!stroke)return;
        compactCoincidentZeroEndpointAtDocumentLimit(m_brushGesture->initial.brushMasks,*stroke);
        AdjustmentState candidate;if(!brushCandidate(*m_brushGesture,std::move(*stroke),&candidate))return;
        const auto resolved=LookProfiles::resolveAsShot(candidate,m_currentMetadata,currentIsRaw());
        auto plan=ProcessingPlan::compile(resolved,ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb,currentIsRaw(),rawBaseExposureStops());
        m_brushPreviewJob->submit({m_previewSource,std::move(plan),m_brushGesture->gesture,m_brushGesture->sample,
            m_fullSource.size(),m_previewFrameRect});
    }catch(...){/* A refused preview never changes or commits a gesture. */}
}
bool PhotoController::beginBrushStroke(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure) {
    return beginBrushStrokeMode(layer,x,y,radius,hardness,opacity,exposure,QStringLiteral("paint"));
}
bool PhotoController::beginBrushStrokeMode(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure,const QString &mode) {
    return beginBrushStrokeModePressure(layer,x,y,radius,hardness,opacity,exposure,mode,1);
}
bool PhotoController::beginBrushStrokeModePressure(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure,const QString &mode,double pressure) {
    return beginBrushStrokeInput(layer,x,y,radius,hardness,opacity,exposure,mode,pressure,false);
}
bool PhotoController::beginStabilizedBrushStrokeModePressure(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure,const QString &mode,double pressure) {
    return beginBrushStrokeInput(layer,x,y,radius,hardness,opacity,exposure,mode,pressure,true);
}
bool PhotoController::beginStabilizedBrushStrokeModePressurePixels(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure,const QString &mode,double pressure,double movementPixels) {
    if(movementPixels!=0.5 && movementPixels!=1.0)return false;
    return beginBrushStrokeInput(layer,x,y,radius,hardness,opacity,exposure,mode,pressure,true,movementPixels);
}
bool PhotoController::beginBrushStrokeInput(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure,const QString &mode,double pressure,bool stabilizePen,double movementPixels) {
    if(QThread::currentThread()!=thread()||m_closing)return false;
    if(!std::isfinite(pressure)||pressure<0||pressure>1|| (mode!="paint"&&mode!="erase"))return false;
    const auto strokeMode=mode=="erase"?BrushMaskGeometry::StrokeMode::Erase:BrushMaskGeometry::StrokeMode::Paint;
    if(m_brushGesture||brushRecoveryAvailable())return false;
    if(!brushReady()){setStatus(brushStatus());return false;}
    const BrushMaskGeometry::Style style{radius,hardness,opacity};
    if(!BrushMaskGeometry::validStyle(style)||!BrushMaskGeometry::validPoint({x,y})
        ||!std::isfinite(exposure)||exposure<-3||exposure>3)return false;
    if(strokeMode==BrushMaskGeometry::StrokeMode::Erase&&layer.isEmpty()) {
        setStatus(uiText("请先选择要擦除的已有笔刷图层。","Select an existing brush layer to erase first."));return false;
    }
    const auto &doc=m_photos[m_currentIndex].state.brushMasks;
    const auto facts=capacityFacts(doc);
    switch(paintRefusal(doc,facts,layer)) {
    case BrushPaintRefusal::Invalid:case BrushPaintRefusal::Missing:return false;
    case BrushPaintRefusal::Disabled:setStatus(uiText("请先启用所选笔刷图层。","Enable the selected brush layer first."));return false;
    case BrushPaintRefusal::Layers:setStatus(uiText("已达笔刷图层上限。","Brush layer limit reached."));return false;
    case BrushPaintRefusal::Strokes:setStatus(uiText("该图层已达笔触上限。","Stroke limit reached for this layer."));return false;
    case BrushPaintRefusal::Dabs:setStatus(uiText("笔刷文档已满，请删除笔触或图层后继续。","Brush document full. Remove a stroke or layer to continue."));return false;
    case BrushPaintRefusal::None:break;
    }
    const QPointer<PhotoController> guard(this);const auto catalog=m_catalogEpoch,photo=m_photoEpoch,generation=m_prepareGeneration,revision=m_requestedRevision;
    const auto ownerCurrent=[this,guard,catalog,photo,generation,revision]{return guard&&!m_closing&&catalog==m_catalogEpoch&&photo==m_photoEpoch&&generation==m_prepareGeneration&&revision==m_requestedRevision;};
    try {
        auto g=std::make_unique<BrushGesture>();g->photoKey=m_photos[m_currentIndex].storageKey();g->epoch=m_photoEpoch;
        g->catalog=m_catalogEpoch;g->gesture=++m_brushGestureSerial;g->sample=1;g->initial=m_photos[m_currentIndex].state;
        if(layer.isEmpty())g->newLayer="brush_"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        g->generation=m_prepareGeneration;g->revision=m_requestedRevision;g->sourceKey=m_fullSource.cacheKey();
        g->layer=layer;g->frame=brushFrameSize();g->geometry=m_photos[m_currentIndex].state.geometry.toJson();g->exposure=exposure;
        g->mode=strokeMode;
        const auto available=BrushMaskDocument::MaxTotalDabs-std::size_t(facts.dabs);
        const auto strokeCapacity=std::min(BrushMaskGeometry::MaxDabsPerStroke,available);
        g->recoveryLimit=strokeCapacity;g->capacityWarningAt=std::max(std::size_t(1),strokeCapacity*3/4);
        g->builder=std::make_unique<BrushMaskGeometry::StrokeBuilder>(g->frame.width(),g->frame.height(),style,strokeMode);
        BrushMaskGeometry::Dab first{{x,y},pressure};
        if(stabilizePen) {
            g->movementStabilizer.emplace(g->frame.width(),g->frame.height(),movementPixels);
            const auto mapped=g->movementStabilizer->map(first);if(!mapped)return false;first=*mapped;
        }
        if(!g->builder->setRecoveryLimit(strokeCapacity)
            ||g->builder->appendForRecovery(first)!=BrushMaskGeometry::StrokeBuilder::RecoveryResult::Appended)return false;
        g->capacityHadPositivePressure=pressure>0;
        const auto gesture=g->gesture;discardBrushRecovery();if(!ownerCurrent())return false;m_brushGesture=std::move(g);
        const auto current=[this,&ownerCurrent,gesture]{return ownerCurrent()&&m_brushGesture&&m_brushGesture->gesture==gesture&&brushGestureCurrent();};
        emit brushDrawingChanged();if(!current())return false;
        updateBrushCapacityWarning();if(!current())return false;requestBrushPreview();return current();
    }catch(...){if(ownerCurrent())setStatus(uiText("无法开始笔触。","Cannot start the stroke."));return false;}
}
bool PhotoController::appendBrushStroke(double x,double y,double pressure) {
    if(QThread::currentThread()!=thread()||m_closing)return false;
    if(!brushGestureCurrent()){if(m_brushGesture)cancelBrushStroke();return false;}
    const QPointer<PhotoController> guard(this);const auto catalog=m_catalogEpoch,photo=m_photoEpoch,generation=m_prepareGeneration,revision=m_requestedRevision,gesture=m_brushGesture->gesture;
    const auto ownerCurrent=[this,guard,catalog,photo,generation,revision]{return guard&&!m_closing&&catalog==m_catalogEpoch&&photo==m_photoEpoch&&generation==m_prepareGeneration&&revision==m_requestedRevision;};
    const auto current=[this,&ownerCurrent,gesture]{return ownerCurrent()&&m_brushGesture&&m_brushGesture->gesture==gesture&&brushGestureCurrent();};
    try {
        BrushMaskGeometry::Dab input{{x,y},pressure};
        if(m_brushGesture->movementStabilizer) {
            const auto mapped=m_brushGesture->movementStabilizer->map(input);
            if(!mapped) {cancelBrushStroke();if(ownerCurrent()&&!m_brushGesture)setStatus(uiText("无效笔输入，已取消该笔触。","Invalid pen input; stroke cancelled."));return false;}
            input=*mapped;
        }
        // An unchanged tablet packet needs no new preview. If a preview was
        // never published, a later duplicate can still restart a bounded retry.
        if(m_brushGesture->sample<std::numeric_limits<quint64>::max()
            &&m_brushGesture->builder->repeatsPreviousInput(input)) {
            if(m_provider&&m_brushPreviewJob&&m_brushPreviewSample!=m_brushGesture->sample
                &&!m_brushPreviewRetryTimer.isActive()&&!m_brushPreviewScheduleTimer.isActive()
                &&!m_brushPreviewJob->containsCurrentRequest([&](const auto &request){
                    return request.gesture==m_brushGesture->gesture&&request.sample==m_brushGesture->sample;
                })) {
                m_brushPreviewRetries=0;requestBrushPreview();
            }
            return current();
        }
        if(m_brushGesture->sample<std::numeric_limits<quint64>::max()) {
            const auto result=m_brushGesture->builder->appendForRecovery(input);
            if(result==BrushMaskGeometry::StrokeBuilder::RecoveryResult::Appended) {
                ++m_brushGesture->sample;m_brushPreviewRetries=0;
                m_brushGesture->capacityHadPositivePressure|=pressure>0;
                updateBrushCapacityWarning();if(!current())return false;
                if(!m_brushPreviewScheduleTimer.isActive())m_brushPreviewScheduleTimer.start();
                return true;
            }
            if(result==BrushMaskGeometry::StrokeBuilder::RecoveryResult::Capacity) {
                const bool warned=brushCapacityWarning();auto g=std::move(m_brushGesture);clearBrushPreview();if(!ownerCurrent())return false;
                emit brushDrawingChanged();if(!ownerCurrent())return false;if(warned){emit brushCapacityWarningChanged();if(!ownerCurrent())return false;}
                offerBrushRecovery(std::move(g));
                if(ownerCurrent()&&!m_brushGesture)setStatus(uiText("笔触超出限制，请拆成较短笔触。","Stroke limit exceeded; use shorter strokes."));return false;
            }
        }
    }catch(...){}
    if(!current())return false;cancelBrushStroke();if(ownerCurrent()&&!m_brushGesture)setStatus(uiText("笔触超出限制，请拆成较短笔触。","Stroke limit exceeded; use shorter strokes."));return false;
}
bool PhotoController::offerBrushRecovery(std::unique_ptr<BrushGesture> g) {
    if(QThread::currentThread()!=thread()||m_closing)return false;
    if(!g||!g->builder||!brushGestureCurrent(*g)||m_brushGesture)return false;
    try {
        if(m_photos[m_currentIndex].state.toJson()!=g->initial.toJson())return false;
        auto copy=*g->builder;auto stroke=copy.finish();
        if(!stroke||std::all_of(stroke->dabs.begin(),stroke->dabs.end(),[](const auto &d){return d.pressure==0;}))return false;
        compactCoincidentZeroEndpointAtDocumentLimit(g->initial.brushMasks,*stroke);
        AdjustmentState candidate;if(!brushCandidate(*g,std::move(*stroke),&candidate))return false;
        const auto resolved=LookProfiles::resolveAsShot(candidate,m_currentMetadata,currentIsRaw());
        const auto plan=ProcessingPlan::compile(resolved,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,currentIsRaw(),rawBaseExposureStops());
        QString error;if(!BrushRenderRequest::preflightGesture(m_fullSource.size(),plan,&error))return false;
        m_brushRecovery=std::move(g);emit brushRecoveryChanged();return true;
    }catch(...){return false;}
}
QString PhotoController::applyBrushRecovery() {
    if(QThread::currentThread()!=thread()||m_closing)return {};
    if(m_brushGesture)return {};
    if(!brushRecoveryAvailable()){discardBrushRecovery();return {};}
    const QPointer<PhotoController> guard(this);const auto catalog=m_catalogEpoch,photo=m_photoEpoch,generation=m_prepareGeneration,revision=m_requestedRevision;
    auto g=std::move(m_brushRecovery);emit brushRecoveryChanged();
    if(!guard||m_closing||catalog!=m_catalogEpoch||photo!=m_photoEpoch||generation!=m_prepareGeneration||revision!=m_requestedRevision||m_brushGesture)return {};
    return commitBrushGesture(std::move(g));
}
QString PhotoController::endBrushStroke() {
    if(QThread::currentThread()!=thread()||m_closing)return {};
    if(!brushGestureCurrent()){if(m_brushGesture)cancelBrushStroke();return {};}
    const QPointer<PhotoController> guard(this);const auto catalog=m_catalogEpoch,photo=m_photoEpoch,generation=m_prepareGeneration,revision=m_requestedRevision;
    const auto current=[this,guard,catalog,photo,generation,revision]{return guard&&!m_closing&&catalog==m_catalogEpoch&&photo==m_photoEpoch&&generation==m_prepareGeneration&&revision==m_requestedRevision&&!m_brushGesture;};
    const bool warned=brushCapacityWarning();auto g=std::move(m_brushGesture);clearBrushPreview();if(!current())return {};
    emit brushDrawingChanged();if(!current())return {};if(warned){emit brushCapacityWarningChanged();if(!current())return {};}
    return commitBrushGesture(std::move(g));
}
QString PhotoController::commitBrushGesture(std::unique_ptr<BrushGesture> g) {
    if(QThread::currentThread()!=thread()||m_closing)return {};
    if(!g||!brushGestureCurrent(*g))return {};
    const QPointer<PhotoController> guard(this);const auto catalog=m_catalogEpoch,photo=m_photoEpoch,generation=m_prepareGeneration,revision=m_requestedRevision;
    const auto current=[this,guard,catalog,photo,generation,revision]{return guard&&!m_closing&&catalog==m_catalogEpoch&&photo==m_photoEpoch&&generation==m_prepareGeneration&&revision==m_requestedRevision;};
    AdjustmentState candidate;
    try {
        std::unique_ptr<BrushMaskGeometry::StrokeBuilder> backup;
        if(g->builder->acceptedDabCount()>=g->recoveryLimit) {
            try { backup=std::make_unique<BrushMaskGeometry::StrokeBuilder>(*g->builder); }catch(...){}
        }
        auto stroke=g->builder->finish();
        if(!stroke){
            if(g->builder->invalid()) {
                if(backup&&backup->restoreRecoveryPrefix()){g->builder=std::move(backup);offerBrushRecovery(std::move(g));}
                if(current())setStatus(uiText("笔触超出限制，请拆成较短笔触。","Stroke limit exceeded; use shorter strokes."));
            }
            return {};
        }
        // Zero-pressure gestures have no coverage; do not create an empty
        // layer or history entry. A stroke that starts at zero and later
        // produces a positive-pressure dab is retained unchanged.
        if(std::all_of(stroke->dabs.begin(),stroke->dabs.end(),
            [](const auto &dab){return dab.pressure==0;}))return {};
        compactCoincidentZeroEndpointAtDocumentLimit(g->initial.brushMasks,*stroke);
        const auto facts=capacityFacts(g->initial.brushMasks);
        const bool exceedsDocument=facts.valid&&std::size_t(facts.dabs)+stroke->dabs.size()>BrushMaskDocument::MaxTotalDabs;
        if(!brushCandidate(*g,std::move(*stroke),&candidate)){
            if(exceedsDocument&&backup&&backup->restoreRecoveryPrefix()){g->builder=std::move(backup);offerBrushRecovery(std::move(g));}
            if(current())setStatus(uiText("笔刷文档已达容量上限。","Brush document capacity reached."));return {};
        }
        if(g->layer.isEmpty())g->layer=g->newLayer;
        const auto resolved=LookProfiles::resolveAsShot(candidate,m_currentMetadata,currentIsRaw());
        const auto plan=ProcessingPlan::compile(resolved,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,currentIsRaw(),rawBaseExposureStops());
        QString error;PerformanceSpan timing("brush_gesture_preflight");
        if(!BrushRenderRequest::preflightGesture(m_fullSource.size(),plan,&error)) {
            setStatus(uiText("该笔触超出处理限制：","Stroke exceeds processing limits: ")+error);return {};
        }
    }catch(...){if(current())setStatus(uiText("笔触未应用：内存不足或处理失败。","Stroke was not applied: allocation or processing failed."));return {};}
    if(!brushGestureCurrent(*g))return {};
    auto &entry=m_photos[m_currentIndex];entry.history.finish();entry.state=std::move(candidate);
    const auto layer=g->layer;
    if(persistAndApply(g->mode==BrushMaskGeometry::StrokeMode::Erase?"brush_erase":"brush_stroke",
        {{"layer",layer},{"mode",g->mode==BrushMaskGeometry::StrokeMode::Erase?"erase":"paint"}}))finishInteraction();return layer;
}
bool PhotoController::setBrushLayerEnabled(const QString &id,bool enabled) {
    return executeEditCommand({{"command","mask.brush.enable"},{"layer",id},{"enabled",enabled}});
}
bool PhotoController::setBrushLayerExposure(const QString &id,double exposure) {
    auto *state=mutableCurrentState();if(!state)return false;auto next=*state;
    if(!CommandRegistry::execute(next,{{"command","mask.brush.exposure"},{"layer",id},{"value",exposure}}))return false;
    if(next.toJson()==state->toJson())return true;
    *state=std::move(next);persistAndApply("brush_layer_exposure",{{"layer",id},{"value",exposure}});return true;
}
bool PhotoController::removeBrushLayer(const QString &id) {
    auto *state=mutableCurrentState();if(!state)return false;auto next=*state;
    if(!CommandRegistry::execute(next,{{"command","mask.brush.remove"},{"layer",id}}))return false;
    m_photos[m_currentIndex].history.finish();*state=std::move(next);
    if(persistAndApply("brush_layer_remove",{{"layer",id}}))finishInteraction();return true;
}
bool PhotoController::removeLastBrushStroke(const QString &id) {
    auto *state=mutableCurrentState();if(!state)return false;auto next=*state;
    if(!CommandRegistry::execute(next,{{"command","mask.brush.remove-last-stroke"},{"layer",id}}))return false;
    m_photos[m_currentIndex].history.finish();*state=std::move(next);
    if(persistAndApply("brush_stroke_remove",{{"layer",id}}))finishInteraction();return true;
}
QVariantMap PhotoController::brushStrokeSelection(const QString &id,int index) const {
    if(!currentSourceAvailable()||!m_sourceIsFull||m_fullSource.isNull()||m_loading||m_closing||m_brushGesture||m_cropEditing)return {};
    try {
        const auto &photo=m_photos[m_currentIndex];const auto &document=photo.state.brushMasks;
        if(!BrushMaskDocument::validDocument(document)||index<0)return {};
        const auto found=std::find_if(document.layers.begin(),document.layers.end(),[&](const auto &layer){return layer.id==id;});
        if(found==document.layers.end()||std::size_t(index)>=found->strokes.size())return {};
        QJsonObject layer;
        if(!BrushMaskDocument::toJson(BrushMaskDocument::Document{{*found}},&layer))return {};
        // Bind transient selection to its catalog, photo, source and entire layer.
        // A shifted index (including duplicate strokes) must never remove a new target.
        const auto frozen=QJsonDocument(QJsonArray{QString::number(m_catalogEpoch),photo.storageKey(),QString::number(m_photoEpoch),
            QString::number(m_fullSource.cacheKey()),photo.state.geometry.toJson(),index,layer}).toJson(QJsonDocument::Compact);
        const auto token=QString::fromLatin1(QCryptographicHash::hash(frozen,QCryptographicHash::Sha256).toHex());
        const auto &dabs=found->strokes[std::size_t(index)].dabs;
        const auto count=std::min(dabs.size(),std::size_t(1024));QVariantList points;points.reserve(qsizetype(count));
        for(std::size_t i=0;i<count;++i) {
            const auto &p=dabs[count==1?0:i*(dabs.size()-1)/(count-1)].center;
            points.append(QVariantMap{{"x",p.x},{"y",p.y}});
        }
        return {{"layer",id},{"index",index},{"token",token},{"points",points},
            {"mode",found->strokes[std::size_t(index)].mode==BrushMaskGeometry::StrokeMode::Erase?"erase":"paint"}};
    }catch(...){return {};}
}
bool PhotoController::removeBrushStroke(const QString &id,int index,const QString &selectionToken) {
    if(selectionToken.isEmpty()||brushStrokeSelection(id,index).value("token").toString()!=selectionToken)return false;
    auto *state=mutableCurrentState();if(!state)return false;auto next=*state;
    if(!CommandRegistry::execute(next,{{"command","mask.brush.remove-stroke"},{"layer",id},{"index",index}}))return false;
    m_photos[m_currentIndex].history.finish();*state=std::move(next);
    if(persistAndApply("brush_stroke_selected_remove",{{"layer",id},{"index",index}}))finishInteraction();return true;
}
