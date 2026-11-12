#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace U16Quantize {
// Same U16 value as lround of the original clamped FLOAT product. A double
// half-unit addition cannot move a just-below-half float over the boundary.
inline std::uint16_t bounded(float value) {
    const float scaled=std::clamp(value,0.f,1.f)*65535.f;
    if(!std::isfinite(scaled))return std::uint16_t(std::lround(scaled));
    return std::uint16_t(static_cast<unsigned>(static_cast<double>(scaled)+.5));
}
}
