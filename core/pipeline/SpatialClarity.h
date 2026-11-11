#pragma once

#include "core/async/LatestJob.h"
#include <QImage>
#include <array>

namespace SpatialClarity {
struct Parameters {
    float amount=0; // -1..1: soften or strengthen local midtone contrast.
    float radius=0; // Effective post-geometry pixel radius, at most24.
};
using Triple=std::array<float,3>;
using Transform=Triple (*)(Triple);
bool valid(Parameters parameters);
int halo(Parameters parameters);
// Own/reuse the linear-ProPhoto plane. Only lightness and alpha are buffered;
// the original chroma is read before each independent center is replaced.
// Peak stage storage is32 bytes/pixel: linear16 + lightness4 + horizontal12.
QImage apply(QImage linear,Parameters parameters,Transform toLab,Transform fromLab,
             const CancelToken &cancel={},bool parallel=true);
}
