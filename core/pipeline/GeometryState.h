#pragma once
#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <QTransform>
#include "core/async/LatestJob.h"
#include <algorithm>
#include <cmath>

struct GeometryState {
    // Normalized coordinates on the corrected grid, before orientation.
    // With all corrections zero these are the original source coordinates.
    QRectF crop{0,0,1,1};
    double straighten = 0; // Clockwise degrees before quarter-turns and flips.
    double perspectiveHorizontal = 0, perspectiveVertical = 0; // Projective slopes, [-.4,.4].
    double distortion = 0; // Inverse radial coefficient at the source corner, [-.3,.3].
    double redCa = 0, blueCa = 0; // Radial magnification delta in percent, [-2,2].
    int quarterTurns = 0;
    bool flipHorizontal = false, flipVertical = false;
    QJsonObject toJson() const {
        QJsonObject json{{"schema",hasCorrections() ? 3 : straighten == 0 ? 1 : 2},{"x",crop.x()},{"y",crop.y()},{"width",crop.width()},{"height",crop.height()},
                {"quarterTurns",quarterTurns},{"flipHorizontal",flipHorizontal},{"flipVertical",flipVertical}};
        if (straighten != 0 || hasCorrections()) json.insert("straighten",straighten);
        if (hasCorrections()) {
            json.insert("perspectiveHorizontal",perspectiveHorizontal); json.insert("perspectiveVertical",perspectiveVertical);
            json.insert("distortion",distortion); json.insert("redCa",redCa); json.insert("blueCa",blueCa);
        }
        return json;
    }
    static bool validStraighten(double value) { return std::isfinite(value) && value>=-45 && value<=45; }
    static bool validCorrection(const QString &parameter, double value) {
        if (!std::isfinite(value)) return false;
        if (parameter=="perspectiveHorizontal" || parameter=="perspectiveVertical") return value>=-.4 && value<=.4;
        if (parameter=="distortion") return value>=-.3 && value<=.3;
        if (parameter=="redCa" || parameter=="blueCa") return value>=-2 && value<=2;
        return false;
    }
    bool hasCorrections() const {
        return perspectiveHorizontal!=0 || perspectiveVertical!=0 || distortion!=0 || redCa!=0 || blueCa!=0;
    }
    bool validCorrections() const {
        return validCorrection("perspectiveHorizontal",perspectiveHorizontal) && validCorrection("perspectiveVertical",perspectiveVertical)
            && validCorrection("distortion",distortion) && validCorrection("redCa",redCa) && validCorrection("blueCa",blueCa);
    }
    static bool validCrop(double x,double y,double width,double height) {
        return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(width)&&std::isfinite(height)
            && x>=0 && y>=0 && width>0 && height>0 && x+width<=1.00000001 && y+height<=1.00000001;
    }
    static GeometryState fromJson(const QJsonObject &json) {
        GeometryState state;
        if(json.isEmpty() || (json["schema"].toDouble()!=1 && json["schema"].toDouble()!=2 && json["schema"].toDouble()!=3))return state;
        if (json["schema"].toInt()>=2) {
            if (!json["straighten"].isDouble() || !validStraighten(json["straighten"].toDouble())) return state;
            state.straighten=json["straighten"].toDouble();
        }
        if (json["schema"].toInt()==3) {
            for (const auto *key : {"perspectiveHorizontal","perspectiveVertical","distortion","redCa","blueCa"})
                if (!json[key].isDouble() || !validCorrection(key,json[key].toDouble())) return {};
            state.perspectiveHorizontal=json["perspectiveHorizontal"].toDouble(); state.perspectiveVertical=json["perspectiveVertical"].toDouble();
            state.distortion=json["distortion"].toDouble(); state.redCa=json["redCa"].toDouble(); state.blueCa=json["blueCa"].toDouble();
        }
        const double x=json["x"].toDouble(),y=json["y"].toDouble(),w=json["width"].toDouble(1),h=json["height"].toDouble(1);
        if(validCrop(x,y,w,h))state.crop={x,y,w,h};
        state.quarterTurns=((json["quarterTurns"].toInt()%4)+4)%4;
        state.flipHorizontal=json["flipHorizontal"].toBool(); state.flipVertical=json["flipVertical"].toBool();
        return state;
    }
    static bool validJson(const QJsonObject &json) { return json.isEmpty() || fromJson(json).toJson()==json; }
    QSize straightenedSize(QSize source) const;
    QSize correctedSize(QSize source) const;
    // Crop overlays use normalized coordinates after rotation and flips, while
    // persisted crop remains in corrected source coordinates. No resampling.
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
private:
    QImage resampleCorrections(const QImage &source, const CancelToken &cancel) const;
};
