#pragma once

#include <QJsonObject>

struct AdjustmentState {
    double exposure = 0.0;
    double temperature = 0.0;
    double tint = 0.0;
    double contrast = 0.0;
    double highlights = 0.0;
    double shadows = 0.0;
    double whites = 0.0;
    double blacks = 0.0;

    [[nodiscard]] QJsonObject toJson() const {
        return {
            {"exposure", exposure}, {"temperature", temperature}, {"tint", tint},
            {"contrast", contrast}, {"highlights", highlights}, {"shadows", shadows},
            {"whites", whites}, {"blacks", blacks}
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
        return s;
    }
};
