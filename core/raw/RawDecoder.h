#pragma once

#include <QImage>
#include <QString>

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
    bool highlightBlendEnabled = true;
};

class RawDecoder final {
public:
    static bool isRawFile(const QString &path);
    static QImage decode(const QString &path, QString *errorMessage = nullptr, RawMetadata *metadata = nullptr);
};
