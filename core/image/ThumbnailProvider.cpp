#include "core/image/ThumbnailProvider.h"
#include "core/raw/RawDecoder.h"

#include <QCache>
#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSemaphore>
#include <QUrl>
#include <QtConcurrent>
#include <memory>

struct ThumbnailStore {
    QMutex mutex;
    // Cache the decoded, bounded thumbnail, never the original RAW or a
    // full-resolution image. QCache costs are measured in image bytes.
    QCache<QString, QImage> images {64 << 20};
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

    // Restrict parallel LibRaw thumbnail unpacking on rapid scrolling. No
    // thumbnail request enters the full RAW develop / project-save path.
    static QSemaphore decodeSlots(2);
    decodeSlots.acquire();
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
    decodeSlots.release();

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

class ThumbnailResponse final : public QQuickImageResponse {
public:
    ThumbnailResponse(QString path, QSize size, std::shared_ptr<ThumbnailStore> store) {
        connect(&m_watcher, &QFutureWatcher<QImage>::finished, this, [this] {
            m_image = m_watcher.result();
            emit finished();
        });
        m_watcher.setFuture(QtConcurrent::run([path = std::move(path), size, store = std::move(store)] {
            return loadThumbnail(path, size, store);
        }));
    }

    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    QFutureWatcher<QImage> m_watcher;
    QImage m_image;
};
} // namespace

ThumbnailProvider::ThumbnailProvider() : m_store(std::make_shared<ThumbnailStore>()) {}

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    // QML uses encodeURIComponent(path). Do not interpret a remote URL here.
    const QString path = QUrl::fromPercentEncoding(id.toUtf8());
    return new ThumbnailResponse(path, requestedSize, m_store);
}
