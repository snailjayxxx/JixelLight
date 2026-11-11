#pragma once
// Only the performance-test copy of ImagePipeline enables this observation.
#ifndef JIXELLIGHT_BRUSH_FRAME_TEST_PROGRESS
#error BrushFrameProgressProbe is only available in the performance-test target
#endif
#include <functional>
namespace BrushFrameProgressProbe {
inline thread_local std::function<void(int, int)> tileCompleted;
inline void notify(int tile, int rows) { if (tileCompleted) tileCompleted(tile, rows); }
}
