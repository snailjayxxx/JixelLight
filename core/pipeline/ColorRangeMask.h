#pragma once
#include "core/pipeline/PointColor.h"
#include <cmath>
namespace ColorRangeMask {
struct Parameters {
    PointColor::Parameters selection;
    float exposure=0;
    bool inverted=false;
};
inline bool valid(const Parameters &p) {
    return PointColor::valid(p.selection)&&std::isfinite(p.exposure)&&std::abs(p.exposure)<=3;
}
// Uses validated parameters and the source's working Oklab before WB/EV.
// Selection weights share Point Color's circular hue and neutral behavior.
inline float gain(PointColor::Lab input,const Parameters &p) {
    if(!p.selection.enabled||p.exposure==0||!std::isfinite(input.L)||!std::isfinite(input.a)||!std::isfinite(input.b))return 1;
    float weight=PointColor::weight(input,p.selection);
    if(p.inverted)weight=1-weight;
    return weight==0?1:std::exp2(p.exposure*weight);
}
}
