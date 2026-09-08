#pragma once
#include "core/pipeline/ImagePipeline.h"
#include <array>

struct alignas(16) Float4 { float x = 0, y = 0, z = 0, w = 0; };
// Exactly the std140 layout in the three compute shaders. Matrix ROWS.
struct ProcessingPlan {
    static constexpr const char *EngineVersion = "jixellight-linear-v4-base1-look4";
    // LibRaw stays scene-linear with no_auto_bright. Jixel Neutral v1 places
    // normal camera exposure headroom into a display-referred starting range
    // without changing the user's Exposure = 0 reference point.
    static constexpr float RawBaseExposureStops = 2.5f;
    static constexpr float RawBaseGain = 5.656854249492381f;
    enum Slot { Wb0=0, Wb1=1, Wb2=2, Tonal=3, Tone=4, Color=5,
                Out0=6, Out1=7, Out2=8, Luminance=9, Flags=10,
                Bands=11, Curves=19, Dimensions=24,
                Input0=25, Input1=26, Input2=27,
                Working0=28, Working1=29, Working2=30, LookStyle=31, LookDetail=32, LookOptions=33,
                LutOut0=34, LutOut1=35, LutOut2=36, SlotCount=37 };
    std::array<Float4, SlotCount> data{};
    AdjustmentState state;
    ImagePipeline::InputEncoding encoding = ImagePipeline::InputEncoding::SRgb;
    ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb;
    static ProcessingPlan compile(const AdjustmentState &state,
                                  ImagePipeline::InputEncoding encoding,
                                  ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb);
};
static_assert(sizeof(Float4) == 16);
static_assert(sizeof(ProcessingPlan::data) == 592);
