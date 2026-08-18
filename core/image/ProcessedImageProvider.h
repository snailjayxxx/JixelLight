#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class ProcessedImageProvider final : public QQuickImageProvider {
public:
    ProcessedImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    void setImage(const QImage &image);

private:
    QMutex m_mutex;
    QImage m_image;
};
