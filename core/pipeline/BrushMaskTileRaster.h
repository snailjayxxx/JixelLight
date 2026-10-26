#pragma once

#include "core/pipeline/BrushMaskDocument.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// F4 M4.2 / M4.4B.1: bounded mask coverage oracle with an optional RGBA32F
// reference renderer. exposureOnly computes just the exposure plane used by
// the M4.4A in-memory scene-linear CPU pipeline, without staging dummy color
// buffers. render keeps its original signed-HDR RGBA input-validation and
// double-precision gain semantics. Both APIs publish results atomically.
// Production RAW/GUI enabled brush editing remains gated pending future work.
namespace BrushMaskTileRaster {

constexpr int MaxTileEdge = 128;
constexpr std::size_t MaxDabPixelVisits = 8 * 1024 * 1024;
constexpr std::size_t MaxFrameDabPixelVisits = 96 * 1024 * 1024;
constexpr std::size_t MaxResearchFramePixels = 2 * 1024 * 1024;
// Index storage has its own budget: a one-pixel intersection can cost a
// reference even when its raster work is tiny. Exhaustion falls back to the
// original scan, without changing which research requests can be rendered.
constexpr std::size_t MaxCandidateIndexReferences = 1024 * 1024;
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

// A mask-only API avoids staging dummy RGBA32F input or allocating a
// synthetic RGBA32F output inside the real CPU renderer. Both public APIs
// deliberately reuse exactly one double-precision coverage implementation.
struct ExposureOutput {
    std::vector<float> weightedExposureEv;
    std::size_t visitedDabPixels=0;
};

namespace detail {
// Shared integer candidate rectangle: planning and rasterization must charge
// exactly the same work, including bounding-box pixels outside the circle.
struct CandidateBounds {
    int left=0,top=0,right=-1,bottom=-1;
    double cx=0,cy=0,radius=0;
    std::size_t area() const {
        return left>right || top>bottom ? 0
            : std::size_t(right-left+1)*std::size_t(bottom-top+1);
    }
};
inline CandidateBounds candidateBounds(BrushMaskGeometry::Dab dab,
        BrushMaskGeometry::Style style,int width,int height,Rect tile,NormalizedFrame frame) {
    const double radius=style.radius*dab.pressure*std::min(width/frame.width,height/frame.height);
    const double cx=(dab.center.x-frame.x)*width/frame.width-0.5;
    const double cy=(dab.center.y-frame.y)*height/frame.height-0.5;
    const int right=tile.x+tile.width-1,bottom=tile.y+tile.height-1;
    if (radius==0 || cx+radius<tile.x || cx-radius>right
        || cy+radius<tile.y || cy-radius>bottom) return {};
    // Clip doubles before converting, including very small panned viewports.
    return {int(std::ceil(std::max(double(tile.x),cx-radius))),
            int(std::ceil(std::max(double(tile.y),cy-radius))),
            int(std::floor(std::min(double(right),cx+radius))),
            int(std::floor(std::min(double(bottom),cy+radius))),cx,cy,radius};
}
struct Coverage {
    std::vector<double> stops;
    std::size_t visitedDabPixels=0;
};
inline bool accumulateDab(const CandidateBounds &bounds,
                          BrushMaskGeometry::Style style,double pressure,
                          Rect tile,std::vector<double> &strokeAlpha,
                          std::size_t &visited,const std::atomic_bool *cancel) {
    const auto [left,top,right,bottom,cx,cy,radius]=bounds;
    const std::size_t area=bounds.area();
    if (!area) return true;
    if (area>MaxDabPixelVisits-visited) return false;
    visited+=area;
    for (int y=top;y<=bottom;++y) {
        if (stopped(cancel)) return false;
        const std::size_t row=std::size_t(y-tile.y)*std::size_t(tile.width);
        for (int x=left;x<=right;++x) {
            const double radial=std::hypot(x-cx,y-cy)/radius;
            if (radial>=1) continue;
            double feather=1;
            if (radial>style.hardness && style.hardness<1) {
                const double t=(radial-style.hardness)/(1-style.hardness);
                feather=1-t*t*(3-2*t);
            }
            const double alpha=style.opacity*pressure*feather;
            const std::size_t offset=row+std::size_t(x-tile.x);
            strokeAlpha[offset]=std::max(strokeAlpha[offset],alpha);
        }
    }
    return true;
}
inline bool calculateCoverage(const BrushMaskDocument::Document &document,
                              int frameWidth,int frameHeight,Rect tile,Coverage *out,
                              const std::atomic_bool *cancel,NormalizedFrame frame) {
    if (!out || !validRect(tile,frameWidth,frameHeight)
        || !validNormalizedFrame(frame)
        || !BrushMaskDocument::validDocument(document) || stopped(cancel))
        return false;
    const std::size_t count=std::size_t(tile.width)*std::size_t(tile.height);
    std::vector<double> stops(count,0.0);
    std::vector<double> layerAlpha(count,0.0),strokeAlpha(count,0.0);
    // Pixels per full-image normalized coordinate depend on the viewport.
    // A brush radius is a fraction of the SHORT full-image dimension. Both
    // tile axes receive the same pixel radius so a circle stays circular
    // across whole-frame, crop, portrait and panned ROI modes.
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
                const auto bounds=candidateBounds(dab,stroke.style,frameWidth,frameHeight,tile,frame);
                if (!accumulateDab(bounds,stroke.style,dab.pressure,tile,strokeAlpha,visited,cancel))
                    return false;
            }
            for (std::size_t i=0;i<count;++i)
                layerAlpha[i]=1-(1-layerAlpha[i])*(1-strokeAlpha[i]);
        }
        for (std::size_t i=0;i<count;++i)
            stops[i]+=layer.exposureEv*layerAlpha[i];
    }


    if (stopped(cancel)) return false;
    *out=Coverage{std::move(stops),visited};
    return true;
}
} // namespace detail

