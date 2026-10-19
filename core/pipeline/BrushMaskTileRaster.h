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
// The prepared viewport can represent a panned/zoomed section of the entire
// post-crop image. Its normalized frame is part of the sample mapping, not a
// new crop applied to the user's mask coordinates.
struct NormalizedFrame { double x=0, y=0, width=1, height=1; };
inline bool validNormalizedFrame(NormalizedFrame frame) {
    return std::isfinite(frame.x) && std::isfinite(frame.y)
        && std::isfinite(frame.width) && std::isfinite(frame.height)
        && frame.x>=0 && frame.y>=0
        && frame.width>=1e-6 && frame.height>=1e-6
        && frame.x+frame.width<=1.000001 && frame.y+frame.height<=1.000001;
}
inline bool anyEnabled(const BrushMaskDocument::Document &document) {
    return std::any_of(document.layers.begin(),document.layers.end(),
        [](const BrushMaskDocument::Layer &layer) { return layer.enabled; });
}
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
                   const std::atomic_bool *cancel=nullptr,
                   NormalizedFrame frame={}) {
    if (!out || !validRect(tile,frameWidth,frameHeight)
        || !validNormalizedFrame(frame)
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
    // Pixels per full-image normalized coordinate depend on the viewport.
    // A brush radius is a fraction of the SHORT full-image dimension. Both
    // tile axes receive the same pixel radius so a circle stays circular
    // across whole-frame, crop, portrait and panned ROI modes.
    const double fullWidth=frameWidth/frame.width, fullHeight=frameHeight/frame.height;
    const double shortEdge=std::min(fullWidth,fullHeight);
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
                const double cx=(dab.center.x-frame.x)*frameWidth/frame.width-0.5;
                const double cy=(dab.center.y-frame.y)*frameHeight/frame.height-0.5;
                const int tileRight=tile.x+tile.width-1, tileBottom=tile.y+tile.height-1;
                // Clip before converting to int: offscreen grips can be many
                // viewport widths away, and an unchecked float-to-int is UB.
                if (cx+radius<tile.x || cx-radius>tileRight
                    || cy+radius<tile.y || cy-radius>tileBottom) continue;
                const int left=int(std::ceil(std::max(double(tile.x),cx-radius)));
                const int top=int(std::ceil(std::max(double(tile.y),cy-radius)));
                const int right=int(std::floor(std::min(double(tileRight),cx+radius)));
                const int bottom=int(std::floor(std::min(double(tileBottom),cy+radius)));
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
