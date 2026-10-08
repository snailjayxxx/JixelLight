#include "core/image/ThumbnailProvider.h"
#include "core/raw/RawDecoder.h"

#include <QCache>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QThreadPool>
#include <QRunnable>
#include <atomic>
#include <QUrl>
#include <memory>

struct ThumbnailStore {
    QMutex mutex;
    // Cache the decoded, bounded thumbnail, never the original RAW or a
    // full-resolution image. QCache costs are measured in image bytes.
    QCache<QString, QImage> images {64 << 20};
    QThreadPool pool;

    ThumbnailStore() { pool.setMaxThreadCount(2); }
};

namespace {
QImage loadThumbnail(const QString &path, QSize size, const std::shared_ptr<ThumbnailStore> &store) {
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isFile() || !info.isReadable()) return {};

    size = size.isValid() ? size.boundedTo(QSize(512, 512)) : QSize(320, 220);
    const QString key = info.absoluteFilePath()
        + QLatin1Char('|') + QString::number(info.size())
        + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch())
        + QLatin1Char('|') + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
    {
        QMutexLocker lock(&store->mutex);
        if (const QImage *cached = store->images.object(key)) return *cached;
    }

    // A dedicated pool limits this to two concurrent image decodes; unlike
    // blocking the global QThreadPool it leaves existing preview/export jobs
    // free to finish while the user rapidly scrolls the filmstrip.
    QImage image;
    if (RawDecoder::isRawFile(path)) {
        image = RawDecoder::thumbnail(path);
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize original = reader.size();
        if (original.isValid())
            reader.setScaledSize(original.scaled(size, Qt::KeepAspectRatio));
        image = reader.read();
    }

    if (image.isNull()) return {};
    if (image.width() > size.width() || image.height() > size.height())
        image = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    {
        QMutexLocker lock(&store->mutex);
        // Two tasks for the same key can safely race: both decoded the same
        // immutable file snapshot and the later cache entry replaces the first.
        store->images.insert(key, new QImage(image), qMax(1, int(image.sizeInBytes())));
    }
    return image;
}

class ThumbnailResponse final : public QQuickImageResponse, public QRunnable {
public:
    ThumbnailResponse(QString path, QSize size, std::shared_ptr<ThumbnailStore> store)
        : m_path(std::move(path)), m_size(size), m_store(std::move(store)) {
        // Qt Quick owns the QQuickImageResponse. Never let QThreadPool delete
        // it as a QRunnable; Qt Quick calls deleteLater() after finished().
        setAutoDelete(false);
    }

    void run() override {
        QImage image;
        if (!m_cancelled.load(std::memory_order_relaxed))
            image = loadThumbnail(m_path, m_size, m_store);
        {
            QMutexLocker lock(&m_resultMutex);
            m_image = std::move(image);
        }
        emit finished();
    }

    void cancel() override { m_cancelled.store(true, std::memory_order_relaxed); }

    QQuickTextureFactory *textureFactory() const override {
        QMutexLocker lock(&m_resultMutex);
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    QString m_path;
    QSize m_size;
    std::shared_ptr<ThumbnailStore> m_store;
    std::atomic_bool m_cancelled {false};
    mutable QMutex m_resultMutex;
    QImage m_image;
};
} // namespace

ThumbnailProvider::ThumbnailProvider() : m_store(std::make_shared<ThumbnailStore>()) {}

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    // QML uses encodeURIComponent(path). No network URLs or full RAW decode.
    const QString path = QUrl::fromPercentEncoding(id.toUtf8());
    auto *response = new ThumbnailResponse(path, requestedSize, m_store);
    m_store->pool.start(response);
    return response;
}
