#pragma once
#include "core/async/LatestJob.h"
#include <QByteArray>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <array>

// EXPERIMENTAL: one float sample per Bayer sensor site, NOT a demosaiced RGB
// source. Calibration and original file digest MUST be independently supplied.
// No user-visible RAW path calls this, and it is never silently cached as RGB.
struct SensorLinearMosaic final {
    struct Layout {
        int rawWidth=0,rawHeight=0,pitchBytes=0;
        QRect active; // sensor coordinates; CFA phase starts at active.topLeft()
    };
    struct Calibration {
        QByteArray sourceSha256;      // lowercase SHA-256 of complete original file
        QByteArray calibrationSha256; // identity of external per-camera/mode evidence
        std::array<quint8,4> cfa{};   // visible-origin Bayer 2x2: 0=R,1=G,2=B,3=G2
        std::array<quint32,4> black{}; // absolute U16 black at each Bayer SITE
        std::array<quint32,4> white{}; // explicitly calibrated U16 white at each SITE
    };
    struct Tile {
        QRect rawRoi; // absolute raw sensor coordinates
        QVector<float> samples; // row-major single-channel, no clamp to [0,1]
        QByteArray decoderSignature;
        QJsonObject statistics;
        QString error;
        bool valid() const { return error.isEmpty() && !samples.isEmpty(); }
    };
    static bool validCalibration(const Calibration &calibration);
    // Versioned, canonical settings identity for future SourceCache signatures.
    static QByteArray decoderSignature(const Calibration &calibration);
    static Tile normalizeTile(const quint16 *raw, const Layout &layout,
                              const Calibration &calibration, const QRect &rawRoi,
                              const CancelToken &cancel={});
};
