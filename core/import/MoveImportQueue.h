#pragma once
#include "MoveImport.h"
#include <QObject>
#include <QFutureWatcher>
#include <QThreadPool>

class MoveImportQueue final:public QObject {
    Q_OBJECT
public:
    explicit MoveImportQueue(QObject *parent=nullptr,MoveImportTrash trash={});
    ~MoveImportQueue()override;
    bool start(QStringList sources,QString directory,QString project,ImportNaming naming={},QStringList times={});
    bool resume(QString journal,QString expectedDigest);
    // Owner imports copies, records their content identities and flushes the
    // catalog before approving this second phase. The worker rechecks on disk.
    bool catalogReady(bool durable);
    void cancel();void stopAndCollect();
    bool retirementPermitted()const{return m_retirementAllowed;}
    bool busy()const{return m_phase!=Phase::Idle;}
    QString operationId()const{return busy()?m_operationId:QString{};}
signals:
    void progress(qint64 copied,qint64 total,int completed,int files,const QString &stage);
    void copiesReady(const MoveImportBatch &batch);
    void finished(const MoveImportBatch &batch);
private:
    enum class Phase{Idle,Copying,Reading,WaitingCatalog,Retiring};
    void deliver();
    CopyImportProgress progressSink(const CancelToken &);
    QThreadPool m_pool;QFutureWatcher<MoveImportBatch> m_watcher;CancelToken m_cancel;
    MoveImportBatch m_batch;MoveImportTrash m_trash;Phase m_phase=Phase::Idle;bool m_retirementAllowed=false;
    QString m_operationId;
};
