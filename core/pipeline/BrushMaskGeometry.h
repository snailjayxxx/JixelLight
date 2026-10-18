#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// F4 M4.0: CPU-only, opt-in reference geometry for future manual brush masks.
// All coordinates are normalized to the complete post-geometry/crop image.
// Radius uses fractions of the SHORT image dimension so dabs stay round in
// pixel space on both portrait and landscape frames. No image I/O or Qt state.
namespace BrushMaskGeometry {
constexpr std::size_t MaxDabsPerStroke = 4096;
constexpr std::size_t MaxStrokes = 64;

struct Point { double x = 0, y = 0; };
struct Dab { Point center; double pressure = 1; };
struct Style { double radius = 0.05, hardness = 0.6, opacity = 1; };
struct Stroke { Style style; std::vector<Dab> dabs; };
struct Bounds { double left = 0, top = 0, right = 0, bottom = 0; };

inline bool finite(double value) { return std::isfinite(value); }
inline bool validFrame(int width, int height) {
    return width > 0 && height > 0 && width <= 131072 && height <= 131072;
}
inline bool validPoint(Point point) {
    return finite(point.x) && finite(point.y) && point.x >= 0 && point.x <= 1
        && point.y >= 0 && point.y <= 1;
}
inline bool validDab(Dab dab) {
    return validPoint(dab.center) && finite(dab.pressure)
        && dab.pressure >= 0 && dab.pressure <= 1;
}
inline bool validStyle(Style style) {
    return finite(style.radius) && finite(style.hardness) && finite(style.opacity)
        && style.radius >= .002 && style.radius <= .5
        && style.hardness >= 0 && style.hardness <= 1
        && style.opacity >= 0 && style.opacity <= 1;
}
inline bool validStroke(const Stroke &stroke) {
    return validStyle(stroke.style) && !stroke.dabs.empty()
        && stroke.dabs.size() <= MaxDabsPerStroke
        && std::all_of(stroke.dabs.begin(), stroke.dabs.end(), validDab);
}
inline double shortEdgeDistance(Point first, Point second, int width, int height) {
    if (!validFrame(width, height) || !validPoint(first) || !validPoint(second))
        return std::numeric_limits<double>::quiet_NaN();
    const double shortEdge = static_cast<double>(std::min(width,height));
    return std::hypot((first.x-second.x)*width/shortEdge,
                      (first.y-second.y)*height/shortEdge);
}

// Accepted strokes only; a conservative normalized rectangle for future
// tile-level prefiltering. Pressure only reduces radius, never enlarges it.
inline std::optional<Bounds> bounds(const Stroke &stroke, int width, int height) {
    if (!validStroke(stroke) || !validFrame(width,height)) return std::nullopt;
    const double shortEdge = static_cast<double>(std::min(width,height));
    const double rx = stroke.style.radius*shortEdge/width;
    const double ry = stroke.style.radius*shortEdge/height;
    Bounds result{1,1,0,0};
    for (const auto &dab : stroke.dabs) {
        const double xrad=rx*dab.pressure, yrad=ry*dab.pressure;
        result.left=std::min(result.left,std::max(0.0,dab.center.x-xrad));
        result.top=std::min(result.top,std::max(0.0,dab.center.y-yrad));
        result.right=std::max(result.right,std::min(1.0,dab.center.x+xrad));
        result.bottom=std::max(result.bottom,std::min(1.0,dab.center.y+yrad));
    }
    return result;
}

// Stable CPU oracle, NOT yet connected to the live RAW/GPU renderer.
// Adjacent dabs in one stroke use maximum alpha (never density-dependent
// overpainting); separately committed strokes combine by source-over union.
inline std::optional<double> strokeOpacity(const Stroke &stroke, Point pixel,
                                          int width, int height) {
    if (!validPoint(pixel)) return std::nullopt;
    const auto box=bounds(stroke,width,height);
    if (!box) return std::nullopt;
    if (pixel.x<box->left || pixel.x>box->right ||
        pixel.y<box->top || pixel.y>box->bottom) return 0.0;
    double result=0;
    for (const auto &dab : stroke.dabs) {
        if (dab.pressure == 0) continue;
        const double distance=shortEdgeDistance(pixel,dab.center,width,height);
        const double radius=stroke.style.radius*dab.pressure;
        if (distance>=radius) continue;
        const double radial=distance/radius;
        double edge=1;
        if (radial>stroke.style.hardness && stroke.style.hardness<1) {
            const double t=(radial-stroke.style.hardness)/(1-stroke.style.hardness);
            edge=1-t*t*(3-2*t);
        }
        result=std::max(result,stroke.style.opacity*dab.pressure*edge);
    }
    return std::clamp(result,0.0,1.0);
}
inline std::optional<double> combinedOpacity(std::span<const Stroke> strokes, Point pixel,
                                            int width, int height) {
    if (strokes.size()>MaxStrokes || !validFrame(width,height) || !validPoint(pixel))
        return std::nullopt;
    double result=0;
    for (const auto &stroke : strokes) {
        const auto value=strokeOpacity(stroke,pixel,width,height);
        if (!value) return std::nullopt;
        result=1-(1-result)*(1-*value);
    }
    return std::clamp(result,0.0,1.0);
}

// Resample mouse/tablet events into fixed short-edge spacing, independent of
// event frequency for the same piecewise-linear path and pressure curve.
// Refuse invalid paths or an oversized stroke rather than returning a partial
// result for serialization. No persistent input is modified by this builder.
class StrokeBuilder {
public:
    StrokeBuilder(int width, int height, Style style)
        : width_(width),height_(height),style_(style),spacing_(style.radius*.25),
          remaining_(spacing_),invalid_(!validFrame(width,height)||!validStyle(style)) {}

