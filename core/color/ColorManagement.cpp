#include "core/color/ColorManagement.h"

#include <lcms2.h>

namespace {
QColorSpace namedSpace(ColorManagement::OutputSpace space) {
    switch (space) {
    case ColorManagement::OutputSpace::DisplayP3:
        return QColorSpace(QColorSpace::DisplayP3);
    case ColorManagement::OutputSpace::AdobeRgb:
        return QColorSpace(QColorSpace::AdobeRgb);
    case ColorManagement::OutputSpace::ProPhotoRgb:
        return QColorSpace(QColorSpace::ProPhotoRgb);
    case ColorManagement::OutputSpace::SRgb:
    default:
        return QColorSpace(QColorSpace::SRgb);
    }
}
}

ColorManagement::OutputSpace ColorManagement::fromKey(const QString &value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("display-p3") || normalized == QStringLiteral("p3"))
        return OutputSpace::DisplayP3;
    if (normalized == QStringLiteral("adobe-rgb") || normalized == QStringLiteral("adobergb"))
        return OutputSpace::AdobeRgb;
    if (normalized == QStringLiteral("prophoto-rgb") || normalized == QStringLiteral("prophoto"))
        return OutputSpace::ProPhotoRgb;
    return OutputSpace::SRgb;
}

QString ColorManagement::key(OutputSpace space) {
    switch (space) {
    case OutputSpace::DisplayP3: return QStringLiteral("display-p3");
    case OutputSpace::AdobeRgb: return QStringLiteral("adobe-rgb");
    case OutputSpace::ProPhotoRgb: return QStringLiteral("prophoto-rgb");
    case OutputSpace::SRgb:
    default: return QStringLiteral("srgb");
    }
}

QString ColorManagement::displayName(OutputSpace space) {
    switch (space) {
    case OutputSpace::DisplayP3: return QStringLiteral("Display P3");
    case OutputSpace::AdobeRgb: return QStringLiteral("Adobe RGB (1998)");
    case OutputSpace::ProPhotoRgb: return QStringLiteral("ProPhoto RGB");
    case OutputSpace::SRgb:
    default: return QStringLiteral("sRGB");
    }
}

QStringList ColorManagement::keys() {
    return {
        QStringLiteral("srgb"),
        QStringLiteral("display-p3"),
        QStringLiteral("adobe-rgb"),
        QStringLiteral("prophoto-rgb")
    };
}

QColorSpace ColorManagement::colorSpace(OutputSpace space) {
    return namedSpace(space);
}

QByteArray ColorManagement::iccProfile(OutputSpace space) {
    return namedSpace(space).iccProfile();
}

bool ColorManagement::validateIcc(const QByteArray &profile, QString *description) {
    if (description) description->clear();
    if (profile.isEmpty()) return false;

    cmsHPROFILE handle = cmsOpenProfileFromMem(profile.constData(), static_cast<cmsUInt32Number>(profile.size()));
    if (!handle) return false;

    const bool rgb = cmsGetColorSpace(handle) == cmsSigRgbData;
    if (description) {
        char buffer[256]{};
        const cmsUInt32Number count = cmsGetProfileInfoASCII(handle, cmsInfoDescription, "en", "US", buffer, sizeof(buffer));
        if (count > 0) *description = QString::fromUtf8(buffer).trimmed();
    }
    cmsCloseProfile(handle);
    return rgb;
}

QImage ColorManagement::convertFromSrgb(const QImage &source, OutputSpace target) {
    if (source.isNull()) return {};

    QImage tagged = source;
    if (!tagged.colorSpace().isValid()) tagged.setColorSpace(QColorSpace(QColorSpace::SRgb));

    const QColorSpace destination = namedSpace(target);
    if (!destination.isValid()) return tagged;
    if (tagged.colorSpace() == destination) return tagged;

    QImage converted = tagged.convertedToColorSpace(destination);
    if (converted.isNull()) return tagged;
    converted = converted.convertToFormat(QImage::Format_RGBA64);
    converted.setColorSpace(destination);
    converted.setText(QStringLiteral("JixelLightOutputColorSpace"), displayName(target));
    converted.setText(QStringLiteral("JixelLightICCManaged"), QStringLiteral("true"));
    return converted;
}
