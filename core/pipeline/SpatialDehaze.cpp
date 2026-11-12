#include "core/pipeline/SpatialDehaze.h"
#include "core/async/ParallelRows.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <new>
#include <vector>
namespace {
constexpr float AlphaFloor=1.e-20f;
bool validPixel(const SpatialDehaze::Pixel &p) {
    return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2])&&std::isfinite(p[3])
        &&p[3]>=0&&p[3]<=1;
}
}
SpatialDehaze::Airlight SpatialDehaze::estimateAirlight(QSize size,const Sampler &sample,const CancelToken &cancel) {
    Airlight result;if(size.isEmpty()||!sample||cancelled(cancel)){result.valid=false;return result;}
    struct Candidate {Triple rgb;float dark=0;double brightness=0;};std::vector<Candidate> candidates;
    try {candidates.reserve(4096);}catch(const std::bad_alloc &){result.valid=false;return result;}
    const int columns=std::min(64,size.width()),rows=std::min(64,size.height());bool invalid=false;
    const auto bilinear=[&](double x,double y)->Pixel {
        x=std::clamp(x,0.,double(size.width()-1));y=std::clamp(y,0.,double(size.height()-1));
        const int left=int(std::floor(x)),top=int(std::floor(y));const double dx=x-left,dy=y-top;
        double alpha=0;std::array<double,3> rgb{};
        for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
            const auto p=sample(std::min(left+i,size.width()-1),std::min(top+j,size.height()-1));
            if(!validPixel(p)){invalid=true;return {};}
            const double weight=(i?dx:1-dx)*(j?dy:1-dy)*p[3];if(p[3]<=AlphaFloor)continue;
            alpha+=weight;for(int c=0;c<3;++c)rgb[c]+=weight*p[c];
        }
        return alpha>AlphaFloor?Pixel{float(rgb[0]/alpha),float(rgb[1]/alpha),float(rgb[2]/alpha),float(alpha)}:Pixel{};
    };
    for(int row=0;row<rows;++row) {
        if(cancelled(cancel)){result.valid=false;return result;}
        for(int column=0;column<columns;++column) {
            const double x=(column+.5)*size.width()/columns-.5,y=(row+.5)*size.height()/rows-.5;
            const auto center=bilinear(x,y);if(invalid){result.valid=false;return result;}if(center[3]<=AlphaFloor)continue;
            float dark=std::numeric_limits<float>::infinity();
            for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i) {
                const auto p=bilinear(x+7*i,y+7*j);if(invalid){result.valid=false;return result;}if(p[3]<=AlphaFloor)continue;
                dark=std::min(dark,std::max(0.f,std::min({p[0],p[1],p[2]})));
            }
            candidates.push_back({{center[0],center[1],center[2]},dark,double(center[0])+center[1]+center[2]});
        }
    }
    result.samples=int(candidates.size());if(candidates.empty())return result;
    const size_t selected=std::max(size_t(1),size_t(std::ceil(candidates.size()*.001)));
    std::partial_sort(candidates.begin(),candidates.begin()+selected,candidates.end(),[](const Candidate &a,const Candidate &b){
        if(a.dark!=b.dark)return a.dark>b.dark;if(a.brightness!=b.brightness)return a.brightness>b.brightness;return a.rgb>b.rgb;
    });
    const auto best=std::max_element(candidates.begin(),candidates.begin()+selected,[](const Candidate &a,const Candidate &b){return a.brightness<b.brightness;});
    const float peak=std::max({best->rgb[0],best->rgb[1],best->rgb[2]});if(!(peak>0))return result;
    const float floor=std::max(1.e-6f,std::min(peak,65536.f)*1.e-4f);
    for(int c=0;c<3;++c)result.rgb[c]=std::clamp(best->rgb[c],floor,65536.f);
    result.available=true;return result;
}
bool SpatialDehaze::valid(Parameters p) {
    return std::isfinite(p.amount)&&p.amount>=-1&&p.amount<=1&&std::isfinite(p.darkRadius)
        &&p.darkRadius>=0&&p.darkRadius<=7&&std::isfinite(p.refineRadius)&&p.refineRadius>=0&&p.refineRadius<=3
        &&std::all_of(p.airlight.begin(),p.airlight.end(),[](float v){return std::isfinite(v)&&v>0&&v<=65536;});
}
int SpatialDehaze::halo(Parameters p) {return valid(p)&&p.amount>0?int(std::ceil(p.darkRadius))+int(std::ceil(p.refineRadius)):0;}
std::array<float,8> SpatialDehaze::refinementWeights(float radius) {
    std::array<float,8> weights{};if(!std::isfinite(radius)||radius<0||radius>3)return weights;
    const int extent=int(std::ceil(radius));const float sigma=std::max(.01f,.5f*radius);
    for(int i=-extent;i<=extent;++i)weights[size_t(i+extent)]=std::clamp(radius+1-std::abs(float(i)),0.f,1.f)*std::exp(-float(i*i)/(2*sigma*sigma));
    return weights;
}
QImage SpatialDehaze::apply(QImage linear,Parameters p,const CancelToken &cancel,bool parallel) {
    if(linear.isNull()||linear.format()!=QImage::Format_RGBA32FPx4||!valid(p)||cancelled(cancel))return {};
    if(p.amount==0)return linear;
    const int width=linear.width(),height=linear.height();const auto stride=linear.bytesPerLine();std::atomic_bool invalid{false};
    if(p.amount<0) {
        auto *bits=linear.bits();if(!bits)return {};
        const float transmission=1+.65f*p.amount;
        ParallelRows::run(height,width,cancel,[&](int y){auto *row=reinterpret_cast<float *>(bits+y*stride);
            for(int x=0;x<width;++x){auto *q=row+4*x;if(!validPixel({q[0],q[1],q[2],q[3]})){invalid=true;return;}if(q[3]<=AlphaFloor)continue;
                for(int c=0;c<3;++c)q[c]=p.airlight[c]+(q[c]-p.airlight[c])*transmission;}
        },parallel);return invalid||cancelled(cancel)?QImage{}:linear;
    }
    const size_t pixels=size_t(width)*height;std::vector<float> dark,horizontal;std::vector<std::array<float,2>> guide;
    if(pixels>dark.max_size()||pixels>guide.max_size())return {};
    try {dark.resize(pixels);horizontal.resize(pixels);guide.resize(pixels);}catch(const std::bad_alloc &){return {};}
    const auto *source=linear.constBits();
    ParallelRows::run(height,width,cancel,[&](int y){const auto *row=reinterpret_cast<const float *>(source+y*stride);
        for(int x=0;x<width;++x){const auto *q=row+4*x;const size_t index=size_t(y)*width+x;
            if(!validPixel({q[0],q[1],q[2],q[3]})){invalid=true;return;}
            guide[index]={std::clamp((q[0]/p.airlight[0]+q[1]/p.airlight[1]+q[2]/p.airlight[2])/3,0.f,4.f),q[3]};
            dark[index]=q[3]<=AlphaFloor?std::numeric_limits<float>::infinity():std::clamp(std::min({q[0]/p.airlight[0],q[1]/p.airlight[1],q[2]/p.airlight[2]}),0.f,1.f);
        }
    },parallel);if(invalid||cancelled(cancel))return {};
    const int radius=int(std::ceil(p.darkRadius));
    ParallelRows::run(height,width,cancel,[&](int y){for(int x=0;x<width;++x){float value=std::numeric_limits<float>::infinity();
        for(int i=-radius;i<=radius;++i)value=std::min(value,dark[size_t(y)*width+std::clamp(x+i,0,width-1)]);horizontal[size_t(y)*width+x]=value;}
    },parallel);if(cancelled(cancel))return {};
    ParallelRows::run(height,width,cancel,[&](int y){for(int x=0;x<width;++x){float value=std::numeric_limits<float>::infinity();
        for(int j=-radius;j<=radius;++j)value=std::min(value,horizontal[size_t(std::clamp(y+j,0,height-1))*width+x]);
        dark[size_t(y)*width+x]=std::isfinite(value)?std::clamp(1-.95f*value,.1f,1.f):1.f;}
    },parallel);if(cancelled(cancel))return {};
    auto *output=linear.bits();if(!output)return {};
    const int refine=int(std::ceil(p.refineRadius));const auto weights=refinementWeights(p.refineRadius);
    ParallelRows::run(height,width,cancel,[&](int y){auto *row=reinterpret_cast<float *>(output+y*stride);
        for(int x=0;x<width;++x){const size_t index=size_t(y)*width+x;const auto center=guide[index];if(center[1]<=AlphaFloor)continue;
            float sum=0,weighted=0;
            for(int j=-refine;j<=refine;++j)for(int i=-refine;i<=refine;++i){const size_t neighbor=size_t(std::clamp(y+j,0,height-1))*width+std::clamp(x+i,0,width-1);const auto g=guide[neighbor];if(g[1]<=AlphaFloor)continue;
                const float delta=g[0]-center[0],weight=weights[size_t(i+refine)]*weights[size_t(j+refine)]*std::exp(-delta*delta/.02f)*g[1];sum+=weight;weighted+=weight*dark[neighbor];}
            const float refined=sum>AlphaFloor?std::clamp(weighted/sum,.1f,1.f):dark[index];
            const float transmission=1-p.amount*(1-refined);if(transmission==1)continue;
            for(int c=0;c<3;++c){const float value=(row[4*x+c]-p.airlight[c])/transmission+p.airlight[c];if(!std::isfinite(value)){invalid=true;return;}row[4*x+c]=value;}
        }
    },parallel);return invalid||cancelled(cancel)?QImage{}:linear;
}