    bool append(Dab value) {
        if (invalid_ || finished_) return false;
        if (!validDab(value)) { invalidate(); return false; }
        if (!started_) {
            stamps_.push_back(value);
            previous_=value;
            started_=true;
            return true;
        }
        const double distance=shortEdgeDistance(previous_.center,value.center,width_,height_);
        if (!finite(distance)) { invalidate(); return false; }
        if (distance==0) { previous_=value; return true; }
        // Guard against pathological path lengths before any potentially huge loop.
        if (distance/spacing_>static_cast<double>(MaxDabsPerStroke)+2) {
            invalidate(); return false;
        }
        const Dab start=previous_;
        double traversed=0;
        double toGo=distance;
        const double epsilon=spacing_*1e-9;
        while (toGo+epsilon>=remaining_) {
            if (stamps_.size()>=MaxDabsPerStroke) { invalidate(); return false; }
            traversed+=remaining_;
            const double t=std::clamp(traversed/distance,0.0,1.0);
            stamps_.push_back({{start.center.x+(value.center.x-start.center.x)*t,
                                start.center.y+(value.center.y-start.center.y)*t},
                               start.pressure+(value.pressure-start.pressure)*t});
            toGo=std::max(0.0,distance-traversed);
            remaining_=spacing_;
        }
        remaining_=std::clamp(remaining_-toGo,epsilon,spacing_);
        previous_=value;
        return true;
    }
    std::optional<Stroke> finish() {
        if (invalid_ || finished_ || !started_) return std::nullopt;
        finished_=true;
        const Dab last=stamps_.back();
        const double remainder=shortEdgeDistance(last.center,previous_.center,width_,height_);
        if (remainder>spacing_*.1 || std::abs(last.pressure-previous_.pressure)>1e-9) {
            if (stamps_.size()>=MaxDabsPerStroke) { invalidate(); return std::nullopt; }
            stamps_.push_back(previous_);
        }
        Stroke stroke{style_,std::move(stamps_)};
        if (!validStroke(stroke)) return std::nullopt;
        return stroke;
    }
    bool invalid() const { return invalid_; }
private:
    void invalidate() { invalid_=true; stamps_.clear(); }
    int width_=0,height_=0;
    Style style_;
    double spacing_=0,remaining_=0;
    bool invalid_=false,started_=false,finished_=false;
    Dab previous_;
    std::vector<Dab> stamps_;
};
}
