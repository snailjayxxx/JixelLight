#pragma once
#include "core/async/LatestJob.h"
#include <QImage>
#include <array>
namespace SpatialTexture {
struct Parameters {float amount=0,smallRadius=2,largeRadius=6;};
using Triple=std::array<float,3>;
using Transform=Triple (*)(Triple);
bool valid(Parameters p);
int halo(Parameters p);
// The owned linear image may detach; a cancelled/invalid stage publishes none.
// RGBA32F16 + lightness4 + horizontal12 + small-band4 =36 bytes/pixel.
QImage apply(QImage linear,Parameters p,Transform toLab,Transform fromLab,
             const CancelToken &cancel={},bool parallel=true);
}
