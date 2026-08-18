#include "core/pipeline/ImagePipeline.h"

#include <algorithm>
#include <cmath>

namespace {
inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float smooth(float x) { x = clamp01(x); return x * x * (3.0f - 2.0f * x); }
}

QImage ImagePipeline::process(const QImage &source, const AdjustmentState &s) {
    if (source.isNull()) return {};

    QImage out = source.convertToFormat(QImage::Format_RGBA8888);
    const float exposureGain = static_cast<float>(std::pow(2.0, s.exposure));
    const float contrast = static_cast<float>(1.0 + s.contrast / 100.0);
    const float temp = static_cast<float>(s.temperature / 100.0);
    const float tint = static_cast<float>(s.tint / 100.0);
    const float hi = static_cast<float>(s.highlights / 100.0);
    const float sh = static_cast<float>(s.shadows / 100.0);
    const float wh = static_cast<float>(s.whites / 100.0);
    const float bl = static_cast<float>(s.blacks / 100.0);

    for (int y = 0; y < out.height(); ++y) {
        auto *line = out.scanLine(y);
        for (int x = 0; x < out.width(); ++x) {
            auto *p = line + x * 4;
            float r = p[0] / 255.0f;
            float g = p[1] / 255.0f;
            float b = p[2] / 255.0f;

            // Preview/reference white-balance transform. A future float pipeline will
            // replace this with camera-space WB + color-management transforms.
            r *= 1.0f + temp * 0.18f + tint * 0.06f;
            g *= 1.0f - tint * 0.10f;
            b *= 1.0f - temp * 0.18f + tint * 0.06f;

            r *= exposureGain; g *= exposureGain; b *= exposureGain;

            float luma = clamp01(0.2126f * r + 0.7152f * g + 0.0722f * b);
            const float shadowWeight = (1.0f - smooth(luma * 1.8f));
            const float highlightWeight = smooth((luma - 0.35f) / 0.65f);
            const float whiteWeight = smooth((luma - 0.72f) / 0.28f);
            const float blackWeight = 1.0f - smooth(luma / 0.28f);

            const float tonalDelta = sh * shadowWeight * 0.28f
                                   + hi * highlightWeight * 0.22f
                                   + wh * whiteWeight * 0.18f
                                   + bl * blackWeight * 0.18f;
            r += tonalDelta; g += tonalDelta; b += tonalDelta;

            r = (r - 0.5f) * contrast + 0.5f;
            g = (g - 0.5f) * contrast + 0.5f;
            b = (b - 0.5f) * contrast + 0.5f;

            p[0] = static_cast<uchar>(std::lround(clamp01(r) * 255.0f));
            p[1] = static_cast<uchar>(std::lround(clamp01(g) * 255.0f));
            p[2] = static_cast<uchar>(std::lround(clamp01(b) * 255.0f));
        }
    }
    return out;
}
