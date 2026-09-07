#pragma once
#include "core/pipeline/ImagePipeline.h"
#include <array>

struct alignas(16) Float4 { float x = 0, y = 0, z = 0, w = 0; };
// Exactly the std140 layout in shaders/pipeline.comp. Matrix rows, not columns.
struct ProcessingPlan {
    static constexpr const char *EngineVersion = "jixellight-linear-v2-perf1";
    enum Slot { Wb0=0, Wb1=1, Wb2=2, Tonal=3, Tone=4, Color=5,
                Out0=6, Out1=7, Out2=8, Luminance=9, Flags=10,
                Bands=11, Curves=19, Dimensions=24, SlotCount=25 };
    std::array<Float4, SlotCount> data{};
    AdjustmentState state;
    ImagePipeline::InputEncoding encoding = ImagePipeline::InputEncoding::SRgb;
    ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb;
    static ProcessingPlan compile(const AdjustmentState &state,
                                  ImagePipeline::InputEncoding encoding,
                                  ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb);
};
static_assert(sizeof(Float4) == 16);
