#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <algorithm>
#include <cmath>
#include <limits>

// Map a pointer on the displayed prepared preview into the complete
// post-geometry/committed-crop image, not the original RAW sensor grid.
// Fit mode uses the full frame; zoom/pan uses a normalized full-frame ROI.
// Keep this independent of image pixels, monitor ICC and edit state.
namespace MaskCanvasGeometry {
inline bool finite(double value) { return std::isfinite(value); }

inline bool valid(const QRectF &frame, const QSizeF &display) {
    return finite(frame.x()) && finite(frame.y()) && finite(frame.width())
        && finite(frame.height()) && finite(display.width()) && finite(display.height())
        && frame.width() > 0.0 && frame.height() > 0.0
        && display.width() > 0.0 && display.height() > 0.0
        && frame.x() >= 0.0 && frame.y() >= 0.0
        && frame.right() <= 1.000001 && frame.bottom() <= 1.000001;
}

inline QPointF invalidPoint() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    return {nan, nan};
}

inline QPointF imageAt(const QPointF &position, const QSizeF &display, const QRectF &frame) {
    if (!valid(frame, display) || !finite(position.x()) || !finite(position.y()))
        return invalidPoint();
    // Do not clamp here: dragging beyond the visible ROI must still reach
    // legitimate offscreen mask centers/radii; clamp only final parameters.
    return {frame.x() + position.x() * frame.width() / display.width(),
            frame.y() + position.y() * frame.height() / display.height()};
}

inline QPointF onDisplay(const QPointF &image, const QSizeF &display, const QRectF &frame) {
    if (!valid(frame, display) || !finite(image.x()) || !finite(image.y()))
        return invalidPoint();
    return {(image.x() - frame.x()) * display.width() / frame.width(),
            (image.y() - frame.y()) * display.height() / frame.height()};
}

inline double clamp01(double value) { return std::clamp(value, 0.0, 1.0); }

// Linear mask's 0° points downward. Both coordinates are image fractions,
// not display pixels, avoiding an angle skew for portrait / wide aspect ratios.
inline double movedLinearCenter(double initialCenter, const QPointF &delta, double angleDegrees) {
    constexpr double degToRad = 3.14159265358979323846 / 180.0;
    const double radians = angleDegrees * degToRad;
    return clamp01(initialCenter + delta.x()*std::sin(radians) + delta.y()*std::cos(radians));
}

inline QPointF movedRadialCenter(const QPointF &start, const QPointF &delta) {
    return {clamp01(start.x()+delta.x()), clamp01(start.y()+delta.y())};
}
}
