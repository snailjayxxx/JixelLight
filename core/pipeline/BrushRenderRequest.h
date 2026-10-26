#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include <atomic>
#include <functional>
#include <memory>

// A separate admission boundary. Existing research APIs keep their 2 Mi cap.
// The budget covers owned operation payloads, including outputs still leased
// after the request is destroyed; source/analysis caches have their own limits.
class BrushRenderBudget final {
public:
    static constexpr qint64 DefaultBytes=3LL*1024*1024*1024;
    struct Snapshot { qint64 limit=0,charged=0,peak=0; };
    explicit BrushRenderBudget(qint64 bytes=DefaultBytes);
    Snapshot snapshot() const;
    static std::shared_ptr<BrushRenderBudget> sharedDefault();
private:
    struct Accounting;
    struct Reservation;
    std::shared_ptr<Accounting> m_accounting;
    std::shared_ptr<const void> reserve(qint64 bytes);
    friend class BrushRenderRequest;
};

class BrushImageLease final {
public:
    BrushImageLease()=default;
    const QImage &image() const { return m_image; }
    explicit operator bool() const { return !m_image.isNull(); }
private:
    // Hold the lease for the entire use of image(); don't escape an unleased
    // shallow QImage alias. All internal consumers follow this contract.
    std::shared_ptr<const void> m_reservation;
    QImage m_image;
    friend class BrushRenderRequest;
};

class BrushRenderRequest final {
public:
    static constexpr qint64 MaxSourcePixels=32LL*1024*1024;
    enum class Layout { Whole, Stripes128 };
    enum class SourceStage { Original, Prepared };
    struct Spec {
        ImagePipeline::InputEncoding encoding=ImagePipeline::InputEncoding::LinearProPhoto;
        ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb;
        bool rawSource=false;
        float rawBaseExposureStops=0;
        Layout layout=Layout::Whole;
        SourceStage sourceStage=SourceStage::Original;
        QRect region; // Default-constructed QRect means the complete frame.
        QRectF normalizedFrame{0,0,1,1};
        bool parallel=true;
    };
    static std::unique_ptr<BrushRenderRequest> create(const QImage &source,const AdjustmentState &state,
        const Spec &spec,const CancelToken &cancel={},QString *error=nullptr,
        std::shared_ptr<BrushRenderBudget> budget={});
    BrushRenderRequest(const BrushRenderRequest &)=delete;
    BrushRenderRequest &operator=(const BrushRenderRequest &)=delete;
    QSize size() const { return m_region.size(); }
    QSize frameSize() const { return m_frameSize; }
    QRect region() const { return m_region; }
    Layout layout() const { return m_spec.layout; }
    ColorManagement::OutputSpace outputSpace() const { return m_spec.output; }
    qint64 reservedBytes() const { return m_reservedBytes; }
    BrushRenderBudget::Snapshot budgetSnapshot() const { return m_budget->snapshot(); }
    // Each request executes once. This also bounds concurrent and retained
    // output payloads; calling another render with the same admission refuses.
    BrushImageLease render(const CancelToken &cancel={},QString *error=nullptr);
    using StripConsumer=std::function<bool(const BrushImageLease &,int outputRow)>;
    bool visitStrips(const StripConsumer &consumer,const CancelToken &cancel={},QString *error=nullptr);
private:
    BrushRenderRequest()=default;
    QImage prepare(const CancelToken &cancel,QString *error) const;
    bool begin(Layout expected,const CancelToken &cancel,QString *error);
    // Reservation outlives all request-owned image/plan payloads.
    std::shared_ptr<BrushRenderBudget> m_budget;
    std::shared_ptr<const void> m_reservation;
    QImage m_source;
    ProcessingPlan m_plan;
    GeometryState m_geometry;
    Spec m_spec;
    QSize m_frameSize;
    QRect m_region;
    qint64 m_reservedBytes=0;
    std::atomic_bool m_used{false};
};
