#include "core/image/ProcessedImageProvider.h"
#include "core/color/MonitorColorTransform.h"
#include <QColorSpace>
#include <QMutexLocker>
#include <memory>

namespace {
QImage retainBrushDisplay(const QImage &display,const QImage &operation) {
    if(display.isNull()||operation.isNull())return {};
    try {
        struct Owner{QImage display,operation;};
        auto owner=std::make_unique<Owner>(Owner{display,operation});const auto *pixels=display.constBits();
        QImage out(pixels,display.width(),display.height(),display.bytesPerLine(),display.format(),
            [](void *p){delete static_cast<Owner *>(p);},owner.get());
        if(out.isNull())return {};owner.release();
        out.setColorSpace(display.colorSpace());out.setDevicePixelRatio(display.devicePixelRatio());
        for(const auto &key:display.textKeys())out.setText(key,display.text(key));
        return out.constBits()==pixels?out:QImage{};
    }catch(...){return {};}
}
}

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
    const bool brush=id.startsWith("brush-live/");
    QImage result = brush ? (id==m_brushId?m_brushImage:QImage{}) :
        id.startsWith("scopes/") ? m_scopePlot : id.startsWith("reference/") ? m_reference : m_image;
    lock.unlock();
    if (size) *size = result.size();
    if (requestedSize.isValid() && !result.isNull()) {
        if(brush) {
            if(qint64(requestedSize.width())*requestedSize.height()>32LL*1024*1024)return {};
            return retainBrushDisplay(result.scaled(requestedSize,Qt::KeepAspectRatio,Qt::SmoothTransformation),result);
        }
        result = result.scaled(requestedSize, id.startsWith("scopes/") ? Qt::IgnoreAspectRatio : Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
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

void ProcessedImageProvider::setScopePlot(const QImage &image) {
    QMutexLocker lock(&m_mutex); m_scopePlot=image;
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
    QImage imageSource,referenceSource,brushSource;QString brushId;
    QString effective=key.isEmpty()?QStringLiteral("identity-srgb"):key;
    {
        QMutexLocker lock(&m_mutex);
        if(m_displayLutKey==effective&&m_displayLut.cacheKey()==atlas.cacheKey())return;
        m_displayLut=atlas;m_displayLutKey=effective;
        imageSource=m_imageSource;referenceSource=m_referenceSource;
        brushSource=m_brushSource;brushId=m_brushId;
    }
    const QImage displayImage=displayCopy(imageSource,atlas,effective);
    const QImage displayReference=displayCopy(referenceSource,atlas,effective);
    const QImage displayBrush=retainBrushDisplay(displayCopy(brushSource,atlas,effective),brushSource);
    QMutexLocker lock(&m_mutex);
    if(m_displayLutKey!=effective||m_displayLut.cacheKey()!=atlas.cacheKey())return;
    if(m_imageSource.cacheKey()==imageSource.cacheKey())m_image=displayImage;
    if(m_referenceSource.cacheKey()==referenceSource.cacheKey())m_reference=displayReference;
    if(m_brushId==brushId&&m_brushSource.cacheKey()==brushSource.cacheKey())m_brushImage=displayBrush;
}

bool ProcessedImageProvider::setBrushPreview(const QString &id,const QImage &image) {
    if(!id.startsWith("brush-live/")||image.isNull())return false;
    QImage lut;QString key;quint64 epoch;
    {QMutexLocker lock(&m_mutex);lut=m_displayLut;key=m_displayLutKey;epoch=++m_brushEpoch;}
    const auto display=retainBrushDisplay(displayCopy(image,lut,key),image);
    if(display.isNull())return false;
    QMutexLocker lock(&m_mutex);
    if(epoch!=m_brushEpoch||key!=m_displayLutKey||lut.cacheKey()!=m_displayLut.cacheKey())return false;
    m_brushId=id;m_brushSource=image;m_brushImage=display;return true;
}
bool ProcessedImageProvider::renameBrushPreview(const QString &id) {
    QMutexLocker lock(&m_mutex);if(!id.startsWith("brush-live/")||m_brushImage.isNull())return false;
    m_brushId=id;++m_brushEpoch;return true;
}
void ProcessedImageProvider::clearBrushPreview() {
    QMutexLocker lock(&m_mutex);++m_brushEpoch;m_brushId.clear();m_brushSource={};m_brushImage={};
}
