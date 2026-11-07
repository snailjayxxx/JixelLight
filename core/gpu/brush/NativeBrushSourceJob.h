#pragma once
#include "core/gpu/brush/NativeBrushUpload.h"
#include "core/async/LatestFrameJob.h"

namespace NativeBrush {
struct SourceJobRequest {
    DeliveryTarget target;
    quint64 revision = 0;
    SourceDescription description;
    SourceCapabilities capabilities;
    std::shared_ptr<BrushRenderBudget> budget;
    // Invoked on the worker. It must produce pixels stable through capture;
    // existing decoder/preparer owning results satisfy this boundary.
    std::function<QImage(const CancelToken &)> load;
};
struct SourceJobResult {
    CanonicalSource::Ptr source;
    UploadAdmission::Ptr upload;
    SourceFailure failure = SourceFailure::Invalid;
};

// GUI facade for the existing bounded worker/watcher queue. Running work is
// cancelled on source/target replacement; only the latest target can publish.
class SourceJob final {
public:
    using Deliver = std::function<void(const SourceJobRequest &, SourceJobResult)>;
    explicit SourceJob(Deliver);
    bool submit(SourceJobRequest);
    void retire();
    bool busy() const { return m_job.busy(); }
private:
    void deliver(const SourceJobRequest &, SourceJobResult);
    Deliver m_deliver;
    DeliveryTarget m_target;
    SourceDescription m_description;
    quint64 m_revision = 0;
    bool m_active = true;
    LatestFrameJob<SourceJobRequest, SourceJobResult> m_job;
};
} // namespace NativeBrush