struct PlannedTile { Rect rect; std::size_t candidateVisits=0; };
class FramePlan;
namespace detail {
inline bool planFrameBounded(const BrushMaskDocument::Document &,int,int,Rect,FramePlan *,
                             const std::atomic_bool *,NormalizedFrame,std::size_t,std::size_t);
}
class FramePlan {
public:
    std::vector<PlannedTile> tiles;
    std::size_t candidateVisits=0;
    bool hasCandidateIndex() const { return prepared_ && prepared_->indexed; }
    std::size_t candidateReferences() const { return prepared_ ? prepared_->references : 0; }
    std::size_t candidateIndexBytes() const {
        if (!hasCandidateIndex()) return 0;
        std::size_t bytes=prepared_->tileDabs.capacity()*sizeof(std::vector<DabReference>);
        for (const auto &refs:prepared_->tileDabs) bytes+=refs.capacity()*sizeof(DabReference);
        return bytes;
    }
private:
    // Indices are compact and point only into this plan's owning snapshot.
    // Neither caller mutation nor document lifetime can stale the candidates.
    struct DabReference { std::uint16_t layer,stroke,dab; };
    struct Prepared {
        BrushMaskDocument::Document document;
        int width=0,height=0;
        NormalizedFrame frame;
        std::vector<PlannedTile> tiles;
        std::vector<std::vector<DabReference>> tileDabs;
        std::size_t visits=0,references=0;
        bool indexed=false;
    };
    std::shared_ptr<const Prepared> prepared_;
    friend bool detail::planFrameBounded(const BrushMaskDocument::Document &,int,int,Rect,
                          FramePlan *,const std::atomic_bool *,NormalizedFrame,std::size_t,std::size_t);
    friend bool exposureOnly(const FramePlan &,std::size_t,ExposureOutput *,const std::atomic_bool *);
};

