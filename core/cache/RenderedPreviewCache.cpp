#include "core/cache/RenderedPreviewCache.h"
#include "core/pipeline/StageGraph.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QMutexLocker>
#include <QRgba64>
#include <algorithm>
#include <limits>

RenderedPreviewCache::RenderedPreviewCache(qint64 bytes)
    : m_entries(int(std::clamp(bytes / 1024, qint64(0), qint64(std::numeric_limits<int>::max())))) {}

QJsonObject RenderedPreviewCache::snapshotLocked() const {
    return {{"schema", 1}, {"backend", "cpu-preview"}, {"key_version", "cpu-preview-v1"},
            {"budget_bytes", qint64(m_entries.maxCost()) * 1024},
            {"charged_bytes", qint64(m_entries.totalCost()) * 1024}, {"entries", m_entries.size()},
            {"hits", m_hits}, {"misses", m_misses}, {"bypasses", m_bypasses},
            {"evictions", m_evictions}, {"cancelled_requests", m_cancelled},
            {"accounting", "image and retained LUT payloads, rounded up per entry to KiB; excludes allocator overhead and live caller images"}};
}
QJsonObject RenderedPreviewCache::snapshot() const {
    QMutexLocker lock(&m_mutex);
    return snapshotLocked();
}
void RenderedPreviewCache::recordLocked() {
    PerformanceRecorder::value("render_cache", snapshotLocked());
}

QImage RenderedPreviewCache::render(const QImage &source, const ProcessingPlan &plan, const CancelToken &cancel) {
    PerformanceSpan timing("preview_render_cached");
    if (cancelled(cancel)) {
        QMutexLocker lock(&m_mutex); ++m_cancelled; recordLocked(); return {};
    }
    if (source.isNull()) return {};
    const auto lut = plan.data[ProcessingPlan::LookStyle].w > 0 ? plan.state.look.lut : nullptr;
    const qint64 bytes = qint64(source.width()) * source.height() * sizeof(QRgba64)
        + (lut ? qint64(lut->rgb.size()) * sizeof(float) : 0);
    const qint64 cost = (bytes + 1023) / 1024;
    const bool cacheable = m_entries.maxCost() > 0 && cost <= m_entries.maxCost();
    const QString key = cacheable ? StageGraph::renderKey(source, plan) : QString{};
    {
        QMutexLocker lock(&m_mutex);
        if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
        if (cacheable) {
            if (const auto *entry = m_entries.object(key)) {
                ++m_hits; PerformanceRecorder::count("render_cache_hit"); recordLocked();
                return cancelled(cancel) ? QImage{} : entry->image;
            }
            ++m_misses; PerformanceRecorder::count("render_cache_miss");
        } else {
            ++m_bypasses; PerformanceRecorder::count("render_cache_bypass");
        }
        recordLocked();
    }
    QImage image = ImagePipeline::processWithPlan(source, plan, cancel);
    QMutexLocker lock(&m_mutex);
    if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
    if (image.isNull() || !cacheable) return image;
    // Rendering runs outside the lock. Concurrent equivalent misses may finish
    // twice, but only one complete immutable entry is retained.
    if (!m_entries.object(key)) {
        const auto previous = m_entries.size();
        m_entries.insert(key, new Entry{image, lut}, int(cost));
        const qint64 evicted = previous + 1 - m_entries.size();
        m_evictions += evicted;
        if (evicted) PerformanceRecorder::count("render_cache_eviction", evicted);
    }
    recordLocked();
    return cancelled(cancel) ? QImage{} : image;
}
