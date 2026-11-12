#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <array>
#include <algorithm>
#include <cmath>
#include "core/look/LookState.h"
#include "core/pipeline/GeometryState.h"
#include "core/pipeline/BrushMaskDocument.h"

struct AdjustmentState {
    static constexpr int ColorBandCount = 8;
    static constexpr int CurvePointCount = 5;
    using ColorBandArray = std::array<double, ColorBandCount>;
    using CurveArray = std::array<double, CurvePointCount>;

    LookState look;
    GeometryState geometry;

    double exposure = 0.0;
    double temperature = 0.0;
    double tint = 0.0;
    double contrast = 0.0;
    double highlights = 0.0;
    double shadows = 0.0;
    double whites = 0.0;
    double blacks = 0.0;
    double highlightRecovery = 0.0;

    double hue = 0.0;
    double saturation = 0.0;
    double vibrance = 0.0;
    double vignetteAmount = 0.0; // EV at the outer ellipse; negative darkens.
    double vignetteMidpoint = 0.5;
    double vignetteFeather = 1.0;
    double grainAmount = 0.0; // 0..100; deterministic monochrome output texture.
    double grainSize = 1.0;   // .5..4, relative to the post-crop image frame.
    double sharpenAmount = 0.0; // 0..100, independent encoded-luma unsharp mask.
    double sharpenRadius = 3.0; // .5..6 source pixels, after geometry/crop.
    static constexpr std::array<double,3> DefaultPointColorLab{.627955f,.224863f,.125846f};
    bool pointColorEnabled=false;
    std::array<double,3> pointColorLab=DefaultPointColorLab; // Working Oklab; independent of output/monitor ICC.
    double pointColorHue=0,pointColorSaturation=0,pointColorLuminance=0;
    double pointColorHueRange=45,pointColorChromaRange=.12,pointColorLightnessRange=.4;
    double dehazeAmount = 0.0; // -100..100; atmospheric veil before texture/clarity.
    double textureAmount = 0.0; // -100..100, pre-tone medium-frequency detail.
    double clarityAmount = 0.0; // -100..100, pre-tone local midtone contrast.
    double denoiseLuma = 0.0; // 0..100, perceptual lightness noise reduction.
    double denoiseChroma = 0.0; // 0..100, joint-guide color noise reduction.
    // F4 M1: one manual scene-linear graduated exposure mask.
    // Normalized post-geometry/crop frame, not the pre-crop RAW sensor.
    bool linearMaskEnabled = false;
    double linearMaskExposure = 0.0; // EV -3..3
    double linearMaskCenter = 0.5;   // position 0..1
    double linearMaskFeather = 0.25; // half-width .02..1
    double linearMaskAngle = 0.0;    // degrees -180..180, down=0, right=90

    // F4 M2: one independent elliptic radial gradient, no rasterized mask data.
    // Center/radii are normalized post-geometry/crop image fractions.
    bool radialMaskEnabled = false;
    bool radialMaskInverted = false;
    double radialMaskExposure = 0.0; // EV -3..3
    double radialMaskCenterX = 0.5;  // 0..1
    double radialMaskCenterY = 0.5;  // 0..1
    double radialMaskRadiusX = 0.35; // horizontal fraction .05..1
    double radialMaskRadiusY = 0.35; // vertical fraction .05..1
    double radialMaskFeather = 0.25; // transition fraction .02..1
    double radialMaskAngle = 0.0; // ellipse rotation degrees [-180, 180]
    bool colorMaskEnabled=false,colorMaskInverted=false;
    std::array<double,3> colorMaskLab=DefaultPointColorLab;
    double colorMaskExposure=0,colorMaskHueRange=45,colorMaskChromaRange=.12,colorMaskLightnessRange=.4;
    // Independent selection from source scene-linear luminance, before WB/EV.
    bool luminanceMaskEnabled=false,luminanceMaskInverted=false;
    double luminanceMaskExposure=0,luminanceMaskLower=0,luminanceMaskUpper=1,luminanceMaskFeather=.1;
    // Canonical bounded brush intent. Enabled layers use explicit admitted
    // production render/export/statistics paths; research APIs keep their cap.
    BrushMaskDocument::Document brushMasks;

