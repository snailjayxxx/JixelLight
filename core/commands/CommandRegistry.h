#pragma once
#include "core/pipeline/AdjustmentState.h"
#include "core/look/LookProfiles.h"
#include <QJsonArray>
#include <QStringList>
#include <algorithm>
#include <cmath>

// UI and offline CLI share parameter names, finite checks and existing ranges.
struct CommandRegistry {
    // Constructed by GUI/CLI from the actual source; never accepted in command
    // JSON. A single batch uses one immutable source metadata snapshot.
    struct LookContext { bool rawSource=false; QVariantMap metadata; };
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
    static bool requiresLookContext(const AdjustmentState &state,const QJsonArray &commands) {
        bool asShot=state.look.mode=="as-shot";
        for(const auto &entry:commands) {
            const auto object=entry.toObject();
            const auto name=object.value("command").toString();
            if(name=="look.mode") {
                const auto mode=object.value("mode").toString();
                if(mode=="as_shot" || (mode=="manual" && asShot)) return true;
                if(mode=="off" || mode=="manual")asShot=false;
            } else if(name=="look.parameter" && asShot) return true;
            else if(name=="look.code" || name=="look.reset" || name=="develop.reset")asShot=false;
        }
        return false;
    }
    static bool executeBatch(AdjustmentState &state,const QJsonArray &commands,QString *error=nullptr,
                             const LookContext *context=nullptr) {
        if(error)error->clear();
        if(commands.size()>10000) {
            if(error)*error="Commands must contain at most 10000 entries";
            return false;
        }
        auto staged=state;
        for(qsizetype i=0;i<commands.size();++i) {
            QString reason;
            if(!commands[i].isObject() || !execute(staged,commands[i].toObject(),&reason,context)) {
                if(error)*error=QString("Command %1: %2").arg(i+1).arg(reason.isEmpty()?QStringLiteral("Expected a command object"):reason);
                return false;
            }
        }
        state=std::move(staged); return true;
    }
    static bool execute(AdjustmentState &state, const QJsonObject &command, QString *error = nullptr,
                        const LookContext *context = nullptr) {
        if (error) error->clear();
        const auto name = command.value("command").toString();
        auto fail = [&] { if (error) *error = "Invalid or unsupported command: " + name; return false; };
        if(name=="look.mode") {
            if(!fields(command,{"mode"}) || !command["mode"].isString())return fail();
            const auto mode=command["mode"].toString();
            if(mode!="off" && mode!="as_shot" && mode!="manual")return fail();
            auto look=state.look;
            if(mode=="off")look.mode="off";
            else if(mode=="as_shot") {
                if(!context || !context->rawSource) {
                    if(error)*error="As-shot Look requires a RAW source context";
                    return false;
                }
                look={};look.mode="as-shot";
            } else {
                if(look.mode=="as-shot") {
                    if(!context) { if(error)*error="Materializing as-shot Look requires source context"; return false; }
                    look=LookProfiles::resolveAsShot(state,context->metadata,context->rawSource).look;
                }
                look.mode=look.lut?"calibrated":"manual";
                if(!look.lut) {
                    QString normalized;
                    look.code=LookProfiles::normalizeCommandCode(look.code,&normalized)?normalized:QStringLiteral("ST");
                }
            }
            state.look=std::move(look);return true;
        }
        if(name=="look.code") {
            if(!fields(command,{"code"}) || !command["code"].isString())return fail();
            QString code;
            if(!LookProfiles::normalizeCommandCode(command["code"].toString(),&code,error))return false;
            LookState look;look.mode="manual";look.code=code;state.look=std::move(look);return true;
        }
        if(name=="look.strength") {
            if(!fields(command,{"value"}) || !number(command,"value"))return fail();
            double value=0;
            if(!LookProfiles::normalizeCommandStrength(command["value"].toDouble(),&value,error))return false;
            state.look.strength=value;return true;
        }
        if(name=="look.parameter") {
            if(!fields(command,{"parameter","value"}) || !command["parameter"].isString() || !number(command,"value"))return fail();
            const auto parameter=command["parameter"].toString();double value=0;
            if(!LookProfiles::normalizeCommandParameter(parameter,command["value"].toDouble(),&value,error))return false;
            auto look=state.look;
            if(look.mode=="as-shot") {
                if(!context) { if(error)*error="Materializing as-shot Look requires source context"; return false; }
                look=LookProfiles::resolveAsShot(state,context->metadata,context->rawSource).look;
            }
            look.mode=look.lut?"calibrated":"manual";
            if(!look.lut && look.code.isEmpty())look.code="ST";
            look.parameters[parameter]=value;
            state.look=std::move(look);return true;
        }
        if(name=="look.reset") {
            if(!fields(command,{}))return fail();state.look={};return true;
        }
        if (name == "develop.set") {
            if (!fields(command,{"parameter","value"}) || !command["parameter"].isString() || !number(command,"value")) return fail();
            return set(state,command["parameter"].toString(),command["value"].toDouble(),error);
        }
        if (name == "develop.reset") {
            if (!fields(command,{})) return fail(); state = {}; return true;
        }
        if(name=="mask.brush.replace") {
            if(!fields(command,{"document"})||!command["document"].isObject())return fail();
            const auto document=command["document"].toObject();
            BrushMaskDocument::Document parsed;
            if(!AdjustmentState::validBrushMaskJson({{"brushMasks",document}})
                ||!BrushMaskDocument::fromJson(document,&parsed))return fail();
            state.brushMasks=std::move(parsed);return true;
        }
        if(name=="mask.brush.reset") {
            if(!fields(command,{}))return fail();state.brushMasks={};return true;
        }
        if(name=="mask.brush.remove"||name=="mask.brush.remove-last-stroke"||name=="mask.brush.remove-stroke") {
            const bool indexed=name=="mask.brush.remove-stroke";
            if(!fields(command,indexed?QStringList{"layer","index"}:QStringList{"layer"})||!command["layer"].isString()
                ||(indexed&&!integer(command,"index",0,BrushMaskGeometry::MaxStrokes-1))
                ||!BrushMaskDocument::validDocument(state.brushMasks))return fail();
            auto &layers=state.brushMasks.layers;
            const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==command["layer"].toString();});
            if(found==layers.end())return fail();
            if(indexed||name=="mask.brush.remove-last-stroke") {
                const auto index=indexed?std::size_t(command["index"].toInt()):found->strokes.size()-1;
                if(index>=found->strokes.size())return fail();
                found->strokes.erase(found->strokes.begin()+index);
                if(!found->strokes.empty())return true;
            }
            layers.erase(found);return true; // Empty layers never become saved intent.
        }
        if(name=="mask.brush.enable"||name=="mask.brush.exposure") {
            const bool toggle=name=="mask.brush.enable";
            if(!fields(command,toggle?QStringList{"layer","enabled"}:QStringList{"layer","value"})
                ||!command["layer"].isString()||!BrushMaskDocument::validDocument(state.brushMasks)
                ||(toggle?!command["enabled"].isBool():!number(command,"value")))return fail();
            if(!toggle&&(command["value"].toDouble()<-3||command["value"].toDouble()>3))return fail();
            for(auto &layer:state.brushMasks.layers)if(layer.id==command["layer"].toString()) {
                if(toggle)layer.enabled=command["enabled"].toBool();else layer.exposureEv=command["value"].toDouble();
                return true;
            }
            return fail(); // Unknown identity never selects an arbitrary layer.
        }
        if (name == "mask.radial.enable") {
            if (!fields(command,{"enabled"}) || !command.value("enabled").isBool()) return fail();
            state.radialMaskEnabled=command.value("enabled").toBool(); return true;
        }
        if (name == "mask.radial.invert") {
            if (!fields(command,{"inverted"}) || !command.value("inverted").isBool()) return fail();
            state.radialMaskInverted=command.value("inverted").toBool(); return true;
        }
        if (name == "mask.radial.set") {
            if (!fields(command,{"parameter","value"}) || !command.value("parameter").isString()
                || !number(command,"value")) return fail();
            const QString key=command.value("parameter").toString();
            const double value=command.value("value").toDouble();
            double *field=nullptr,minimum=0,maximum=0;
            if (key=="exposure") { field=&state.radialMaskExposure; minimum=-3; maximum=3; }
            else if (key=="centerX") { field=&state.radialMaskCenterX; minimum=0; maximum=1; }
            else if (key=="centerY") { field=&state.radialMaskCenterY; minimum=0; maximum=1; }
            else if (key=="radiusX") { field=&state.radialMaskRadiusX; minimum=.05; maximum=1; }
            else if (key=="radiusY") { field=&state.radialMaskRadiusY; minimum=.05; maximum=1; }
            else if (key=="feather") { field=&state.radialMaskFeather; minimum=.02; maximum=1; }
            else if (key=="angle") { field=&state.radialMaskAngle; minimum=-180; maximum=180; }
            if (!field || value<minimum || value>maximum) return fail();
            *field=value; return true;
        }
        if (name == "mask.radial.reset") {
            if (!fields(command,{})) return fail();
            state.radialMaskEnabled=false;state.radialMaskInverted=false;state.radialMaskExposure=0;
            state.radialMaskCenterX=.5;state.radialMaskCenterY=.5;
            state.radialMaskRadiusX=.35;state.radialMaskRadiusY=.35;state.radialMaskFeather=.25;
            state.radialMaskAngle=0;
            return true;
        }
        if (name == "mask.linear.enable") {
            if (!fields(command,{"enabled"}) || !command.value("enabled").isBool()) return fail();
            state.linearMaskEnabled=command.value("enabled").toBool();
            return true;
        }
        if (name == "mask.linear.set") {
            if (!fields(command,{"parameter","value"}) || !command.value("parameter").isString()
                || !number(command,"value")) return fail();
            const QString key=command.value("parameter").toString();
            const double value=command.value("value").toDouble();
            double *field=nullptr, minimum=0, maximum=0;
            if (key=="exposure") { field=&state.linearMaskExposure; minimum=-3; maximum=3; }
            else if (key=="center") { field=&state.linearMaskCenter; minimum=0; maximum=1; }
            else if (key=="feather") { field=&state.linearMaskFeather; minimum=.02; maximum=1; }
            else if (key=="angle") { field=&state.linearMaskAngle; minimum=-180; maximum=180; }
            if (!field || value<minimum || value>maximum) return fail();
            *field=value; return true;
        }
        if (name == "mask.linear.reset") {
            if (!fields(command,{})) return fail();
            state.linearMaskEnabled=false; state.linearMaskExposure=0;
            state.linearMaskCenter=.5; state.linearMaskFeather=.25; state.linearMaskAngle=0;
            return true;
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
        if (name == "grading.enable") {
            if (!fields(command,{"enabled"}) || !command["enabled"].isBool()) return fail();
            state.gradingEnabled=command["enabled"].toBool(); return true;
        }
        if (name == "grading.set") {
            if (!fields(command,{"region","component","value"}) || !integer(command,"region",0,2)
                || !command["component"].isString() || !number(command,"value")) return fail();
            const auto component=command["component"].toString();
            auto *values=component=="hue"?&state.gradingHue:component=="saturation"?&state.gradingSaturation:nullptr;
            if (!values) return fail();
            const double upper=component=="hue"?360.0:100.0;
            (*values)[std::size_t(command["region"].toInt())]=std::clamp(command["value"].toDouble(),0.0,upper);
            return true;
        }
        if (name == "grading.balance") {
            if (!fields(command,{"value"}) || !number(command,"value")) return fail();
            state.gradingBalance=std::clamp(command["value"].toDouble(),-100.0,100.0); return true;
        }
        if (name == "grading.reset") {
            if (!fields(command,{})) return fail();
            state.gradingEnabled=false; state.gradingHue={}; state.gradingSaturation={};
            state.gradingBalance=0; return true;
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
            QJsonObject{{"name","look.mode"},{"fields",QJsonArray{"mode"}},
                {"mode",QJsonArray{"off","as_shot","manual"}},{"stored_as_shot_mode","as-shot"},
                {"source_context","RAW-only as_shot; source-specific metadata required to materialize as-shot into manual"},
                {"operation","off retains controls/LUT; as_shot resets Look intent; manual preserves controls and any existing validated LUT"}},
            QJsonObject{{"name","look.code"},{"fields",QJsonArray{"code"}},
                {"codes",QJsonArray::fromStringList(LookProfiles::commandCodes())},
                {"code_aliases",QJsonArray{"STANDARD","PORTRAIT","NEUTRAL","VIVID","VIVID 2","B&W","BLACK & WHITE","SEPIA"}},
                {"normalization","trim and uppercase supported codes/aliases; reject control characters and inputs longer than 32"},
                {"operation","fresh manual approximation, strength 1, clear overrides and existing LUT"}},
            QJsonObject{{"name","look.strength"},{"fields",QJsonArray{"value"}},
                {"range",QJsonArray{0,1}},{"range_policy","clamp"},{"precision","finite fractional double; no integer rounding"}},
            QJsonObject{{"name","look.parameter"},{"fields",QJsonArray{"parameter","value"}},
                {"parameters",QJsonArray::fromVariantList(LookProfiles::commandParameters())},
                {"value_policy","round_then_clamp"},{"rounding","nearest integer; half ties away from zero"}},
            QJsonObject{{"name","look.reset"},{"fields",QJsonArray{}},{"operation","reset Look only; preserve all other edits"}},
            QJsonObject{{"name","develop.set"},{"fields",QJsonArray{"parameter","value"}}},
            QJsonObject{{"name","develop.reset"},{"fields",QJsonArray{}}},
            QJsonObject{{"name","mask.brush.replace"},{"fields",QJsonArray{"document"}},
                {"schema",BrushMaskDocument::Schema},{"supported_schemas",QJsonArray{BrushMaskDocument::Schema,BrushMaskDocument::EraserSchema}},
                {"schema_policy","paint-only schema1; schema2 requires an erase stroke and explicit paint/erase mode on every stroke"},
                {"stroke_modes",QJsonArray{"paint","erase"}},{"maximum_layers",int(BrushMaskDocument::MaxLayers)},
                {"maximum_dabs",int(BrushMaskDocument::MaxTotalDabs)},
                {"operation","replace canonical brush document atomically; enabled effects use CPU-reference admission"}},
            QJsonObject{{"name","mask.brush.enable"},{"fields",QJsonArray{"layer","enabled"}},
                {"identity","exact existing stable layer id"}},
            QJsonObject{{"name","mask.brush.exposure"},{"fields",QJsonArray{"layer","value"}},
                {"range",QJsonArray{-3,3}},{"range_policy","reject"}},
            QJsonObject{{"name","mask.brush.remove"},{"fields",QJsonArray{"layer"}},
                {"identity","exact existing stable layer id"},{"operation","remove only the selected brush layer"}},
            QJsonObject{{"name","mask.brush.remove-last-stroke"},{"fields",QJsonArray{"layer"}},
                {"identity","exact existing stable layer id"},{"operation","remove the selected layer's last stroke; remove its layer when empty"}},
            QJsonObject{{"name","mask.brush.remove-stroke"},{"fields",QJsonArray{"layer","index"}},
                {"identity","exact existing stable layer id"},{"index_base",0},{"index_policy","reject non-integer or missing stroke"},
                {"operation","remove only the indexed whole stroke; remove its layer when empty"}},
            QJsonObject{{"name","mask.brush.reset"},{"fields",QJsonArray{}},
                {"operation","remove brush layers only; preserve gradients and other edits"}},
            QJsonObject{{"name","mask.radial.enable"},{"fields",QJsonArray{"enabled"}},
                {"operation","enable or disable one independent oval local exposure gradient"}},
            QJsonObject{{"name","mask.radial.invert"},{"fields",QJsonArray{"inverted"}},
                {"operation","apply exposure outside rather than inside the feathered oval"}},
            QJsonObject{{"name","mask.radial.set"},{"fields",QJsonArray{"parameter","value"}},
                {"parameters",QJsonArray{"exposure","centerX","centerY","radiusX","radiusY","feather","angle"}},
                {"ranges","exposure [-3,3] EV; centerX/Y [0,1]; radiusX/Y [.05,1]; feather [.02,1]"},
                {"range_policy","reject"}},
            QJsonObject{{"name","mask.radial.reset"},{"fields",QJsonArray{}},
                {"operation","reset radial mask only; preserve linear mask and other edits"}},
            QJsonObject{{"name","mask.linear.enable"},{"fields",QJsonArray{"enabled"}},
                {"operation","enable or disable one linear gradient without losing stored controls"}},
            QJsonObject{{"name","mask.linear.set"},{"fields",QJsonArray{"parameter","value"}},
                {"parameters",QJsonArray{"exposure","center","feather","angle"}},
                {"ranges","exposure [-3,3] EV; center [0,1]; feather [.02,1]; angle [-180,180] degrees"},
                {"range_policy","reject"}},
            QJsonObject{{"name","mask.linear.reset"},{"fields",QJsonArray{}},
                {"operation","reset local linear gradient only"}},
            QJsonObject{{"name","vignette.reset"},{"fields",QJsonArray{}},{"operation","reset post-crop vignette amount/midpoint/feather; preserve other edits"}},
            QJsonObject{{"name","bw.enable"},{"fields",QJsonArray{"enabled"}},{"operation","toggle monochrome without discarding 8 color-channel luminance values"}},
            QJsonObject{{"name","bw.set"},{"fields",QJsonArray{"band","value"}},{"band",QJsonArray{0,7}},{"value",QJsonArray{-100,100}},{"range_policy","clamp"},{"unit","1/100 EV"}},
            QJsonObject{{"name","bw.reset"},{"fields",QJsonArray{}},{"operation","disable monochrome and reset all 8 channel mixes"}},
            QJsonObject{{"name","grading.enable"},{"fields",QJsonArray{"enabled"}},{"operation","enable/disable optional three-way grading without clearing controls"}},
            QJsonObject{{"name","grading.set"},{"fields",QJsonArray{"region","component","value"}},
                {"regions",QJsonArray{"shadows","midtones","highlights"}},{"component",QJsonArray{"hue","saturation"}},
                {"hue_range",QJsonArray{0,360}},{"saturation_range",QJsonArray{0,100}},{"range_policy","clamp"}},
            QJsonObject{{"name","grading.balance"},{"fields",QJsonArray{"value"}},{"range",QJsonArray{-100,100}},{"range_policy","clamp"}},
            QJsonObject{{"name","grading.reset"},{"fields",QJsonArray{}},{"operation","disable grading and clear three hue/saturation wheels and balance"}},
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
        return {{"schema",1},{"command","develop.set"},{"parameters",descriptors},{"commands",commands},{"unknown_fields","reject"},{"range_policy","clamp"},
            {"command_batch",QJsonObject{{"maximum_commands",10000},{"state_commit","atomic after every command validates"},
                {"look_lut_payloads","unsupported; use explicit validated profile import/calibration"}}}};
    }
};
