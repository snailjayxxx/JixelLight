#include "core/image/ProcessedImageProvider.h"
#include "core/color/MonitorColorTransform.h"
#include <QColorSpace>
#include <QMutexLocker>

ProcessedImageProvider::ProcessedImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ProcessedImageProvider::displayCopy(const QImage &source,const QImage &atlas,const QString &key) {
    if(source.isNull())return {};
    const QColorSpace srgb(QColorSpace::SRgb);
    if(key.startsWith(QStringLiteral("identity"))||atlas.isNull()) {
        QImage out=source;
        if(out.colorSpace().isValid()&&out.colorSpace()!=srgb) {
            const QImage converted=out.convertedToColorSpace(srgb,QImage::Format_RGBA64);
            if(!converted.isNull())out=converted;
        }
#if defined(Q_OS_WIN)
        // Windows uses an explicit display-device transform in alpha.11. With
        // an identity/sRGB monitor the pixels are already the device values;
        // keep them untagged so no second image-profile conversion is invited.
        out.setColorSpace(QColorSpace());
        out.setText(QStringLiteral("JixelLightDisplayManaged"),QStringLiteral("identity-srgb"));
#else
        // On platforms where alpha.11 does not install an explicit monitor ICC
        // LUT, preserve the sRGB tag and let the platform/Qt presentation path
        // perform its native color management instead of stripping metadata.
        if(!out.colorSpace().isValid())out.setColorSpace(srgb);
        out.setText(QStringLiteral("JixelLightDisplayManaged"),QStringLiteral("platform-srgb"));
#endif
        return out;
    }
    const QImage transformed=MonitorColorTransform::applyLut(source,atlas,33);
    return transformed.isNull()?source:transformed;
}

QImage ProcessedImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    QMutexLocker lock(&m_mutex);
    QImage result = id.startsWith("reference/") ? m_reference : m_image;
    lock.unlock();
    if (size) *size = result.size();
    if (requestedSize.isValid() && !result.isNull())
        result = result.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return result;
}

void ProcessedImageProvider::setImage(const QImage &image) {
    QImage lut;QString key;
    {
        QMutexLocker lock(&m_mutex);
        m_imageSource=image;lut=m_displayLut;key=m_displayLutKey;
    }
    const QImage display=displayCopy(image,lut,key);
    QMutexLocker lock(&m_mutex);
    if(m_imageSource.cacheKey()==image.cacheKey())m_image=display;
}

void ProcessedImageProvider::setReference(const QImage &image) {
    QImage lut;QString key;
    {
        QMutexLocker lock(&m_mutex);
        m_referenceSource=image;lut=m_displayLut;key=m_displayLutKey;
    }
    const QImage display=displayCopy(image,lut,key);
    QMutexLocker lock(&m_mutex);
    if(m_referenceSource.cacheKey()==image.cacheKey())m_reference=display;
}

void ProcessedImageProvider::setDisplayColorLut(const QImage &atlas,const QString &key) {
    QImage imageSource,referenceSource;QString effective=key.isEmpty()?QStringLiteral("identity-srgb"):key;
    {
        QMutexLocker lock(&m_mutex);
        if(m_displayLutKey==effective&&m_displayLut.cacheKey()==atlas.cacheKey())return;
        m_displayLut=atlas;m_displayLutKey=effective;
        imageSource=m_imageSource;referenceSource=m_referenceSource;
    }
    const QImage displayImage=displayCopy(imageSource,atlas,effective);
    const QImage displayReference=displayCopy(referenceSource,atlas,effective);
    QMutexLocker lock(&m_mutex);
    if(m_displayLutKey!=effective||m_displayLut.cacheKey()!=atlas.cacheKey())return;
    if(m_imageSource.cacheKey()==imageSource.cacheKey())m_image=displayImage;
    if(m_referenceSource.cacheKey()==referenceSource.cacheKey())m_reference=displayReference;
}
