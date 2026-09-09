#include "core/color/MonitorColorTransform.h"
#include "core/color/ColorManagement.h"

#include <QCryptographicHash>
#include <QFile>
#include <QScreen>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

#include <lcms2.h>

#if defined(Q_OS_WIN)
#include <QWindowsScreen>
#include <windows.h>
#include <vector>
#endif

namespace {
QImage makeAtlas(const QVector<float> &rgb, int n) {
    if (n < 2 || rgb.size() != n * n * n * 3) return {};
    QImage out(n * n, n, QImage::Format_RGBA32FPx4);
    if (out.isNull()) return {};
    for (int b = 0; b < n; ++b) {
        for (int g = 0; g < n; ++g) {
            auto *row = reinterpret_cast<float *>(out.scanLine(g));
            for (int r = 0; r < n; ++r) {
                const int source = (b * n * n + g * n + r) * 3;
                const int target = (b * n + r) * 4;
                row[target + 0] = rgb[source + 0];
                row[target + 1] = rgb[source + 1];
                row[target + 2] = rgb[source + 2];
                row[target + 3] = 1.0f;
            }
        }
    }
    return out;
}
}

namespace MonitorColorTransform {
MonitorColorProfile profileForScreen(QScreen *screen) {
    MonitorColorProfile result;
#if defined(Q_OS_WIN)
    if (!screen) return result;
    const auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
    if (!native || !native->handle()) return result;

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(native->handle(), &info)) return result;

    // MONITORINFOEX::szDevice is the native display-device name. Using a
    // monitor-specific DC is important: CreateDC("DISPLAY", nullptr, ...) spans
    // the virtual desktop and cannot identify the correct per-monitor profile.
    HDC dc = CreateDCW(info.szDevice, info.szDevice, nullptr, nullptr);
    if (!dc) dc = CreateDCW(L"DISPLAY", info.szDevice, nullptr, nullptr);
    if (!dc) return result;
    SetICMMode(dc, ICM_ON);

    DWORD chars = 0;
    GetICMProfileW(dc, &chars, nullptr);
    if (chars == 0 || chars > 32768) {
        DeleteDC(dc);
        return result;
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(chars) + 1u, L'\0');
    DWORD capacity = static_cast<DWORD>(buffer.size());
    if (!GetICMProfileW(dc, &capacity, buffer.data())) {
        DeleteDC(dc);
        return result;
    }
    DeleteDC(dc);

    result.sourcePath = QString::fromWCharArray(buffer.data()).trimmed();
    QFile file(result.sourcePath);
    if (result.sourcePath.isEmpty() || !file.open(QIODevice::ReadOnly)) return result;
    result.icc = file.readAll();
    if (result.icc.isEmpty()) return result;

    QString description;
    if (!ColorManagement::validateIcc(result.icc, &description)) {
        result.icc.clear();
        return result;
    }
    result.description = description.isEmpty() ? screen->name() : description;
    result.key = QString::fromLatin1(QCryptographicHash::hash(result.icc, QCryptographicHash::Sha256).toHex());
    result.valid = true;
#else
    Q_UNUSED(screen);
#endif
    return result;
}

QImage identityLut(int size) {
    const int n = std::clamp(size, 2, 65);
    QVector<float> rgb(n * n * n * 3);
    int index = 0;
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                rgb[index++] = float(r) / float(n - 1);
                rgb[index++] = float(g) / float(n - 1);
                rgb[index++] = float(b) / float(n - 1);
            }
    return makeAtlas(rgb, n);
}

QImage srgbToMonitorLut(const QByteArray &monitorIcc, int size, QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    const int n = std::clamp(size, 2, 65);
    if (monitorIcc.isEmpty()) return identityLut(n);

    cmsHPROFILE source = cmsCreate_sRGBProfile();
    cmsHPROFILE target = cmsOpenProfileFromMem(monitorIcc.constData(), static_cast<cmsUInt32Number>(monitorIcc.size()));
    if (!source || !target || cmsGetColorSpace(target) != cmsSigRgbData) {
        if (errorMessage) *errorMessage = QStringLiteral("Monitor profile is not a readable RGB ICC profile");
        if (source) cmsCloseProfile(source);
        if (target) cmsCloseProfile(target);
        return {};
    }

    const cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_HIGHRESPRECALC;
    cmsHTRANSFORM transform = cmsCreateTransform(source, TYPE_RGB_FLT, target, TYPE_RGB_FLT,
                                                 INTENT_RELATIVE_COLORIMETRIC, flags);
    if (!transform) {
        if (errorMessage) *errorMessage = QStringLiteral("LittleCMS could not create the sRGB-to-monitor transform");
        cmsCloseProfile(source);
        cmsCloseProfile(target);
        return {};
    }

    QVector<float> input(n * n * n * 3);
    int index = 0;
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                input[index++] = float(r) / float(n - 1);
                input[index++] = float(g) / float(n - 1);
                input[index++] = float(b) / float(n - 1);
            }
    QVector<float> output(input.size());
    cmsDoTransform(transform, input.constData(), output.data(), static_cast<cmsUInt32Number>(n * n * n));
    cmsDeleteTransform(transform);
    cmsCloseProfile(source);
    cmsCloseProfile(target);

    for (float &value : output) {
        if (!std::isfinite(value)) {
            if (errorMessage) *errorMessage = QStringLiteral("Monitor ICC transform produced a non-finite sample");
            return {};
        }
        value = std::clamp(value, 0.0f, 1.0f);
    }
    return makeAtlas(output, n);
}
}
