#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/async/ParallelRows.h"
#include "core/look/LookProfiles.h"
#include "core/look/LookDetail.h"
#include "diagnostics/PerformanceRecorder.h"

#include <QColorSpace>
#include <QRgba64>
#include <algorithm>
#include <array>
#include <cmath>
#include <bit>
#include <cstdint>

namespace {
constexpr float kPi = 3.14159265358979323846f;
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Oklab { float L = 0.0f, a = 0.0f, b = 0.0f; };
inline float orderedDot(float ax, float ay, float az, Vec3 v) {
    const float xy = ax * v.x + ay * v.y;
    return xy + az * v.z;
}
inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float chromaLength(float a, float b) {
    const float aa = a * a;
    const float bb = b * b;
    return std::sqrt(aa + bb);
}
inline float stableCubeRoot(float value) {
    const float magnitude = std::fabs(value);
    if (magnitude == 0.0f) return 0.0f;
    float root = std::pow(magnitude, 1.0f / 3.0f);
    root = (2.0f * root + magnitude / (root * root)) / 3.0f;
    root = (2.0f * root + magnitude / (root * root)) / 3.0f;
    return std::copysign(root, value);
}
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
        orderedDot(0.7976749f,0.1351917f,0.0313534f,c),
        orderedDot(0.2880402f,0.7118741f,0.0000857f,c),
        orderedDot(0.0f,0.0f,0.8252100f,c)
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
    const float l = orderedDot(0.4122214708f,0.5363325363f,0.0514459929f,c);
    const float m = orderedDot(0.2119034982f,0.6806995451f,0.1073969566f,c);
    const float s = orderedDot(0.0883024619f,0.2817188376f,0.6299787005f,c);
    const float lp = stableCubeRoot(l);
    const float mp = stableCubeRoot(m);
    const float sp = stableCubeRoot(s);
    const Vec3 roots{lp,mp,sp};
    return {
        orderedDot(0.2104542553f,0.7936177850f,-0.0040720468f,roots),
        orderedDot(1.9779984951f,-2.4285922050f,0.4505937099f,roots),
        orderedDot(0.0259040371f,0.7827717662f,-0.8086757660f,roots)
    };
}
inline Vec3 oklabToLinearSrgb(Oklab c) {
    const Vec3 lab{c.L,c.a,c.b};
    const float lp = orderedDot(1.0f,0.3963377774f,0.2158037573f,lab);
    const float mp = orderedDot(1.0f,-0.1055613458f,-0.0638541728f,lab);
    const float sp = orderedDot(1.0f,-0.0894841775f,-1.2914855480f,lab);
    const float l = lp*lp*lp;
    const float m = mp*mp*mp;
    const float s = sp*sp*sp;
    const Vec3 cubes{l,m,s};
    return {
        orderedDot(4.0767416621f,-3.3077115913f,0.2309699292f,cubes),
        orderedDot(-1.2684380046f,2.6097574011f,-0.3413193965f,cubes),
        orderedDot(-0.0041960863f,-0.7034186147f,1.7076147010f,cubes)
    };
}
inline float snapPerceptualInput(float v) {
    if (!std::isfinite(v) || v == 0.0f) return v;
    const uint32_t word = std::bit_cast<uint32_t>(v) & 0xfffffffcu;
    return std::bit_cast<float>(word);
}
inline Vec3 snapPerceptualInput(Vec3 v) {
    return {snapPerceptualInput(v.x),snapPerceptualInput(v.y),snapPerceptualInput(v.z)};
}
inline Vec3 oklabScaledChromaToLinearSrgb(Oklab c, float gain) {
    const Vec3 chroma{0.0f,c.a,c.b};
    const Vec3 d{
        orderedDot(0.0f,0.3963377774f,0.2158037573f,chroma),
        orderedDot(0.0f,-0.1055613458f,-0.0638541728f,chroma),
        orderedDot(0.0f,-0.0894841775f,-1.2914855480f,chroma)};
    const Vec3 d2{d.x*d.x,d.y*d.y,d.z*d.z};
    const Vec3 d3{d2.x*d.x,d2.y*d.y,d2.z*d.z};
    const float L2=c.L*c.L,L3=L2*c.L;
    auto channel=[&](float x,float y,float z) {
        const float c1=3.0f*L2*orderedDot(x,y,z,d);
        const float c2=3.0f*c.L*orderedDot(x,y,z,d2);
        const float c3=orderedDot(x,y,z,d3);
        return L3+gain*(c1+gain*(c2+gain*c3));
    };
    return {channel(4.0767416621f,-3.3077115913f,0.2309699292f),
            channel(-1.2684380046f,2.6097574011f,-0.3413193965f),
            channel(-0.0041960863f,-0.7034186147f,1.7076147010f)};
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
inline Vec3 applyPerceptualColor(Vec3 linearSrgb, const ProcessingPlan &plan) {
    const auto &state = plan.state;
    Oklab lab = linearSrgbToOklab(linearSrgb);
    const float labLimit = plan.data[ProcessingPlan::LookOptions].w;
    bool noBands = true;
    for (int i=0; i<8; ++i) {
        const auto band = plan.data[ProcessingPlan::Bands+i];
        noBands &= band.x==0 && band.y==0 && band.z==0;
    }
    if (state.hue==0 && noBands) {
        lab = linearSrgbToOklab(snapPerceptualInput(linearSrgb));
        const float c = chromaLength(lab.a,lab.b);
        const float gain = std::max(0.0f,1.0f+float(state.saturation/100))
            * std::max(0.0f,1.0f+float(state.vibrance/100)*(1.0f-clamp01(c/.30f))*.85f);
        lab.L = std::clamp(lab.L,0.0f,labLimit);
        return oklabScaledChromaToLinearSrgb(lab,gain);
    }
    float chroma = chromaLength(lab.a,lab.b);
    float hue = chroma > 1.0e-6f ? wrapHue(std::atan2(lab.b, lab.a) * 180.0f / kPi) : 0.0f;
    hue = wrapHue(hue + static_cast<float>(state.hue));
    float chromaScale = std::max(0.0f, 1.0f + static_cast<float>(state.saturation / 100.0));
    const float chromaNorm = clamp01(chroma / 0.30f);
    const float vibrance = static_cast<float>(state.vibrance / 100.0);
    chromaScale *= std::max(0.0f, 1.0f + vibrance * (1.0f - chromaNorm) * 0.85f);
    float hueDelta = 0.0f;
    float satDelta = 0.0f;
    float lumDelta = 0.0f;
    for (int i = 0; i < AdjustmentState::ColorBandCount; ++i) {
        const auto &band = plan.data[ProcessingPlan::Bands + i];
        if (band.x == 0.0f && band.y == 0.0f && band.z == 0.0f) continue;
        const float w = hueBandWeight(hue, band.w);
        hueDelta += band.x * w;
        satDelta += band.y * w;
        lumDelta += band.z * w;
    }
    hue = wrapHue(hue + hueDelta);
    chroma *= chromaScale * std::max(0.0f, 1.0f + satDelta);
    lab.L = std::clamp(lab.L + lumDelta, 0.0f, labLimit);
    lab.a = chroma * std::cos(hue * kPi / 180.0f);
    lab.b = chroma * std::sin(hue * kPi / 180.0f);
    return oklabToLinearSrgb(lab);
}
inline Vec3 compressNegativeGamut(Vec3 rgb, const Float4 &lum) {
    const float y = std::max(0.0f, orderedDot(lum.x,lum.y,lum.z,rgb));
    const float minChannel = std::min({rgb.x, rgb.y, rgb.z});
    if (minChannel < 0.0f && y > 1.0e-6f) {
        const float inset = 1.0f - 0.005f * smooth(-minChannel / (0.005f * std::max(1.0f,y)));
        const float denominator = y - minChannel;
        rgb.x = y * (1.0f + ((rgb.x - y) / denominator) * inset);
        rgb.y = y * (1.0f + ((rgb.y - y) / denominator) * inset);
        rgb.z = y * (1.0f + ((rgb.z - y) / denominator) * inset);
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
inline Vec3 applyRawNeutralTone(Vec3 proPhoto) {
    // Preserve chromaticity and map the scene-linear neutral anchor to display
    // middle gray with a rational shoulder. Unlike alpha.10's fixed x5.6569,
    // the curve is exactly 1.0 at scene white and remains bounded above it.
    const float y = std::max(0.0f, proPhotoToXyzD50(proPhoto).y);
    if (y <= 1.0e-6f) return proPhoto;
    constexpr float sg = ProcessingPlan::RawNeutralSceneGray;
    constexpr float dg = ProcessingPlan::RawNeutralDisplayGray;
    constexpr float g = dg * (1.0f - sg) / (sg * (1.0f - dg));
    const float mapped = (g * y) / (1.0f + (g - 1.0f) * y);
    return scale(proPhoto, mapped / y);
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
inline Vec3 applyMasterCurve(Vec3 rgb, const AdjustmentState::CurveArray &curve, const Float4 &lum) {
    const float y = std::max(0.0f, orderedDot(lum.x,lum.y,lum.z,rgb));
    if (y <= 1.0e-6f) return rgb;
    const float mapped = curveSample(curve, y);
    return scale(rgb, mapped / y);
}
inline float middleGrayContrast(float y, float factor) {
    constexpr float pivot = 0.18f;
    if (y <= 0.0f) return 0.0f;
    return pivot * std::pow(y / pivot, factor);
}
Vec3 multiply(const Float4 *rows, Vec3 v) {
    return {orderedDot(rows[0].x,rows[0].y,rows[0].z,v),
            orderedDot(rows[1].x,rows[1].y,rows[1].z,v),
            orderedDot(rows[2].x,rows[2].y,rows[2].z,v)};
}
template<class Function> std::array<Float4, 3> matrixOf(Function transform) {
    const Vec3 a = transform(Vec3{1,0,0}), b = transform(Vec3{0,1,0}), c = transform(Vec3{0,0,1});
    return {Float4{a.x,b.x,c.x,0}, Float4{a.y,b.y,c.y,0}, Float4{a.z,b.z,c.z,0}};
}
const auto InputMatrix = matrixOf(linearSrgbToProPhoto);
const auto WorkingToSrgb = matrixOf(proPhotoToLinearSrgb);
Vec3 toOutput(Vec3 rgb, ColorManagement::OutputSpace space) {
    if (space == ColorManagement::OutputSpace::SRgb) return rgb;
    if (space == ColorManagement::OutputSpace::ProPhotoRgb) return linearSrgbToProPhoto(rgb);
    const Vec3 xyz = linearSrgbToXyzD65(rgb);
    if (space == ColorManagement::OutputSpace::DisplayP3)
        return {2.49349691f*xyz.x - .93138362f*xyz.y - .40271078f*xyz.z,
                -.82948897f*xyz.x + 1.76266406f*xyz.y + .02362469f*xyz.z,
                .03584583f*xyz.x - .07617239f*xyz.y + .95688452f*xyz.z};
    return {2.0413690f*xyz.x - .5649464f*xyz.y - .3446944f*xyz.z,
            -.9692660f*xyz.x + 1.8760108f*xyz.y + .0415560f*xyz.z,
            .0134474f*xyz.x - .1183897f*xyz.y + 1.0154096f*xyz.z};
}
float encodeOutput(float v, ColorManagement::OutputSpace space) {
    v = clamp01(v);
    if (space == ColorManagement::OutputSpace::AdobeRgb) return std::pow(v, 1.0f / 2.2f);
    if (space == ColorManagement::OutputSpace::ProPhotoRgb) return std::pow(v, 1.0f / 1.8f);
    return linearToSrgb(v);
}
bool identity(const AdjustmentState::CurveArray &curve) {
    return curve == AdjustmentState::CurveArray{0, .25, .5, .75, 1};
}
}

ProcessingPlan ProcessingPlan::compile(const AdjustmentState &original, ImagePipeline::InputEncoding encoding,
                                       ColorManagement::OutputSpace output) {
    // Backwards-compatible interpretation for low-level tests/tools. The app
    // uses the explicit overload because linear ProPhoto is a representation,
    // not proof that the source was RAW.
    return compile(original, encoding, output,
                   encoding == ImagePipeline::InputEncoding::LinearProPhoto, 0.0f);
}

ProcessingPlan ProcessingPlan::compile(const AdjustmentState &original, ImagePipeline::InputEncoding encoding,
                                       ColorManagement::OutputSpace output, bool rawSource,
                                       float baseExposureStops) {
    const AdjustmentState state=LookProfiles::effective(original);
    ProcessingPlan plan;
    plan.rawSource = rawSource;
    plan.baseExposureStops = rawSource ? std::clamp(baseExposureStops, -8.0f, 8.0f) : 0.0f;
    const auto style=LookProfiles::style(original.look),detail=LookProfiles::detail(original.look);
    plan.data[LookStyle]={style[0],style[1],style[2],style[3]};
    plan.data[LookDetail]={detail[0],detail[1],detail[2],detail[3]};
    const bool lut=style[3]>0 && bool(original.look.lut);
    // Neutral v2 keeps values near display range before Oklab, so the old
    // alpha.10 cbrt(+2.5 EV) expansion is no longer needed.
    constexpr float perceptualLabLimit = 1.5f;
    plan.data[LookOptions]={lut?float(original.look.lut->size):0,float(int(output)),float(original.look.strength),perceptualLabLimit};
    const auto targetRows=matrixOf([&](Vec3 v){return toOutput(v,output);});
    for(int i=0;i<3;++i)plan.data[LutOut0+i]=targetRows[i];
    const auto kernelOutput=lut?ColorManagement::OutputSpace::SRgb:output;
    plan.state = state; plan.encoding = encoding; plan.output = output;
    for (int i=0; i<3; ++i) {
        plan.data[Input0+i] = InputMatrix[i];
        plan.data[Working0+i] = WorkingToSrgb[i];
    }
    const float temperature = float(state.temperature / 100.0), tint = float(state.tint / 100.0);
    const float gain = float(std::exp2(std::clamp(state.exposure, -20.0, 20.0)));
    const auto wb = matrixOf([&](Vec3 v) {
        return scale(temperature == 0 && tint == 0 ? v : applyWhiteBalanceDelta(v, temperature, tint), gain);
    });
    for (int i=0; i<3; ++i) plan.data[Wb0+i] = wb[i];
    plan.data[Tonal] = {float(state.highlights/100), float(state.shadows/100), float(state.whites/100), float(state.blacks/100)};
    plan.data[Tone] = {std::clamp(float(1+state.contrast/100), .05f, 2.5f), clamp01(float(state.highlightRecovery/100)), float(state.saturation/100), float(state.vibrance/100)};
    bool color = state.hue != 0 || state.saturation != 0 || state.vibrance != 0;
    constexpr float centers[]{28,58,95,145,200,260,305,340};
    for (int i=0; i<8; ++i) {
        plan.data[Bands+i] = {float(state.hslHue[i]/100*35), float(state.hslSaturation[i]/100), float(state.hslLuminance[i]/100*.18), centers[i]};
        color |= state.hslHue[i] != 0 || state.hslSaturation[i] != 0 || state.hslLuminance[i] != 0;
    }
    plan.data[Color] = {float(state.hue), encoding == ImagePipeline::InputEncoding::SRgb ? 0.0f : 1.0f, color ? 1.0f : 0.0f,
                       (state.highlights != 0 || state.shadows != 0 || state.whites != 0 || state.blacks != 0) ? 1.0f : 0.0f};
    const auto out = matrixOf([&](Vec3 v) { return toOutput(v, kernelOutput); });
    for (int i=0; i<3; ++i) plan.data[Out0+i] = out[i];
    Float4 lum{.2126f,.7152f,.0722f,float(int(kernelOutput))};
    if (kernelOutput == ColorManagement::OutputSpace::DisplayP3) lum = {.2289746f,.6917385f,.0792869f,float(int(output))};
    if (kernelOutput == ColorManagement::OutputSpace::AdobeRgb) lum = {.2973769f,.6273491f,.0752741f,float(int(output))};
    if (kernelOutput == ColorManagement::OutputSpace::ProPhotoRgb) lum = {.2880402f,.7118741f,.0000857f,float(int(output))};
    plan.data[Luminance] = lum;
    // Flags.w: 0 = not RAW, >0 = RAW camera baseline gain. A RAW with a 0 EV
    // camera offset therefore carries 1.0, while non-RAW linear ProPhoto does
    // not accidentally enter the RAW base-rendering path.
    const float cameraBaseGain = rawSource ? std::exp2(plan.baseExposureStops) : 0.0f;
    plan.data[Flags] = {state.contrast == 0 ? 1.0f : 0.0f, identity(state.masterCurve) ? 1.0f : 0.0f,
                       identity(state.redCurve) && identity(state.greenCurve) && identity(state.blueCurve) ? 1.0f : 0.0f,
                       cameraBaseGain};
    for (int i=0; i<5; ++i) plan.data[Curves+i] = {float(state.masterCurve[i]),float(state.redCurve[i]),float(state.greenCurve[i]),float(state.blueCurve[i])};
    return plan;
}
QImage ImagePipeline::process(const QImage &source, const AdjustmentState &state, InputEncoding encoding,
                             ColorManagement::OutputSpace output, const CancelToken &token, bool parallel) {
    return processWithPlan(source, ProcessingPlan::compile(state, encoding, output), token, parallel);
}
QImage ImagePipeline::processWithPlan(const QImage &source, const ProcessingPlan &plan, const CancelToken &token, bool parallel) {
    if (source.isNull() || cancelled(token)) return {};
    PerformanceSpan timing(QStringLiteral("cpu_pipeline"), {{"pixels", qint64(source.width())*source.height()}, {"parallel",parallel}});
    const QImage input = source.format() == QImage::Format_RGBA64 ? source : source.convertToFormat(QImage::Format_RGBA64);
    QImage out(input.size(), QImage::Format_RGBA64);
    if (out.isNull()) return {};
    const uchar *src = input.constBits(); const qsizetype srcStride = input.bytesPerLine();
    uchar *dst = out.bits(); const qsizetype dstStride = out.bytesPerLine();
    const auto tone = plan.data[ProcessingPlan::Tone];
    const auto tonal = plan.data[ProcessingPlan::Tonal];
    const auto color = plan.data[ProcessingPlan::Color];
    const auto flags = plan.data[ProcessingPlan::Flags];
    const auto lum = plan.data[ProcessingPlan::Luminance];
    const auto style=plan.data[ProcessingPlan::LookStyle];
    const auto kernelOutput=ColorManagement::OutputSpace(int(lum.w));
    const auto lut=style.w>0?plan.state.look.lut:nullptr;
    ParallelRows::run(out.height(), out.width(), token, [&](int y) {
        const auto *in = reinterpret_cast<const QRgba64 *>(src+y*srcStride);
        auto *line = reinterpret_cast<QRgba64 *>(dst+y*dstStride);
        for (int x=0; x<out.width(); ++x) {
            const QRgba64 original = in[x];
            Vec3 v{original.red()/65535.0f, original.green()/65535.0f, original.blue()/65535.0f};
            if (plan.encoding == InputEncoding::SRgb) {
                v = {srgbToLinear(v.x),srgbToLinear(v.y),srgbToLinear(v.z)};
                v = multiply(plan.data.data()+ProcessingPlan::Input0,v);
            }
            v = multiply(plan.data.data()+ProcessingPlan::Wb0,v);
            if (flags.w > 0.0f) v = scale(v, flags.w); // camera/model exposure zero-point only
            v = applyHighlightRecovery(v,tone.y);
            if (color.w != 0) {
                const float Y = std::max(0.0f,proPhotoToXyzD50(v).y);
                const float stops = tonal.y*(1-smooth(Y/.32f)) + tonal.x*smooth((Y-.26f)/.82f)
                                  + tonal.z*smooth((Y-.62f)/.70f)*.75f + tonal.w*(1-smooth(Y/.16f))*.75f;
                v = scale(v,std::exp2(stops));
            }
            if (flags.x == 0) {
                const float Y = std::max(0.0f,proPhotoToXyzD50(v).y);
                if (Y > 1.0e-6f) v = scale(v,middleGrayContrast(Y,tone.x)/Y);
            }
            if (flags.w > 0.0f) v = applyRawNeutralTone(v);
            v = multiply(plan.data.data()+ProcessingPlan::Working0,v);
            if (color.z != 0) v = applyPerceptualColor(v,plan);
            else if (std::max({v.x,v.y,v.z}) > 3.3f || std::min({v.x,v.y,v.z}) < 0) {
                auto lab = linearSrgbToOklab(v); lab.L = std::clamp(lab.L,0.0f,plan.data[ProcessingPlan::LookOptions].w); v = oklabToLinearSrgb(lab);
            }
            if(style.x>0) {
                const float y=(.2126f*v.x+.7152f*v.y)+.0722f*v.z;
                v=style.x>=1 ? Vec3{y,y,y} : Vec3{v.x+(y-v.x)*style.x,v.y+(y-v.y)*style.x,v.z+(y-v.z)*style.x};
                if(style.y>0)v={v.x*(1+.09f*style.y),v.y*(1-.01f*style.y),v.z*(1-.22f*style.y)};
            }
            v = multiply(plan.data.data()+ProcessingPlan::Out0,v);
            v = compressNegativeGamut(v,lum);
            v = {displayShoulder(v.x,tone.y),displayShoulder(v.y,tone.y),displayShoulder(v.z,tone.y)};
            if (flags.y == 0) v = applyMasterCurve(v,plan.state.masterCurve,lum);
            if (flags.z == 0) v = {curveSample(plan.state.redCurve,v.x),curveSample(plan.state.greenCurve,v.y),curveSample(plan.state.blueCurve,v.z)};
            if(style.z>0)v={v.x*(1-style.z)+style.z,v.y*(1-style.z)+style.z,v.z*(1-style.z)+style.z};
            v={encodeOutput(v.x,kernelOutput),encodeOutput(v.y,kernelOutput),encodeOutput(v.z,kernelOutput)};
            if(lut) {
                const auto mapped=lut->sample(v.x,v.y,v.z);const float w=plan.data[ProcessingPlan::LookOptions].z;
                v={v.x+(mapped[0]-v.x)*w,v.y+(mapped[1]-v.y)*w,v.z+(mapped[2]-v.z)*w};
                if(plan.output!=ColorManagement::OutputSpace::SRgb) {
                    v={srgbToLinear(v.x),srgbToLinear(v.y),srgbToLinear(v.z)};
                    v=multiply(plan.data.data()+ProcessingPlan::LutOut0,v);
                    v={encodeOutput(v.x,plan.output),encodeOutput(v.y,plan.output),encodeOutput(v.z,plan.output)};
                }
            }
            const auto quantize = [&](float value) { return quint16(std::lround(clamp01(value)*65535.0f)); };
            line[x] = QRgba64::fromRgba64(quantize(v.x),quantize(v.y),quantize(v.z),original.alpha());
        }
    }, parallel);
    if (cancelled(token)) return {};
    const auto d=plan.data[ProcessingPlan::LookDetail];
    out=LookDetail::apply(out,{d.x,d.y,d.z,d.w},token);
    if(out.isNull())return {};
    out.setColorSpace(ColorManagement::colorSpace(plan.output));
    out.setText(QStringLiteral("JixelLightPipeline"), QString::fromLatin1(ProcessingPlan::EngineVersion));
    out.setText(QStringLiteral("JixelLightICCManaged"), QStringLiteral("true"));
    out.setText(QStringLiteral("JixelLightBaseRendering"),
                plan.rawSource
                    ? QStringLiteral("Jixel Neutral v2 / camera baseline %1 EV / luminance tone placement / soft display shoulder")
                          .arg(QString::number(plan.baseExposureStops, 'f', 3))
                    : QStringLiteral("none"));
    return out;
}

QImage ImagePipeline::processRegion(const QImage &source,const ProcessingPlan &plan,const QRect &region,const CancelToken &token) {
    const QRect roi=region.intersected(source.rect());if(roi.isEmpty())return {};
    const auto d=plan.data[ProcessingPlan::LookDetail];const int pad=LookDetail::halo({d.x,d.y,d.z,d.w});
    const QRect expanded=roi.adjusted(-pad,-pad,pad,pad).intersected(source.rect());
    const QImage result=processWithPlan(source.copy(expanded),plan,token);
    if(result.isNull())return {};
    return result.copy(QRect(roi.topLeft()-expanded.topLeft(),roi.size()));
}