// Preflight the complete requested region before any pixel rasterization or
// image staging. Map each dab's clipped integer bounds to intersecting tiles
// once, preserving layer/stroke/dab order in each tile. The index has bounded
// storage independent of work budgets; index-budget exhaustion uses the accepted
// scan path. A plan owns immutable inputs and is never a cross-request cache.
inline bool detail::planFrameBounded(const BrushMaskDocument::Document &document,int width,int height,
                      Rect region,FramePlan *out,const std::atomic_bool *cancel,
                      NormalizedFrame frame,std::size_t indexReferenceBudget,std::size_t pixelBudget) {
    if (!out || stopped(cancel) || !BrushMaskGeometry::validFrame(width,height)
        || !validNormalizedFrame(frame) || !BrushMaskDocument::validDocument(document)
        || region.x<0 || region.y<0 || region.width<=0 || region.height<=0
        || region.width>width || region.height>height
        || region.x>width-region.width || region.y>height-region.height
        || std::size_t(region.width)*region.height>pixelBudget) return false;
    FramePlan ready;
    auto prepared=std::make_shared<FramePlan::Prepared>();
    prepared->document=document;
    prepared->width=width;prepared->height=height;prepared->frame=frame;
    prepared->indexed=indexReferenceBudget!=0;
    indexReferenceBudget=std::min(indexReferenceBudget,MaxCandidateIndexReferences);
    for(int y=region.y;y<region.y+region.height;y+=MaxTileEdge) {
        for(int x=region.x;x<region.x+region.width;x+=MaxTileEdge) {
            if (stopped(cancel)) return false;
            ready.tiles.push_back({{x,y,std::min(MaxTileEdge,region.x+region.width-x),
                                 std::min(MaxTileEdge,region.y+region.height-y)},0});
        }
    }
    if (prepared->indexed) prepared->tileDabs.resize(ready.tiles.size());
    const int columns=(region.width+MaxTileEdge-1)/MaxTileEdge;
    for (std::size_t li=0;li<prepared->document.layers.size();++li) {
        if (stopped(cancel)) return false;
        const auto &layer=prepared->document.layers[li];
        if (!layer.enabled || layer.exposureEv==0) continue;
        for (std::size_t si=0;si<layer.strokes.size();++si) {
            if (stopped(cancel)) return false;
            const auto &stroke=layer.strokes[si];
            if (stroke.style.opacity==0) continue;
            for (std::size_t di=0;di<stroke.dabs.size();++di) {
                if (stopped(cancel)) return false;
                const auto &dab=stroke.dabs[di];
                if (dab.pressure==0) continue;
                const auto bounds=detail::candidateBounds(dab,stroke.style,width,height,region,frame);
                if (!bounds.area()) continue;
                const int firstColumn=(bounds.left-region.x)/MaxTileEdge;
                const int lastColumn=(bounds.right-region.x)/MaxTileEdge;
                const int firstRow=(bounds.top-region.y)/MaxTileEdge;
                const int lastRow=(bounds.bottom-region.y)/MaxTileEdge;
                for (int row=firstRow;row<=lastRow;++row) {
                    for (int column=firstColumn;column<=lastColumn;++column) {
                        if (stopped(cancel)) return false;
                        const std::size_t index=std::size_t(row)*columns+column;
                        auto &tile=ready.tiles[index];
                        const auto area=std::size_t(std::min(bounds.right,tile.rect.x+tile.rect.width-1)
                                                       -std::max(bounds.left,tile.rect.x)+1)
                                       *std::size_t(std::min(bounds.bottom,tile.rect.y+tile.rect.height-1)
                                                       -std::max(bounds.top,tile.rect.y)+1);
                        if (area>MaxDabPixelVisits-tile.candidateVisits
                            || area>MaxFrameDabPixelVisits-ready.candidateVisits) return false;
                        tile.candidateVisits+=area;
                        ready.candidateVisits+=area;
                        ++prepared->references;
                        if (prepared->indexed) {
                            if (prepared->references>indexReferenceBudget) {
                                // Release all index capacity and keep planning
                                // exact budgets for the unchanged scan fallback.
                                prepared->indexed=false;
                                std::vector<std::vector<FramePlan::DabReference>>().swap(prepared->tileDabs);
                            } else {
                                prepared->tileDabs[index].push_back({std::uint16_t(li),
                                                                   std::uint16_t(si),std::uint16_t(di)});
                            }
                        }
                    }
                }
            }
        }
    }
    if (stopped(cancel)) return false;
    prepared->tiles=ready.tiles;prepared->visits=ready.candidateVisits;
    ready.prepared_=std::move(prepared);
    *out=std::move(ready);
    return true;
}

// Existing tools retain the original 2 Mi-pixel research boundary. A larger
// internal plan is usable only through an owning admitted render request.
inline bool planFrame(const BrushMaskDocument::Document &document,int width,int height,
                      Rect region,FramePlan *out,const std::atomic_bool *cancel=nullptr,
                      NormalizedFrame frame={},std::size_t indexReferenceBudget=MaxCandidateIndexReferences) {
    return detail::planFrameBounded(document,width,height,region,out,cancel,frame,
                                    indexReferenceBudget,MaxResearchFramePixels);
}

