#include "MoveImportQueue.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>
MoveImportQueue::MoveImportQueue(QObject *parent,MoveImportTrash trash):QObject(parent),m_trash(std::move(trash)){
    m_pool.setMaxThreadCount(1);connect(&m_watcher,&QFutureWatcher<MoveImportBatch>::finished,this,&MoveImportQueue::deliver);
}
MoveImportQueue::~MoveImportQueue(){disconnect(&m_watcher,nullptr,this,nullptr);cancel();m_watcher.waitForFinished();m_pool.waitForDone();}
CopyImportProgress MoveImportQueue::progressSink(const CancelToken &token){return [this,token](qint64 copied,qint64 total,int done,int files,const QString &stage){
    QMetaObject::invokeMethod(this,[=,this]{if(busy()&&token==m_cancel)emit progress(copied,total,done,files,stage);},Qt::QueuedConnection);
};}
bool MoveImportQueue::start(QStringList sources,QString directory,QString project,ImportNaming naming,QStringList times){
    if(busy()||sources.isEmpty())return false;m_operationId=QUuid::createUuid().toString(QUuid::WithoutBraces);m_phase=Phase::Copying;m_cancel=std::make_shared<std::atomic_bool>(false);const auto token=m_cancel;const auto sink=progressSink(token);
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[sources=std::move(sources),directory=std::move(directory),project=std::move(project),naming=std::move(naming),times=std::move(times),token,sink]{
        try{return prepareMoveImport(sources,directory,project,token,sink,naming,times);}catch(...){MoveImportBatch batch;batch.error="Move copy worker failed; sources retained";return batch;}
    }));return true;
}
bool MoveImportQueue::resume(QString journal,QString expectedDigest){
    if(busy()||expectedDigest.isEmpty())return false;m_operationId=QUuid::createUuid().toString(QUuid::WithoutBraces);m_phase=Phase::Reading;m_cancel=std::make_shared<std::atomic_bool>(false);
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[journal=std::move(journal),expectedDigest=std::move(expectedDigest)]{try{QString error;auto batch=readMoveImport(journal,&error);if(!error.isEmpty()||batch.recordDigest!=expectedDigest){batch.journal.clear();batch.error="Move recovery record changed or is invalid; inspect it again";}return batch;}catch(...){MoveImportBatch batch;batch.error="Move recovery worker failed; sources retained";return batch;}}));return true;
}
bool MoveImportQueue::catalogReady(bool durable){
    if(m_phase!=Phase::WaitingCatalog)return false;
    if(!durable||!m_retirementAllowed||cancelled(m_cancel)||m_batch.journal.isEmpty()||m_batch.files.isEmpty()){
        if(m_batch.error.isEmpty())m_batch.error=cancelled(m_cancel)?"Move import cancelled; sources retained":"Catalog handoff failed; sources retained";
        m_batch.wasCancelled=cancelled(m_cancel);m_phase=Phase::Idle;emit finished(m_batch);return true;
    }
    m_phase=Phase::Retiring;const auto journal=m_batch.journal;const auto token=m_cancel;const auto sink=progressSink(token);const auto trash=m_trash;const auto expected=m_batch.recordDigest;
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[journal,token,sink,trash,expected]{try{return retireMoveImport(journal,token,sink,trash,expected);}catch(...){MoveImportBatch batch;batch.journal=journal;batch.error="Move retirement worker failed; inspect recovery record";return batch;}}));return true;
}
void MoveImportQueue::cancel(){if(m_cancel)m_cancel->store(true,std::memory_order_relaxed);}
void MoveImportQueue::stopAndCollect(){
    cancel();if(m_phase==Phase::WaitingCatalog){catalogReady(false);return;}
    if(busy()){m_watcher.waitForFinished();deliver();}
}
void MoveImportQueue::deliver(){
    if(!busy()||m_phase==Phase::WaitingCatalog||!m_watcher.isFinished())return;
    const auto previous=m_phase;m_batch=m_watcher.result();
    m_retirementAllowed=previous==Phase::Reading?!m_batch.journal.isEmpty():(m_batch.error.isEmpty()&&!m_batch.wasCancelled);
    if(previous==Phase::Retiring){m_phase=Phase::Idle;emit finished(m_batch);return;}
    // Failed/canceled first passes still hand completed copies to the owner,
    // but never retire a source without a separate explicit recovery request.
    if(previous==Phase::Copying&&(!m_batch.error.isEmpty()||m_batch.wasCancelled)){
        m_phase=Phase::WaitingCatalog;emit copiesReady(m_batch);if(m_phase==Phase::WaitingCatalog)catalogReady(false);return;
    }
    m_phase=Phase::WaitingCatalog;emit copiesReady(m_batch);
}
