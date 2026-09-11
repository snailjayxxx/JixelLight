#pragma once

#include <QRect>
#include <QString>

struct RawContainerInfo {
    QRect defaultCrop;
    int blackLevel = -1;
    int whiteLevel = -1;
    QString cropSource;
};

namespace RawContainerMetadata {
// Reads only bounded TIFF/DNG structural tags needed before LibRaw processing.
// Unsupported containers fail closed and return an empty result.
RawContainerInfo read(const QString &path);
}
