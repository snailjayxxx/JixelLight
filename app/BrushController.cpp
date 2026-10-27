#include "app/PhotoController.h"
#include "core/commands/CommandRegistry.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>

QVariantList PhotoController::brushLayers() const {
    QVariantList result;if(!hasImage())return result;
    for(const auto &layer:m_photos[m_currentIndex].state.brushMasks.layers) {
        int dabs=0;for(const auto &stroke:layer.strokes)dabs+=int(stroke.dabs.size());
        result.append(QVariantMap{{"id",layer.id},{"enabled",layer.enabled},{"exposure",layer.exposureEv},
            {"strokes",int(layer.strokes.size())},{"dabs",dabs}});
    }
    return result;
}
QSize PhotoController::brushFrameSize() const {
    return !hasImage()||m_fullSource.isNull()?QSize{}:
        BrushRenderRequest::geometrySize(m_fullSource.size(),m_photos[m_currentIndex].state.geometry);
}
bool PhotoController::brushReady() const {
    return currentSourceAvailable()&&m_sourceIsFull&&!m_fullSource.isNull()&&previewReady()
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
bool PhotoController::brushGestureCurrent() const {
    return m_brushGesture&&brushGestureCurrent(*m_brushGesture);
}
bool PhotoController::brushGestureCurrent(const BrushGesture &g) const {
    if(!hasImage()||!currentSourceAvailable()||m_closing||m_loading||!m_sourceIsFull)return false;
    return g.photoKey==m_photos[m_currentIndex].storageKey()&&g.epoch==m_photoEpoch
        &&g.generation==m_prepareGeneration&&g.revision==m_requestedRevision
        &&g.sourceKey==m_fullSource.cacheKey()&&!m_preparing&&!m_cropEditing
        &&g.geometry==m_photos[m_currentIndex].state.geometry.toJson();
}
void PhotoController::cancelBrushStroke() {
    if(!m_brushGesture)return;m_brushGesture.reset();emit brushDrawingChanged();
}
bool PhotoController::beginBrushStroke(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure) {
    if(m_brushGesture)return false;
    if(!brushReady()){setStatus(brushStatus());return false;}
    const BrushMaskGeometry::Style style{radius,hardness,opacity};
    if(!BrushMaskGeometry::validStyle(style)||!BrushMaskGeometry::validPoint({x,y})
        ||!std::isfinite(exposure)||exposure<-3||exposure>3)return false;
    const auto &doc=m_photos[m_currentIndex].state.brushMasks;
    if(!BrushMaskDocument::validDocument(doc))return false;
    if(layer.isEmpty()) {
        if(doc.layers.size()>=BrushMaskDocument::MaxLayers){setStatus(uiText("已达笔刷图层上限。","Brush layer limit reached."));return false;}
    }else {
        const auto found=std::find_if(doc.layers.begin(),doc.layers.end(),[&](const auto &l){return l.id==layer;});
        if(found==doc.layers.end())return false;
        if(!found->enabled){setStatus(uiText("请先启用所选笔刷图层。","Enable the selected brush layer first."));return false;}
        if(found->strokes.size()>=BrushMaskGeometry::MaxStrokes){setStatus(uiText("该图层已达笔触上限。","Stroke limit reached for this layer."));return false;}
    }
    try {
        auto g=std::make_unique<BrushGesture>();g->photoKey=m_photos[m_currentIndex].storageKey();g->epoch=m_photoEpoch;
        g->generation=m_prepareGeneration;g->revision=m_requestedRevision;g->sourceKey=m_fullSource.cacheKey();
        g->layer=layer;g->frame=brushFrameSize();g->geometry=m_photos[m_currentIndex].state.geometry.toJson();g->exposure=exposure;
        g->builder=std::make_unique<BrushMaskGeometry::StrokeBuilder>(g->frame.width(),g->frame.height(),style);
        if(!g->builder->append({{x,y},1}))return false;
        m_brushGesture=std::move(g);emit brushDrawingChanged();return brushGestureCurrent();
    }catch(...){setStatus(uiText("无法开始笔触。","Cannot start the stroke."));return false;}
}
bool PhotoController::appendBrushStroke(double x,double y,double pressure) {
    if(!brushGestureCurrent()){cancelBrushStroke();return false;}
    try {if(m_brushGesture->builder->append({{x,y},pressure}))return true;}catch(...){}
    cancelBrushStroke();setStatus(uiText("笔触超出限制，请拆成较短笔触。","Stroke limit exceeded; use shorter strokes."));return false;
}
QString PhotoController::endBrushStroke() {
    if(!brushGestureCurrent()){cancelBrushStroke();return {};}
    auto g=std::move(m_brushGesture);emit brushDrawingChanged();
    if(!brushGestureCurrent(*g))return {};
    AdjustmentState candidate;
    try {
        auto stroke=g->builder->finish();if(!stroke)return {};
        candidate=m_photos[m_currentIndex].state;
        if(g->layer.isEmpty()) {
            g->layer="brush_"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            candidate.brushMasks.layers.push_back({g->layer,true,g->exposure,{std::move(*stroke)}});
        }else {
            auto found=std::find_if(candidate.brushMasks.layers.begin(),candidate.brushMasks.layers.end(),[&](const auto &l){return l.id==g->layer;});
            if(found==candidate.brushMasks.layers.end()||!found->enabled)return {};
            found->strokes.push_back(std::move(*stroke));
        }
        if(!BrushMaskDocument::validDocument(candidate.brushMasks)){setStatus(uiText("笔刷文档已达容量上限。","Brush document capacity reached."));return {};}
        const auto resolved=LookProfiles::resolveAsShot(candidate,m_currentMetadata,currentIsRaw());
        const auto plan=ProcessingPlan::compile(resolved,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,currentIsRaw(),rawBaseExposureStops());
        QString error;PerformanceSpan timing("brush_gesture_preflight");
        if(!BrushRenderRequest::preflightGesture(m_fullSource.size(),plan,&error)) {
            setStatus(uiText("该笔触超出处理限制：","Stroke exceeds processing limits: ")+error);return {};
        }
    }catch(...){setStatus(uiText("笔触未应用：内存不足或处理失败。","Stroke was not applied: allocation or processing failed."));return {};}
    if(!brushGestureCurrent(*g))return {};
    auto &photo=m_photos[m_currentIndex];photo.history.finish();photo.state=std::move(candidate);
    persistAndApply("brush_stroke",{{"layer",g->layer}});finishInteraction();return g->layer;
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
    persistAndApply("brush_layer_remove",{{"layer",id}});finishInteraction();return true;
}
bool PhotoController::removeLastBrushStroke(const QString &id) {
    auto *state=mutableCurrentState();if(!state)return false;auto next=*state;
    if(!CommandRegistry::execute(next,{{"command","mask.brush.remove-last-stroke"},{"layer",id}}))return false;
    m_photos[m_currentIndex].history.finish();*state=std::move(next);
    persistAndApply("brush_stroke_remove",{{"layer",id}});finishInteraction();return true;
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
        return {{"layer",id},{"index",index},{"token",token},{"points",points}};
    }catch(...){return {};}
}
bool PhotoController::removeBrushStroke(const QString &id,int index,const QString &selectionToken) {
    if(selectionToken.isEmpty()||brushStrokeSelection(id,index).value("token").toString()!=selectionToken)return false;
    auto *state=mutableCurrentState();if(!state)return false;auto next=*state;
    if(!CommandRegistry::execute(next,{{"command","mask.brush.remove-stroke"},{"layer",id},{"index",index}}))return false;
    m_photos[m_currentIndex].history.finish();*state=std::move(next);
    persistAndApply("brush_stroke_selected_remove",{{"layer",id},{"index",index}});finishInteraction();return true;
}
