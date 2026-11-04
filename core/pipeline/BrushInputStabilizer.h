#pragma once
#include "core/pipeline/BrushMaskGeometry.h"

namespace BrushMaskGeometry {
// Explicitly selected input rule, not an approximation in the mask renderer.
// Pressure passes through exactly. The owner creates a new filter per gesture.
class PenMovementStabilizer {
public:
    PenMovementStabilizer(int width,int height)
        : width_(width),height_(height),invalid_(!validFrame(width,height)) {}
    std::optional<Dab> map(Dab input) {
        if (invalid_ || !validDab(input)) { invalid_=true;return std::nullopt; }
        if (!anchor_) {anchor_=input.center;return input;}
        const double distance=std::hypot((input.center.x-anchor_->x)*width_,
                                         (input.center.y-anchor_->y)*height_);
        if (!std::isfinite(distance)) {invalid_=true;return std::nullopt;}
        if (distance<=0.5) input.center=*anchor_;
        else anchor_=input.center;
        return input;
    }
private:
    int width_,height_;
    bool invalid_;
    std::optional<Point> anchor_;
};
}
