#pragma once
#include "core/async/ParallelRows.h"
#include "core/pipeline/FloatGeometrySampling.h"
#include "core/pipeline/ImagePipeline.h"
#include <QImage>
#include <algorithm>
#include <cmath>
#include <vector>

// Separately filtered, scene-linear FP32 preview resizing. Integer RAW/JPEG
// images continue to use the existing Qt smoother: do not silently change the
// accepted pixel baseline when adding support for future float camera sources.
namespace FloatSceneResampler {
struct Tap { int index; double weight; };
using Axis = std::vector<std::vector<Tap>>;
struct Associated { double r=0,g=0,b=0,a=0; };

// Area integration when decimating, aligned bilinear when enlarging. Both
// branches have nonnegative, unit-sum weights and preserve signed / >1 RGB.
inline Axis makeAxis(int sourceCount,int targetCount) {
    Axis axis(static_cast<std::size_t>(targetCount));
    const double ratio=double(sourceCount)/targetCount;
    for(int dst=0;dst<targetCount;++dst) {
        auto &taps=axis[std::size_t(dst)];
        if(ratio>1.0) {
            const double start=dst*ratio,end=(dst+1)*ratio;
            const int first=std::max(0,int(std::floor(start)));
            const int last=std::min(sourceCount,int(std::ceil(end)));
            for(int src=first;src<last;++src) {
                const double overlap=std::min(end,double(src+1))-std::max(start,double(src));
                if(overlap>0) taps.push_back({src,overlap/ratio});
            }
        } else {
            const double position=(dst+.5)*ratio-.5;
            const int lo=int(std::floor(position));
            const double fraction=position-lo;
            const int a=std::clamp(lo,0,sourceCount-1);
            const int b=std::clamp(lo+1,0,sourceCount-1);
            if(a==b) taps.push_back({a,1.0});
            else {
                taps.push_back({a,1.0-fraction});
                taps.push_back({b,fraction});
            }
        }
    }
    return axis;
}

// A separable associated-alpha filter is O(source pixels + destination pixels
// times source width), instead of O(scaleX*scaleY*destination pixels).
// Each worker owns one source-width scanline scratch buffer. No full-frame
// premultiplied image or additional floating-point working plane is allocated.
inline QImage scale(const QImage &source,QSize size,const CancelToken &cancel={}) {
    if(source.isNull() || source.format()!=QImage::Format_RGBA32FPx4
       || size.isEmpty() || cancelled(cancel)) return {};
    if(!ImagePipeline::validFloatSource(source,cancel)) return {};
    if(source.size()==size) return source; // preserve even hidden RGB bits
    const auto horizontal=makeAxis(source.width(),size.width());
    const auto vertical=makeAxis(source.height(),size.height());
    QImage result(size,QImage::Format_RGBA32FPx4);
    if(result.isNull()) return {};
    FloatGeometrySampling::copyMetadata(source,result);
    auto *bits=result.bits();
    const qsizetype stride=result.bytesPerLine();
    ParallelRows::run(result.height(),source.width(),cancel,[&](int y) {
        std::vector<Associated> row(static_cast<std::size_t>(source.width()));
        for(const auto &tap:vertical[std::size_t(y)]) {
            const auto *input=reinterpret_cast<const float *>(source.constScanLine(tap.index));
            for(int x=0;x<source.width();++x) {
                const auto *p=input+4*x;
                const double a=double(p[3])*tap.weight;
                auto &v=row[std::size_t(x)];
                v.a+=a;
                v.r+=a*double(p[0]);
                v.g+=a*double(p[1]);
                v.b+=a*double(p[2]);
            }
        }
        auto *out=reinterpret_cast<float *>(bits+qsizetype(y)*stride);
        for(int x=0;x<result.width();++x) {
            if((x&255)==0 && cancelled(cancel)) return;
            Associated sum;
            for(const auto &tap:horizontal[std::size_t(x)]) {
                const auto &v=row[std::size_t(tap.index)];
                sum.a+=tap.weight*v.a;
                sum.r+=tap.weight*v.r;
                sum.g+=tap.weight*v.g;
                sum.b+=tap.weight*v.b;
            }
            auto *pixel=out+4*x;
            // The input alpha is bounded [0,1]. Roundoff may straddle one
            // when integrating many taps, so cap only alpha, never scene RGB.
            pixel[3]=float(std::clamp(sum.a,0.0,1.0));
            if(sum.a>0) {
                pixel[0]=float(sum.r/sum.a);
                pixel[1]=float(sum.g/sum.a);
                pixel[2]=float(sum.b/sum.a);
            } else pixel[0]=pixel[1]=pixel[2]=0;
        }
    });
    return cancelled(cancel)?QImage{}:result;
}
} // namespace FloatSceneResampler
