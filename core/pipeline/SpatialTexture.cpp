#include "core/pipeline/SpatialTexture.h"
#include "core/async/ParallelRows.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <new>
#include <vector>

bool SpatialTexture::valid(Parameters p) {
    return std::isfinite(p.amount)&&std::isfinite(p.smallRadius)&&std::isfinite(p.largeRadius)
        &&p.amount>=-1&&p.amount<=1&&p.smallRadius>=0&&p.smallRadius<=2
        &&p.largeRadius>=p.smallRadius&&p.largeRadius<=6;
}
int SpatialTexture::halo(Parameters p) {return valid(p)&&p.amount!=0?int(std::ceil(p.largeRadius)):0;}
std::array<float,13> SpatialTexture::kernelWeights(float radius) {
    std::array<float,13> weights{};if(!std::isfinite(radius)||radius<0||radius>6)return weights;
    const int extent=int(std::ceil(radius));const float sigma=std::max(.01f,.5f*radius);
    for(int i=-extent;i<=extent;++i)
        weights[size_t(i+extent)]=std::clamp(radius+1-std::abs(float(i)),0.f,1.f)*std::exp(-float(i*i)/(2*sigma*sigma));
    return weights;
}
QImage SpatialTexture::apply(QImage linear,Parameters p,Transform toLab,Transform fromLab,
                            const CancelToken &cancel,bool parallel) {
    if(linear.isNull()||linear.format()!=QImage::Format_RGBA32FPx4||!valid(p)
        ||!toLab||!fromLab||cancelled(cancel))return {};
    if(p.amount==0||p.smallRadius==p.largeRadius)return linear;
    const int width=linear.width(),height=linear.height();const size_t pixels=size_t(width)*height;
    using Horizontal=std::array<float,3>;
    std::vector<float> guide,smallBand;std::vector<Horizontal> horizontal;
    if(pixels>guide.max_size()||pixels>horizontal.max_size())return {};
    try {guide.resize(pixels);smallBand.resize(pixels);horizontal.resize(pixels);}
    catch(const std::bad_alloc &){return {};}
    std::atomic_bool invalid{false};
    const auto *source=linear.constBits();const auto stride=linear.bytesPerLine();
    ParallelRows::run(height,width,cancel,[&](int y) {
        const auto *row=reinterpret_cast<const float *>(source+y*stride);
        for(int x=0;x<width;++x) {
            const auto *pixel=row+4*x;
            if(!std::isfinite(pixel[0])||!std::isfinite(pixel[1])||!std::isfinite(pixel[2])
                ||!std::isfinite(pixel[3])||pixel[3]<0||pixel[3]>1){invalid=true;return;}
            if(pixel[3]<=1.e-20f){guide[size_t(y)*width+x]=0;continue;}
            const auto lab=toLab({pixel[0],pixel[1],pixel[2]});
            if(!std::isfinite(lab[0])||!std::isfinite(lab[1])||!std::isfinite(lab[2])){invalid=true;return;}
            guide[size_t(y)*width+x]=lab[0];
        }
    },parallel);
    if(invalid||cancelled(cancel))return {};
    // Each scale uses associated-alpha relative sums. Transparent samples never
    // supply color, and a constant visible field has an exact zero difference.
    const auto filter=[&](float radius,bool final)->bool {
        const int extent=int(std::ceil(radius));const auto weights=kernelWeights(radius);
        ParallelRows::run(height,width,cancel,[&](int y) {
            const auto *row=reinterpret_cast<const float *>(source+y*stride);
            for(int x=0;x<width;++x) {
                float value=0,alpha=0,reference=guide[size_t(y)*width+x];
                if(row[4*x+3]<=1.e-20f)for(int i=-extent;i<=extent;++i) {
                    const int xx=std::clamp(x+i,0,width-1);
                    if(row[4*xx+3]>1.e-20f){reference=guide[size_t(y)*width+xx];break;}
                }
                for(int i=-extent;i<=extent;++i) {
                    const int xx=std::clamp(x+i,0,width-1);const float a=row[4*xx+3];if(a<=1.e-20f)continue;
                    const float weight=weights[size_t(i+extent)];
                    value+=weight*(guide[size_t(y)*width+xx]-reference)*a;alpha+=weight*a;
                }
                horizontal[size_t(y)*width+x]={value,alpha,reference};
            }
        },parallel);
        if(cancelled(cancel))return false;
        // Detach only after the final scale's horizontal pass has finished.
        // No later filter reads source pixels after these owned writes begin.
        auto *output=final?linear.bits():nullptr;if(final&&!output)return false;
        ParallelRows::run(height,width,cancel,[&](int y) {
            const auto *inputRow=final?nullptr:reinterpret_cast<const float *>(source+y*stride);
            auto *out=final?reinterpret_cast<float *>(output+y*stride):nullptr;
            for(int x=0;x<width;++x) {
                const size_t index=size_t(y)*width+x;
                const float a=final?out[4*x+3]:inputRow[4*x+3];if(a<=1.e-20f)continue;
                const float lightness=guide[index];float value=0,alpha=0;
                for(int i=-extent;i<=extent;++i) {
                    const auto sample=horizontal[size_t(std::clamp(y+i,0,height-1))*width+x];
                    const float weight=weights[size_t(i+extent)];
                    value+=weight*(sample[0]+(sample[2]-lightness)*sample[1]);alpha+=weight*sample[1];
                }
                const float relative=alpha>1.e-20f?value/alpha:0;
                if(!final){smallBand[index]=relative;continue;}
                const float midtone=std::clamp(lightness,0.f,1.f);
                const float delta=.8f*p.amount*(smallBand[index]-relative)*(4*midtone*(1-midtone));
                if(delta==0)continue;
                auto *pixel=out+4*x;auto lab=toLab({pixel[0],pixel[1],pixel[2]});lab[0]+=delta;
                const auto rgb=fromLab(lab);
                if(!std::isfinite(rgb[0])||!std::isfinite(rgb[1])||!std::isfinite(rgb[2])){invalid=true;return;}
                std::copy(rgb.begin(),rgb.end(),pixel);
            }
        },parallel);
        return !invalid&&!cancelled(cancel);
    };
    return filter(p.smallRadius,false)&&filter(p.largeRadius,true)?linear:QImage{};
}
