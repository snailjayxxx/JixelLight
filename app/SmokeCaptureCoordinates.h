#pragma once
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <cmath>

// A grab may be clipped to the native backing store while QWindow still
// reports a larger requested geometry. Scene coordinates use the renderer's
// device scale; stretching them by captured-size/window-size samples elsewhere.
namespace SmokeCaptureCoordinates {
inline QColor sample(const QImage &image,QPointF scene,qreal deviceScale) {
    if(image.isNull()||!std::isfinite(deviceScale)||deviceScale<=0)return {};
    const double x=scene.x()*deviceScale,y=scene.y()*deviceScale;
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=image.width()||y>=image.height())return {};
    return image.pixelColor(int(std::floor(x)),int(std::floor(y)));
}
inline QRect region(const QImage &image,QRectF scene,qreal deviceScale) {
    if(image.isNull()||!std::isfinite(deviceScale)||deviceScale<=0)return {};
    scene=scene.normalized();const double left=scene.left()*deviceScale,top=scene.top()*deviceScale;
    const double right=scene.right()*deviceScale,bottom=scene.bottom()*deviceScale;
    if(!std::isfinite(left)||!std::isfinite(top)||!std::isfinite(right)||!std::isfinite(bottom))return {};
    const auto clipped=QRectF(QPointF(left,top),QPointF(right,bottom)).intersected(QRectF(image.rect()));
    if(clipped.isEmpty())return {};
    const int x=int(std::floor(clipped.left())),y=int(std::floor(clipped.top()));
    return QRect(x,y,int(std::ceil(clipped.right()))-x,int(std::ceil(clipped.bottom()))-y).intersected(image.rect());
}
}
