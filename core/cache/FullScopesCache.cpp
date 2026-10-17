#include "core/cache/FullScopesCache.h"
#include "core/preview/PreviewTasks.h"
#include "core/pipeline/StageGraph.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QMutexLocker>
#include <algorithm>
#include <limits>

FullScopesCache::FullScopesCache(qint64 bytes)
    : m_entries(int(std::clamp(bytes/1024,qint64(0),qint64(std::numeric_limits<int>::max())))) {}
QJsonObject FullScopesCache::snapshotLocked() const {
    return {{"schema",1},{"backend","cpu-full-scopes"},{"key_version","full-scopes-v1"},{"budget_bytes",qint64(m_entries.maxCost())*1024},
        {"charged_bytes",qint64(m_entries.totalCost())*1024},{"entries",m_entries.size()},
        {"hits",m_hits},{"misses",m_misses},{"bypasses",m_bypasses},{"evictions",m_evictions},{"cancelled_requests",m_cancelled},
        {"accounting","entry, 4096 QVariant bins and retained LUT payload, rounded to KiB; allocator/key overhead excluded; no frames retained"}};
}
QJsonObject FullScopesCache::snapshot() const { QMutexLocker lock(&m_mutex); return snapshotLocked(); }
void FullScopesCache::recordLocked() { PerformanceRecorder::value("full_scopes_cache",snapshotLocked()); }
ScopesResult FullScopesCache::analyze(const ScopeRequest &request, const CancelToken &cancel) {
    PerformanceSpan timing("full_scopes_cached");
    if (cancelled(cancel)) { QMutexLocker lock(&m_mutex); ++m_cancelled; recordLocked(); return {}; }
    if (request.image.isNull()) return {};
    const auto lut=request.plan.data[ProcessingPlan::LookStyle].w>0 ? request.plan.state.look.lut : nullptr;
    const qint64 bytes=sizeof(Entry)+4*1024*sizeof(QVariant)+(lut ? qint64(lut->rgb.size())*sizeof(float) : 0);
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
    const auto image=request.geometry.apply(request.image,cancel);
    auto result=ScopesEngine::analyzeFull(image,request.plan,cancel);
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
