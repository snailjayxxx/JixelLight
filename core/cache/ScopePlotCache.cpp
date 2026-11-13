#include "ScopePlotCache.h"
#include "core/cache/AnalysisFrameCache.h"
#include "core/pipeline/StageGraph.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QMutexLocker>
#include <QScopeGuard>
#include <algorithm>
#include <limits>

ScopePlotCache::ScopePlotCache(qint64 bytes,std::shared_ptr<AnalysisFrameCache> analysisFrames,std::shared_ptr<BrushRenderBudget> brushBudget)
    : m_entries(int(std::clamp(bytes/1024,qint64(0),qint64(std::numeric_limits<int>::max())))),
      m_analysisFrames(std::move(analysisFrames)),m_brushBudget(std::move(brushBudget)) {}
QJsonObject ScopePlotCache::snapshotLocked() const {
    return {{"schema",1},{"backend","cpu-scope-plots"},{"key_version","scope-plots-v1"},
        {"budget_bytes",qint64(m_entries.maxCost())*1024},{"charged_bytes",qint64(m_entries.totalCost())*1024},{"entries",m_entries.size()},
        {"hits",m_hits},{"misses",m_misses},{"bypasses",m_bypasses},{"evictions",m_evictions},{"cancelled_requests",m_cancelled},
        {"accounting","three ARGB32 ink images, entry and retained LUT payload, rounded to KiB; allocator/key overhead excluded; optional analysis frames use a separate budget"}};
}
QJsonObject ScopePlotCache::snapshot() const { QMutexLocker lock(&m_mutex); return snapshotLocked(); }
void ScopePlotCache::recordLocked() { PerformanceRecorder::value("scope_plot_cache",snapshotLocked()); }
ScopePlotResult ScopePlotCache::render(const ScopePlotRequest &request,const CancelToken &cancel) {
    const auto recordAnalysis=qScopeGuard([&] {
        if (m_analysisFrames) {
            try { PerformanceRecorder::value("analysis_frame_cache",m_analysisFrames->snapshot()); }
            catch (...) {} // Optional diagnostics must not fail plot work.
        }
    });
    PerformanceSpan timing("scope_plot_cached");
    const int index=request.mode=="waveform" ? 0 : request.mode=="parade" ? 1 : request.mode=="vectorscope" ? 2 : -1;
    if (index<0 || request.source.isNull()) return {};
    if (cancelled(cancel)) { QMutexLocker lock(&m_mutex); ++m_cancelled; recordLocked(); return {}; }
    if(analysisDomainKey(request.domain).isEmpty())return {{},0,"Invalid analysis domain"};
    const bool linear=request.domain!=AnalysisDomain::Display;
    const bool brush=request.domain!=AnalysisDomain::Source&&BrushMaskTileRaster::anyEnabled(request.plan.state.brushMasks);
    QString error;
    if(brush&&!BrushRenderRequest::validPreparedPlan(request.plan,&error))return {{},0,error};
    const auto lut=request.domain!=AnalysisDomain::Source&&request.plan.data[ProcessingPlan::LookStyle].w>0 ? request.plan.state.look.lut : nullptr;
    constexpr qint64 inkBytes=qint64(4*ScopePlotCounts::Columns*ScopePlotCounts::Levels+ScopePlotCounts::ChromaSize*ScopePlotCounts::ChromaSize)*sizeof(QRgb);
    const qint64 bytes=sizeof(Entry)+inkBytes+(lut ? qint64(lut->rgb.capacity())*sizeof(float) : 0),cost=(bytes+1023)/1024;
    const bool cacheable=m_entries.maxCost()>0 && cost<=m_entries.maxCost();
    const auto key=cacheable ? StageGraph::scopePlotsKey(request) : QString{};
    if(linear&&StageGraph::linearAnalysisKey(request.source,request.plan,request.geometry,request.domain).isEmpty())return {{},0,"Invalid linear analysis request"};
    {
        QMutexLocker lock(&m_mutex);
        if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
        if (cacheable) {
            if (const auto *entry=m_entries.object(key); entry && !entry->results[index].image.isNull()
                    && entry->results[index].pixels && entry->results[index].error.isEmpty()) {
                ++m_hits; PerformanceRecorder::count("scope_plot_cache_hit"); recordLocked();
                return cancelled(cancel) ? ScopePlotResult{} : entry->results[index];
            }
            ++m_misses; PerformanceRecorder::count("scope_plot_cache_miss");
        } else { ++m_bypasses; PerformanceRecorder::count("scope_plot_cache_bypass"); }
        recordLocked();
    }
    const auto analysisKey=m_analysisFrames && request.fullResolution&&!linear
        ? StageGraph::analysisFrameKey(request.source,request.plan,request.geometry):QString{};
    // Look up completed frames only. A producer still filling its strips does
    // not delay this independent plot request or share its cancellation token.
    const auto completed=m_analysisFrames && !analysisKey.isEmpty()
        ? m_analysisFrames->find(analysisKey,cancel):AnalysisFrameCache::Lease{};
    const auto linearKey=m_analysisFrames&&request.fullResolution&&linear
        ?StageGraph::linearAnalysisFrameKey(request.source,request.plan,request.geometry,request.domain):QString{};
    const auto linearFrame=m_analysisFrames&&!linearKey.isEmpty()
        ?m_analysisFrames->findLinear(linearKey,request.domain,cancel):AnalysisFrameCache::LinearLease{};
    std::unique_ptr<BrushRenderRequest> admitted;
    std::unique_ptr<BrushAnalysisRequest> working;
    if(brush&&linear&&!linearFrame) {
        BrushAdmissionFailure reason;
        working=request.fullResolution?BrushAnalysisRequest::create(request.source,request.plan,request.geometry,
            BrushRenderRequest::Layout::Stripes128,cancel,&error,m_brushBudget,&reason)
            :BrushAnalysisRequest::createPrepared(request.source,request.plan,BrushRenderRequest::Layout::Stripes128,cancel,&error,m_brushBudget,&reason);
        if(!working)return cancelled(cancel)?ScopePlotResult{}:ScopePlotResult{{},0,error,reason==BrushAdmissionFailure::BudgetUnavailable};
    } else if(brush&&!linear&&!completed) {
        BrushAdmissionFailure reason;
        admitted=BrushRenderRequest::createOriginal(request.source,request.plan,request.geometry,
            BrushRenderRequest::Layout::Stripes128,cancel,&error,m_brushBudget,&reason);
        if(!admitted)return cancelled(cancel)?ScopePlotResult{}:ScopePlotResult{{},0,error,reason==BrushAdmissionFailure::BudgetUnavailable};
    }
    // A disabled/oversize cache renders only the requested plot, retaining
    // bounded tiles and avoiding speculative work that cannot be reused.
    if (!cacheable) {
        auto result=completed ? renderEncodedScopePlot(completed.image(),request.mode,cancel)
            :linearFrame?renderLinearScopePlot(linearFrame.image(),request.mode,cancel):working?renderScopePlot(*working,request.mode,cancel):admitted?renderScopePlot(*admitted,request.mode,cancel):renderScopePlot(request,cancel);
        if (cancelled(cancel)) { QMutexLocker lock(&m_mutex); ++m_cancelled; recordLocked(); return {}; }
        return result;
    }
    // A large completed analysis frame already supplies every encoded sample.
    // Deliver the requested full-resolution plot without first counting two
    // unrequested modes. Small/missing frames keep the shared three-mode pass.
    const bool requestedOnly=(completed&&completed.image().sizeInBytes()>=64*1024*1024)
        ||(linearFrame&&linearFrame.image().sizeInBytes()>=64*1024*1024);
    std::array<ScopePlotResult,3> results;
    if (requestedOnly) results[index]=linearFrame?renderLinearScopePlot(linearFrame.image(),request.mode,cancel):renderEncodedScopePlot(completed.image(),request.mode,cancel);
    else results=completed ? renderEncodedScopePlots(completed.image(),cancel)
        :linearFrame?renderLinearScopePlots(linearFrame.image(),cancel):working?renderScopePlots(*working,cancel):admitted?renderScopePlots(*admitted,cancel):renderScopePlots(request,cancel);
    QMutexLocker lock(&m_mutex);
    if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
    const auto valid=[](const ScopePlotResult &r) { return !r.image.isNull() && r.pixels && r.error.isEmpty(); };
    if (!valid(results[index])) return results[index];
    if (!requestedOnly) for (const auto &result : results) if (!valid(result)) return results[index];
    if (auto *entry=m_entries.object(key)) {
        // Concurrent requests for different modes can complete under one key.
        // Merge completed ink; cancellation above prevents partial publication.
        for (int i=0;i<3;++i) if (valid(results[i]) && !valid(entry->results[i])) entry->results[i]=results[i];
    } else {
        const auto previous=m_entries.size();
        m_entries.insert(key,new Entry{results,lut},int(cost));
        const qint64 evicted=previous+1-m_entries.size(); m_evictions+=evicted;
        if (evicted) PerformanceRecorder::count("scope_plot_cache_eviction",evicted);
    }
    recordLocked();
    return cancelled(cancel) ? ScopePlotResult{} : results[index];
}
