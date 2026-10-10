#include "core/pipeline/ColorStageDiagnostics.h"
#include "core/pipeline/ProcessingPlan.h"
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace {
constexpr std::array<const char *,int(ColorStage::Count)> StageIds{
    "input_linear", "wb_exposure", "highlight_recovery", "tone_neutral",
    "perceptual_look", "output_linear", "output_transfer", "look_lut"};
QJsonArray rgbArray(const std::array<double,3> &rgb) {
    return {rgb[0],rgb[1],rgb[2]}; // QJson represents non-finite numbers as null.
}
}

ColorStageDiagnostics::ColorStageDiagnostics(int width, int height, const ProcessingPlan &plan)
    : m_width(width), m_height(height),
      m_inputEncoding(plan.encoding==ImagePipeline::InputEncoding::SRgb ? "srgb-encoded" : "linear-prophoto-d50") {
    const auto kernelSpace=ColorManagement::OutputSpace(int(plan.data[ProcessingPlan::Luminance].w));
    const auto kernelKey=ColorManagement::key(kernelSpace), targetKey=ColorManagement::key(plan.output);
    m_spaces={"linear-prophoto-d50","linear-prophoto-d50","linear-prophoto-d50","linear-prophoto-d50",
              "linear-srgb-d65","linear-"+kernelKey,"encoded-"+kernelKey,"encoded-"+targetKey};
    for (int i=0;i<Count;++i) {
        auto &boundary=m_boundaries[i];
        boundary.row.resize(qsizetype(width)*3*sizeof(float));
        boundary.minimum.fill(std::numeric_limits<double>::infinity());
        boundary.maximum.fill(-std::numeric_limits<double>::infinity());
        boundary.hash=std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
        QByteArray descriptor; QDataStream stream(&descriptor,QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << QStringLiteral("color-stage-fp32-v1") << QString::fromLatin1(ProcessingPlan::EngineVersion)
               << QString::fromLatin1(StageIds[i]) << m_spaces[i] << width << height
               << QStringLiteral("row-major RGB IEEE754 binary32 little-endian");
        boundary.hash->addData(descriptor);
    }
}

void ColorStageDiagnostics::observe(ColorStage stage, int x, float r, float g, float b) {
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
    auto &boundary=m_boundaries[int(stage)];
    const std::array<float,3> rgb{r,g,b};
    for (int c=0;c<3;++c) {
        qToLittleEndian<quint32>(std::bit_cast<quint32>(rgb[c]),
            boundary.row.data()+(qsizetype(x)*3+c)*sizeof(float));
        if (std::isfinite(rgb[c])) {
            boundary.minimum[c]=std::min(boundary.minimum[c],double(rgb[c]));
            boundary.maximum[c]=std::max(boundary.maximum[c],double(rgb[c]));
        } else ++boundary.nonFiniteValues;
        if (boundary.pixels==0) boundary.first[c]=rgb[c];
    }
    ++boundary.pixels;
}

void ColorStageDiagnostics::finishRow() {
    for (auto &boundary:m_boundaries) boundary.hash->addData(QByteArrayView(boundary.row));
    ++m_rows;
}

QJsonObject ColorStageDiagnostics::result() const {
    QJsonArray entries;
    for (int i=0;i<Count;++i) {
        const auto &boundary=m_boundaries[i];
        entries.append(QJsonObject{{"id",StageIds[i]},{"color_space",m_spaces[i]},
            {"pixel_sha256",QString::fromLatin1(boundary.hash->result().toHex())},
            {"pixels",boundary.pixels},{"non_finite_values",boundary.nonFiniteValues},
            {"minimum_rgb",rgbArray(boundary.minimum)},{"maximum_rgb",rgbArray(boundary.maximum)},
            {"first_rgb",rgbArray(boundary.first)}});
    }
    return {{"schema",1},{"available",m_rows==m_height},{"backend","cpu-reference"},
        {"engine",ProcessingPlan::EngineVersion},{"width",m_width},{"height",m_height},
        {"input_encoding",m_inputEncoding},{"source_storage","RGBA64; transient FP32 color arithmetic"},
        {"hash_format","descriptor plus row-major RGB IEEE754 binary32 little-endian; no alpha or row padding"},
        {"row_buffer_bytes",qint64(Count)*m_width*3*qint64(sizeof(float))},
        {"monitor_icc","excluded; before screen presentation"},
        {"note","CPU boundaries, not float RAW decoding or GPU readback; FP32 hashes may vary with platform math"},
        {"entries",entries}};
}
