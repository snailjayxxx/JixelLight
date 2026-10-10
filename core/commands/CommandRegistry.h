#pragma once
#include "core/pipeline/AdjustmentState.h"
#include <QJsonArray>
#include <QStringList>
#include <algorithm>
#include <cmath>

// UI and offline CLI share parameter names, finite checks and existing ranges.
struct CommandRegistry {
    struct Parameter { const char *name; double AdjustmentState::*member; double minimum, maximum; };
    struct GeometryParameter { const char *name; double GeometryState::*member; double minimum, maximum; };
    static const auto &geometryParameters() {
        static const std::array<GeometryParameter,5> descriptors{{
            {"perspectiveHorizontal",&GeometryState::perspectiveHorizontal,-.4,.4},
            {"perspectiveVertical",&GeometryState::perspectiveVertical,-.4,.4},
            {"distortion",&GeometryState::distortion,-.3,.3},
            {"redCa",&GeometryState::redCa,-2,2},
            {"blueCa",&GeometryState::blueCa,-2,2}
        }};
        return descriptors;
    }
    static const auto &parameters() {
        static const std::array<Parameter,15> descriptors{{
            {"exposure",&AdjustmentState::exposure,-5,5},
            {"temperature",&AdjustmentState::temperature,-180,180},
            {"tint",&AdjustmentState::tint,-180,180},
            {"contrast",&AdjustmentState::contrast,-180,180},
            {"highlights",&AdjustmentState::highlights,-180,180},
            {"shadows",&AdjustmentState::shadows,-180,180},
            {"whites",&AdjustmentState::whites,-180,180},
            {"blacks",&AdjustmentState::blacks,-180,180},
            {"highlightRecovery",&AdjustmentState::highlightRecovery,0,100},
            {"hue",&AdjustmentState::hue,-180,180},
            {"saturation",&AdjustmentState::saturation,-100,100},
            {"vibrance",&AdjustmentState::vibrance,-100,100},
            {"vignetteAmount",&AdjustmentState::vignetteAmount,-3,3},
            {"vignetteMidpoint",&AdjustmentState::vignetteMidpoint,0,.95},
            {"vignetteFeather",&AdjustmentState::vignetteFeather,.01,1}
        }};
        return descriptors;
    }
    static bool set(AdjustmentState &state, const QString &name, double value, QString *error = nullptr) {
        if (error) error->clear();
        if (!std::isfinite(value)) { if(error)*error="Value must be finite"; return false; }
        for (const auto &parameter : parameters()) if (name == QLatin1String(parameter.name)) {
            state.*parameter.member = std::clamp(value,parameter.minimum,parameter.maximum);
            return true;
        }
        if(error)*error="Unknown develop parameter: " + name;
        return false;
    }
    static bool fields(const QJsonObject &command, const QStringList &names) {
        if (command.size() != names.size()+1 || !command.value("command").isString()) return false;
        for (const auto &name : names) if (!command.contains(name)) return false;
        return true;
    }
    static bool number(const QJsonObject &command, const char *key) {
        const auto value = command.value(QLatin1String(key));
        return value.isDouble() && std::isfinite(value.toDouble());
    }
    static bool integer(const QJsonObject &command, const char *key, double minimum, double maximum) {
        if (!number(command,key)) return false;
        const auto value = command.value(QLatin1String(key)).toDouble();
        return value >= minimum && value <= maximum && value == std::floor(value);
    }
    static bool execute(AdjustmentState &state, const QJsonObject &command, QString *error = nullptr) {
        if (error) error->clear();
        const auto name = command.value("command").toString();
        auto fail = [&] { if (error) *error = "Invalid or unsupported command: " + name; return false; };
        if (name == "develop.set") {
            if (!fields(command,{"parameter","value"}) || !command["parameter"].isString() || !number(command,"value")) return fail();
            return set(state,command["parameter"].toString(),command["value"].toDouble(),error);
        }
        if (name == "develop.reset") {
            if (!fields(command,{})) return fail(); state = {}; return true;
        }
        if (name == "vignette.reset") {
            if (!fields(command,{})) return fail();
            state.vignetteAmount=0; state.vignetteMidpoint=.5; state.vignetteFeather=1; return true;
        }
        if (name == "bw.enable") {
            if (!fields(command,{"enabled"}) || !command["enabled"].isBool()) return fail();
            state.blackWhite=command["enabled"].toBool(); return true;
        }
        if (name == "bw.set") {
            if (!fields(command,{"band","value"}) || !integer(command,"band",0,7)
                || !number(command,"value")) return fail();
            state.bwMix[std::size_t(command["band"].toInt())]=std::clamp(command["value"].toDouble(),-100.0,100.0);
            return true;
        }
        if (name == "bw.reset") {
            if (!fields(command,{})) return fail();
            state.blackWhite=false; state.bwMix={}; return true;
        }
        if (name == "geometry.crop") {
            if (!fields(command,{"x","y","width","height"}) || !number(command,"x") || !number(command,"y")
                || !number(command,"width") || !number(command,"height")) return fail();
            const auto x=command["x"].toDouble(), y=command["y"].toDouble(), w=command["width"].toDouble(), h=command["height"].toDouble();
            if (!GeometryState::validCrop(x,y,w,h)) return fail(); state.geometry.crop={x,y,w,h}; return true;
        }
        if (name == "geometry.rotate") {
            if (!fields(command,{"quarterTurns"}) || !integer(command,"quarterTurns",-2147483648.0,2147483647.0)) return fail();
            const int turns=command["quarterTurns"].toInt(); state.geometry.quarterTurns=((state.geometry.quarterTurns+turns%4)%4+4)%4; return true;
        }
        if (name == "geometry.straighten") {
            if (!fields(command,{"degrees"}) || !number(command,"degrees") || !GeometryState::validStraighten(command["degrees"].toDouble())) return fail();
            state.geometry.straighten=command["degrees"].toDouble(); return true;
        }
        if (name == "geometry.set") {
            if (!fields(command,{"parameter","value"}) || !command["parameter"].isString() || !number(command,"value")) return fail();
            const auto parameter=command["parameter"].toString(); const double value=command["value"].toDouble();
            if (!GeometryState::validCorrection(parameter,value)) return fail();
            for (const auto &p : geometryParameters()) if (parameter==QLatin1String(p.name)) { state.geometry.*p.member=value; return true; }
            return fail();
        }
        if (name == "geometry.resetCorrections") {
            if (!fields(command,{})) return fail();
            for (const auto &p : geometryParameters()) state.geometry.*p.member=0;
            return true;
        }
        if (name == "geometry.flip") {
            if (!fields(command,{"axis"}) || !command["axis"].isString()) return fail();
            const auto axis=command["axis"].toString();
            if (axis == "horizontal") state.geometry.flipHorizontal = !state.geometry.flipHorizontal;
            else if (axis == "vertical") state.geometry.flipVertical = !state.geometry.flipVertical;
            else return fail(); return true;
        }
        if (name == "geometry.reset") {
            if (!fields(command,{})) return fail(); state.geometry={}; return true;
        }
        if (name == "hsl.set") {
            if (!fields(command,{"band","component","value"}) || !integer(command,"band",0,7)
                || !command["component"].isString() || !number(command,"value")) return fail();
            const auto component=command["component"].toString();
            auto *values = component == "hue" ? &state.hslHue : component == "saturation" ? &state.hslSaturation : component == "luminance" ? &state.hslLuminance : nullptr;
            if (!values) return fail(); (*values)[command["band"].toInt()]=std::clamp(command["value"].toDouble(),-100.0,100.0); return true;
        }
        if (name == "curve.set" || name == "curve.reset") {
            if (!fields(command,name == "curve.set" ? QStringList{"channel","point","value"} : QStringList{"channel"}) || !command["channel"].isString()) return fail();
            const auto channel=command["channel"].toString();
            auto *curve=channel == "master" ? &state.masterCurve : channel == "red" ? &state.redCurve : channel == "green" ? &state.greenCurve : channel == "blue" ? &state.blueCurve : nullptr;
            if (!curve) return fail();
            if (name == "curve.reset") { *curve=AdjustmentState::CurveArray{0,.25,.5,.75,1}; return true; }
            if (!integer(command,"point",0,4) || !number(command,"value")) return fail();
            (*curve)[command["point"].toInt()]=std::clamp(command["value"].toDouble(),0.0,1.0); return true;
        }
        return fail();
    }
    static QJsonObject schema() {
        QJsonArray descriptors,geometryDescriptors;
        for (const auto &p : parameters()) descriptors.append(QJsonObject{{"name",p.name},{"minimum",p.minimum},{"maximum",p.maximum}});
        for (const auto &p : geometryParameters()) geometryDescriptors.append(QJsonObject{{"name",p.name},{"minimum",p.minimum},{"maximum",p.maximum}});
        const QJsonArray commands{
            QJsonObject{{"name","develop.set"},{"fields",QJsonArray{"parameter","value"}}},
            QJsonObject{{"name","develop.reset"},{"fields",QJsonArray{}}},
            QJsonObject{{"name","vignette.reset"},{"fields",QJsonArray{}},{"operation","reset post-crop vignette amount/midpoint/feather; preserve other edits"}},
            QJsonObject{{"name","bw.enable"},{"fields",QJsonArray{"enabled"}},{"operation","toggle monochrome without discarding 8 color-channel luminance values"}},
            QJsonObject{{"name","bw.set"},{"fields",QJsonArray{"band","value"}},{"band",QJsonArray{0,7}},{"value",QJsonArray{-100,100}},{"range_policy","clamp"},{"unit","1/100 EV"}},
            QJsonObject{{"name","bw.reset"},{"fields",QJsonArray{}},{"operation","disable monochrome and reset all 8 channel mixes"}},
            QJsonObject{{"name","geometry.crop"},{"fields",QJsonArray{"x","y","width","height"}},{"coordinates","normalized corrected grid after lens/perspective/straighten, before quarter-turns and flips"}},
            QJsonObject{{"name","geometry.straighten"},{"fields",QJsonArray{"degrees"}},{"degrees",QJsonArray{-45,45}},{"operation","absolute clockwise angle before quarter-turns/flips; inscribed crop, no upscale; out-of-range rejected"}},
            QJsonObject{{"name","geometry.set"},{"fields",QJsonArray{"parameter","value"}},{"parameters",geometryDescriptors},{"range_policy","reject"},
                {"operation","manual lens/CA -> perspective -> straighten; one CPU resample, auto trim, dimensions never enlarged"}},
            QJsonObject{{"name","geometry.resetCorrections"},{"fields",QJsonArray{}},{"operation","reset manual perspective/lens/CA; preserve straighten/crop/orientation"}},
            QJsonObject{{"name","geometry.rotate"},{"fields",QJsonArray{"quarterTurns"}},{"operation","relative clockwise quarter turns, int32"}},
            QJsonObject{{"name","geometry.flip"},{"fields",QJsonArray{"axis"}},{"axis",QJsonArray{"horizontal","vertical"}},{"operation","toggle after rotation"}},
            QJsonObject{{"name","geometry.reset"},{"fields",QJsonArray{}}},
            QJsonObject{{"name","hsl.set"},{"fields",QJsonArray{"band","component","value"}},{"band",QJsonArray{0,7}},{"component",QJsonArray{"hue","saturation","luminance"}},{"value",QJsonArray{-100,100}}},
            QJsonObject{{"name","curve.set"},{"fields",QJsonArray{"channel","point","value"}},{"channel",QJsonArray{"master","red","green","blue"}},{"point",QJsonArray{0,4}},{"value",QJsonArray{0,1}}},
            QJsonObject{{"name","curve.reset"},{"fields",QJsonArray{"channel"}},{"channel",QJsonArray{"master","red","green","blue"}}}
        };
        return {{"schema",1},{"command","develop.set"},{"parameters",descriptors},{"commands",commands},{"unknown_fields","reject"},{"range_policy","clamp"}};
    }
};
