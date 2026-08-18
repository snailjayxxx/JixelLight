#include "core/metadata/MetadataReader.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <exiv2/exiv2.hpp>

namespace {
QString valueFor(const Exiv2::ExifData &data, const char *key) {
    try {
        const auto it = data.findKey(Exiv2::ExifKey(key));
        if (it == data.end()) return {};
        const std::string printed = it->print(&data);
        if (!printed.empty()) return QString::fromStdString(printed).trimmed();
        return QString::fromStdString(it->toString()).trimmed();
    } catch (...) {
        return {};
    }
}

QString firstValue(const Exiv2::ExifData &data, std::initializer_list<const char *> keys) {
    for (const char *key : keys) {
        const QString value = valueFor(data, key);
        if (!value.isEmpty()) return value;
    }
    return {};
}

void putIfPresent(QVariantMap &map, const QString &key, const QString &value) {
    if (!value.isEmpty()) map.insert(key, value);
}
}

QVariantMap MetadataReader::read(const QString &path, QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    QVariantMap result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return result;
    }

    const qint64 fileSize = file.size();
    if (fileSize <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Empty image file");
        return result;
    }

    uchar *mapped = file.map(0, fileSize);
    QByteArray fallback;
    const Exiv2::byte *data = nullptr;
    std::size_t size = 0;

    if (mapped) {
        data = reinterpret_cast<const Exiv2::byte *>(mapped);
        size = static_cast<std::size_t>(fileSize);
    } else {
        fallback = file.readAll();
        if (fallback.isEmpty()) {
            if (errorMessage) *errorMessage = file.errorString();
            return result;
        }
        data = reinterpret_cast<const Exiv2::byte *>(fallback.constData());
        size = static_cast<std::size_t>(fallback.size());
    }

    try {
        auto image = Exiv2::ImageFactory::open(data, size);
        if (!image) {
            if (errorMessage) *errorMessage = QStringLiteral("Exiv2 could not identify the image format");
            if (mapped) file.unmap(mapped);
            return result;
        }

        image->readMetadata();
        const Exiv2::ExifData &exif = image->exifData();

        result.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
        result.insert(QStringLiteral("fileSizeBytes"), fileSize);

        putIfPresent(result, QStringLiteral("make"), firstValue(exif, {"Exif.Image.Make", "Exif.Photo.Make"}));
        putIfPresent(result, QStringLiteral("model"), firstValue(exif, {"Exif.Image.Model", "Exif.Photo.Model"}));
        putIfPresent(result, QStringLiteral("lens"), firstValue(exif, {
            "Exif.Photo.LensModel", "Exif.CanonCs.LensType", "Exif.NikonLd3.LensIDNumber",
            "Exif.Sony2.LensID", "Exif.Pentax.LensType"
        }));
        putIfPresent(result, QStringLiteral("shutter"), firstValue(exif, {"Exif.Photo.ExposureTime", "Exif.Photo.ShutterSpeedValue"}));
        putIfPresent(result, QStringLiteral("aperture"), firstValue(exif, {"Exif.Photo.FNumber", "Exif.Photo.ApertureValue"}));
        putIfPresent(result, QStringLiteral("iso"), firstValue(exif, {"Exif.Photo.PhotographicSensitivity", "Exif.Photo.ISOSpeedRatings"}));
        putIfPresent(result, QStringLiteral("focalLength"), firstValue(exif, {"Exif.Photo.FocalLength", "Exif.Photo.FocalLengthIn35mmFilm"}));
        putIfPresent(result, QStringLiteral("captureTime"), firstValue(exif, {"Exif.Photo.DateTimeOriginal", "Exif.Photo.DateTimeDigitized", "Exif.Image.DateTime"}));
        putIfPresent(result, QStringLiteral("exposureBias"), firstValue(exif, {"Exif.Photo.ExposureBiasValue"}));
        putIfPresent(result, QStringLiteral("meteringMode"), firstValue(exif, {"Exif.Photo.MeteringMode"}));
        putIfPresent(result, QStringLiteral("flash"), firstValue(exif, {"Exif.Photo.Flash"}));
        putIfPresent(result, QStringLiteral("orientation"), firstValue(exif, {"Exif.Image.Orientation"}));
        putIfPresent(result, QStringLiteral("colorSpace"), firstValue(exif, {"Exif.Photo.ColorSpace"}));

        if (image->pixelWidth() > 0) result.insert(QStringLiteral("pixelWidth"), static_cast<qulonglong>(image->pixelWidth()));
        if (image->pixelHeight() > 0) result.insert(QStringLiteral("pixelHeight"), static_cast<qulonglong>(image->pixelHeight()));
    } catch (const Exiv2::Error &error) {
        if (errorMessage) *errorMessage = QString::fromUtf8(error.what());
    } catch (const std::exception &error) {
        if (errorMessage) *errorMessage = QString::fromUtf8(error.what());
    }

    if (mapped) file.unmap(mapped);
    return result;
}
