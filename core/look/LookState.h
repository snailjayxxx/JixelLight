#pragma once
#include "core/look/LookLut.h"
#include <QJsonObject>
#include <QMap>
#include <cmath>
#include <algorithm>

struct LookState {
    QString mode="off"; // off | as-shot | manual | calibrated
    QString code;
    double strength=1;
    QMap<QString,double> parameters;
    std::shared_ptr<const LookLut> lut;
    QString error;
    QJsonObject toJson() const {
        QJsonObject p;for(auto i=parameters.cbegin();i!=parameters.cend();++i)p[i.key()]=i.value();
        QJsonObject j{{"schema",1},{"mode",mode},{"code",code},{"strength",strength},{"parameters",p}};
        if(lut)j["lut"]=lut->toJson();return j;
    }
    static LookState fromJson(const QJsonObject &j) {
        LookState s;
        if(j.isEmpty())return s; // Old projects keep their original rendering.
        if(j["schema"].toInt()!=1){s.error="Unsupported look schema";return s;}
        s.mode=j["mode"].toString("off");
        if(s.mode!="off"&&s.mode!="as-shot"&&s.mode!="manual"&&s.mode!="calibrated") {s.mode="off";s.error="Unknown look mode";}
        s.code=j["code"].toString().left(32);
        const double amount=j["strength"].toDouble(1);s.strength=std::isfinite(amount)?std::clamp(amount,0.0,1.0):1;
        const auto p=j["parameters"].toObject();
        for(auto i=p.begin();i!=p.end();++i)if(i.value().isDouble()&&std::isfinite(i.value().toDouble()))s.parameters[i.key()]=i.value().toDouble();
        if(j.contains("lut")){s.lut=LookLut::fromJson(j["lut"].toObject(),&s.error);if(!s.lut)s.mode="off";}
        if(s.mode=="calibrated"&&!s.lut){s.mode="off";s.error="Missing calibrated LUT";}
        return s;
    }
};
