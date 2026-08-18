#include "core/image/ProcessedImageProvider.h"
#include <QMutexLocker>

ProcessedImageProvider::ProcessedImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ProcessedImageProvider::requestImage(const QString &, QSize *size, const QSize &requestedSize) {
    QMutexLocker lock(&m_mutex);
    QImage result = m_image;
    if (size) *size = result.size();
    if (requestedSize.isValid() && !result.isNull())
        result = result.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return result;
}

void ProcessedImageProvider::setImage(const QImage &image) {
    QMutexLocker lock(&m_mutex);
    m_image = image;
}
