#pragma once

#include <QImage>
#include <QString>
#include <QJsonObject>
#include "core/async/LatestJob.h"
#include "core/raw/SensorLinearMosaic.h"

struct RawMetadata {
    QString make;
    QString model;
    int width = 0;
    int height = 0;
    int bitsPerChannel = 0;
    QString workingSpace = QStringLiteral("Linear ProPhoto RGB");
    QString demosaic = QStringLiteral("AHD");
    bool cameraMatrixEnabled = true;
    bool cameraWhiteBalanceEnabled = true;
    // Decoder policy is LibRaw highlight mode 1 (unclip). JixelLight keeps
    // clipped-channel reconstruction out of LibRaw so downstream highlight
    // recovery/tone controls have one clearly owned rendering stage.
    bool highlightBlendEnabled = false;
    int highlightMode = 1;
    float adjustMaximumThreshold = 0.75f;
    // Diagnostic opt-in only. Real RAW development never depends on the
    // pre-demosaic sensor audit and remains the accepted RGBA64 path.
    bool captureSensorAudit = false;
    QJsonObject sensorAudit;
};

class RawDecoder final {
public:
    static bool isRawFile(const QString &path);
    static QImage decode(const QString &path, QString *errorMessage = nullptr, RawMetadata *metadata = nullptr, const CancelToken &cancel = {});
    // Explicit calibrated CFA tile experiment only; never called by normal rendering.
    static SensorLinearMosaic::Tile calibratedMosaicTile(const QString &path,
        const SensorLinearMosaic::Calibration &calibration, const QRect &rawRoi,
        QString *errorMessage=nullptr, const CancelToken &cancel={});
    static QImage thumbnail(const QString &path, const CancelToken &cancel = {});
};
