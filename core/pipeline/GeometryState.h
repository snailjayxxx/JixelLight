#pragma once
#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <QTransform>
#include "core/async/LatestJob.h"
#include <algorithm>
#include <cmath>

struct GeometryState {
    // Normalized coordinates after optional straighten, before orientation.
    // At zero degrees these are the unchanged original source coordinates.
    QRectF crop{0,0,1,1};
    double straighten = 0; // Clockwise degrees before quarter-turns and flips.
    int quarterTurns = 0;
    bool flipHorizontal = false, flipVertical = false;
    QJsonObject toJson() const {
        QJsonObject json{{"schema",straighten == 0 ? 1 : 2},{"x",crop.x()},{"y",crop.y()},{"width",crop.width()},{"height",crop.height()},
                {"quarterTurns",quarterTurns},{"flipHorizontal",flipHorizontal},{"flipVertical",flipVertical}};
        if (straighten != 0) json.insert("straighten",straighten);
        return json;
    }
    static bool validStraighten(double value) { return std::isfinite(value) && value>=-45 && value<=45; }
    static bool validCrop(double x,double y,double width,double height) {
        return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(width)&&std::isfinite(height)
            && x>=0 && y>=0 && width>0 && height>0 && x+width<=1.00000001 && y+height<=1.00000001;
    }
    static GeometryState fromJson(const QJsonObject &json) {
        GeometryState state;
        if(json.isEmpty() || (json["schema"].toDouble()!=1 && json["schema"].toDouble()!=2))return state;
        if (json["schema"].toInt()==2) {
            if (!json["straighten"].isDouble() || !validStraighten(json["straighten"].toDouble())) return state;
            state.straighten=json["straighten"].toDouble();
        }
        const double x=json["x"].toDouble(),y=json["y"].toDouble(),w=json["width"].toDouble(1),h=json["height"].toDouble(1);
        if(validCrop(x,y,w,h))state.crop={x,y,w,h};
        state.quarterTurns=((json["quarterTurns"].toInt()%4)+4)%4;
        state.flipHorizontal=json["flipHorizontal"].toBool(); state.flipVertical=json["flipVertical"].toBool();
        return state;
    }
    static bool validJson(const QJsonObject &json) { return json.isEmpty() || fromJson(json).toJson()==json; }
    QSize straightenedSize(QSize source) const;
    // Crop overlays use normalized coordinates after rotation and flips, while
    // persisted crop remains in the straightened source coordinates. No resampling.
    QRectF orientedRect(QRectF rect, bool inverse = false) const {
        auto flip = [&] {
            if (flipHorizontal) rect.moveLeft(1-rect.right());
            if (flipVertical) rect.moveTop(1-rect.bottom());
        };
        if (inverse) flip();
        const int turns = inverse ? (4-quarterTurns%4)%4 : quarterTurns%4;
        for (int i=0;i<turns;++i) rect = QRectF(1-rect.bottom(),rect.x(),rect.height(),rect.width());
        if (!inverse) flip();
        // Roundoff from 1-(x+w) can be a tiny negative at a full-frame edge.
        return rect.intersected(QRectF(0,0,1,1));
    }
    QImage apply(const QImage &source, const CancelToken &cancel = {}) const;
};
