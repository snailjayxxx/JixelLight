#pragma once
#include "core/async/LatestJob.h"
#include <QImage>
#include <array>
#include <functional>
namespace SpatialDehaze {
using Triple=std::array<float,3>;
using Pixel=std::array<float,4>;
using Sampler=std::function<Pixel(int,int)>;
struct Airlight {Triple rgb{1,1,1};int samples=0;bool valid=true,available=false;};
// At most64x64 cells and nine associated-alpha bilinear probes per cell.
// Call on the complete corrected frame, before any viewport or preview resize.
Airlight estimateAirlight(QSize size,const Sampler &linearProPhoto,const CancelToken &cancel={});
struct Parameters {float amount=0,darkRadius=7,refineRadius=3;Triple airlight{1,1,1};};
bool valid(Parameters p);
int halo(Parameters p);
// Owned RGBA32F16 + intensity/alpha8 + dark/coarse4 + horizontal4 =32B/pixel.
// Positive removes a bounded atmospheric veil; negative adds a uniform veil.
QImage apply(QImage linear,Parameters p,const CancelToken &cancel={},bool parallel=true);
}
