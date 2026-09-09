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
    // LibRaw mode 2 reconstructs/blends clipped channel relationships during
    // RAW development. JixelLight's later highlightRecovery/displayShoulder
    // are tonal controls, not a second clipped-channel reconstruction pass.
    bool highlightBlendEnabled = true;
    int highlightMode = 2;
    float adjustMaximumThreshold = 0.75f;
};

class RawDecoder final {
public:
    static bool isRawFile(const QString &path);
    static QImage decode(const QString &path, QString *errorMessage = nullptr, RawMetadata *metadata = nullptr, const CancelToken &cancel = {});
    static QImage thumbnail(const QString &path, const CancelToken &cancel = {});
};
