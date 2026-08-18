#pragma once

#include <QByteArray>
#include <QColorSpace>
#include <QImage>
#include <QString>
#include <QStringList>

class ColorManagement {
public:
    enum class OutputSpace {
        SRgb,
        DisplayP3,
        AdobeRgb,
        ProPhotoRgb
    };

    static OutputSpace fromKey(const QString &key);
    static QString key(OutputSpace space);
    static QString displayName(OutputSpace space);
    static QStringList keys();

    static QColorSpace colorSpace(OutputSpace space);
    static QByteArray iccProfile(OutputSpace space);
    static bool validateIcc(const QByteArray &profile, QString *description = nullptr);

    // alpha.6 export transform: the editing graph still renders its display result in
    // sRGB. This function performs a color-managed encoding transform and attaches
    // the destination ICC profile. A later pipeline stage will expose wide-gamut
    // working pixels directly for export without the display-referred intermediate.
    static QImage convertFromSrgb(const QImage &source, OutputSpace target);
};
