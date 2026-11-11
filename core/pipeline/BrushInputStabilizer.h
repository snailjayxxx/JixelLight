#pragma once
#include "core/pipeline/BrushMaskGeometry.h"

namespace BrushMaskGeometry {
// Explicitly selected input rule, not an approximation in the mask renderer.
// Pressure passes through exactly. The owner creates a new filter per gesture.
class PenMovementStabilizer {
public:
    PenMovementStabilizer(int width,int height,double movementPixels=0.5)
        : width_(width),height_(height),movementPixels_(movementPixels),
          invalid_(!validFrame(width,height)||!std::isfinite(movementPixels)||(movementPixels!=0.5&&movementPixels!=1.0)) {}
    std::optional<Dab> map(Dab input) {
        if (invalid_ || !validDab(input)) { invalid_=true;return std::nullopt; }
        if (!anchor_) {anchor_=input.center;return input;}
        const double distance=std::hypot((input.center.x-anchor_->x)*width_,
                                         (input.center.y-anchor_->y)*height_);
        if (!std::isfinite(distance)) {invalid_=true;return std::nullopt;}
        if (distance<=movementPixels_) input.center=*anchor_;
        else anchor_=input.center;
        return input;
    }
private:
    int width_,height_;
    double movementPixels_;
    bool invalid_;
    std::optional<Point> anchor_;
};
}
