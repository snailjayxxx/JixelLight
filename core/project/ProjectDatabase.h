#pragma once
#include "core/commands/EditHistory.h"
#include "core/library/CatalogTags.h"
#include "core/library/PhotoTimeline.h"
#include <QObject>
#include <QByteArray>
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
    struct OriginalIdentity {
        qint64 sizeBytes = -1;
        QByteArray sha256; // Complete file SHA-256, 64 lowercase hexadecimal bytes.
        bool isKnown() const;
        bool operator==(const OriginalIdentity &) const = default;
    };
    struct RelinkRequest {
        QString sourceKey, candidatePath;
        OriginalIdentity candidateIdentity;
        bool acknowledgeUnknownIdentity = false;
    };
    struct SavedPhoto { QString path; AdjustmentState adjustments; int rating = 0; QString flag = QStringLiteral("none"); EditHistory history; CatalogTags tags; QString copyKey; QString versionName; PhotoTimeline timeline; QString sourceKey; OriginalIdentity identity; };
    explicit ProjectDatabase(QObject *parent = nullptr);
    ~ProjectDatabase() override;
    bool create(const QString &projectDirectory, const QString &projectName);
    // Read and validate an existing .jlp catalog before switching the writer.
    // A failed open never replaces the currently opened database.
    bool open(const QString &projectDirectory, QVector<SavedPhoto> *photos);
    // A consistent read-only snapshot; never adopts a writer or migrates tables.
    bool readSnapshot(const QString &projectDirectory, QVector<SavedPhoto> *photos);
    // Family comparison key only, never a path to persist or display. Lexical
    // resolution works for missing files; Windows compares without case.
    static QString normalizedSourcePath(const QString &projectDirectory, const QString &sourceKey);
    // Unknown identities return true with a default identity. Reads never migrate.
    bool originalIdentity(const QString &sourceKey, OriginalIdentity *identity);
    // Caller hashes/verifies the source. A known baseline cannot be replaced.
    bool recordOriginalIdentity(const QString &sourceKey, const OriginalIdentity &identity);
    // Caller verifies candidate bytes before calling. No file I/O or hashing
    // occurs here; all source-family database changes commit atomically.
    bool relinkSource(const RelinkRequest &request);
    // Curation is independent of Develop adjustments, so syncing presets
    // cannot overwrite a photo's personal pick/reject/rating decision.
    bool updateCurationBatch(const QHash<QString, PhotoCuration> &changes);
    bool addVirtualCopy(const QString &key, const QString &source, const QString &name, const AdjustmentState &adjustments, const EditHistory &history, const CatalogTags &tags, const PhotoCuration &curation, const PhotoTimeline &timeline = {});
    bool renameVirtualCopy(const QString &key, const QString &name);
    bool removeVirtualCopy(const QString &key);
    bool updateTagsBatch(const QHash<QString, CatalogTags> &changes);
    bool addOrUpdatePhoto(const QString &path, const AdjustmentState &state);
    bool updateAdjustment(const QString &path, const AdjustmentState &state);
    bool updateBatch(const QHash<QString, AdjustmentState> &states, const QHash<QString, EditHistory> &histories = {}, const QHash<QString, PhotoTimeline> &timelines = {});
    bool flush();
    bool isOpen() const { return m_open; }
    QString projectPath() const { return m_projectPath; }
    QString projectName() const { return m_projectName; }
    QString lastError() const;
signals:
    void saved(int count);
    void writeFailed(const QString &message);
private:
    bool openCatalog(const QString &projectDirectory, QVector<SavedPhoto> *photos, bool readOnly);
    bool modifyVirtualCopy(const QString &key, const QString &name, bool remove);
    struct WorkerState { QString connectionName; QString error; QHash<QString,QString> failedWrites; QMutex mutex; };
    std::shared_ptr<WorkerState> m_state;
    QThread m_thread;
    QObject *m_worker = nullptr;
    QString m_projectPath, m_projectName;
    QString m_lastOpenError; // GUI thread only; does not poison the active writer's save status
    bool m_open = false;
};
