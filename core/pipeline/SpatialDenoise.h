#pragma once
#include "core/async/LatestJob.h"
#include <QImage>
#include <array>

namespace SpatialDenoise {
struct Parameters {
    float lightness=0, chroma=0, radius=0;
};
using Triple=std::array<float,3>;
using InverseGuide=Triple (*)(Triple);
bool valid(const Parameters &parameters);
int halo(const Parameters &parameters);
// Internal CPU stage: guide RGB stores perceptual L/a/b, not display RGB.
// Both planes are FP32, with identical original alpha. Neighbor reads use
// only the immutable guide, allowing the owned linear plane to be reused.
QImage apply(QImage linear,const QImage &guide,const Parameters &parameters,
             InverseGuide inverse,const CancelToken &cancel={},bool parallel=true);
}
