#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

class ProcessedImageProvider final : public QQuickImageProvider {
public:
    ProcessedImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    void setImage(const QImage &image);
    void setReference(const QImage &image);
    // Presentation-only transform shared with the GPU display pass. Source
    // images are retained unchanged so a monitor change can regenerate display
    // copies without touching processing/scopes/export pixels.
    void setDisplayColorLut(const QImage &atlas, const QString &key);

private:
    static QImage displayCopy(const QImage &source, const QImage &atlas, const QString &key);
    QMutex m_mutex;
    QImage m_imageSource, m_referenceSource;
    QImage m_image, m_reference;
    QImage m_displayLut;
    QString m_displayLutKey = QStringLiteral("identity-srgb");
};
