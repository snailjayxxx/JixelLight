#pragma once
#include "core/async/LatestJob.h"
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
    SourceData get(const QString &key);
    void put(const SourceData &source);
    SourceData diskPreview(const QString &key);
    void storeDiskPreview(const SourceData &source, const CancelToken &cancel = {});
    qint64 memoryBytes() const;
    qint64 budgetBytes() const { return qint64(m_budgetKiB) * 1024; }
private:
    void trimDisk();
    mutable QMutex m_mutex;
    QMutex m_diskMutex;
    QCache<QString, SourceData> m_memory;
    QString m_diskDirectory;
    int m_budgetKiB;
};
SourceData loadSource(SourceCache &cache, const QString &path, const CancelToken &cancel,
                      const std::function<void(SourceData)> &partial = {});
