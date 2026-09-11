#pragma once

#include <QImage>
#include <QString>
#include "core/async/LatestJob.h"

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
    bool cameraMatrixAvailable = false;
    bool cameraWhiteBalanceAvailable = false;
    bool cameraProfileApplied = false;
    QString cameraProfileSource;
    int calibratedBlackLevel = -1;
    int calibratedWhiteLevel = -1;
    QString defaultCropSource;
    // Decoder policy is LibRaw highlight mode 1 (unclip). JixelLight keeps
    // clipped-channel reconstruction out of LibRaw so downstream highlight
    // recovery/tone controls have one clearly owned rendering stage.
    bool highlightBlendEnabled = false;
    int highlightMode = 1;
    float adjustMaximumThreshold = 0.75f;
};

class RawDecoder final {
public:
    static bool isRawFile(const QString &path);
    static QImage decode(const QString &path, QString *errorMessage = nullptr, RawMetadata *metadata = nullptr, const CancelToken &cancel = {});
    // Returns the largest bounded camera-generated JPEG preview advertised by
    // the RAW container when available. This is a presentation/reference image,
    // never a substitute for the editable RAW pixels.
    static QImage thumbnail(const QString &path, const CancelToken &cancel = {});
};
