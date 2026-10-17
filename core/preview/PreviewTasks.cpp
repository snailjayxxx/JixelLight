#include "core/preview/PreviewTasks.h"
#include "diagnostics/PerformanceRecorder.h"
#include "core/pipeline/StageGraph.h"
#include "core/pipeline/FloatSceneResampler.h"
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

// Keep the long-standing Qt integer resize pixel identity intact. Explicit
// FP32 scene input needs a linear, alpha-aware filter: Qt's generic QImage
// conversion may otherwise clip scene highlights or out-of-gamut channels.
QImage resizeWithin(const QImage &input,const QSize &bounds,const CancelToken &cancel) {
    if(input.format()!=QImage::Format_RGBA32FPx4)
        return input.scaled(bounds,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    const QSize target=input.size().scaled(bounds,Qt::KeepAspectRatio);
    if(target==input.size()) return input;
    PerformanceSpan timer(QStringLiteral("preview_fp32_scale"),
        {{"source_pixels",qint64(input.width())*input.height()},
         {"target_pixels",qint64(target.width())*target.height()}});
    PerformanceRecorder::count("preview_fp32_scale_calls");
    PerformanceRecorder::value("preview_fp32_scale_filter",
        QStringLiteral("separable area down / bilinear up; scene-linear associated-alpha"));
    return FloatSceneResampler::scale(input,target,cancel);
}
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
        QImage source = request.geometry.apply(request.image,cancel);
        if (source.isNull() || cancelled(cancel)) return {};
        const QSize viewport = request.viewport.expandedTo(QSize(64, 64));
        if (request.zoom > 0 && request.fullResolution) {
            const int w = std::min(source.width(), std::max(1, int(std::ceil(viewport.width() / request.zoom))));
            const int h = std::min(source.height(), std::max(1, int(std::ceil(viewport.height() / request.zoom))));
            const int x = std::clamp(int(request.centerX * source.width() - w / 2.0), 0, source.width() - w);
            const int y = std::clamp(int(request.centerY * source.height() - h / 2.0), 0, source.height() - h);
            result.viewportOnly = w != source.width() || h != source.height();
            result.frameRect={double(x)/source.width(),double(y)/source.height(),double(w)/source.width(),double(h)/source.height()};
            source = source.copy(x, y, w, h);
            result.displayPixels = QSizeF(w * request.zoom, h * request.zoom);
        } else {
            const double fit = std::min(double(viewport.width()) / source.width(), double(viewport.height()) / source.height());
            result.displayPixels = QSizeF(source.width() * fit, source.height() * fit);
            const QSize target = result.displayPixels.toSize().boundedTo(source.size());
            if (source.size() != target) source = resizeWithin(source,target,cancel);
        }
        if (cancelled(cancel)) return {};
        // Bound interactive texture sizes. At 100% the view is an original-pixel
        // ROI, not the entire photograph; no RAW re-decode is needed when panning.
        if (std::max(source.width(), source.height()) > 4096)
            source = resizeWithin(source,{4096,4096},cancel);
        result.normal = source;
        result.fast = std::max(source.width(), source.height()) > 1024
            ? resizeWithin(source,{1024,1024},cancel) : source;
        if (cancelled(cancel)) return {};
        if (result.normal.isNull() || result.fast.isNull()) {
            result.error=QStringLiteral("Unable to allocate prepared image or FP32 preview scale");
            return result;
        }
        // Float conversion is performed once for a source/viewport change, not
        // once per parameter edit. GPU working textures remain FP32.
        result.gpu = ImagePipeline::floatSource(source,cancel);
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
