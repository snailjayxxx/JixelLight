#pragma once

#include <QImage>
#include <QString>

struct RawMetadata {
    QString make;
    QString model;
    int width = 0;
    int height = 0;
    int bitsPerChannel = 0;
};

class RawDecoder final {
public:
    static bool isRawFile(const QString &path);
    static QImage decode(const QString &path, QString *errorMessage = nullptr, RawMetadata *metadata = nullptr);
};
