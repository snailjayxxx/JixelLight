#pragma once
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/LuminanceRangeMask.h"
#include "core/pipeline/ColorRangeMask.h"
#include <array>
#include <algorithm>
#include <QRectF>
#include <memory>

struct alignas(16) Float4 { float x = 0, y = 0, z = 0, w = 0; };
// Exactly the std140 layout in the compute shaders. Matrix ROWS.
struct ProcessingPlan {
    static constexpr const char *EngineVersion = "jixellight-linear-v5-base2-look4";
    // Jixel Neutral v2 keeps camera exposure calibration separate from the
    // universal scene-to-display tone placement. These are rendering anchors,
    // not a hidden user Exposure adjustment.
    static constexpr float RawNeutralSceneGray = 0.03f;
    static constexpr float RawNeutralDisplayGray = 0.18f;
    // Source-compatibility only: alpha.11 no longer applies a fixed RAW gain.
    static constexpr float RawBaseExposureStops = 0.0f;
    static constexpr float RawBaseGain = 1.0f;
    enum Slot { Wb0=0, Wb1=1, Wb2=2, Tonal=3, Tone=4, Color=5,
                Out0=6, Out1=7, Out2=8, Luminance=9, Flags=10,
                Bands=11, Curves=19, Dimensions=24,
                Input0=25, Input1=26, Input2=27,
                Working0=28, Working1=29, Working2=30, LookStyle=31, LookDetail=32, LookOptions=33,
                LutOut0=34, LutOut1=35, LutOut2=36, Vignette=37, FrameRect=38, PixelMap=39,
                BlackWhite=40, BlackWhiteMix0=41, BlackWhiteMix1=42,
                GradingOptions=43, GradingShadows=44, GradingMidtones=45,
                GradingHighlights=46, LinearMask=47, LinearMaskDirection=48,
                RadialMask=49, RadialMaskShape=50, Grain=51, IndependentDetail=52, Denoise=53, Clarity=54, Texture=55, Dehaze=56, DehazeAirlight=57, PointColorAdjust=58, PointColorCenter=59, PointColorRange=60, LuminanceMask=61, LuminanceMaskOptions=62, ColorMask=63, ColorMaskRange=64, SlotCount=65 };
    std::array<Float4, SlotCount> data{};
    AdjustmentState state;
    ImagePipeline::InputEncoding encoding = ImagePipeline::InputEncoding::SRgb;
    ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb;
    bool rawSource = false;
    float baseExposureStops = 0.0f;

