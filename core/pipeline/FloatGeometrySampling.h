#pragma once
#include <QImage>
#include <QColorSpace>
#include <algorithm>
#include <cmath>

// Helper for explicit scene-linear RGBA32FPx4 geometry. This is deliberately
// separate from the accepted RGBA64 path: no integer conversion, gamut clamp,
// tone mapping or hidden Qt image-format conversion occurs during resampling.
namespace FloatGeometrySampling {
struct Sample {
    double r=0, g=0, b=0, a=0;
};

inline void copyMetadata(const QImage &source,QImage &target) {
    target.setColorSpace(source.colorSpace());
    target.setDevicePixelRatio(source.devicePixelRatio());
    target.setDotsPerMeterX(source.dotsPerMeterX());
    target.setDotsPerMeterY(source.dotsPerMeterY());
    for (const auto &key:source.textKeys()) target.setText(key,source.text(key));
}

// Caller validates the FP32 source and coordinate bounds before invocation.
// Interpolate premultiplied RGB in double precision; return unassociated
// scene-linear RGB and alpha. Fully transparent samples do not inject hidden
// color into an opaque neighbor. Output RGB is not clipped to display white.
inline Sample bilinear(const QImage &source,double x,double y) {
    x=std::clamp(x,0.0,double(source.width()-1));
    y=std::clamp(y,0.0,double(source.height()-1));
    const int ix=int(x), iy=int(y);
    const int jx=std::min(ix+1,source.width()-1),jy=std::min(iy+1,source.height()-1);
    const auto *top=reinterpret_cast<const float *>(source.constScanLine(iy));
    const auto *bottom=reinterpret_cast<const float *>(source.constScanLine(jy));
    const float *pixels[]{top+4*ix,top+4*jx,bottom+4*ix,bottom+4*jx};
    const double dx=x-ix,dy=y-iy;
    const double weights[]{(1-dx)*(1-dy),dx*(1-dy),(1-dx)*dy,dx*dy};
    Sample result;
    for(int i=0;i<4;++i) {
        const double a=weights[i]*double(pixels[i][3]);
        result.a+=a;
        result.r+=a*double(pixels[i][0]);
        result.g+=a*double(pixels[i][1]);
        result.b+=a*double(pixels[i][2]);
    }
    if(result.a>0) {
        result.r/=result.a;result.g/=result.a;result.b/=result.a;
    } else {
        result.r=result.g=result.b=0;
    }
    return result;
}

inline void write(float *pixel,const Sample &sample) {
    pixel[0]=float(sample.r);
    pixel[1]=float(sample.g);
    pixel[2]=float(sample.b);
    pixel[3]=float(sample.a);
}
} // namespace FloatGeometrySampling
