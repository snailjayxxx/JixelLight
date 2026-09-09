#pragma once
#include "core/pipeline/ImagePipeline.h"
#include <array>

struct alignas(16) Float4 { float x = 0, y = 0, z = 0, w = 0; };
// Exactly the std140 layout in the three compute shaders. Matrix ROWS.
struct ProcessingPlan {
    static constexpr const char *EngineVersion = "jixellight-linear-v5-base2-look4";
    // Jixel Neutral v2 keeps camera exposure calibration separate from the
    // universal scene-to-display tone placement. These are rendering anchors,
    // not a hidden user Exposure adjustment.
    static constexpr float RawNeutralSceneGray = 0.03f;
    static constexpr float RawNeutralDisplayGray = 0.18f;
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
    bool rawSource = false;
    float baseExposureStops = 0.0f;

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
};
static_assert(sizeof(Float4) == 16);
static_assert(sizeof(ProcessingPlan::data) == 592);
