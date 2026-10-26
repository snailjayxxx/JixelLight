#pragma once
#include "core/async/LatestJob.h"
#include <QByteArray>
#include <QCache>
#include <QImage>
#include <QMutex>
#include <QVariantMap>
#include <functional>

struct SourceData {
    QImage image;
    QVariantMap metadata;
    QString key;
    QString error;
    bool fullResolution = false;
    bool placeholder = false;
};
class SourceCache final {
public:
    explicit SourceCache(qint64 memoryBytes = 0, QString diskDirectory = {});
    static QString fileKey(const QString &path);
    // Explicit relinking can replace bytes without changing size/mtime. Make
    // old in-flight/cache keys unreachable and remove the old disk preview.
    void invalidatePath(const QString &path);
    SourceData get(const QString &key);
    void put(const SourceData &source);
    SourceData diskPreview(const QString &key);
    void storeDiskPreview(const SourceData &source, const CancelToken &cancel = {});
    // Opt-in, separate versioned FP32 source cache for future camera-linear
    // decoding. Not used by today's LibRaw 16-bit RAW path or .jlpv previews.
    // decoderSignature is the 64-character SHA-256 of *all* decode settings,
    // sensor interpretation and profile versions, never a user-facing label.
    static QString floatDecoderSignature(const QByteArray &configuration);
    SourceData floatScene(const QString &sourceKey, const QString &decoderSignature,
                          const CancelToken &cancel = {});
    bool storeFloatScene(const SourceData &source, const QString &decoderSignature,
                         const CancelToken &cancel = {});
    qint64 memoryBytes() const;
    qint64 budgetBytes() const { return qint64(m_budgetKiB) * 1024; }
private:
    void trimDisk();
    void trimFloatDisk();
    mutable QMutex m_mutex;
    QMutex m_diskMutex;
    QCache<QString, SourceData> m_memory;
    QString m_diskDirectory;
    int m_budgetKiB;
};
SourceData loadSource(SourceCache &cache, const QString &path, const CancelToken &cancel,
                      const std::function<void(SourceData)> &partial = {});
// Validation must decode actual candidate bytes, never a previous cache hit.
SourceData loadSourceFresh(const QString &path, const CancelToken &cancel);
