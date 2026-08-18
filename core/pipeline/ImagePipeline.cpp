#include "core/pipeline/ImagePipeline.h"

#include <QRgba64>
#include <algorithm>
#include <cmath>

namespace {
inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float smooth(float x) { x = clamp01(x); return x * x * (3.0f - 2.0f * x); }

inline float srgbToLinear(float v) {
    v = std::max(v, 0.0f);
    return v <= 0.04045f ? v / 12.92f
                         : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

inline float linearToSrgb(float v) {
    v = std::max(v, 0.0f);
    return v <= 0.0031308f ? 12.92f * v
                           : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// A gentle display-referred shoulder. It is deliberately applied only after
// RAW-domain exposure/tone work so positive exposure does not immediately
// behave like clipping an already-rendered JPEG.
inline float displayShoulder(float linear) {
    linear = std::max(linear, 0.0f);
    constexpr float start = 0.65f;
    constexpr float range = 1.0f - start;
    if (linear <= start) return linear;
    return start + range * (1.0f - std::exp(-(linear - start) / range));
}

inline float contrastAroundMiddleGray(float v, float factor) {
    constexpr float pivot = 0.18f;
    if (v <= 0.0f) return 0.0f;
    return pivot * std::pow(v / pivot, factor);
}
}

QImage ImagePipeline::process(const QImage &source, const AdjustmentState &s, InputEncoding inputEncoding) {
    if (source.isNull()) return {};

    QImage out = source.convertToFormat(QImage::Format_RGBA64);
    const float exposureGain = static_cast<float>(std::pow(2.0, s.exposure));
    const float contrastFactor = std::clamp(static_cast<float>(1.0 + s.contrast / 100.0), 0.05f, 2.0f);
    const float temp = static_cast<float>(s.temperature / 100.0);
    const float tint = static_cast<float>(s.tint / 100.0);
    const float hi = static_cast<float>(s.highlights / 100.0);
    const float sh = static_cast<float>(s.shadows / 100.0);
    const float wh = static_cast<float>(s.whites / 100.0);
    const float bl = static_cast<float>(s.blacks / 100.0);
    const bool sourceIsLinear = inputEncoding == InputEncoding::Linear;

    for (int y = 0; y < out.height(); ++y) {
        auto *line = reinterpret_cast<QRgba64 *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgba64 original = line[x];
            float r = original.red() / 65535.0f;
            float g = original.green() / 65535.0f;
            float b = original.blue() / 65535.0f;

            if (!sourceIsLinear) {
                r = srgbToLinear(r);
                g = srgbToLinear(g);
                b = srgbToLinear(b);
            }

            // White-balance fine tuning is now performed before exposure in
            // linear light. Camera WB is already applied by the RAW decoder;
            // these controls are non-destructive deltas around that baseline.
            r *= std::max(0.05f, 1.0f + temp * 0.20f + tint * 0.06f);
            g *= std::max(0.05f, 1.0f - tint * 0.10f);
            b *= std::max(0.05f, 1.0f - temp * 0.20f + tint * 0.06f);

            // Exposure is the critical RAW-domain operation: one EV doubles
            // linear scene-referred values before any display transfer curve.
            r *= exposureGain;
            g *= exposureGain;
            b *= exposureGain;

            const float luma = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
            const float shadowWeight = 1.0f - smooth(luma / 0.32f);
            const float highlightWeight = smooth((luma - 0.28f) / 0.72f);
            const float whiteWeight = smooth((luma - 0.62f) / 0.50f);
            const float blackWeight = 1.0f - smooth(luma / 0.16f);

            const float localStops = sh * shadowWeight
                                   + hi * highlightWeight
                                   + wh * whiteWeight * 0.75f
                                   + bl * blackWeight * 0.75f;
            const float localGain = std::pow(2.0f, localStops);
            r *= localGain;
            g *= localGain;
            b *= localGain;

            r = contrastAroundMiddleGray(r, contrastFactor);
            g = contrastAroundMiddleGray(g, contrastFactor);
            b = contrastAroundMiddleGray(b, contrastFactor);

            r = linearToSrgb(displayShoulder(r));
            g = linearToSrgb(displayShoulder(g));
            b = linearToSrgb(displayShoulder(b));

            line[x] = QRgba64::fromRgba64(
                static_cast<quint16>(std::lround(clamp01(r) * 65535.0f)),
                static_cast<quint16>(std::lround(clamp01(g) * 65535.0f)),
                static_cast<quint16>(std::lround(clamp01(b) * 65535.0f)),
                original.alpha());
        }
    }
    return out;
}
