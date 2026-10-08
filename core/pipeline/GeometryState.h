#pragma once
#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <QTransform>
#include <algorithm>
#include <cmath>

struct GeometryState {
    QRectF crop{0,0,1,1}; // Normalized original coordinates, before orientation.
    int quarterTurns = 0;
    bool flipHorizontal = false, flipVertical = false;
    QJsonObject toJson() const {
        return {{"schema",1},{"x",crop.x()},{"y",crop.y()},{"width",crop.width()},{"height",crop.height()},
                {"quarterTurns",quarterTurns},{"flipHorizontal",flipHorizontal},{"flipVertical",flipVertical}};
    }
    static bool validCrop(double x,double y,double width,double height) {
        return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(width)&&std::isfinite(height)
            && x>=0 && y>=0 && width>0 && height>0 && x+width<=1.00000001 && y+height<=1.00000001;
    }
    static GeometryState fromJson(const QJsonObject &json) {
        GeometryState state;
        if(json.isEmpty() || json["schema"].toInt()!=1)return state;
        const double x=json["x"].toDouble(),y=json["y"].toDouble(),w=json["width"].toDouble(1),h=json["height"].toDouble(1);
        if(validCrop(x,y,w,h))state.crop={x,y,w,h};
        state.quarterTurns=((json["quarterTurns"].toInt()%4)+4)%4;
        state.flipHorizontal=json["flipHorizontal"].toBool(); state.flipVertical=json["flipVertical"].toBool();
        return state;
    }
    QImage apply(const QImage &source) const {
        if(source.isNull())return {};
        QImage result=source;
        if(crop!=QRectF(0,0,1,1)) {
            if(!validCrop(crop.x(),crop.y(),crop.width(),crop.height()))return {};
            const int left=std::clamp(int(std::floor(crop.x()*source.width())),0,source.width()-1);
            const int top=std::clamp(int(std::floor(crop.y()*source.height())),0,source.height()-1);
            const int right=std::clamp(int(std::ceil(crop.right()*source.width())),left+1,source.width());
            const int bottom=std::clamp(int(std::ceil(crop.bottom()*source.height())),top+1,source.height());
            result=result.copy(left,top,right-left,bottom-top);
        }
        if(quarterTurns%4)result=result.transformed(QTransform().rotate(90*(quarterTurns%4)),Qt::FastTransformation);
        if(flipHorizontal||flipVertical)result=result.mirrored(flipHorizontal,flipVertical);
        return result;
    }
};