    // Independent monochrome rendering and eight hue-weighted luminance controls.
    // Values -100..100 map to -1..+1 EV; they do not modify Sony Look state.
    bool blackWhite = false;
    ColorBandArray bwMix{};

    // Optional three-way color grading in Oklab, without altering the Sony LUT
    // or the HSL color mixer. Hue degrees [0, 360], saturation [0, 100].
    static constexpr int GradingRegionCount = 3;
    using GradingArray = std::array<double, GradingRegionCount>;
    bool gradingEnabled = false;
    GradingArray gradingHue{};
    GradingArray gradingSaturation{};
    double gradingBalance = 0.0; // -100 shadows ... +100 highlights

    ColorBandArray hslHue{};
    ColorBandArray hslSaturation{};
    ColorBandArray hslLuminance{};

    CurveArray masterCurve{0.0, 0.25, 0.5, 0.75, 1.0};
    CurveArray redCurve{0.0, 0.25, 0.5, 0.75, 1.0};
    CurveArray greenCurve{0.0, 0.25, 0.5, 0.75, 1.0};
    CurveArray blueCurve{0.0, 0.25, 0.5, 0.75, 1.0};

    template <std::size_t N>
    static QJsonArray toJsonArray(const std::array<double, N> &values) {
        QJsonArray a;
        for (double v : values) a.append(v);
        return a;
    }

    template <std::size_t N>
    static void readJsonArray(const QJsonObject &object, const char *key, std::array<double, N> &target) {
        const QJsonArray a = object.value(QString::fromLatin1(key)).toArray();
        if (a.size() != static_cast<int>(N)) return;
        for (int i = 0; i < a.size(); ++i) target[static_cast<std::size_t>(i)] = a.at(i).toDouble(target[static_cast<std::size_t>(i)]);
    }