    void setFrameRect(const QRectF &rect) {
        // A research-only brush must use the same normalized viewport ROI as
        // legacy spatial edits, even if no linear/radial/vignette is active.
        const bool brush=std::any_of(state.brushMasks.layers.begin(),state.brushMasks.layers.end(),
            [](const auto &layer){return layer.enabled;});
        if (data[Vignette].x!=0 || data[LinearMask].x!=0 || data[RadialMask].x!=0 || data[Grain].x!=0 || data[IndependentDetail].x!=0 || denoiseActive() || dehazeActive() || clarityActive() || textureActive() || brush)
            data[FrameRect]={float(rect.x()),float(rect.y()),float(rect.width()),float(rect.height())};
    }
    Float4 pixelMap(const QSize &size) const {
        const auto rect=data[FrameRect];
        // Centered ellipse coordinates keep opposite full-image pixels exact
        // mirrors; both CPU tiles and GPU use this host-computed mapping.
        return {2*rect.x+rect.z-1,2*rect.y+rect.w-1,2*rect.z/std::max(1,size.width()),2*rect.w/std::max(1,size.height())};
    }
    void setReferenceFrameSize(const QSize &size) {
        if (data[IndependentDetail].x!=0 && size.width()>0 && size.height()>0) {
            data[IndependentDetail].z=float(size.width());data[IndependentDetail].w=float(size.height());
        }
        if(dehazeActive()&&size.width()>0&&size.height()>0) {
            data[Dehaze].z=float(size.width());data[Dehaze].w=float(size.height());
        }
        if(textureActive()&&size.width()>0&&size.height()>0) {
            data[Texture].z=float(size.width());data[Texture].w=float(size.height());
        }
        if(clarityActive()&&size.width()>0&&size.height()>0) {
            data[Clarity].z=float(size.width());data[Clarity].w=float(size.height());
        }
        if(denoiseActive()&&size.width()>0&&size.height()>0) {
            data[Denoise].z=float(size.width());data[Denoise].w=float(size.height());
        }
    }
    bool validIndependentDetail() const {
        const auto d=data[IndependentDetail];
        if(!std::isfinite(d.x)||!std::isfinite(d.y)||d.x<0||d.x>1.5f||d.y<0||d.y>6)return false;
        if(d.z==0&&d.w==0)return true;
        return d.x>0&&std::isfinite(d.z)&&std::isfinite(d.w)&&d.z>=1&&d.w>=1
            &&d.z<=33554432&&d.w<=33554432&&d.z==std::floor(d.z)&&d.w==std::floor(d.w);
    }
    Float4 independentDetail(const QSize &size) const {
        auto result=data[IndependentDetail];const auto frame=data[FrameRect];
        if (result.x==0||!validIndependentDetail()) return {};
        float scale=1;
        if(result.z>0 && result.w>0 && frame.z>0 && frame.w>0)
            scale=std::min(float(size.width())/(result.z*frame.z),float(size.height())/(result.w*frame.w));
        result.y=std::clamp(result.y*scale,0.f,12.f);result.z=0;result.w=0;return result;
    }
    bool denoiseActive() const {return data[Denoise].x>0||data[Denoise].y>0;}
    bool validDenoise() const {
        const auto d=data[Denoise];
        if(!std::isfinite(d.x)||!std::isfinite(d.y)||d.x<0||d.x>1||d.y<0||d.y>1)return false;
        if(d.z==0&&d.w==0)return true;
        return denoiseActive()&&std::isfinite(d.z)&&std::isfinite(d.w)&&d.z>=1&&d.w>=1
            &&d.z<=33554432&&d.w<=33554432&&d.z==std::floor(d.z)&&d.w==std::floor(d.w);
    }
    bool colorMaskActive() const {return data[ColorMask].x!=0;}
    ColorRangeMask::Parameters colorMaskParameters() const {
        if(!colorMaskActive())return {};
        const auto c=data[ColorMask],r=data[ColorMaskRange];
        return {{true,0,0,0,c.y,c.z,c.w,r.x,r.y,r.z},c.x,r.w==1};
    }
    bool validColorMask() const {
        const auto c=data[ColorMask],r=data[ColorMaskRange];
        if(c.x==0)return c.y==0&&c.z==0&&c.w==0&&r.x==0&&r.y==0&&r.z==0&&r.w==0;
        return (r.w==0||r.w==1)&&ColorRangeMask::valid(colorMaskParameters());
    }
    bool luminanceMaskActive() const {return data[LuminanceMask].x!=0;}
    LuminanceRangeMask::Parameters luminanceMaskParameters() const {
        if(!luminanceMaskActive())return {};
        const auto m=data[LuminanceMask];return {true,data[LuminanceMaskOptions].x==1,m.x,m.y,m.z,m.w};
    }
    bool validLuminanceMask() const {
        const auto m=data[LuminanceMask],o=data[LuminanceMaskOptions];
        if(m.x==0)return m.y==0&&m.z==0&&m.w==0&&o.x==0&&o.y==0&&o.z==0&&o.w==0;
        return (o.x==0||o.x==1)&&o.y==0&&o.z==0&&o.w==0&&LuminanceRangeMask::valid(luminanceMaskParameters());
    }
    bool pointColorActive() const {return data[PointColorAdjust].x!=0;}
    bool pointColorNeedsCpuPrecision() const {
        const auto range=data[PointColorRange];
        // D3D11 WARP exceeds40/65535 at a1-degree hue shoulder (CI381).
        // Keep one shared conservative precision region on every backend.
        return pointColorActive()&&(range.x<5.f||range.y<.003f||range.z<.003f);
    }
    PointColor::Parameters pointColorParameters() const {
        const auto a=data[PointColorAdjust],c=data[PointColorCenter],r=data[PointColorRange];
        return {a.x>.5f,a.y,a.z,a.w,c.x,c.y,c.z,r.x,r.y,r.z};
    }
    bool validPointColor() const {
        const auto a=data[PointColorAdjust],c=data[PointColorCenter],r=data[PointColorRange];
        if(a.x==0) {
            for(const auto v:{a,c,r})if(v.x!=0||v.y!=0||v.z!=0||v.w!=0)return false;
            return true;
        }
        return a.x==1&&c.w==0&&r.w==0&&PointColor::valid(pointColorParameters());
    }
    bool dehazeActive() const {return data[Dehaze].x!=0;}
    bool validDehaze() const {
        const auto d=data[Dehaze],a=data[DehazeAirlight];
        if(!std::isfinite(d.x)||d.x<-1||d.x>1||d.y!=0)return false;
        if(d.z!=0||d.w!=0) {
            if(!dehazeActive()||!std::isfinite(d.z)||!std::isfinite(d.w)||d.z<1||d.w<1
                ||d.z>33554432||d.w>33554432||d.z!=std::floor(d.z)||d.w!=std::floor(d.w))return false;
        }
        if(a.w==0)return a.x==0&&a.y==0&&a.z==0;
        return dehazeActive()&&(a.w==1||a.w==2)&&std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)
            &&a.x>0&&a.y>0&&a.z>0&&a.x<=65536&&a.y<=65536&&a.z<=65536;
    }
    void setDehazeAirlight(const SpatialDehaze::Airlight &a) {
        if(dehazeActive())data[DehazeAirlight]={a.rgb[0],a.rgb[1],a.rgb[2],a.valid?(a.available?1.f:2.f):-1.f};
    }
    Float4 dehaze(const QSize &size) const {
        const auto d=data[Dehaze],frame=data[FrameRect];if(!dehazeActive()||!validDehaze())return {};
        if(d.x<0)return {d.x,0,0,0}; // Uniform veil has no neighborhood support.
        float density=1;
        if(d.z>0&&d.w>0&&frame.z>0&&frame.w>0)
            density=std::min(float(size.width())/(d.z*frame.z),float(size.height())/(d.w*frame.w));
        return {d.x,std::clamp(7.f*density,0.f,7.f),std::clamp(3.f*density,0.f,3.f),0};
    }
    bool textureActive() const {return data[Texture].x!=0;}
    bool clarityActive() const {return data[Clarity].x!=0;}
    bool spatialInputActive() const {return denoiseActive()||dehazeActive()||textureActive()||clarityActive();}
    bool validTexture() const {
        const auto c=data[Texture];
        if(!std::isfinite(c.x)||c.x<-1||c.x>1||c.y!=0)return false;
        if(c.z==0&&c.w==0)return true;
        return textureActive()&&std::isfinite(c.z)&&std::isfinite(c.w)&&c.z>=1&&c.w>=1
            &&c.z<=33554432&&c.w<=33554432&&c.z==std::floor(c.z)&&c.w==std::floor(c.w);
    }
    Float4 texture(const QSize &size) const {
        const auto c=data[Texture],frame=data[FrameRect];if(!textureActive()||!validTexture())return {};
        float density=1;
        if(c.z>0&&c.w>0&&frame.z>0&&frame.w>0)
            density=std::min(float(size.width())/(c.z*frame.z),float(size.height())/(c.w*frame.w));
        return {c.x,std::clamp(2.f*density,0.f,2.f),std::clamp(6.f*density,0.f,6.f),0};
    }
    bool validClarity() const {
        const auto c=data[Clarity];
        if(!std::isfinite(c.x)||c.x<-1||c.x>1||c.y!=0)return false;
        if(c.z==0&&c.w==0)return true;
        return clarityActive()&&std::isfinite(c.z)&&std::isfinite(c.w)&&c.z>=1&&c.w>=1
            &&c.z<=33554432&&c.w<=33554432&&c.z==std::floor(c.z)&&c.w==std::floor(c.w);
    }
    Float4 clarity(const QSize &size) const {
        const auto c=data[Clarity],frame=data[FrameRect];if(!clarityActive()||!validClarity())return {};
        float density=1;
        if(c.z>0&&c.w>0&&frame.z>0&&frame.w>0)
            density=std::min(float(size.width())/(c.z*frame.z),float(size.height())/(c.w*frame.w));
        return {c.x,std::clamp(24.f*density,0.f,24.f),0,0};
    }
    bool validSpatialSettings() const {return validColorMask()&&validLuminanceMask()&&validPointColor()&&validIndependentDetail()&&validDenoise()&&validDehaze()&&validTexture()&&validClarity();}
    Float4 denoise(const QSize &size) const {
        const auto d=data[Denoise],frame=data[FrameRect];if(!denoiseActive()||!validDenoise())return {};
        float density=1;
        if(d.z>0&&d.w>0&&frame.z>0&&frame.w>0)
            density=std::min(float(size.width())/(d.z*frame.z),float(size.height())/(d.w*frame.w));
        return {d.x,d.y,std::clamp(2.f*density,0.f,2.f),0};
    }

    // Compatibility entry point for tests/tools that historically used
    // LinearProPhoto to mean RAW. Application code should use the explicit
    // overload below so pixel encoding and source semantics are independent.
    static ProcessingPlan compile(const AdjustmentState &state,
                                  ImagePipeline::InputEncoding encoding,
                                  ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb);
    static ProcessingPlan compile(const AdjustmentState &state,
                                  ImagePipeline::InputEncoding encoding,
                                  ColorManagement::OutputSpace output,
                                  bool rawSource,
                                  float baseExposureStops);
private:
    // state above is LookProfiles::effective(intent), so recompiling it would
    // apply the Look twice. Only explicit brush admission may use this snapshot.
    std::shared_ptr<const AdjustmentState> m_brushIntent;
    friend class BrushRenderRequest;
};
static_assert(sizeof(Float4) == 16);
static_assert(sizeof(ProcessingPlan::data) == 1040);
