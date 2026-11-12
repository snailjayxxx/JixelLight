#pragma once
#include <array>
namespace PointColor {
struct Lab {float L=0,a=0,b=0;};
Lab fromEncodedSrgb(const std::array<float,3> &);
std::array<float,3> toEncodedSrgb(Lab);
struct Parameters {
    bool enabled=false;
    float hueShift=0,saturationShift=0,lightnessShift=0;
    float centerLightness=.6f,centerChroma=.2f,centerHue=0;
    float hueRadius=45,chromaRadius=.12f,lightnessRadius=.4f;
};
bool valid(const Parameters &);
float weight(Lab,const Parameters &);
// Call with validated parameters; no allocation or neighborhood dependency.
Lab apply(Lab,const Parameters &);
}
