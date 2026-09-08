#include "core/preview/PreviewTasks.h"
#include "diagnostics/PerformanceRecorder.h"
#include <algorithm>
#include <cmath>

PreparedPreview preparePreview(const PrepareRequest &request, const CancelToken &cancel) {
    PerformanceSpan timer("preview_prepare");
    PreparedPreview result;
    try {
        if (request.image.isNull() || cancelled(cancel)) return result;
        QImage source = request.image;
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
    } catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
      catch (...) { result.error = QStringLiteral("Preview preparation failed"); }
    return result;
}
