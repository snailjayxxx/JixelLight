#include "core/color/MonitorColorTransform.h"
#include "core/color/ColorManagement.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QRgba64>
#include <QScreen>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

#include <lcms2.h>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

struct Rgb { float r=0,g=0,b=0; };
Rgb lutFetch(const QImage &atlas,int n,int r,int g,int b) {
    const auto *row=reinterpret_cast<const float *>(atlas.constScanLine(g));
    const int x=b*n+r;
    return {row[x*4+0],row[x*4+1],row[x*4+2]};
}
Rgb mixRgb(Rgb a,Rgb b,float t) {
    return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};
}
Rgb sampleLut(const QImage &atlas,int n,float r,float g,float b) {
    const float limit=float(n-1);
    const float pr=std::clamp(r,0.0f,1.0f)*limit;
    const float pg=std::clamp(g,0.0f,1.0f)*limit;
    const float pb=std::clamp(b,0.0f,1.0f)*limit;
    const int r0=std::min(int(std::floor(pr)),n-1),r1=std::min(r0+1,n-1);
    const int g0=std::min(int(std::floor(pg)),n-1),g1=std::min(g0+1,n-1);
    const int b0=std::min(int(std::floor(pb)),n-1),b1=std::min(b0+1,n-1);
    const float fr=pr-r0,fg=pg-g0,fb=pb-b0;
    const Rgb c000=lutFetch(atlas,n,r0,g0,b0),c100=lutFetch(atlas,n,r1,g0,b0);
    const Rgb c010=lutFetch(atlas,n,r0,g1,b0),c110=lutFetch(atlas,n,r1,g1,b0);
    const Rgb c001=lutFetch(atlas,n,r0,g0,b1),c101=lutFetch(atlas,n,r1,g0,b1);
    const Rgb c011=lutFetch(atlas,n,r0,g1,b1),c111=lutFetch(atlas,n,r1,g1,b1);
    const Rgb z0=mixRgb(mixRgb(c000,c100,fr),mixRgb(c010,c110,fr),fg);
    const Rgb z1=mixRgb(mixRgb(c001,c101,fr),mixRgb(c011,c111,fr),fg);
    return mixRgb(z0,z1,fb);
}
}

namespace MonitorColorTransform {
MonitorColorProfile profileForScreen(QScreen *screen) {
    MonitorColorProfile result;
#if defined(Q_OS_WIN)
    if (!screen) return result;
    // On Windows QScreen::name() is the display device identifier exposed by
    // the platform plugin (normally \\.\DISPLAYn). Using that device name for
    // the DC avoids any Qt private/native-interface header dependency while
    // still selecting the ICC profile of the screen that contains the window.
    const std::wstring device = screen->name().toStdWString();
    if (device.empty()) return result;
    HDC dc = CreateDCW(device.c_str(), device.c_str(), nullptr, nullptr);
    if (!dc) dc = CreateDCW(L"DISPLAY", device.c_str(), nullptr, nullptr);
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

QImage applyLut(const QImage &encodedImage,const QImage &atlas,int size) {
    if(encodedImage.isNull())return {};
    const int n=std::clamp(size,2,65);
    if(atlas.isNull()||atlas.format()!=QImage::Format_RGBA32FPx4||atlas.size()!=QSize(n*n,n))return {};

    const QColorSpace srgb(QColorSpace::SRgb);
    QImage input=encodedImage;
    if(input.colorSpace().isValid()&&input.colorSpace()!=srgb) {
        const QImage converted=input.convertedToColorSpace(srgb,QImage::Format_RGBA64);
        if(!converted.isNull())input=converted;
        else input=input.convertToFormat(QImage::Format_RGBA64);
    } else {
        input=input.convertToFormat(QImage::Format_RGBA64);
    }
    if(input.isNull())return {};

    QImage out(input.size(),QImage::Format_RGBA64);
    if(out.isNull())return {};
    for(int y=0;y<input.height();++y) {
        const auto *src=reinterpret_cast<const QRgba64 *>(input.constScanLine(y));
        auto *dst=reinterpret_cast<QRgba64 *>(out.scanLine(y));
        for(int x=0;x<input.width();++x) {
            const auto p=src[x];
            const Rgb mapped=sampleLut(atlas,n,p.red()/65535.0f,p.green()/65535.0f,p.blue()/65535.0f);
            const auto q=[](float v){return quint16(std::lround(std::clamp(v,0.0f,1.0f)*65535.0f));};
            dst[x]=QRgba64::fromRgba64(q(mapped.r),q(mapped.g),q(mapped.b),p.alpha());
        }
    }
    // The pixels are already in monitor-device RGB. Leaving the image untagged
    // prevents a second image-profile conversion in presentation layers.
    out.setColorSpace(QColorSpace());
    out.setText(QStringLiteral("JixelLightDisplayManaged"),QStringLiteral("true"));
    out.setText(QStringLiteral("JixelLightDisplayTransform"),QStringLiteral("shared 33^3 sRGB-to-monitor LUT"));
    return out;
}
}