// Execute a request-local plan. The public summary cannot forge trusted
// geometry/work counts: compare it with the immutable private snapshot before
// touching output. The original document API remains the unindexed oracle.
inline bool exposureOnly(const FramePlan &plan,std::size_t tileIndex,
                         ExposureOutput *out,const std::atomic_bool *cancel=nullptr) {
    if (!out || stopped(cancel) || !plan.prepared_) return false;
    const auto &prepared=*plan.prepared_;
    if (plan.tiles.size()!=prepared.tiles.size() || tileIndex>=prepared.tiles.size()
        || plan.candidateVisits!=prepared.visits) return false;
    const auto &tile=prepared.tiles[tileIndex];
    const auto &summary=plan.tiles[tileIndex];
    if (summary.rect.x!=tile.rect.x || summary.rect.y!=tile.rect.y
        || summary.rect.width!=tile.rect.width || summary.rect.height!=tile.rect.height
        || summary.candidateVisits!=tile.candidateVisits) return false;
    detail::Coverage coverage;
    if (!prepared.indexed) {
        if (!detail::calculateCoverage(prepared.document,prepared.width,prepared.height,
                                      tile.rect,&coverage,cancel,prepared.frame)) return false;
    } else {
        const std::size_t count=std::size_t(tile.rect.width)*tile.rect.height;
        coverage.stops.assign(count,0.0);
        const auto &refs=prepared.tileDabs[tileIndex];
        // Empty strokes/layers have identity contribution. Keep the original
        // max/union/add ordering for every contributing group and dab.
        if (!refs.empty()) {
            std::vector<double> layerAlpha(count),strokeAlpha(count);
            std::size_t i=0;
            while (i<refs.size()) {
                if (stopped(cancel)) return false;
                const auto layerIndex=refs[i].layer;
                const auto &layer=prepared.document.layers[layerIndex];
                std::fill(layerAlpha.begin(),layerAlpha.end(),0.0);
                while (i<refs.size() && refs[i].layer==layerIndex) {
                    if (stopped(cancel)) return false;
                    const auto strokeIndex=refs[i].stroke;
                    const auto &stroke=layer.strokes[strokeIndex];
                    std::fill(strokeAlpha.begin(),strokeAlpha.end(),0.0);
                    do {
                        if (stopped(cancel)) return false;
                        const auto &dab=stroke.dabs[refs[i].dab];
                        const auto bounds=detail::candidateBounds(dab,stroke.style,
                            prepared.width,prepared.height,tile.rect,prepared.frame);
                        if (!detail::accumulateDab(bounds,stroke.style,dab.pressure,tile.rect,
                                                 strokeAlpha,coverage.visitedDabPixels,cancel)) return false;
                        ++i;
                    } while (i<refs.size() && refs[i].layer==layerIndex && refs[i].stroke==strokeIndex);
                    for (std::size_t p=0;p<count;++p)
                        layerAlpha[p]=1-(1-layerAlpha[p])*(1-strokeAlpha[p]);
                }
                for (std::size_t p=0;p<count;++p)
                    coverage.stops[p]+=layer.exposureEv*layerAlpha[p];
            }
        }
    }
    if (coverage.visitedDabPixels!=tile.candidateVisits || stopped(cancel)) return false;
    ExposureOutput ready;
    ready.visitedDabPixels=coverage.visitedDabPixels;
    ready.weightedExposureEv.reserve(coverage.stops.size());
    for (const double value:coverage.stops) {
        if (stopped(cancel) || !std::isfinite(value)
            || std::abs(value)>double(std::numeric_limits<float>::max())) return false;
        ready.weightedExposureEv.push_back(float(value));
    }
    if (stopped(cancel)) return false;
    *out=std::move(ready);
    return true;
}

// Only computes scalar exposure weights, with no pixel/alpha input and no
// RGBA scratch output. Preserve *out exactly on malformed frame, cancellation,
// oversized candidate work, or an invalid document.
inline bool exposureOnly(const BrushMaskDocument::Document &document,
                         int frameWidth,int frameHeight,Rect tile,
                         ExposureOutput *out,const std::atomic_bool *cancel=nullptr,
                         NormalizedFrame frame={}) {
    if (!out) return false;
    detail::Coverage coverage;
    if (!detail::calculateCoverage(document,frameWidth,frameHeight,tile,
                                   &coverage,cancel,frame)) return false;
    ExposureOutput ready;
    ready.visitedDabPixels=coverage.visitedDabPixels;
    ready.weightedExposureEv.reserve(coverage.stops.size());
    for (double value : coverage.stops) {
        if (stopped(cancel) || !std::isfinite(value)
            || std::abs(value)>double(std::numeric_limits<float>::max())) return false;
        ready.weightedExposureEv.push_back(float(value));
    }
    if (stopped(cancel)) return false;
    *out=std::move(ready);
    return true;
}

// Legacy M4.2 RGBA oracle retains double-precision gain and full input
// validation, now sharing the exact coverage core with exposureOnly.
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
    detail::Coverage coverage;
    if (!detail::calculateCoverage(document,frameWidth,frameHeight,tile,
                                   &coverage,cancel,frame)) return false;
    Output generated;
    generated.weightedExposureEv.resize(count);
    generated.rgba32f.assign(rgbaInput.begin(),rgbaInput.end());
    generated.visitedDabPixels=coverage.visitedDabPixels;
    for (std::size_t i=0;i<count;++i) {
        if (stopped(cancel)) return false;
        const double gain=std::exp2(coverage.stops[i]);
        if (!std::isfinite(gain)) return false;
        generated.weightedExposureEv[i]=float(coverage.stops[i]);
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
