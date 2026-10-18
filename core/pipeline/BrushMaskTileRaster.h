#pragma once

#include "core/pipeline/BrushMaskDocument.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

// F4 M4.2: standalone scene-linear CPU TILE ORACLE for synthetic testing.
// NO live RAW/GUI/project integration: the committed adjustment source and
// QRhi shader are untouched. All input is nonpremultiplied, row-tight RGBA32F.
// The builder never mutates output unless ALL validation, computation and
// optional cancellation checks have passed.
namespace BrushMaskTileRaster {

constexpr int MaxTileEdge = 128;
constexpr std::size_t MaxDabPixelVisits = 8 * 1024 * 1024;
constexpr double MaxChannelMagnitude = 65536.0;

struct Rect { int x=0, y=0, width=0, height=0; };
struct Output {
    std::vector<float> weightedExposureEv; // one float per requested pixel
    std::vector<float> rgba32f;             // unassociated linear RGB and original alpha
    std::size_t visitedDabPixels=0;
};

inline bool validRect(Rect rect,int frameWidth,int frameHeight) {
    return BrushMaskGeometry::validFrame(frameWidth,frameHeight)
        && rect.x>=0 && rect.y>=0 && rect.width>0 && rect.height>0
        && rect.width<=MaxTileEdge && rect.height<=MaxTileEdge
        && rect.x<=frameWidth-rect.width && rect.y<=frameHeight-rect.height;
}

inline bool stopped(const std::atomic_bool *cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

inline bool render(const BrushMaskDocument::Document &document,
                   int frameWidth,int frameHeight,Rect tile,
                   std::span<const float> rgbaInput,Output *out,
                   const std::atomic_bool *cancel=nullptr) {
    if (!out || !validRect(tile,frameWidth,frameHeight)
        || !BrushMaskDocument::validDocument(document) || stopped(cancel))
        return false;
    const std::size_t count=std::size_t(tile.width)*std::size_t(tile.height);
    if (rgbaInput.size()!=count*4) return false;
    for (std::size_t i=0;i<count;++i) {
        if (stopped(cancel)) return false;
        const std::size_t offset=i*4;
        for (int channel=0;channel<3;++channel) {
            const double value=rgbaInput[offset+channel];
            if (!std::isfinite(value) || std::abs(value)>MaxChannelMagnitude)
                return false;
        }
        const double alpha=rgbaInput[offset+3];
        if (!std::isfinite(alpha) || alpha<0 || alpha>1) return false;
    }

    std::vector<double> stops(count,0.0);
    std::vector<double> layerAlpha(count,0.0),strokeAlpha(count,0.0);
    const double shortEdge=double(std::min(frameWidth,frameHeight));
    std::size_t visited=0;
    for (const auto &layer : document.layers) {
        if (stopped(cancel)) return false;
        if (!layer.enabled || layer.exposureEv==0) continue;
        std::fill(layerAlpha.begin(),layerAlpha.end(),0.0);
        for (const auto &stroke : layer.strokes) {
            if (stopped(cancel)) return false;
            std::fill(strokeAlpha.begin(),strokeAlpha.end(),0.0);
            for (const auto &dab : stroke.dabs) {
                if (stopped(cancel)) return false;
                if (dab.pressure==0 || stroke.style.opacity==0) continue;
                const double radius=stroke.style.radius*dab.pressure*shortEdge;
                const double cx=dab.center.x*frameWidth-0.5;
                const double cy=dab.center.y*frameHeight-0.5;
                const int left=std::max(tile.x,int(std::ceil(cx-radius)));
                const int top=std::max(tile.y,int(std::ceil(cy-radius)));
                const int right=std::min(tile.x+tile.width-1,int(std::floor(cx+radius)));
                const int bottom=std::min(tile.y+tile.height-1,int(std::floor(cy+radius)));
                if (left>right || top>bottom) continue;
                const std::size_t area=std::size_t(right-left+1)*std::size_t(bottom-top+1);
                if (area>MaxDabPixelVisits-visited) return false;
                visited+=area;
                for (int y=top;y<=bottom;++y) {
                    if (stopped(cancel)) return false;
                    const std::size_t row=std::size_t(y-tile.y)*std::size_t(tile.width);
                    for (int x=left;x<=right;++x) {
                        const double radial=std::hypot(x-cx,y-cy)/radius;
                        if (radial>=1) continue;
                        double feather=1;
                        if (radial>stroke.style.hardness && stroke.style.hardness<1) {
                            const double t=(radial-stroke.style.hardness)/(1-stroke.style.hardness);
                            feather=1-t*t*(3-2*t);
                        }
                        const double alpha=stroke.style.opacity*dab.pressure*feather;
                        const std::size_t offset=row+std::size_t(x-tile.x);
                        strokeAlpha[offset]=std::max(strokeAlpha[offset],alpha);
                    }
                }
            }
            for (std::size_t i=0;i<count;++i)
                layerAlpha[i]=1-(1-layerAlpha[i])*(1-strokeAlpha[i]);
        }
        for (std::size_t i=0;i<count;++i)
            stops[i]+=layer.exposureEv*layerAlpha[i];
    }

    Output generated;
    generated.weightedExposureEv.resize(count);
    generated.rgba32f.assign(rgbaInput.begin(),rgbaInput.end());
    generated.visitedDabPixels=visited;
    for (std::size_t i=0;i<count;++i) {
        if (stopped(cancel)) return false;
        const double gain=std::exp2(stops[i]);
        if (!std::isfinite(gain)) return false;
        generated.weightedExposureEv[i]=float(stops[i]);
        for(int channel=0;channel<3;++channel) {
            const double value=double(rgbaInput[i*4+channel])*gain;
            if (!std::isfinite(value) || std::abs(value)>MaxChannelMagnitude)
                return false; // never silently clamp scene-linear/overrange RGB
            generated.rgba32f[i*4+channel]=float(value);
        }
        // Alpha is bit-preserved by the original vector copy.
    }
    if (stopped(cancel)) return false;
    *out=std::move(generated);
    return true;
}
}
