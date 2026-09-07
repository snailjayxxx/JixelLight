#include "core/export/ExportQueue.h"
#include "core/export/JpegExporter.h"
#include <QtConcurrent/QtConcurrentRun>
#include <future>

ExportQueue::ExportQueue(std::shared_ptr<SourceCache> cache,QObject *parent) : QObject(parent),m_cache(std::move(cache)) {
    m_pool.setMaxThreadCount(1);
    connect(&m_watcher,&QFutureWatcher<QVector<ExportResult>>::finished,this,[this] {
        int succeeded=0,failed=0;
        const auto results=m_watcher.result();
        for (const auto &result:results) {
            if (result.ok) ++succeeded; else ++failed;
            emit fileFinished(result.source,result.destination,result.ok,result.error);
        }
        m_busy=false;
        emit finished(succeeded,failed,cancelled(m_cancel));
    });
}
ExportQueue::~ExportQueue() {
    disconnect(&m_watcher,nullptr,this,nullptr); cancel();
    m_watcher.waitForFinished(); m_pool.waitForDone();
}
void ExportQueue::cancel() { if (m_cancel) m_cancel->store(true,std::memory_order_relaxed); }
bool ExportQueue::start(QVector<ExportRequest> requests) {
    if (m_busy || requests.isEmpty()) return false;
    m_busy=true;
    m_cancel=std::make_shared<std::atomic_bool>(false);
    const auto token=m_cancel,interactive=m_interactive;
    const auto cache=m_cache;
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[this,requests=std::move(requests),token,interactive,cache] {
        QVector<ExportResult> results;
        std::future<SourceData> next;
        try {
            for (int index=0;index<requests.size() && !cancelled(token);++index) {
                const auto &request=requests[index];
                SourceData source;
                if (next.valid()) source=next.get();
                else if (!request.source.isNull()) source.image=request.source;
                else source=loadSource(*cache,request.sourcePath,token);
                if (cancelled(token)) break;
                // At most one next source in flight, and only when the estimated
                // pair fits the decoded-source budget. The RAW decoder also has
                // a process-wide single-decode lease.
                if (index+1<requests.size() && !source.image.isNull() && source.image.sizeInBytes()*2<=cache->budgetBytes()) {
                    const auto following=requests[index+1];
                    next=std::async(std::launch::async,[cache,following,token] {
                        if (!following.source.isNull()) { SourceData s; s.image=following.source; return s; }
                        return loadSource(*cache,following.sourcePath,token);
                    });
                }
                ExportResult result{request.sourcePath,request.destination,source.error,false};
                if (!source.image.isNull()) result.ok=exportJpegTiled(source.image,request.state,request.destination,request.space,request.quality,token,&result.error,
                    [this,index,total=requests.size(),file=request.sourcePath](int percent) {
                        QMetaObject::invokeMethod(this,[this,index,total,percent,file] { emit progress(index,int(total),percent,file); },Qt::QueuedConnection);
                    },interactive);
                results.push_back(result);
            }
            if (next.valid()) next.get();
        } catch (const std::exception &e) { results.push_back({{}, {},QString::fromUtf8(e.what()),false}); }
        catch (...) { results.push_back({{}, {},QStringLiteral("Unknown export error"),false}); }
        return results;
    }));
    return true;
}
