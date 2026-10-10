#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <array>
#include <algorithm>
#include <cmath>
#include "core/look/LookState.h"
#include "core/pipeline/GeometryState.h"

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

    // Independent monochrome rendering and eight hue-weighted luminance controls.
    // Values -100..100 map to -1..+1 EV; they do not modify Sony Look state.
    bool blackWhite = false;
    ColorBandArray bwMix{};

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
        // Keep the exact legacy snapshot when the new tool is at its defaults.
        if (vignetteAmount!=0 || vignetteMidpoint!=.5 || vignetteFeather!=1)
            result["vignette"]=QJsonObject{{"schema",1},{"amount",vignetteAmount},{"midpoint",vignetteMidpoint},{"feather",vignetteFeather}};
        const bool hasMix = std::any_of(bwMix.begin(), bwMix.end(), [](double v){ return v != 0.0; });
        if (blackWhite || hasMix)
            result["blackAndWhite"] = QJsonObject{{"schema", 1}, {"enabled", blackWhite}, {"mix", toJsonArray(bwMix)}};
        return result;
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
        if (o.contains("vignette") && validVignetteJson(o)) {
            const auto v=o["vignette"].toObject(); s.vignetteAmount=v["amount"].toDouble();
            s.vignetteMidpoint=v["midpoint"].toDouble(); s.vignetteFeather=v["feather"].toDouble();
        }
        if (o.contains("blackAndWhite") && validBlackAndWhiteJson(o)) {
            const auto b = o.value("blackAndWhite").toObject();
            s.blackWhite = b.value("enabled").toBool();
            readJsonArray(b, "mix", s.bwMix);
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
