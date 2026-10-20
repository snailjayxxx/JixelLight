#include "ScopePlotCache.h"
#include "core/pipeline/StageGraph.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QMutexLocker>
#include <algorithm>
#include <limits>

ScopePlotCache::ScopePlotCache(qint64 bytes)
    : m_entries(int(std::clamp(bytes/1024,qint64(0),qint64(std::numeric_limits<int>::max())))) {}
QJsonObject ScopePlotCache::snapshotLocked() const {
    return {{"schema",1},{"backend","cpu-scope-plots"},{"key_version","scope-plots-v1"},
        {"budget_bytes",qint64(m_entries.maxCost())*1024},{"charged_bytes",qint64(m_entries.totalCost())*1024},{"entries",m_entries.size()},
        {"hits",m_hits},{"misses",m_misses},{"bypasses",m_bypasses},{"evictions",m_evictions},{"cancelled_requests",m_cancelled},
        {"accounting","three ARGB32 ink images, entry and retained LUT payload, rounded to KiB; allocator/key overhead excluded; no source or rendered frames retained"}};
}
QJsonObject ScopePlotCache::snapshot() const { QMutexLocker lock(&m_mutex); return snapshotLocked(); }
void ScopePlotCache::recordLocked() { PerformanceRecorder::value("scope_plot_cache",snapshotLocked()); }
ScopePlotResult ScopePlotCache::render(const ScopePlotRequest &request,const CancelToken &cancel) {
    PerformanceSpan timing("scope_plot_cached");
    const int index=request.mode=="waveform" ? 0 : request.mode=="parade" ? 1 : request.mode=="vectorscope" ? 2 : -1;
    if (index<0 || request.source.isNull()) return {};
    if (cancelled(cancel)) { QMutexLocker lock(&m_mutex); ++m_cancelled; recordLocked(); return {}; }
    const auto lut=request.plan.data[ProcessingPlan::LookStyle].w>0 ? request.plan.state.look.lut : nullptr;
    constexpr qint64 inkBytes=qint64(4*ScopePlotCounts::Columns*ScopePlotCounts::Levels+ScopePlotCounts::ChromaSize*ScopePlotCounts::ChromaSize)*sizeof(QRgb);
    const qint64 bytes=sizeof(Entry)+inkBytes+(lut ? qint64(lut->rgb.size())*sizeof(float) : 0),cost=(bytes+1023)/1024;
    const bool cacheable=m_entries.maxCost()>0 && cost<=m_entries.maxCost();
    const auto key=cacheable ? StageGraph::scopePlotsKey(request) : QString{};
    {
        QMutexLocker lock(&m_mutex);
        if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
        if (cacheable) {
            if (const auto *entry=m_entries.object(key)) {
                ++m_hits; PerformanceRecorder::count("scope_plot_cache_hit"); recordLocked();
                return cancelled(cancel) ? ScopePlotResult{} : entry->results[index];
            }
            ++m_misses; PerformanceRecorder::count("scope_plot_cache_miss");
        } else { ++m_bypasses; PerformanceRecorder::count("scope_plot_cache_bypass"); }
        recordLocked();
    }
    // A disabled/oversize cache renders only the requested plot, retaining
    // bounded tiles and avoiding speculative work that cannot be reused.
    if (!cacheable) {
        auto result=renderScopePlot(request,cancel);
        if (cancelled(cancel)) { QMutexLocker lock(&m_mutex); ++m_cancelled; recordLocked(); return {}; }
        return result;
    }
    auto results=renderScopePlots(request,cancel);
    QMutexLocker lock(&m_mutex);
    if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
    for (const auto &result : results) if (result.image.isNull() || !result.pixels || !result.error.isEmpty()) return results[index];
    if (!m_entries.object(key)) {
        const auto previous=m_entries.size();
        m_entries.insert(key,new Entry{results,lut},int(cost));
        const qint64 evicted=previous+1-m_entries.size(); m_evictions+=evicted;
        if (evicted) PerformanceRecorder::count("scope_plot_cache_eviction",evicted);
    }
    recordLocked();
    return cancelled(cancel) ? ScopePlotResult{} : results[index];
}
