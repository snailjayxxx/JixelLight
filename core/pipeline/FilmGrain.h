#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Fixed-seed value noise, shared arithmetically with both color shaders.
// Coordinates describe the entire post-crop frame, never a strip or viewport.
// Unsigned overflow is intentional; no RNG state, frame time or monitor input.
namespace FilmGrain {
inline float lattice(int x,int y) {
    std::uint32_t h=std::uint32_t(x)*0x9e3779b9u ^ std::uint32_t(y)*0x85ebca6bu ^ 0x68bc21ebu;
    h=(h^(h>>16))*0x7feb352du;
    h=(h^(h>>15))*0x846ca68bu;
    h^=h>>16;
    return float(h>>16)*(1.0f/32768.0f)-1.0f;
}
inline float sample(float x,float y) {
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    float fx=x-float(ix),fy=y-float(iy);
    fx=fx*fx*(3.0f-2.0f*fx); fy=fy*fy*(3.0f-2.0f*fy);
    const float a=lattice(ix,iy),b=lattice(ix+1,iy),c=lattice(ix,iy+1),d=lattice(ix+1,iy+1);
    const float top=a+(b-a)*fx,bottom=c+(d-c)*fx;
    return top+(bottom-top)*fy;
}
inline float attenuation(float footprint) {
    // Fade frequencies above the preview pixel grid instead of aliasing them
    // into a new random pattern. At >=2 lattice cells/pixel grain disappears.
    return std::clamp(2.0f-footprint,0.0f,1.0f);
}
}