    [[nodiscard]] QJsonObject toJson() const {
        QJsonObject result{
            {"geometry", geometry.toJson()}, {"look", look.toJson()}, {"exposure", exposure}, {"temperature", temperature}, {"tint", tint},
            {"contrast", contrast}, {"highlights", highlights}, {"shadows", shadows},
            {"whites", whites}, {"blacks", blacks}, {"highlightRecovery", highlightRecovery},
            {"hue", hue}, {"saturation", saturation}, {"vibrance", vibrance},
            {"hslHue", toJsonArray(hslHue)},
            {"hslSaturation", toJsonArray(hslSaturation)},
            {"hslLuminance", toJsonArray(hslLuminance)},
            {"masterCurve", toJsonArray(masterCurve)},
            {"redCurve", toJsonArray(redCurve)},
            {"greenCurve", toJsonArray(greenCurve)},
            {"blueCurve", toJsonArray(blueCurve)}
        };
        if (linearMaskEnabled || linearMaskExposure!=0 || linearMaskCenter!=.5
            || linearMaskFeather!=.25 || linearMaskAngle!=0)
            result["linearMask"]=QJsonObject{{"schema",1},{"enabled",linearMaskEnabled},
                {"exposure",linearMaskExposure},{"center",linearMaskCenter},
                {"feather",linearMaskFeather},{"angle",linearMaskAngle}};
        if (radialMaskEnabled || radialMaskInverted || radialMaskExposure!=0
            || radialMaskCenterX!=.5 || radialMaskCenterY!=.5
            || radialMaskRadiusX!=.35 || radialMaskRadiusY!=.35 || radialMaskFeather!=.25
            || radialMaskAngle!=0)
            result["radialMask"]=QJsonObject{{"schema",1},{"enabled",radialMaskEnabled},
                {"inverted",radialMaskInverted},{"exposure",radialMaskExposure},
                {"centerX",radialMaskCenterX},{"centerY",radialMaskCenterY},
                {"radiusX",radialMaskRadiusX},{"radiusY",radialMaskRadiusY},
                {"feather",radialMaskFeather}};
        if (radialMaskAngle!=0) {
            auto m=result.value("radialMask").toObject();
            m.insert("angle",radialMaskAngle); result.insert("radialMask",m);
        }
        if(colorMaskEnabled||colorMaskInverted||colorMaskLab!=DefaultPointColorLab||colorMaskExposure!=0
            ||colorMaskHueRange!=45||colorMaskChromaRange!=.12||colorMaskLightnessRange!=.4)
            result["colorMask"]=QJsonObject{{"schema",1},{"space","oklab"},{"enabled",colorMaskEnabled},{"inverted",colorMaskInverted},
                {"exposure",colorMaskExposure},{"center",toJsonArray(colorMaskLab)},
                {"hueRange",colorMaskHueRange},{"chromaRange",colorMaskChromaRange},{"lightnessRange",colorMaskLightnessRange}};
        if(luminanceMaskEnabled||luminanceMaskInverted||luminanceMaskExposure!=0
            ||luminanceMaskLower!=0||luminanceMaskUpper!=1||luminanceMaskFeather!=.1)
            result["luminanceMask"]=QJsonObject{{"schema",1},{"enabled",luminanceMaskEnabled},{"inverted",luminanceMaskInverted},
                {"exposure",luminanceMaskExposure},{"lower",luminanceMaskLower},{"upper",luminanceMaskUpper},{"feather",luminanceMaskFeather}};
        QJsonObject brushDraft;
        if (BrushMaskDocument::toJson(brushMasks,&brushDraft) && !brushDraft.isEmpty())
            result["brushMasks"]=brushDraft;
        // Keep the exact legacy snapshot when the new tool is at its defaults.
        if (vignetteAmount!=0 || vignetteMidpoint!=.5 || vignetteFeather!=1)
            result["vignette"]=QJsonObject{{"schema",1},{"amount",vignetteAmount},{"midpoint",vignetteMidpoint},{"feather",vignetteFeather}};
        if (grainAmount!=0 || grainSize!=1)
            result["grain"]=QJsonObject{{"schema",1},{"amount",grainAmount},{"size",grainSize}};
        if (sharpenAmount!=0 || sharpenRadius!=3)
            result["sharpen"]=QJsonObject{{"schema",1},{"amount",sharpenAmount},{"radius",sharpenRadius}};
        if(pointColorEnabled||pointColorLab!=DefaultPointColorLab
            ||pointColorHue!=0||pointColorSaturation!=0||pointColorLuminance!=0
            ||pointColorHueRange!=45||pointColorChromaRange!=.12||pointColorLightnessRange!=.4)
            result["pointColor"]=QJsonObject{{"schema",1},{"space","oklab"},{"enabled",pointColorEnabled},{"center",toJsonArray(pointColorLab)},
                {"hue",pointColorHue},{"saturation",pointColorSaturation},{"luminance",pointColorLuminance},
                {"hueRange",pointColorHueRange},{"chromaRange",pointColorChromaRange},{"lightnessRange",pointColorLightnessRange}};
        if (dehazeAmount!=0)
            result["dehaze"]=QJsonObject{{"schema",1},{"amount",dehazeAmount}};
        if (textureAmount!=0)
            result["texture"]=QJsonObject{{"schema",1},{"amount",textureAmount}};
        if (clarityAmount!=0)
            result["clarity"]=QJsonObject{{"schema",1},{"amount",clarityAmount}};
        if (denoiseLuma!=0 || denoiseChroma!=0)
            result["denoise"]=QJsonObject{{"schema",1},{"lightness",denoiseLuma},{"chroma",denoiseChroma}};
        const bool hasMix = std::any_of(bwMix.begin(), bwMix.end(), [](double v){ return v != 0.0; });
        if (blackWhite || hasMix)
            result["blackAndWhite"] = QJsonObject{{"schema", 1}, {"enabled", blackWhite}, {"mix", toJsonArray(bwMix)}};
        const bool hasGradingControls = gradingBalance != 0.0
            || std::any_of(gradingHue.begin(),gradingHue.end(),[](double v){ return v != 0.0; })
            || std::any_of(gradingSaturation.begin(),gradingSaturation.end(),[](double v){ return v != 0.0; });
        if (gradingEnabled || hasGradingControls)
            result["colorGrading"] = QJsonObject{{"schema",1},{"enabled",gradingEnabled},
                {"hue",toJsonArray(gradingHue)},{"saturation",toJsonArray(gradingSaturation)},
                {"balance",gradingBalance}};
        return result;
    }

