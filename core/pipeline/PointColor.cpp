#include "core/pipeline/PointColor.h"
#include <algorithm>
#include <cmath>
namespace {
constexpr float Pi=3.14159265358979323846f;
float smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
float range(float distance,float radius) {
    if(distance<=radius*.5f)return 1;
    if(distance>=radius)return 0;
    return 1-smooth((distance-radius*.5f)/(radius*.5f));
}
bool finite(PointColor::Lab v){return std::isfinite(v.L)&&std::isfinite(v.a)&&std::isfinite(v.b);}
}
bool PointColor::valid(const Parameters &p) {
    return std::isfinite(p.hueShift)&&std::abs(p.hueShift)<=180
        &&std::isfinite(p.saturationShift)&&std::abs(p.saturationShift)<=1
        &&std::isfinite(p.lightnessShift)&&std::abs(p.lightnessShift)<=.3f
        &&std::isfinite(p.centerLightness)&&std::abs(p.centerLightness)<=4
        &&std::isfinite(p.centerChroma)&&p.centerChroma>=0&&p.centerChroma<=8
        &&std::isfinite(p.centerHue)&&p.centerHue>=0&&p.centerHue<=360
        &&std::isfinite(p.hueRadius)&&p.hueRadius>=1&&p.hueRadius<=180
        &&std::isfinite(p.chromaRadius)&&p.chromaRadius>=.001f&&p.chromaRadius<=1
        &&std::isfinite(p.lightnessRadius)&&p.lightnessRadius>=.001f&&p.lightnessRadius<=1;
}
float PointColor::weight(Lab v,const Parameters &p) {
    if(!p.enabled||!finite(v))return 0;
    const float chroma=std::hypot(v.a,v.b);
    const float wL=range(std::abs(v.L-p.centerLightness),p.lightnessRadius);
    const float wC=range(std::abs(chroma-p.centerChroma),p.chromaRadius);
    if(wL==0||wC==0)return 0;
    float wH=1;
    const float confidence=smooth((chroma-.005f)/.02f)*smooth((p.centerChroma-.005f)/.02f);
    if(p.hueRadius<180&&confidence>0) {
        const float hue=std::atan2(v.b,v.a)*(180.f/Pi);
        const float distance=std::abs(std::remainder(hue-p.centerHue,360.f));
        wH=1+(range(distance,p.hueRadius)-1)*confidence;
    }
    return wL*wC*wH;
}
PointColor::Lab PointColor::apply(Lab v,const Parameters &p) {
    if(!p.enabled||(p.hueShift==0&&p.saturationShift==0&&p.lightnessShift==0))return v;
    const float w=weight(v,p);if(w==0)return v;
    const float saturation=1+p.saturationShift*w;
    Lab result{v.L+p.lightnessShift*w,v.a*saturation,v.b*saturation};
    if(p.hueShift!=0) {
        const float angle=p.hueShift*(Pi/180.f)*w,c=std::cos(angle),s=std::sin(angle);
        result.a=(v.a*c-v.b*s)*saturation;
        result.b=(v.a*s+v.b*c)*saturation;
    }
    return result;
}

PointColor::Lab PointColor::fromEncodedSrgb(const std::array<float,3> &rgb) {
    const auto decode=[](float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);};
    const float r=decode(rgb[0]),g=decode(rgb[1]),b=decode(rgb[2]);
    const float l=std::cbrt((.4122214708f*r+.5363325363f*g)+.0514459929f*b);
    const float m=std::cbrt((.2119034982f*r+.6806995451f*g)+.1073969566f*b);
    const float s=std::cbrt((.0883024619f*r+.2817188376f*g)+.6299787005f*b);
    return {(.2104542553f*l+.7936177850f*m)-.0040720468f*s,
        (1.9779984951f*l-2.4285922050f*m)+.4505937099f*s,
        (.0259040371f*l+.7827717662f*m)-.8086757660f*s};
}
std::array<float,3> PointColor::toEncodedSrgb(Lab c) {
    float l=(c.L+.3963377774f*c.a)+.2158037573f*c.b;
    float m=(c.L-.1055613458f*c.a)-.0638541728f*c.b;
    float s=(c.L-.0894841775f*c.a)-1.2914855480f*c.b;
    l=l*l*l;m=m*m*m;s=s*s*s;
    const auto encode=[](float x){return std::clamp(x<=.0031308f?12.92f*x:1.055f*std::pow(x,1.f/2.4f)-.055f,0.f,1.f);};
    return {encode((4.0767416621f*l-3.3077115913f*m)+.2309699292f*s),
        encode((-1.2684380046f*l+2.6097574011f*m)-.3413193965f*s),
        encode((-.0041960863f*l-.7034186147f*m)+1.7076147010f*s)};
}
