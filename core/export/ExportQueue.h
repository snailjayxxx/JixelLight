#pragma once
#include "core/cache/SourceCache.h"
#include "core/pipeline/AdjustmentState.h"
#include "core/color/ColorManagement.h"
#include <QObject>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QVector>

struct ExportRequest {
    QString sourcePath, destination;
    QImage source;
    AdjustmentState state;
    ColorManagement::OutputSpace space=ColorManagement::OutputSpace::SRgb;
    int quality=92;
};
struct ExportResult { QString source, destination, error; bool ok=false; };
class ExportQueue final : public QObject {
    Q_OBJECT
public:
    explicit ExportQueue(std::shared_ptr<SourceCache> cache,QObject *parent=nullptr);
    ~ExportQueue() override;
    bool start(QVector<ExportRequest> requests);
    void cancel();
    void setInteractive(bool interactive) { m_interactive->store(interactive,std::memory_order_relaxed); }
    bool busy() const { return m_busy; }
signals:
    void progress(int completed,int total,int currentPercent,const QString &file);
    void fileFinished(const QString &source,const QString &destination,bool ok,const QString &error);
    void finished(int succeeded,int failed,bool cancelled);
private:
    std::shared_ptr<SourceCache> m_cache;
    std::shared_ptr<std::atomic_bool> m_interactive=std::make_shared<std::atomic_bool>(false);
    QThreadPool m_pool;
    QFutureWatcher<QVector<ExportResult>> m_watcher;
    CancelToken m_cancel;
    bool m_busy=false;
};
