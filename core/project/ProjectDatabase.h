#pragma once
#include "core/pipeline/AdjustmentState.h"
#include <QObject>
#include <QHash>
#include <QThread>
#include <QMutex>
#include <memory>

// All SQL handles are created, queried and destroyed on one dedicated thread.
class ProjectDatabase final : public QObject {
    Q_OBJECT
public:
    explicit ProjectDatabase(QObject *parent = nullptr);
    ~ProjectDatabase() override;
    bool create(const QString &projectDirectory, const QString &projectName);
    bool addOrUpdatePhoto(const QString &path, const AdjustmentState &state);
    bool updateAdjustment(const QString &path, const AdjustmentState &state);
    bool updateBatch(const QHash<QString, AdjustmentState> &states);
    bool flush();
    bool isOpen() const { return m_open; }
    QString projectPath() const { return m_projectPath; }
    QString projectName() const { return m_projectName; }
    QString lastError() const;
signals:
    void saved(int count);
    void writeFailed(const QString &message);
private:
    struct WorkerState { QString connectionName; QString error; QMutex mutex; };
    std::shared_ptr<WorkerState> m_state;
    QThread m_thread;
    QObject *m_worker = nullptr;
    QString m_projectPath, m_projectName;
    bool m_open = false;
};
