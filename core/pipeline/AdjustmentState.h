#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <array>

struct AdjustmentState {
    static constexpr int ColorBandCount = 8;
    static constexpr int CurvePointCount = 5;
    using ColorBandArray = std::array<double, ColorBandCount>;
    using CurveArray = std::array<double, CurvePointCount>;

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
        return {
            {"exposure", exposure}, {"temperature", temperature}, {"tint", tint},
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
    }

    static AdjustmentState fromJson(const QJsonObject &o) {
        AdjustmentState s;
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
