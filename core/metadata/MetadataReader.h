#pragma once

#include <QString>
#include <QVariantMap>

class MetadataReader {
public:
    static QVariantMap read(const QString &path, QString *errorMessage = nullptr);
};
