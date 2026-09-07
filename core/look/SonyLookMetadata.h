#pragma once
#include <QVariantMap>
#include <exiv2/exiv2.hpp>

// Reads only whitelisted Sony MakerNote fields. Never guesses a look from pixels.
namespace SonyLookMetadata {
QVariantMap read(const Exiv2::ExifData &exif);
QString normalize(const QString &name);
QStringList parameterNames();
QPair<int,int> parameterRange(const QString &name);
}
