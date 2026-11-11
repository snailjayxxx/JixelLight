#include "SpatialDenoise.h"
#include "core/async/ParallelRows.h"
#include <algorithm>
#include <atomic>
#include <cmath>

bool SpatialDenoise::valid(const Parameters &p) {
    return std::isfinite(p.lightness)&&std::isfinite(p.chroma)&&std::isfinite(p.radius)
        &&p.lightness>=0&&p.lightness<=1&&p.chroma>=0&&p.chroma<=1&&p.radius>=0&&p.radius<=2;
}
int SpatialDenoise::halo(const Parameters &p) {
    return valid(p)&&(p.lightness>0||p.chroma>0)?int(std::ceil(p.radius)):0;
}
QImage SpatialDenoise::apply(QImage linear,const QImage &guide,const Parameters &p,
                            InverseGuide inverse,const CancelToken &cancel,bool parallel) {
    if(linear.isNull()||cancelled(cancel)||!valid(p))return {};
    if(halo(p)==0)return linear;
    if(!inverse||linear.format()!=QImage::Format_RGBA32FPx4
        ||guide.format()!=QImage::Format_RGBA32FPx4||guide.size()!=linear.size())return {};
    const int width=linear.width(),height=linear.height();std::atomic_bool invalid=false;
    ParallelRows::run(height,width,cancel,[&](int y) {
        const auto *a=reinterpret_cast<const float *>(linear.constScanLine(y));
        const auto *b=reinterpret_cast<const float *>(guide.constScanLine(y));
        for(int x=0;x<width;++x) {
            for(int c=0;c<4;++c)if(!std::isfinite(a[4*x+c])||!std::isfinite(b[4*x+c]))invalid=true;
            if(a[4*x+3]<0||a[4*x+3]>1||a[4*x+3]!=b[4*x+3])invalid=true;
        }
    },parallel);
    if(invalid||cancelled(cancel))return {};
    struct Tap {int x,y;float weight;};std::array<Tap,24> taps{};int count=0;
    const int extent=halo(p);const float sigma=std::max(.01f,.6f*p.radius);
    for(int y=-extent;y<=extent;++y)for(int x=-extent;x<=extent;++x) {
        if(x==0&&y==0)continue;
        const float edgeX=std::clamp(p.radius+1-std::abs(float(x)),0.f,1.f);
        const float edgeY=std::clamp(p.radius+1-std::abs(float(y)),0.f,1.f);
        const float weight=edgeX*edgeY*std::exp(-float(x*x+y*y)/(2*sigma*sigma));
        if(weight>0)taps[std::size_t(count++)]={x,y,weight};
    }
    // Detach before writing; shallow caller aliases and the guide stay intact.
    auto *bits=linear.bits();if(!bits)return {};const auto stride=linear.bytesPerLine();
    constexpr float invLightnessVariance=1.f/(.04f*.04f),invChromaVariance=1.f/(.06f*.06f);
    ParallelRows::run(height,width,cancel,[&](int y) {
        const auto *centerRow=reinterpret_cast<const float *>(guide.constScanLine(y));
        auto *out=reinterpret_cast<float *>(bits+y*stride);
        for(int x=0;x<width;++x) {
            const auto *center=centerRow+4*x;if(center[3]<=0)continue;
            float sum=center[3],dL=0,da=0,db=0;
            for(int i=0;i<count;++i) {
                const auto &tap=taps[std::size_t(i)];
                const auto *neighbor=reinterpret_cast<const float *>(guide.constScanLine(std::clamp(y+tap.y,0,height-1)))+4*std::clamp(x+tap.x,0,width-1);
                if(neighbor[3]<=0)continue;
                const float L=neighbor[0]-center[0],a=neighbor[1]-center[1],b=neighbor[2]-center[2];
                const float distance=L*L*invLightnessVariance+(a*a+b*b)*invChromaVariance;
                const float weight=tap.weight*neighbor[3]*std::exp(-.5f*distance);
                sum+=weight;dL+=weight*L;da+=weight*a;db+=weight*b;
            }
            if(sum<=1.e-20f)continue;
            dL=p.lightness*(dL/sum);da=p.chroma*(da/sum);db=p.chroma*(db/sum);
            // Accumulating differences preserves truly flat fields exactly,
            // and avoids a needless color round trip when nothing changes.
            if(dL==0&&da==0&&db==0)continue;
            const auto rgb=inverse({center[0]+dL,center[1]+da,center[2]+db});
            if(!std::isfinite(rgb[0])||!std::isfinite(rgb[1])||!std::isfinite(rgb[2])){invalid=true;continue;}
            out[4*x]=rgb[0];out[4*x+1]=rgb[1];out[4*x+2]=rgb[2];
        }
    },parallel);
    return invalid||cancelled(cancel)?QImage{}:linear;
}
