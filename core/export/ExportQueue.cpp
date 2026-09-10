#include "core/look/LookProfiles.h"
#include "core/raw/RawDecoder.h"
#include "core/metadata/MetadataReader.h"
#include "core/export/ExportQueue.h"
#include "core/export/JpegExporter.h"
#include <QtConcurrent/QtConcurrentRun>
#include <future>
#include <algorithm>
#include <cmath>

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
                if (index+1<requests.size() && !source.image.isNull() && source.image.sizeInBytes()*2<=cache->budgetBytes()) {
                    const auto following=requests[index+1];
                    next=std::async(std::launch::async,[cache,following,token] {
                        if (!following.source.isNull()) { SourceData s; s.image=following.source; return s; }
                        return loadSource(*cache,following.sourcePath,token);
                    });
                }
                ExportResult result{request.sourcePath,request.destination,source.error,false};
                const bool rawSource=RawDecoder::isRawFile(request.sourcePath);
                QVariantMap metadata=source.metadata;
                // exportCurrent can supply the already-decoded full image, so
                // recover only the small metadata context instead of decoding
                // RAW a second time.
                if(rawSource && metadata.isEmpty()) metadata=MetadataReader::read(request.sourcePath);
                const auto state=metadata.isEmpty()?request.state:LookProfiles::resolveAsShot(request.state,metadata,rawSource);
                double rawBaseExposure=metadata.value("rawBaseExposureStops",
                    metadata.value("rawBaselineExposure",0.0)).toDouble();
                if(!std::isfinite(rawBaseExposure))rawBaseExposure=0.0;
                rawBaseExposure=std::clamp(rawBaseExposure,-8.0,8.0);
                if (!source.image.isNull()) result.ok=exportJpegTiled(source.image,state,request.destination,request.space,request.quality,token,&result.error,
                    [this,index,total=requests.size(),file=request.sourcePath](int percent) {
                        QMetaObject::invokeMethod(this,[this,index,total,percent,file] { emit progress(index,int(total),percent,file); },Qt::QueuedConnection);
                    },interactive,rawSource,float(rawBaseExposure));
                results.push_back(result);
            }
            if (next.valid()) next.get();
        } catch (const std::exception &e) { results.push_back({{}, {},QString::fromUtf8(e.what()),false}); }
        catch (...) { results.push_back({{}, {},QStringLiteral("Unknown export error"),false}); }
        return results;
    }));
    return true;
}
