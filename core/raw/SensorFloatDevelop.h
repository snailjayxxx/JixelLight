#pragma once
#include "core/raw/SensorLinearMosaic.h"
#include <QImage>
#include <QJsonObject>
#include <array>

// Stage 6B laboratory-only Bayer -> scene-linear FP32. No default RAW,
// UI, history, cache or export behavior is changed.
struct SensorFloatDevelop final {
    struct Profile {
        QByteArray profileSha256; // independently evidenced WB/color calibration
        std::array<double,4> whiteBalanceBySite{1.,1.,1.,1.};
        // Row-major camera RGB -> D50 linear ProPhoto (after WB/demosaic).
        std::array<double,9> cameraToProPhoto{1.,0.,0.,0.,1.,0.,0.,0.,1.};
    };
    struct Frame {
        QImage image;             // RGBA32FPx4 / linear ProPhoto; alpha=1
        QRect rawRoi;             // absolute sensor pixel coordinates
        QByteArray decoderSignature;
        QJsonObject diagnostics;
        QString error;
        bool valid() const { return error.isEmpty() && !image.isNull(); }
    };
    static bool validProfile(const Profile &profile);
    // Includes Stage 6A source/calibration signature and every WB/matrix bit.
    static QByteArray decoderSignature(const SensorLinearMosaic::Calibration &sensor,
                                       const Profile &profile);
    // Input must contain a 1-pixel halo of outputRoi wherever active CFA
    // pixels exist. Incomplete support, mismatch or cancellation fail closed.
    static Frame developTile(const SensorLinearMosaic::Tile &mosaic,
                             const SensorLinearMosaic::Calibration &sensor,
                             const Profile &profile,const QRect &outputRoi,
                             const CancelToken &cancel={});
};