    static bool validBrushMaskJson(const QJsonObject &object) {
        if (!object.contains("brushMasks")) return true; // canonical legacy absence
        BrushMaskDocument::Document parsed;
        QJsonObject canonical;
        if (!BrushMaskDocument::fromJson(object.value("brushMasks"),&parsed)
            || !BrushMaskDocument::toJson(parsed,&canonical)
            || canonical.isEmpty() || canonical != object.value("brushMasks").toObject())
            return false;
        return true;
    }
    static bool validRadialMaskJson(const QJsonObject &object) {
        if (!object.contains("radialMask")) return true;
        if (!object.value("radialMask").isObject()) return false;
        const auto mask=object.value("radialMask").toObject();
        if ((mask.size()!=9 && mask.size()!=10) || (mask.size()==10 && !mask.contains("angle"))
            || !mask.value("schema").isDouble()
            || mask.value("schema").toDouble()!=1
            || !mask.value("enabled").isBool()
            || !mask.value("inverted").isBool()) return false;
        const auto range=[&](const char *name,double lo,double hi) {
            const QJsonValue v=mask.value(QLatin1String(name));
            return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=lo && v.toDouble()<=hi;
        };
        if (!range("exposure",-3,3) || !range("centerX",0,1) || !range("centerY",0,1)
            || !range("radiusX",.05,1) || !range("radiusY",.05,1)
            || !range("feather",.02,1)
            || (mask.contains("angle") && (!range("angle",-180,180) || mask.value("angle").toDouble()==0)))
            return false;
        // Reject a redundant all-default block; retain inactive nondefault intent.
        return mask.value("enabled").toBool() || mask.value("inverted").toBool()
            || mask.value("exposure").toDouble()!=0
            || mask.value("centerX").toDouble()!=.5
            || mask.value("centerY").toDouble()!=.5
            || mask.value("radiusX").toDouble()!=.35
            || mask.value("radiusY").toDouble()!=.35
            || mask.value("feather").toDouble()!=.25;
    }

    static bool validLinearMaskJson(const QJsonObject &object) {
        if (!object.contains("linearMask")) return true;
        if (!object.value("linearMask").isObject()) return false;
        const auto m=object.value("linearMask").toObject();
        if (m.size()!=6 || !m.value("schema").isDouble() || m.value("schema").toDouble()!=1
            || !m.value("enabled").isBool()) return false;
        const auto range=[&](const char *name,double lo,double hi) {
            const auto v=m.value(QLatin1String(name));
            return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=lo && v.toDouble()<=hi;
        };
        if (!range("exposure",-3,3) || !range("center",0,1) || !range("feather",.02,1)
            || !range("angle",-180,180)) return false;
        return m.value("enabled").toBool() || m.value("exposure").toDouble()!=0
            || m.value("center").toDouble()!=.5 || m.value("feather").toDouble()!=.25
            || m.value("angle").toDouble()!=0;
    }
    static bool validVignetteJson(const QJsonObject &object) {
        if (!object.contains("vignette")) return true;
        if (!object["vignette"].isObject()) return false;
        const auto v=object["vignette"].toObject();
        if (v.size()!=4 || !v["schema"].isDouble() || v["schema"].toDouble()!=1) return false;
        const auto valid=[&](const char *key,double lo,double hi) {
            return v[key].isDouble() && std::isfinite(v[key].toDouble()) && v[key].toDouble()>=lo && v[key].toDouble()<=hi;
        };
        return valid("amount",-3,3) && valid("midpoint",0,.95) && valid("feather",.01,1)
            && (v["amount"].toDouble()!=0 || v["midpoint"].toDouble()!=.5 || v["feather"].toDouble()!=1);
    }

