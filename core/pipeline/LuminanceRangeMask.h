#pragma once
#include <algorithm>
#include <cmath>

namespace LuminanceRangeMask {
struct Parameters {
    bool enabled=false,inverted=false;
    float exposure=0,lower=0,upper=1,feather=.1f;
};
inline bool valid(const Parameters &p) {
    return std::isfinite(p.exposure)&&std::abs(p.exposure)<=3
        &&std::isfinite(p.lower)&&p.lower>=0&&p.lower<=2
        &&std::isfinite(p.upper)&&p.upper>=p.lower&&p.upper<=2
        &&std::isfinite(p.feather)&&p.feather>=.001f&&p.feather<=1;
}
// Call with validated parameters. Selection uses post-spatial, scene-linear ProPhoto Y before white balance
// and every local/global exposure. Negative Y selects as black; RGB is not clipped.
inline float weight(float luminance,const Parameters &p) {
    if(!p.enabled||!std::isfinite(luminance))return 0;
    const float y=std::max(0.f,luminance);
    const auto smooth=[](float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);};
    const float rising=smooth(1+(y-p.lower)/p.feather);
    const float falling=smooth(1+(p.upper-y)/p.feather);
    const float selected=rising*falling;
    return p.inverted?1-selected:selected;
}
inline float gain(float luminance,const Parameters &p) {
    if(!p.enabled||p.exposure==0)return 1;
    const float selected=weight(luminance,p);
    return selected==0?1:std::exp2(p.exposure*selected);
}
}
