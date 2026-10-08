#pragma once
#include "CopyImport.h"
#include <QObject>
#include <QFutureWatcher>
#include <QThreadPool>

class CopyImportQueue final : public QObject {
    Q_OBJECT
public:
    explicit CopyImportQueue(QObject *parent=nullptr);
    ~CopyImportQueue() override;
    bool start(QStringList sources,QString directory);
    void cancel();
    // Normal window close consumes completed copies before catalog flushing.
    void stopAndCollect();
    bool busy() const { return m_busy; }
signals:
    void progress(qint64 copied,qint64 total,int completed,int files,const QString &stage);
    void finished(const CopyImportResult &result);
private:
    void deliver();
    QThreadPool m_pool;
    QFutureWatcher<CopyImportResult> m_watcher;
    CancelToken m_cancel;
    bool m_busy=false;
};
