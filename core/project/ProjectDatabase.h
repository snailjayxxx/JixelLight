#pragma once
#include "core/commands/EditHistory.h"
#include "core/library/CatalogTags.h"
#include <QObject>
#include <QHash>
#include <QVector>
#include <QThread>
#include <QMutex>
#include <memory>

// All SQL handles are created, queried and destroyed on one dedicated thread.
class ProjectDatabase final : public QObject {
    Q_OBJECT
public:
    struct PhotoCuration { int rating = 0; QString flag = QStringLiteral("none"); };
    struct SavedPhoto { QString path; AdjustmentState adjustments; int rating = 0; QString flag = QStringLiteral("none"); EditHistory history; CatalogTags tags; QString copyKey; QString versionName; };
    explicit ProjectDatabase(QObject *parent = nullptr);
    ~ProjectDatabase() override;
    bool create(const QString &projectDirectory, const QString &projectName);
    // Read and validate an existing .jlp catalog before switching the writer.
    // A failed open never replaces the currently opened database.
    bool open(const QString &projectDirectory, QVector<SavedPhoto> *photos);
    // Curation is independent of Develop adjustments, so syncing presets
    // cannot overwrite a photo's personal pick/reject/rating decision.
    bool updateCurationBatch(const QHash<QString, PhotoCuration> &changes);
    bool addVirtualCopy(const QString &key, const QString &source, const QString &name, const AdjustmentState &adjustments, const EditHistory &history, const CatalogTags &tags, const PhotoCuration &curation);
    bool updateTagsBatch(const QHash<QString, CatalogTags> &changes);
    bool addOrUpdatePhoto(const QString &path, const AdjustmentState &state);
    bool updateAdjustment(const QString &path, const AdjustmentState &state);
    bool updateBatch(const QHash<QString, AdjustmentState> &states, const QHash<QString, EditHistory> &histories = {});
    bool flush();
    bool isOpen() const { return m_open; }
    QString projectPath() const { return m_projectPath; }
    QString projectName() const { return m_projectName; }
    QString lastError() const;
signals:
    void saved(int count);
    void writeFailed(const QString &message);
private:
    struct WorkerState { QString connectionName; QString error; QHash<QString,QString> failedWrites; QMutex mutex; };
    std::shared_ptr<WorkerState> m_state;
    QThread m_thread;
    QObject *m_worker = nullptr;
    QString m_projectPath, m_projectName;
    QString m_lastOpenError; // GUI thread only; does not poison the active writer's save status
    bool m_open = false;
};
