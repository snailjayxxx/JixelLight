#pragma once
#include "core/pipeline/ImagePipeline.h"
#include <array>
#include <algorithm>
#include <QRectF>
#include <memory>

struct alignas(16) Float4 { float x = 0, y = 0, z = 0, w = 0; };
// Exactly the std140 layout in the compute shaders. Matrix ROWS.
struct ProcessingPlan {
    static constexpr const char *EngineVersion = "jixellight-linear-v5-base2-look4";
    // Jixel Neutral v2 keeps camera exposure calibration separate from the
    // universal scene-to-display tone placement. These are rendering anchors,
    // not a hidden user Exposure adjustment.
    static constexpr float RawNeutralSceneGray = 0.03f;
    static constexpr float RawNeutralDisplayGray = 0.18f;
    // Source-compatibility only: alpha.11 no longer applies a fixed RAW gain.
    static constexpr float RawBaseExposureStops = 0.0f;
    static constexpr float RawBaseGain = 1.0f;
    enum Slot { Wb0=0, Wb1=1, Wb2=2, Tonal=3, Tone=4, Color=5,
                Out0=6, Out1=7, Out2=8, Luminance=9, Flags=10,
                Bands=11, Curves=19, Dimensions=24,
                Input0=25, Input1=26, Input2=27,
                Working0=28, Working1=29, Working2=30, LookStyle=31, LookDetail=32, LookOptions=33,
                LutOut0=34, LutOut1=35, LutOut2=36, Vignette=37, FrameRect=38, PixelMap=39,
                BlackWhite=40, BlackWhiteMix0=41, BlackWhiteMix1=42,
                GradingOptions=43, GradingShadows=44, GradingMidtones=45,
                GradingHighlights=46, LinearMask=47, LinearMaskDirection=48,
                RadialMask=49, RadialMaskShape=50, Grain=51, IndependentDetail=52, SlotCount=53 };
    std::array<Float4, SlotCount> data{};
    AdjustmentState state;
    ImagePipeline::InputEncoding encoding = ImagePipeline::InputEncoding::SRgb;
    ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb;
    bool rawSource = false;
    float baseExposureStops = 0.0f;

    void setFrameRect(const QRectF &rect) {
        // A research-only brush must use the same normalized viewport ROI as
        // legacy spatial edits, even if no linear/radial/vignette is active.
        const bool brush=std::any_of(state.brushMasks.layers.begin(),state.brushMasks.layers.end(),
            [](const auto &layer){return layer.enabled;});
        if (data[Vignette].x!=0 || data[LinearMask].x!=0 || data[RadialMask].x!=0 || data[Grain].x!=0 || data[IndependentDetail].x!=0 || brush)
            data[FrameRect]={float(rect.x()),float(rect.y()),float(rect.width()),float(rect.height())};
    }
    Float4 pixelMap(const QSize &size) const {
        const auto rect=data[FrameRect];
        // Centered ellipse coordinates keep opposite full-image pixels exact
        // mirrors; both CPU tiles and GPU use this host-computed mapping.
        return {2*rect.x+rect.z-1,2*rect.y+rect.w-1,2*rect.z/std::max(1,size.width()),2*rect.w/std::max(1,size.height())};
    }
    void setReferenceFrameSize(const QSize &size) {
        if (data[IndependentDetail].x!=0 && size.width()>0 && size.height()>0) {
            data[IndependentDetail].z=float(size.width());data[IndependentDetail].w=float(size.height());
        }
    }
    bool validIndependentDetail() const {
        const auto d=data[IndependentDetail];
        if(!std::isfinite(d.x)||!std::isfinite(d.y)||d.x<0||d.x>1.5f||d.y<0||d.y>6)return false;
        if(d.z==0&&d.w==0)return true;
        return d.x>0&&std::isfinite(d.z)&&std::isfinite(d.w)&&d.z>=1&&d.w>=1
            &&d.z<=33554432&&d.w<=33554432&&d.z==std::floor(d.z)&&d.w==std::floor(d.w);
    }
    Float4 independentDetail(const QSize &size) const {
        auto result=data[IndependentDetail];const auto frame=data[FrameRect];
        if (result.x==0||!validIndependentDetail()) return {};
        float scale=1;
        if(result.z>0 && result.w>0 && frame.z>0 && frame.w>0)
            scale=std::min(float(size.width())/(result.z*frame.z),float(size.height())/(result.w*frame.w));
        result.y=std::clamp(result.y*scale,0.f,12.f);result.z=0;result.w=0;return result;
    }

    // Compatibility entry point for tests/tools that historically used
    // LinearProPhoto to mean RAW. Application code should use the explicit
    // overload below so pixel encoding and source semantics are independent.
    static ProcessingPlan compile(const AdjustmentState &state,
                                  ImagePipeline::InputEncoding encoding,
                                  ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb);
    static ProcessingPlan compile(const AdjustmentState &state,
                                  ImagePipeline::InputEncoding encoding,
                                  ColorManagement::OutputSpace output,
                                  bool rawSource,
                                  float baseExposureStops);
private:
    // state above is LookProfiles::effective(intent), so recompiling it would
    // apply the Look twice. Only explicit brush admission may use this snapshot.
    std::shared_ptr<const AdjustmentState> m_brushIntent;
    friend class BrushRenderRequest;
};
static_assert(sizeof(Float4) == 16);
static_assert(sizeof(ProcessingPlan::data) == 848);
