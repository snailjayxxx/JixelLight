#pragma once
// Only the performance-test copy of GeometryCorrections enables this probe.
#ifndef JIXELLIGHT_GEOMETRY_TEST_PROGRESS
#error GeometryProgressProbe is only available in the performance-test target
#endif
#include <functional>
namespace GeometryProgressProbe {
inline thread_local std::function<void(int)> rowCompleted;
inline void notify(int row) { if (rowCompleted) rowCompleted(row); }
}
