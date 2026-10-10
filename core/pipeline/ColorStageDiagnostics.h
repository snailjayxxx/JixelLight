#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonObject>
#include <array>
#include <memory>

struct ProcessingPlan;
enum class ColorStage { InputLinear, WbExposure, HighlightRecovery, ToneNeutral,
                        PerceptualLook, OutputLinear, OutputTransfer, LookLut, Count };

// One RGB FP32 row per boundary, consumed in source row order. No full-frame
// float intermediates and no dependence on QImage padding, alpha or monitor ICC.
class ColorStageDiagnostics {
public:
    ColorStageDiagnostics(int width, int height, const ProcessingPlan &plan);
    void observe(ColorStage stage, int x, float r, float g, float b);
    void finishRow();
    QJsonObject result() const;
private:
    static constexpr int Count = int(ColorStage::Count);
    struct Boundary {
        std::unique_ptr<QCryptographicHash> hash;
        QByteArray row;
        std::array<double,3> minimum, maximum, first{};
        qint64 pixels = 0, nonFiniteValues = 0;
    };
    std::array<Boundary,Count> m_boundaries;
    std::array<QString,Count> m_spaces;
    int m_width, m_height, m_rows = 0;
    QString m_inputEncoding;
};
