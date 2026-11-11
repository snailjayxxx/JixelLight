#include "core/pipeline/SpatialClarity.h"
#include "core/async/ParallelRows.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <new>
#include <vector>

bool SpatialClarity::valid(Parameters p) {
    return std::isfinite(p.amount)&&std::isfinite(p.radius)
        &&p.amount>=-1&&p.amount<=1&&p.radius>=0&&p.radius<=24;
}
int SpatialClarity::halo(Parameters p) {
    return valid(p)&&p.amount!=0?int(std::ceil(p.radius)):0;
}
std::array<float,49> SpatialClarity::kernelWeights(float radius) {
    std::array<float,49> weights{};
    if(!std::isfinite(radius)||radius<0||radius>24)return weights;
    const int extent=int(std::ceil(radius));const float sigma=std::max(.01f,.5f*radius);
    for(int i=-extent;i<=extent;++i) {
        const float edge=std::clamp(radius+1-std::abs(float(i)),0.f,1.f);
        weights[size_t(i+extent)]=edge*std::exp(-float(i*i)/(2*sigma*sigma));
    }
    return weights;
}
QImage SpatialClarity::apply(QImage linear,Parameters p,Transform toLab,Transform fromLab,
                            const CancelToken &cancel,bool parallel) {
    if(linear.isNull()||linear.format()!=QImage::Format_RGBA32FPx4||!valid(p)
        ||!toLab||!fromLab||cancelled(cancel))return {};
    if(p.amount==0||p.radius==0)return linear;
    const int width=linear.width(),height=linear.height(),extent=halo(p);
    const auto weights=kernelWeights(p.radius);
    using Horizontal=std::array<float,3>; // Relative sum, alpha sum, local reference.
    std::vector<float> guide;
    std::vector<Horizontal> horizontal;
    if(size_t(width)*height>guide.max_size()||size_t(width)*height>horizontal.max_size())return {};
    try {guide.resize(size_t(width)*height);horizontal.resize(size_t(width)*height);}
    catch(const std::bad_alloc &){return {};}
    std::atomic_bool invalid{false};
    const auto *source=linear.constBits();const auto stride=linear.bytesPerLine();
    ParallelRows::run(height,width,cancel,[&](int y) {
        const auto *row=reinterpret_cast<const float *>(source+y*stride);
        for(int x=0;x<width;++x) {
            const auto *pixel=row+4*x;
            if(!std::isfinite(pixel[0])||!std::isfinite(pixel[1])||!std::isfinite(pixel[2])
                ||!std::isfinite(pixel[3])||pixel[3]<0||pixel[3]>1) {invalid=true;return;}
            if(pixel[3]<=1.e-20f){guide[size_t(y)*width+x]=0;continue;}
            const auto lab=toLab({pixel[0],pixel[1],pixel[2]});
            if(!std::isfinite(lab[0])||!std::isfinite(lab[1])||!std::isfinite(lab[2])){invalid=true;return;}
            guide[size_t(y)*width+x]=lab[0];
        }
    },parallel);
    if(invalid||cancelled(cancel))return {};
    ParallelRows::run(height,width,cancel,[&](int y) {
        const auto *row=reinterpret_cast<const float *>(source+y*stride);
        for(int x=0;x<width;++x) {
            float value=0,alpha=0;
            float center=guide[size_t(y)*width+x];
            // A transparent center never supplies a color reference. Choose
            // the first visible local neighbor so flat visible fields remain
            // bit-exact even through irregular alpha holes.
            if(row[4*x+3]<=1.e-20f)for(int i=-extent;i<=extent;++i) {
                const int xx=std::clamp(x+i,0,width-1);
                if(row[4*xx+3]>1.e-20f){center=guide[size_t(y)*width+xx];break;}
            }
            for(int i=-extent;i<=extent;++i) {
                const int xx=std::clamp(x+i,0,width-1);const float a=row[4*xx+3];if(a<=1.e-20f)continue;
                const float weight=weights[size_t(i+extent)];
                value+=weight*(guide[size_t(y)*width+xx]-center)*a;alpha+=weight*a;
            }
            horizontal[size_t(y)*width+x]={value,alpha,center};
        }
    },parallel);
    if(cancelled(cancel))return {};
    auto *output=linear.bits();if(!output)return {};
    ParallelRows::run(height,width,cancel,[&](int y) {
        auto *row=reinterpret_cast<float *>(output+y*stride);
        for(int x=0;x<width;++x) {
            auto *pixel=row+4*x;if(pixel[3]<=1.e-20f)continue;
            float value=0,alpha=0;
            const float lightness=guide[size_t(y)*width+x];
            for(int i=-extent;i<=extent;++i) {
                const auto index=size_t(std::clamp(y+i,0,height-1))*width+x;
                const auto sample=horizontal[index];const float weight=weights[size_t(i+extent)];
                value+=weight*(sample[0]+(sample[2]-lightness)*sample[1]);alpha+=weight*sample[1];
            }
            if(alpha<=1.e-20f)continue;
            const float midtone=std::clamp(lightness,0.f,1.f);
            const float delta=-.6f*p.amount*(value/alpha)*(4*midtone*(1-midtone));
            if(delta==0)continue;
            auto lab=toLab({pixel[0],pixel[1],pixel[2]});lab[0]+=delta;
            const auto result=fromLab(lab);
            if(!std::isfinite(result[0])||!std::isfinite(result[1])||!std::isfinite(result[2])){invalid=true;return;}
            std::copy(result.begin(),result.end(),pixel);
        }
    },parallel);
    return invalid||cancelled(cancel)?QImage{}:linear;
}
