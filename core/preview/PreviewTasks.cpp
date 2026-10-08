#include "core/preview/PreviewTasks.h"
#include "diagnostics/PerformanceRecorder.h"
#include "core/pipeline/StageGraph.h"
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>

namespace {
// Bound independently from the decoded-source cache. QImage snapshots are
// immutable and implicitly shared; cancellation never publishes a partial entry.
struct PreparedCache {
    QMutex mutex;
    QCache<QString, PreparedPreview> entries{128 * 1024}; // KiB
};
PreparedCache &preparedCache() { static PreparedCache cache; return cache; }
}

PreparedPreview preparePreview(const PrepareRequest &request, const CancelToken &cancel) {
    PerformanceSpan timer("preview_prepare");
    PreparedPreview result;
    try {
        if (request.image.isNull() || cancelled(cancel)) return result;
        const QString cacheKey = StageGraph::prepareKey(request);
        auto &cache = preparedCache();
        {
            QMutexLocker lock(&cache.mutex);
            if (const auto *cached = cache.entries.object(cacheKey)) {
                PerformanceRecorder::count("prepare_cache_hit");
                return cancelled(cancel) ? PreparedPreview{} : *cached;
            }
        }
        PerformanceRecorder::count("prepare_cache_miss");
        QImage source = request.geometry.apply(request.image);
        if (source.isNull() || cancelled(cancel)) return {};
        const QSize viewport = request.viewport.expandedTo(QSize(64, 64));
        if (request.zoom > 0 && request.fullResolution) {
            const int w = std::min(source.width(), std::max(1, int(std::ceil(viewport.width() / request.zoom))));
            const int h = std::min(source.height(), std::max(1, int(std::ceil(viewport.height() / request.zoom))));
            const int x = std::clamp(int(request.centerX * source.width() - w / 2.0), 0, source.width() - w);
            const int y = std::clamp(int(request.centerY * source.height() - h / 2.0), 0, source.height() - h);
            result.viewportOnly = w != source.width() || h != source.height();
            source = source.copy(x, y, w, h);
            result.displayPixels = QSizeF(w * request.zoom, h * request.zoom);
        } else {
            const double fit = std::min(double(viewport.width()) / source.width(), double(viewport.height()) / source.height());
            result.displayPixels = QSizeF(source.width() * fit, source.height() * fit);
            const QSize target = result.displayPixels.toSize().boundedTo(source.size());
            if (source.size() != target) source = source.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (cancelled(cancel)) return {};
        // Bound interactive texture sizes. At 100% the view is an original-pixel
        // ROI, not the entire photograph; no RAW re-decode is needed when panning.
        if (std::max(source.width(), source.height()) > 4096)
            source = source.scaled(4096, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        result.normal = source;
        result.fast = std::max(source.width(), source.height()) > 1024
            ? source.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation) : source;
        if (cancelled(cancel)) return {};
        // Float conversion is performed once for a source/viewport change, not
        // once per parameter edit. GPU working textures remain FP32.
        result.gpu = source.convertToFormat(QImage::Format_RGBA32FPx4);
        if (result.gpu.isNull()) result.error = QStringLiteral("Unable to allocate GPU source staging image");
        if (cancelled(cancel)) return {};
        if (result.error.isEmpty()) {
            const qint64 bytes = result.normal.sizeInBytes() + result.fast.sizeInBytes() + result.gpu.sizeInBytes();
            QMutexLocker lock(&cache.mutex);
            if (bytes <= qint64(cache.entries.maxCost()) * 1024) {
                cache.entries.insert(cacheKey, new PreparedPreview(result), int((bytes + 1023) / 1024));
                PerformanceRecorder::value("prepare_cache_bytes", qint64(cache.entries.totalCost()) * 1024);
            }
        }
    } catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
      catch (...) { result.error = QStringLiteral("Preview preparation failed"); }
    return result;
}