    static bool validColorMaskJson(const QJsonObject &object) {
        if(!object.contains("colorMask"))return true;
        if(!object["colorMask"].isObject())return false;
        const auto m=object["colorMask"].toObject();
        if(m.size()!=9||!m["schema"].isDouble()||m["schema"].toDouble()!=1||m["space"]!=QStringLiteral("oklab")
            ||!m["enabled"].isBool()||!m["inverted"].isBool()||!m["center"].isArray()||m["center"].toArray().size()!=3)return false;
        const auto lab=m["center"].toArray();for(auto v:lab)if(!v.isDouble()||!std::isfinite(v.toDouble())||std::abs(v.toDouble())>4)return false;
        const auto number=[&](const char *key,double lo,double hi){const auto v=m[key];return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=lo&&v.toDouble()<=hi;};
        if(!number("exposure",-3,3)||!number("hueRange",1,180)||!number("chromaRange",.001,1)||!number("lightnessRange",.001,1))return false;
        return m["enabled"].toBool()||m["inverted"].toBool()||lab!=toJsonArray(DefaultPointColorLab)||m["exposure"].toDouble()!=0
            ||m["hueRange"].toDouble()!=45||m["chromaRange"].toDouble()!=.12||m["lightnessRange"].toDouble()!=.4;
    }
    static bool validLuminanceMaskJson(const QJsonObject &object) {
        if(!object.contains("luminanceMask"))return true;
        if(!object["luminanceMask"].isObject())return false;
        const auto m=object["luminanceMask"].toObject();
        if(m.size()!=7||!m["schema"].isDouble()||m["schema"].toDouble()!=1
            ||!m["enabled"].isBool()||!m["inverted"].isBool())return false;
        const auto number=[&](const char *key,double lo,double hi){const auto v=m[key];return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=lo&&v.toDouble()<=hi;};
        if(!number("exposure",-3,3)||!number("lower",0,2)||!number("upper",0,2)||!number("feather",.001,1)
            ||m["lower"].toDouble()>m["upper"].toDouble())return false;
        return m["enabled"].toBool()||m["inverted"].toBool()||m["exposure"].toDouble()!=0
            ||m["lower"].toDouble()!=0||m["upper"].toDouble()!=1||m["feather"].toDouble()!=.1;
    }
    static bool validPointColorJson(const QJsonObject &object) {
        if(!object.contains("pointColor"))return true;
        if(!object["pointColor"].isObject())return false;
        const auto p=object["pointColor"].toObject();
        if(p.size()!=10||!p["schema"].isDouble()||p["schema"].toDouble()!=1||!p["enabled"].isBool()
            ||p["space"]!=QStringLiteral("oklab")||!p["center"].isArray()||p["center"].toArray().size()!=3)return false;
        const auto color=p["center"].toArray();
        for(const auto v:color)if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<-4||v.toDouble()>4)return false;
        const auto number=[&](const char *key,double lo,double hi){const auto v=p[key];return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=lo&&v.toDouble()<=hi;};
        if(!number("hue",-180,180)||!number("saturation",-100,100)||!number("luminance",-100,100)
            ||!number("hueRange",1,180)||!number("chromaRange",.001,1)||!number("lightnessRange",.001,1))return false;
        return p["enabled"].toBool()||color!=toJsonArray(DefaultPointColorLab)||p["hue"].toDouble()!=0
            ||p["saturation"].toDouble()!=0||p["luminance"].toDouble()!=0||p["hueRange"].toDouble()!=45
            ||p["chromaRange"].toDouble()!=.12||p["lightnessRange"].toDouble()!=.4;
    }
    static bool validDehazeJson(const QJsonObject &object) {
        if(!object.contains("dehaze"))return true;
        if(!object["dehaze"].isObject())return false;
        const auto c=object["dehaze"].toObject();
        return c.size()==2&&c["schema"].isDouble()&&c["schema"].toDouble()==1
            &&c["amount"].isDouble()&&std::isfinite(c["amount"].toDouble())
            &&c["amount"].toDouble()>=-100&&c["amount"].toDouble()<=100&&c["amount"].toDouble()!=0;
    }
    static bool validTextureJson(const QJsonObject &object) {
        if(!object.contains("texture"))return true;
        if(!object["texture"].isObject())return false;
        const auto c=object["texture"].toObject();
        return c.size()==2&&c["schema"].isDouble()&&c["schema"].toDouble()==1
            &&c["amount"].isDouble()&&std::isfinite(c["amount"].toDouble())
            &&c["amount"].toDouble()>=-100&&c["amount"].toDouble()<=100&&c["amount"].toDouble()!=0;
    }
    static bool validClarityJson(const QJsonObject &object) {
        if(!object.contains("clarity"))return true;
        if(!object["clarity"].isObject())return false;
        const auto c=object["clarity"].toObject();
        return c.size()==2&&c["schema"].isDouble()&&c["schema"].toDouble()==1
            &&c["amount"].isDouble()&&std::isfinite(c["amount"].toDouble())
            &&c["amount"].toDouble()>=-100&&c["amount"].toDouble()<=100&&c["amount"].toDouble()!=0;
    }
    static bool validDenoiseJson(const QJsonObject &object) {
        if(!object.contains("denoise"))return true;
        if(!object["denoise"].isObject())return false;
        const auto d=object["denoise"].toObject();
        const auto amount=[&](const char *key){return d[key].isDouble()&&std::isfinite(d[key].toDouble())&&d[key].toDouble()>=0&&d[key].toDouble()<=100;};
        return d.size()==3&&d["schema"].isDouble()&&d["schema"].toDouble()==1
            &&amount("lightness")&&amount("chroma")&&(d["lightness"].toDouble()!=0||d["chroma"].toDouble()!=0);
    }
    static bool validSharpenJson(const QJsonObject &object) {
        if (!object.contains("sharpen")) return true;
        if (!object["sharpen"].isObject()) return false;
        const auto s=object["sharpen"].toObject();
        const auto range=[&](const char *key,double lo,double hi) {
            return s[key].isDouble() && std::isfinite(s[key].toDouble())
                && s[key].toDouble()>=lo && s[key].toDouble()<=hi;
        };
        return s.size()==3 && s["schema"].isDouble() && s["schema"].toDouble()==1
            && range("amount",0,100) && range("radius",.5,6)
            && (s["amount"].toDouble()!=0 || s["radius"].toDouble()!=3);
    }
    static bool validGrainJson(const QJsonObject &object) {
        if (!object.contains("grain")) return true;
        if (!object["grain"].isObject()) return false;
        const auto g=object["grain"].toObject();
        const auto range=[&](const char *key,double lo,double hi) {
            return g[key].isDouble() && std::isfinite(g[key].toDouble())
                && g[key].toDouble()>=lo && g[key].toDouble()<=hi;
        };
        return g.size()==3 && g["schema"].isDouble() && g["schema"].toDouble()==1
            && range("amount",0,100) && range("size",.5,4)
            && (g["amount"].toDouble()!=0 || g["size"].toDouble()!=1);
    }

    static bool validBlackAndWhiteJson(const QJsonObject &object) {
        if (!object.contains("blackAndWhite")) return true;
        if (!object.value("blackAndWhite").isObject()) return false;
        const auto b = object.value("blackAndWhite").toObject();
        if (b.size() != 3 || !b.value("schema").isDouble() || b.value("schema").toDouble() != 1
            || !b.value("enabled").isBool() || !b.value("mix").isArray()) return false;
        const auto values = b.value("mix").toArray();
        if (values.size() != ColorBandCount) return false;
        bool hasMix = false;
        for (const auto &entry : values) {
            if (!entry.isDouble() || !std::isfinite(entry.toDouble())
                || entry.toDouble() < -100 || entry.toDouble() > 100) return false;
            hasMix |= entry.toDouble() != 0;
        }
        return b.value("enabled").toBool() || hasMix;
    }

    static bool validColorGradingJson(const QJsonObject &object) {
        if (!object.contains("colorGrading")) return true;
        if (!object.value("colorGrading").isObject()) return false;
        const auto g = object.value("colorGrading").toObject();
        if (g.size()!=5 || !g.value("schema").isDouble() || g.value("schema").toDouble()!=1
            || !g.value("enabled").isBool() || !g.value("hue").isArray()
            || !g.value("saturation").isArray() || !g.value("balance").isDouble()) return false;
        const double balance=g.value("balance").toDouble();
        if (!std::isfinite(balance) || balance < -100 || balance > 100) return false;
        const auto hue=g.value("hue").toArray(), saturation=g.value("saturation").toArray();
        if (hue.size()!=GradingRegionCount || saturation.size()!=GradingRegionCount) return false;
        bool changed=balance!=0;
        for (int i=0;i<GradingRegionCount;++i) {
            if (!hue[i].isDouble() || !saturation[i].isDouble()) return false;
            const double h=hue[i].toDouble(), s=saturation[i].toDouble();
            if (!std::isfinite(h) || h<0 || h>360 || !std::isfinite(s) || s<0 || s>100) return false;
            changed |= h!=0 || s!=0;
        }
        // Canonical snapshots never add an empty optional section.
        return g.value("enabled").toBool() || changed;
    }

    static AdjustmentState fromJson(const QJsonObject &o) {
        AdjustmentState s;
        s.geometry = GeometryState::fromJson(o.value("geometry").toObject());
        s.look = LookState::fromJson(o.value("look").toObject());
        s.exposure = o.value("exposure").toDouble();
        s.temperature = o.value("temperature").toDouble();
        s.tint = o.value("tint").toDouble();
        s.contrast = o.value("contrast").toDouble();
        s.highlights = o.value("highlights").toDouble();
        s.shadows = o.value("shadows").toDouble();
        s.whites = o.value("whites").toDouble();
        s.blacks = o.value("blacks").toDouble();
        s.highlightRecovery = o.value("highlightRecovery").toDouble();
        s.hue = o.value("hue").toDouble();
        s.saturation = o.value("saturation").toDouble();
        s.vibrance = o.value("vibrance").toDouble();
        if (validBrushMaskJson(o))
            BrushMaskDocument::fromJson(o.value("brushMasks"),&s.brushMasks);
        if (o.contains("radialMask") && validRadialMaskJson(o)) {
            const auto m=o.value("radialMask").toObject();
            s.radialMaskEnabled=m.value("enabled").toBool();
            s.radialMaskInverted=m.value("inverted").toBool();
            s.radialMaskExposure=m.value("exposure").toDouble();
            s.radialMaskCenterX=m.value("centerX").toDouble();
            s.radialMaskCenterY=m.value("centerY").toDouble();
            s.radialMaskRadiusX=m.value("radiusX").toDouble();
            s.radialMaskRadiusY=m.value("radiusY").toDouble();
            s.radialMaskFeather=m.value("feather").toDouble();
            s.radialMaskAngle=m.value("angle").toDouble(0);
        }
        if (o.contains("linearMask") && validLinearMaskJson(o)) {
            const auto m=o.value("linearMask").toObject();
            s.linearMaskEnabled=m.value("enabled").toBool();
            s.linearMaskExposure=m.value("exposure").toDouble();
            s.linearMaskCenter=m.value("center").toDouble();
            s.linearMaskFeather=m.value("feather").toDouble();
            s.linearMaskAngle=m.value("angle").toDouble();
        }
        if (o.contains("vignette") && validVignetteJson(o)) {
            const auto v=o["vignette"].toObject(); s.vignetteAmount=v["amount"].toDouble();
            s.vignetteMidpoint=v["midpoint"].toDouble(); s.vignetteFeather=v["feather"].toDouble();
        }
        if (o.contains("grain") && validGrainJson(o)) {
            const auto g=o["grain"].toObject();
            s.grainAmount=g["amount"].toDouble(); s.grainSize=g["size"].toDouble();
        }
        if (o.contains("sharpen") && validSharpenJson(o)) {
            const auto d=o["sharpen"].toObject();
            s.sharpenAmount=d["amount"].toDouble(); s.sharpenRadius=d["radius"].toDouble();
        }
        if(o.contains("colorMask")&&validColorMaskJson(o)) {
            const auto m=o["colorMask"].toObject();s.colorMaskEnabled=m["enabled"].toBool();s.colorMaskInverted=m["inverted"].toBool();
            for(int i=0;i<3;++i)s.colorMaskLab[i]=m["center"].toArray()[i].toDouble();
            s.colorMaskExposure=m["exposure"].toDouble();s.colorMaskHueRange=m["hueRange"].toDouble();s.colorMaskChromaRange=m["chromaRange"].toDouble();s.colorMaskLightnessRange=m["lightnessRange"].toDouble();
        }
        if(o.contains("luminanceMask")&&validLuminanceMaskJson(o)) {
            const auto m=o["luminanceMask"].toObject();s.luminanceMaskEnabled=m["enabled"].toBool();s.luminanceMaskInverted=m["inverted"].toBool();
            s.luminanceMaskExposure=m["exposure"].toDouble();s.luminanceMaskLower=m["lower"].toDouble();s.luminanceMaskUpper=m["upper"].toDouble();s.luminanceMaskFeather=m["feather"].toDouble();
        }
        if(o.contains("pointColor")&&validPointColorJson(o)) {
            const auto p=o["pointColor"].toObject();s.pointColorEnabled=p["enabled"].toBool();
            for(int i=0;i<3;++i)s.pointColorLab[i]=p["center"].toArray()[i].toDouble();
            s.pointColorHue=p["hue"].toDouble();s.pointColorSaturation=p["saturation"].toDouble();s.pointColorLuminance=p["luminance"].toDouble();
            s.pointColorHueRange=p["hueRange"].toDouble();s.pointColorChromaRange=p["chromaRange"].toDouble();s.pointColorLightnessRange=p["lightnessRange"].toDouble();
        }
        if(o.contains("dehaze")&&validDehazeJson(o))s.dehazeAmount=o["dehaze"].toObject()["amount"].toDouble();
        if(o.contains("texture")&&validTextureJson(o))s.textureAmount=o["texture"].toObject()["amount"].toDouble();
        if(o.contains("clarity")&&validClarityJson(o))s.clarityAmount=o["clarity"].toObject()["amount"].toDouble();
        if(o.contains("denoise")&&validDenoiseJson(o)) {
            const auto d=o["denoise"].toObject();s.denoiseLuma=d["lightness"].toDouble();s.denoiseChroma=d["chroma"].toDouble();
        }
        if (o.contains("blackAndWhite") && validBlackAndWhiteJson(o)) {
            const auto b = o.value("blackAndWhite").toObject();
            s.blackWhite = b.value("enabled").toBool();
            readJsonArray(b, "mix", s.bwMix);
        }
        if (o.contains("colorGrading") && validColorGradingJson(o)) {
            const auto g=o.value("colorGrading").toObject();
            s.gradingEnabled=g.value("enabled").toBool();
            readJsonArray(g,"hue",s.gradingHue);
            readJsonArray(g,"saturation",s.gradingSaturation);
            s.gradingBalance=g.value("balance").toDouble();
        }
        readJsonArray(o, "hslHue", s.hslHue);
        readJsonArray(o, "hslSaturation", s.hslSaturation);
        readJsonArray(o, "hslLuminance", s.hslLuminance);
        readJsonArray(o, "masterCurve", s.masterCurve);
        readJsonArray(o, "redCurve", s.redCurve);
        readJsonArray(o, "greenCurve", s.greenCurve);
        readJsonArray(o, "blueCurve", s.blueCurve);
        return s;
    }
};
