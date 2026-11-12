#include "core/commands/EditRecipe.h"
#include "core/commands/CommandRegistry.h"
#include <QJsonDocument>

EditRecipe::Result EditRecipe::encode(const AdjustmentState &state,bool rawSource) {
    if(state.look.lut || !state.look.error.isEmpty() || state.look.mode=="calibrated")
        return {Error::UnsupportedLook};
    if(!BrushMaskDocument::validDocument(state.brushMasks))return {Error::UnrepresentableState};
    QJsonArray commands{QJsonObject{{"command","develop.reset"}}};
    const auto add=[&](const char *name,QJsonObject fields={}) {
        fields.insert("command",QLatin1String(name));commands.append(fields);
    };
    const auto set=[&](const char *name,const char *parameter,double value) {
        add(name,{{"parameter",QLatin1String(parameter)},{"value",value}});
    };
    for(const auto &p:CommandRegistry::parameters())set("develop.set",p.name,state.*p.member);
    const auto &g=state.geometry;
    add("geometry.crop",{{"x",g.crop.x()},{"y",g.crop.y()},{"width",g.crop.width()},{"height",g.crop.height()}});
    add("geometry.straighten",{{"degrees",g.straighten}});
    add("geometry.rotate",{{"quarterTurns",g.quarterTurns}});
    if(g.flipHorizontal)add("geometry.flip",{{"axis","horizontal"}});
    if(g.flipVertical)add("geometry.flip",{{"axis","vertical"}});
    for(const auto &p:CommandRegistry::geometryParameters())set("geometry.set",p.name,g.*p.member);
    const std::array<std::pair<const char *,const AdjustmentState::ColorBandArray *>,3> hsl{{
        {"hue",&state.hslHue},{"saturation",&state.hslSaturation},{"luminance",&state.hslLuminance}}};
    for(const auto &[component,values]:hsl)for(int band=0;band<8;++band)
        add("hsl.set",{{"band",band},{"component",QLatin1String(component)},{"value",(*values)[band]}});
    const std::array<std::pair<const char *,const AdjustmentState::CurveArray *>,4> curves{{
        {"master",&state.masterCurve},{"red",&state.redCurve},{"green",&state.greenCurve},{"blue",&state.blueCurve}}};
    for(const auto &[channel,values]:curves)for(int point=0;point<5;++point)
        add("curve.set",{{"channel",QLatin1String(channel)},{"point",point},{"value",(*values)[point]}});
    add("bw.enable",{{"enabled",state.blackWhite}});
    for(int band=0;band<8;++band)add("bw.set",{{"band",band},{"value",state.bwMix[band]}});
    add("grading.enable",{{"enabled",state.gradingEnabled}});
    for(int region=0;region<3;++region) {
        add("grading.set",{{"region",region},{"component","hue"},{"value",state.gradingHue[region]}});
        add("grading.set",{{"region",region},{"component","saturation"},{"value",state.gradingSaturation[region]}});
    }
    add("grading.balance",{{"value",state.gradingBalance}});
    add("point.color.select.lab",{{"lab",AdjustmentState::toJsonArray(state.pointColorLab)}});
    add("point.color.enable",{{"enabled",state.pointColorEnabled}});
    set("mask.linear.set","exposure",state.linearMaskExposure);
    set("mask.linear.set","center",state.linearMaskCenter);
    set("mask.linear.set","feather",state.linearMaskFeather);
    set("mask.linear.set","angle",state.linearMaskAngle);
    add("mask.linear.enable",{{"enabled",state.linearMaskEnabled}});
    set("mask.radial.set","exposure",state.radialMaskExposure);
    set("mask.radial.set","centerX",state.radialMaskCenterX);
    set("mask.radial.set","centerY",state.radialMaskCenterY);
    set("mask.radial.set","radiusX",state.radialMaskRadiusX);
    set("mask.radial.set","radiusY",state.radialMaskRadiusY);
    set("mask.radial.set","feather",state.radialMaskFeather);
    set("mask.radial.set","angle",state.radialMaskAngle);
    add("mask.radial.invert",{{"inverted",state.radialMaskInverted}});
    add("mask.radial.enable",{{"enabled",state.radialMaskEnabled}});
    add("mask.color.select.lab",{{"lab",AdjustmentState::toJsonArray(state.colorMaskLab)}});
    set("mask.color.set","exposure",state.colorMaskExposure);
    set("mask.color.set","hueRange",state.colorMaskHueRange);
    set("mask.color.set","chromaRange",state.colorMaskChromaRange);
    set("mask.color.set","lightnessRange",state.colorMaskLightnessRange);
    add("mask.color.invert",{{"inverted",state.colorMaskInverted}});
    add("mask.color.enable",{{"enabled",state.colorMaskEnabled}});
    add("mask.luminance.range",{{"lower",state.luminanceMaskLower},{"upper",state.luminanceMaskUpper},{"feather",state.luminanceMaskFeather}});
    set("mask.luminance.set","exposure",state.luminanceMaskExposure);
    add("mask.luminance.invert",{{"inverted",state.luminanceMaskInverted}});
    add("mask.luminance.enable",{{"enabled",state.luminanceMaskEnabled}});
    if(!state.brushMasks.layers.empty()) {
        QJsonObject document;
        if(!BrushMaskDocument::toJson(state.brushMasks,&document))return {Error::UnrepresentableState};
        add("mask.brush.replace",{{"document",document}});
    }
    if(state.look.mode=="as-shot")add("look.mode",{{"mode","as_shot"}});
    else {
        if(!state.look.code.isEmpty())add("look.code",{{"code",state.look.code}});
        for(auto i=state.look.parameters.cbegin();i!=state.look.parameters.cend();++i)
            add("look.parameter",{{"parameter",i.key()},{"value",i.value()}});
    }
    add("look.strength",{{"value",state.look.strength}});
    if(state.look.mode=="off")add("look.mode",{{"mode","off"}});
    else if(state.look.mode!="manual" && state.look.mode!="as-shot")return {Error::UnrepresentableState};
    // Validate the serialized bytes through the same parser and command registry
    // used by the GUI/CLI. Never silently normalize or drop saved intent.
    auto text=QString::fromUtf8(QJsonDocument(commands).toJson(QJsonDocument::Compact));
    if(text.size()>MaximumTextLength)return {Error::TooLong};
    const auto parsed=QJsonDocument::fromJson(text.toUtf8());
    AdjustmentState replay;
    const CommandRegistry::LookContext context{rawSource,{}};
    if(!parsed.isArray() || !CommandRegistry::executeBatch(replay,parsed.array(),nullptr,&context)
        || replay.toJson()!=state.toJson())return {Error::UnrepresentableState};
    return {Error::None,std::move(text),commands.size()};
}
