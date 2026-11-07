#pragma once
#include "core/gpu/brush/NativeBrushFrame.h"
#include "core/async/LatestFrameJob.h"
#include <QTimer>
#include "app/NativeBrushTelemetry.h"

class PhotoController;
struct NativeBrushView {
    bool wanted=false,busy=false,refused=false;
    Qt::HANDLE guiThread=nullptr;
    std::shared_ptr<NativeBrushTelemetry> facts;
    quint64 sourceCacheHits=0,sourceCaptures=0;
    std::shared_ptr<std::atomic_bool> context;
    NativeBrush::DeliveryTarget target;
    quint64 revision=0,prepareGeneration=0;
    qint64 qtSourceKey=0;
    NativeBrush::CanonicalSource::Ptr source;
    NativeBrush::UploadAdmission::Ptr upload;
    NativeBrush::FrameAdmission::Ptr frame;
};

// GUI-only coordinator. Its work functions capture value snapshots, not a
// controller. Only actual decoded/prepared, internally immutable pixels enter
// the source worker; no arbitrary external QImage queue is offered here.
class NativeBrushController final {
public:
    explicit NativeBrushController(PhotoController *);
    ~NativeBrushController();
    void refresh();
    void suspend(bool);
    void rhiReady(const std::shared_ptr<std::atomic_bool> &,NativeBrush::SourceCapabilities,NativeBrush::DomainRetirement);
    NativeBrushView view() const;
    bool matches(const std::shared_ptr<std::atomic_bool> &,const NativeBrush::FrameStamp &,quint64 generation) const;
    void failure(const std::shared_ptr<std::atomic_bool> &,const QString &);
    void trace(QJsonObject);
    QJsonObject trace() const;
private:
    struct SourceRequest {
        std::shared_ptr<std::atomic_bool> context;
        NativeBrush::DeliveryTarget target;
        NativeBrush::SourceDescription description;
        NativeBrush::SourceCapabilities caps;
        NativeBrush::CanonicalSource::Ptr cached;
        QImage stableImage;
        std::shared_ptr<BrushRenderBudget> budget;
        qint64 qtKey=0;
    };
    struct SourceResult {
        NativeBrush::CanonicalSource::Ptr source;
        NativeBrush::UploadAdmission::Ptr upload;
        bool reused=false;
        NativeBrush::SourceFailure failure=NativeBrush::SourceFailure::Invalid;
    };
    struct FrameRequest {
        std::shared_ptr<std::atomic_bool> context;
        NativeBrush::SourceCapabilities caps;
        NativeBrush::CanonicalSource::Ptr source;
        NativeBrush::DeliveryTarget target;
        ProcessingPlan plan;
        quint64 revision=0;
    };
    struct FrameResult {
        NativeBrush::FrameAdmission::Ptr frame;
        NativeBrush::FrameFailure failure=NativeBrush::FrameFailure::CPUPlan;
    };
    void invalidate(bool clearCanonical);
    void requestSource();
    void requestFrame();
    void refuse();
    void retry();
    PhotoController *m_controller;
    Qt::HANDLE m_guiThread=QThread::currentThreadId();
    QString m_instance;
    NativeBrushView m_view;
    NativeBrush::SourceCapabilities m_caps;
    NativeBrush::DomainRetirement m_owner;
    NativeBrush::SourceDescription m_description,m_cachedDescription;
    NativeBrush::CanonicalSource::Ptr m_cached;
    qint64 m_cachedQtKey=0;
    QJsonObject m_trace;
    bool m_suspended=false;
    quint64 m_sourceCacheHits=0,m_sourceCaptures=0;
    int m_retries=0;
    QTimer m_retry;
    LatestFrameJob<SourceRequest,SourceResult> m_sources;
    LatestFrameJob<FrameRequest,FrameResult> m_frames;
};
