#include "core/cache/FullScopesCache.h"
#include "core/cache/AnalysisFrameCache.h"
#include "core/preview/PreviewTasks.h"
#include "core/pipeline/StageGraph.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QMutexLocker>
#include <QScopeGuard>
#include <algorithm>
#include <limits>

FullScopesCache::FullScopesCache(qint64 bytes,std::shared_ptr<AnalysisFrameCache> analysisFrames,std::shared_ptr<BrushRenderBudget> brushBudget)
    : m_entries(int(std::clamp(bytes/1024,qint64(0),qint64(std::numeric_limits<int>::max())))),
      m_analysisFrames(std::move(analysisFrames)),m_brushBudget(std::move(brushBudget)) {}
QJsonObject FullScopesCache::snapshotLocked() const {
    return {{"schema",1},{"backend","cpu-full-scopes"},{"key_version","full-scopes-v1"},{"budget_bytes",qint64(m_entries.maxCost())*1024},
        {"charged_bytes",qint64(m_entries.totalCost())*1024},{"entries",m_entries.size()},
        {"hits",m_hits},{"misses",m_misses},{"bypasses",m_bypasses},{"evictions",m_evictions},{"cancelled_requests",m_cancelled},
        {"accounting","histogram entry, 4096 QVariant bins and retained LUT payload, rounded to KiB; allocator/key overhead excluded; optional analysis frames use a separate budget"}};
}
QJsonObject FullScopesCache::snapshot() const { QMutexLocker lock(&m_mutex); return snapshotLocked(); }
void FullScopesCache::recordLocked() { PerformanceRecorder::value("full_scopes_cache",snapshotLocked()); }
ScopesResult FullScopesCache::analyze(const ScopeRequest &request, const CancelToken &cancel) {
    const auto recordAnalysis=qScopeGuard([&] {
        // Snapshot after this call's frame lease/writer has been released.
        if (m_analysisFrames) {
            try { PerformanceRecorder::value("analysis_frame_cache",m_analysisFrames->snapshot()); }
            catch (...) {} // Optional diagnostics must not fail histogram work.
        }
    });
    PerformanceSpan timing("full_scopes_cached");
    if (cancelled(cancel)) { QMutexLocker lock(&m_mutex); ++m_cancelled; recordLocked(); return {}; }
    if (request.image.isNull()) return {};
    const auto failed=[](const QString &message,bool retry=false){ScopesResult r;r.error=message;r.retryableBudget=retry;return r;};
    const bool brush=BrushMaskTileRaster::anyEnabled(request.plan.state.brushMasks);
    QString error;
    if(brush&&!BrushRenderRequest::validPreparedPlan(request.plan,&error))return failed(error);
    const auto lut=request.plan.data[ProcessingPlan::LookStyle].w>0 ? request.plan.state.look.lut : nullptr;
    const qint64 bytes=sizeof(Entry)+4*1024*sizeof(QVariant)+(lut ? qint64(lut->rgb.capacity())*sizeof(float) : 0);
    const qint64 cost=(bytes+1023)/1024;
    const bool cacheable=m_entries.maxCost()>0 && cost<=m_entries.maxCost();
    const auto key=cacheable ? StageGraph::fullScopesKey(request) : QString{};
    {
        QMutexLocker lock(&m_mutex);
        if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
        if (cacheable) {
            if (const auto *entry=m_entries.object(key)) {
                ++m_hits; PerformanceRecorder::count("full_scopes_cache_hit"); recordLocked();
                return cancelled(cancel) ? ScopesResult{} : entry->result;
            }
            ++m_misses; PerformanceRecorder::count("full_scopes_cache_miss");
        } else { ++m_bypasses; PerformanceRecorder::count("full_scopes_cache_bypass"); }
        recordLocked();
    }
    const auto analysisKey=m_analysisFrames
        ? StageGraph::analysisFrameKey(request.image,request.plan,request.geometry):QString{};
    const auto completed=m_analysisFrames && !analysisKey.isEmpty()
        ? m_analysisFrames->find(analysisKey,cancel):AnalysisFrameCache::Lease{};
    ScopesResult result;
    if (completed) {
        result=ScopesEngine::analyze(completed.image(),1024,cancel);
        PerformanceRecorder::count("full_scopes_encoded_frame_hits");
    } else if(brush) {
        BrushAdmissionFailure reason;
        auto admitted=BrushRenderRequest::createOriginal(request.image,request.plan,request.geometry,
            BrushRenderRequest::Layout::Stripes128,cancel,&error,m_brushBudget,&reason);
        if(!admitted)return cancelled(cancel)?ScopesResult{}:failed(error,reason==BrushAdmissionFailure::BudgetUnavailable);
        auto writer=m_analysisFrames&&!analysisKey.isEmpty()
            ?m_analysisFrames->begin(analysisKey,admitted->size(),lut,cancel):AnalysisFrameCache::Writer{};
        result=ScopesEngine::analyzeAdmitted(*admitted,cancel,&error,nullptr,[&](const QImage &strip,int y){
            if(writer&&!writer.append(strip,y,cancel))writer={};
        });
        if(!result.pixelCount)return cancelled(cancel)?ScopesResult{}:failed(error.isEmpty()?QString("Full histogram rendering failed"):error);
        if(writer&&!cancelled(cancel))writer.publish(cancel);
    } else {
        const auto image=request.geometry.apply(request.image,cancel);
        // A collector is optional. Admission happens after the same B5 guard
        // and cannot turn a legal streamed request into a cache refusal.
        if (!ImagePipeline::preflightTiledResearchRequest(image.size(),request.plan,128,cancel)) return {};
        auto writer=m_analysisFrames && !analysisKey.isEmpty()
            ? m_analysisFrames->begin(analysisKey,image.size(),lut,cancel):AnalysisFrameCache::Writer{};
        result=ScopesEngine::analyzeFull(image,request.plan,cancel,
            [&](const QImage &strip,int y) {
                if (writer && !writer.append(strip,y,cancel)) writer={};
            });
        if (result.pixelCount && !cancelled(cancel) && writer) writer.publish(cancel);
    }
    QMutexLocker lock(&m_mutex);
    if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
    if (!result.pixelCount || !cacheable) return result;
    if (!m_entries.object(key)) {
        const auto previous=m_entries.size();
        m_entries.insert(key,new Entry{result,lut},int(cost));
        const qint64 evicted=previous+1-m_entries.size(); m_evictions+=evicted;
        if (evicted) PerformanceRecorder::count("full_scopes_cache_eviction",evicted);
    }
    recordLocked();
    return cancelled(cancel) ? ScopesResult{} : result;
}
