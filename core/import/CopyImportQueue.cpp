#include "CopyImportQueue.h"
#include <QtConcurrent/QtConcurrentRun>

CopyImportQueue::CopyImportQueue(QObject *parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    connect(&m_watcher,&QFutureWatcher<CopyImportResult>::finished,this,&CopyImportQueue::deliver);
}
CopyImportQueue::~CopyImportQueue() {
    disconnect(&m_watcher,nullptr,this,nullptr); cancel();
    m_watcher.waitForFinished(); m_pool.waitForDone();
}
bool CopyImportQueue::start(QStringList sources,QString directory,ImportNaming naming) {
    if (m_busy || sources.isEmpty()) return false;
    m_busy=true; m_cancel=std::make_shared<std::atomic_bool>(false); const auto token=m_cancel;
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[this,sources=std::move(sources),directory=std::move(directory),naming=std::move(naming),token] {
        return copyImportFiles(sources,directory,token,[this,token](qint64 copied,qint64 total,int done,int files,const QString &stage) {
            QMetaObject::invokeMethod(this,[this,token,copied,total,done,files,stage] {
                if (m_busy && token==m_cancel) emit progress(copied,total,done,files,stage);
            },Qt::QueuedConnection);
        },naming);
    }));
    return true;
}
void CopyImportQueue::cancel() { if (m_cancel) m_cancel->store(true,std::memory_order_relaxed); }
void CopyImportQueue::stopAndCollect() {
    if (!m_busy) return;
    cancel(); m_watcher.waitForFinished(); deliver();
}
void CopyImportQueue::deliver() {
    if (!m_busy || !m_watcher.isFinished()) return;
    const auto result=m_watcher.result(); m_busy=false;
    emit finished(result);
}
