#include "core/pipeline/ImagePipeline.h"

#include <QColorSpace>
#include <QRgba64>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Oklab { float L = 0.0f, a = 0.0f, b = 0.0f; };

inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float smooth(float x) { x = clamp01(x); return x * x * (3.0f - 2.0f * x); }
inline Vec3 scale(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }

inline float srgbToLinear(float v) {
    v = std::max(v, 0.0f);
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

inline float linearToSrgb(float v) {
    v = std::max(v, 0.0f);
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

inline Vec3 linearSrgbToXyzD65(Vec3 c) {
    return {
        0.4124564f*c.x + 0.3575761f*c.y + 0.1804375f*c.z,
        0.2126729f*c.x + 0.7151522f*c.y + 0.0721750f*c.z,
        0.0193339f*c.x + 0.1191920f*c.y + 0.9503041f*c.z
    };
}

inline Vec3 xyzD65ToLinearSrgb(Vec3 c) {
    return {
         3.2404542f*c.x - 1.5371385f*c.y - 0.4985314f*c.z,
        -0.9692660f*c.x + 1.8760108f*c.y + 0.0415560f*c.z,
         0.0556434f*c.x - 0.2040259f*c.y + 1.0572252f*c.z
    };
}

inline Vec3 xyzD65ToD50(Vec3 c) {
    return {
        1.0478112f*c.x + 0.0228866f*c.y - 0.0501270f*c.z,
        0.0295424f*c.x + 0.9904844f*c.y - 0.0170491f*c.z,
       -0.0092345f*c.x + 0.0150436f*c.y + 0.7521316f*c.z
    };
}

inline Vec3 xyzD50ToD65(Vec3 c) {
    return {
         0.9555766f*c.x - 0.0230393f*c.y + 0.0631636f*c.z,
        -0.0282895f*c.x + 1.0099416f*c.y + 0.0210077f*c.z,
         0.0122982f*c.x - 0.0204830f*c.y + 1.3299098f*c.z
    };
}

inline Vec3 proPhotoToXyzD50(Vec3 c) {
    return {
        0.7976749f*c.x + 0.1351917f*c.y + 0.0313534f*c.z,
        0.2880402f*c.x + 0.7118741f*c.y + 0.0000857f*c.z,
        0.0000000f*c.x + 0.0000000f*c.y + 0.8252100f*c.z
    };
}

inline Vec3 xyzD50ToProPhoto(Vec3 c) {
    return {
         1.3459433f*c.x - 0.2556075f*c.y - 0.0511118f*c.z,
        -0.5445989f*c.x + 1.5081673f*c.y + 0.0205351f*c.z,
         0.0000000f*c.x + 0.0000000f*c.y + 1.2118128f*c.z
    };
}

inline Vec3 linearSrgbToProPhoto(Vec3 c) {
    return xyzD50ToProPhoto(xyzD65ToD50(linearSrgbToXyzD65(c)));
}

inline Vec3 proPhotoToLinearSrgb(Vec3 c) {
    return xyzD65ToLinearSrgb(xyzD50ToD65(proPhotoToXyzD50(c)));
}

inline Vec3 xyzD50ToBradfordLms(Vec3 c) {
    return {
         0.8951f*c.x + 0.2664f*c.y - 0.1614f*c.z,
        -0.7502f*c.x + 1.7135f*c.y + 0.0367f*c.z,
         0.0389f*c.x - 0.0685f*c.y + 1.0296f*c.z
    };
}

inline Vec3 bradfordLmsToXyzD50(Vec3 c) {
    return {
         0.9869929f*c.x - 0.1470543f*c.y + 0.1599627f*c.z,
         0.4323053f*c.x + 0.5183603f*c.y + 0.0492912f*c.z,
        -0.0085287f*c.x + 0.0400428f*c.y + 0.9684867f*c.z
    };
}

inline Vec3 applyWhiteBalanceDelta(Vec3 proPhoto, float temperature, float tint) {
    Vec3 lms = xyzD50ToBradfordLms(proPhotoToXyzD50(proPhoto));
    const float warm = std::clamp(temperature, -1.0f, 1.0f);
    const float magenta = std::clamp(tint, -1.0f, 1.0f);
    lms.x *= std::exp2( 0.18f * warm + 0.05f * magenta);
    lms.y *= std::exp2(-0.16f * magenta);
    lms.z *= std::exp2(-0.30f * warm + 0.04f * magenta);
    return xyzD50ToProPhoto(bradfordLmsToXyzD50(lms));
}

inline Oklab linearSrgbToOklab(Vec3 c) {
    const float l = 0.4122214708f*c.x + 0.5363325363f*c.y + 0.0514459929f*c.z;
    const float m = 0.2119034982f*c.x + 0.6806995451f*c.y + 0.1073969566f*c.z;
    const float s = 0.0883024619f*c.x + 0.2817188376f*c.y + 0.6299787005f*c.z;
    const float lp = std::cbrt(l);
    const float mp = std::cbrt(m);
    const float sp = std::cbrt(s);
    return {
        0.2104542553f*lp + 0.7936177850f*mp - 0.0040720468f*sp,
        1.9779984951f*lp - 2.4285922050f*mp + 0.4505937099f*sp,
        0.0259040371f*lp + 0.7827717662f*mp - 0.8086757660f*sp
    };
}

inline Vec3 oklabToLinearSrgb(Oklab c) {
    const float lp = c.L + 0.3963377774f*c.a + 0.2158037573f*c.b;
    const float mp = c.L - 0.1055613458f*c.a - 0.0638541728f*c.b;
    const float sp = c.L - 0.0894841775f*c.a - 1.2914855480f*c.b;
    const float l = lp*lp*lp;
    const float m = mp*mp*mp;
    const float s = sp*sp*sp;
    return {
         4.0767416621f*l - 3.3077115913f*m + 0.2309699292f*s,
        -1.2684380046f*l + 2.6097574011f*m - 0.3413193965f*s,
        -0.0041960863f*l - 0.7034186147f*m + 1.7076147010f*s
    };
}

inline float wrapHue(float degrees) {
    while (degrees < 0.0f) degrees += 360.0f;
    while (degrees >= 360.0f) degrees -= 360.0f;
    return degrees;
}

inline float hueDistance(float a, float b) {
    float d = std::fabs(wrapHue(a) - wrapHue(b));
    return std::min(d, 360.0f - d);
}

inline float hueBandWeight(float hue, float center) {
    const float d = hueDistance(hue, center);
    if (d >= 52.0f) return 0.0f;
    return 0.5f + 0.5f * std::cos(kPi * d / 52.0f);
}

inline Vec3 applyPerceptualColor(Vec3 linearSrgb, const AdjustmentState &state) {
    Oklab lab = linearSrgbToOklab(linearSrgb);
    float chroma = std::hypot(lab.a, lab.b);
    float hue = chroma > 1.0e-6f ? wrapHue(std::atan2(lab.b, lab.a) * 180.0f / kPi) : 0.0f;

    hue = wrapHue(hue + static_cast<float>(state.hue));

    float chromaScale = std::max(0.0f, 1.0f + static_cast<float>(state.saturation / 100.0));
    const float chromaNorm = clamp01(chroma / 0.30f);
    const float vibrance = static_cast<float>(state.vibrance / 100.0);
    chromaScale *= std::max(0.0f, 1.0f + vibrance * (1.0f - chromaNorm) * 0.85f);

    static constexpr std::array<float, AdjustmentState::ColorBandCount> centers{
        28.0f, 58.0f, 95.0f, 145.0f, 200.0f, 260.0f, 305.0f, 340.0f
    };

    float hueDelta = 0.0f;
    float satDelta = 0.0f;
    float lumDelta = 0.0f;
    for (int i = 0; i < AdjustmentState::ColorBandCount; ++i) {
        const float w = hueBandWeight(hue, centers[static_cast<std::size_t>(i)]);
        hueDelta += static_cast<float>(state.hslHue[static_cast<std::size_t>(i)] / 100.0) * 35.0f * w;
        satDelta += static_cast<float>(state.hslSaturation[static_cast<std::size_t>(i)] / 100.0) * w;
        lumDelta += static_cast<float>(state.hslLuminance[static_cast<std::size_t>(i)] / 100.0) * 0.18f * w;
    }

    hue = wrapHue(hue + hueDelta);
    chroma *= chromaScale * std::max(0.0f, 1.0f + satDelta);
    lab.L = std::clamp(lab.L + lumDelta, 0.0f, 1.5f);
    lab.a = chroma * std::cos(hue * kPi / 180.0f);
    lab.b = chroma * std::sin(hue * kPi / 180.0f);
    return oklabToLinearSrgb(lab);
}

inline Vec3 compressNegativeGamut(Vec3 rgb) {
    const float y = std::max(0.0f, 0.2126f*rgb.x + 0.7152f*rgb.y + 0.0722f*rgb.z);
    const float minChannel = std::min({rgb.x, rgb.y, rgb.z});
    if (minChannel < 0.0f && y > 1.0e-6f) {
        const float factor = std::clamp(y / (y - minChannel), 0.0f, 1.0f) * 0.995f;
        rgb.x = y + (rgb.x - y) * factor;
        rgb.y = y + (rgb.y - y) * factor;
        rgb.z = y + (rgb.z - y) * factor;
    }
    rgb.x = std::max(0.0f, rgb.x);
    rgb.y = std::max(0.0f, rgb.y);
    rgb.z = std::max(0.0f, rgb.z);
    return rgb;
}

inline Vec3 applyHighlightRecovery(Vec3 proPhoto, float amount) {
    amount = clamp01(amount);
    if (amount <= 0.0f) return proPhoto;
    const float maxChannel = std::max({proPhoto.x, proPhoto.y, proPhoto.z});
    if (maxChannel <= 0.82f) return proPhoto;
    const float weight = smooth((maxChannel - 0.82f) / 1.25f) * amount;
    const float targetMax = 0.82f + 0.62f * (1.0f - std::exp(-(maxChannel - 0.82f) / 0.62f));
    const float gain = maxChannel > 1.0e-6f ? targetMax / maxChannel : 1.0f;
    return scale(proPhoto, 1.0f + (gain - 1.0f) * weight);
}

inline float curveSample(const AdjustmentState::CurveArray &curve, float x) {
    x = clamp01(x);
    const float position = x * static_cast<float>(AdjustmentState::CurvePointCount - 1);
    const int segment = std::min(static_cast<int>(position), AdjustmentState::CurvePointCount - 2);
    const float t = position - static_cast<float>(segment);
    const float a = static_cast<float>(curve[static_cast<std::size_t>(segment)]);
    const float b = static_cast<float>(curve[static_cast<std::size_t>(segment + 1)]);
    return clamp01(a + (b - a) * t);
}

inline float displayShoulder(float linear, float recovery) {
    linear = std::max(linear, 0.0f);
    recovery = clamp01(recovery);
    const float start = 0.82f - 0.18f * recovery;
    if (linear <= start) return linear;
    const float span = std::max(0.01f, 1.0f - start);
    const float strength = 1.65f + 1.85f * recovery;
    return start + span * (1.0f - std::exp(-strength * (linear - start) / span));
}

inline Vec3 applyMasterCurve(Vec3 rgb, const AdjustmentState::CurveArray &curve) {
    const float y = std::max(0.0f, 0.2126f*rgb.x + 0.7152f*rgb.y + 0.0722f*rgb.z);
    if (y <= 1.0e-6f) return rgb;
    const float mapped = curveSample(curve, y);
    return scale(rgb, mapped / y);
}

inline float middleGrayContrast(float y, float factor) {
    constexpr float pivot = 0.18f;
    if (y <= 0.0f) return 0.0f;
    return pivot * std::pow(y / pivot, factor);
}
}

QImage ImagePipeline::process(const QImage &source, const AdjustmentState &state, InputEncoding inputEncoding) {
    if (source.isNull()) return {};

    QImage out = source.convertToFormat(QImage::Format_RGBA64);
    const float exposureGain = static_cast<float>(std::exp2(state.exposure));
    const float temperature = static_cast<float>(state.temperature / 100.0);
    const float tint = static_cast<float>(state.tint / 100.0);
    const float contrastFactor = std::clamp(static_cast<float>(1.0 + state.contrast / 100.0), 0.05f, 2.5f);
    const float hi = static_cast<float>(state.highlights / 100.0);
    const float sh = static_cast<float>(state.shadows / 100.0);
    const float wh = static_cast<float>(state.whites / 100.0);
    const float bl = static_cast<float>(state.blacks / 100.0);
    const float recovery = clamp01(static_cast<float>(state.highlightRecovery / 100.0));

    for (int y = 0; y < out.height(); ++y) {
        auto *line = reinterpret_cast<QRgba64 *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgba64 original = line[x];
            Vec3 working{
                original.red() / 65535.0f,
                original.green() / 65535.0f,
                original.blue() / 65535.0f
            };

            if (inputEncoding == InputEncoding::SRgb) {
                Vec3 linearSrgb{srgbToLinear(working.x), srgbToLinear(working.y), srgbToLinear(working.z)};
                working = linearSrgbToProPhoto(linearSrgb);
            }

            // RAW camera WB is the baseline from LibRaw. User WB is a scene-linear
            // chromatic-adaptation delta in the wide-gamut working space.
            working = applyWhiteBalanceDelta(working, temperature, tint);

            // Exposure is defined in scene-linear stops. One EV always doubles values here.
            working = scale(working, exposureGain);
            working = applyHighlightRecovery(working, recovery);

            // Tonal zones operate on scene luminance and scale RGB together to preserve hue.
            const float sceneY = std::max(0.0f, proPhotoToXyzD50(working).y);
            const float shadowWeight = 1.0f - smooth(sceneY / 0.32f);
            const float highlightWeight = smooth((sceneY - 0.26f) / 0.82f);
            const float whiteWeight = smooth((sceneY - 0.62f) / 0.70f);
            const float blackWeight = 1.0f - smooth(sceneY / 0.16f);
            const float localStops = sh * shadowWeight
                                   + hi * highlightWeight
                                   + wh * whiteWeight * 0.75f
                                   + bl * blackWeight * 0.75f;
            working = scale(working, std::exp2(localStops));

            const float contrastY = std::max(0.0f, proPhotoToXyzD50(working).y);
            if (contrastY > 1.0e-6f) {
                const float mappedY = middleGrayContrast(contrastY, contrastFactor);
                working = scale(working, mappedY / contrastY);
            }

            // Perceptual color operations run before the final display transform.
            Vec3 displayLinear = proPhotoToLinearSrgb(working);
            displayLinear = applyPerceptualColor(displayLinear, state);
            displayLinear = compressNegativeGamut(displayLinear);

            displayLinear.x = displayShoulder(displayLinear.x, recovery);
            displayLinear.y = displayShoulder(displayLinear.y, recovery);
            displayLinear.z = displayShoulder(displayLinear.z, recovery);

            displayLinear = applyMasterCurve(displayLinear, state.masterCurve);
            displayLinear.x = curveSample(state.redCurve, displayLinear.x);
            displayLinear.y = curveSample(state.greenCurve, displayLinear.y);
            displayLinear.z = curveSample(state.blueCurve, displayLinear.z);

            const float r = linearToSrgb(clamp01(displayLinear.x));
            const float g = linearToSrgb(clamp01(displayLinear.y));
            const float b = linearToSrgb(clamp01(displayLinear.z));

            line[x] = QRgba64::fromRgba64(
                static_cast<quint16>(std::lround(clamp01(r) * 65535.0f)),
                static_cast<quint16>(std::lround(clamp01(g) * 65535.0f)),
                static_cast<quint16>(std::lround(clamp01(b) * 65535.0f)),
                original.alpha());
        }
    }

    out.setColorSpace(QColorSpace(QColorSpace::SRgb));
    out.setText(QStringLiteral("JixelLightPipeline"), QStringLiteral("Linear ProPhoto RGB -> Perceptual Color -> Display sRGB"));
    return out;
}
