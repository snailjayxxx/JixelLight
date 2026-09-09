#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

class QScreen;

struct MonitorColorProfile {
    QByteArray icc;
    QString description;
    QString sourcePath;
    QString key;
    bool valid = false;
};

namespace MonitorColorTransform {
// The display path is intentionally separate from the image-processing path:
// this profile/LUT must never affect export pixels, histograms, or RAW fitting.
MonitorColorProfile profileForScreen(QScreen *screen);
QImage identityLut(int size = 33);
QImage srgbToMonitorLut(const QByteArray &monitorIcc, int size = 33,
                        QString *errorMessage = nullptr);
}
