#include "core/gpu/brush/NativeBrushSourceJob.h"

namespace NativeBrush {
SourceJob::SourceJob(Deliver deliver) : m_deliver(std::move(deliver)), m_job(
    [](const SourceJobRequest &request, const CancelToken &cancel) {
        SourceJobResult result;
        if (cancelled(cancel)) { result.failure = SourceFailure::Cancelled; return result; }
        const auto image = request.load(cancel);
        result.source = CanonicalSource::capture(image, request.description, request.capabilities.threads(),
            request.budget, &result.failure, cancel);
        if (result.source && !cancelled(cancel))
            result.upload = UploadAdmission::prepare(result.source, request.capabilities, &result.failure, cancel);
        return result;
    }, [this](const SourceJobRequest &request, SourceJobResult result) { this->deliver(request, std::move(result)); }) {}

bool SourceJob::submit(SourceJobRequest request) {
    if (!m_active || !request.target.valid() || request.revision <= m_revision || !request.description.valid()
        || !request.load || !request.budget) return false;
    m_job.cancel(); m_target = request.target; m_description = request.description; m_revision = request.revision;
    m_job.submit(std::move(request)); return true;
}
void SourceJob::retire() { m_active = false; m_job.cancel(); }
void SourceJob::deliver(const SourceJobRequest &request, SourceJobResult result) {
    if (!m_active || request.target != m_target || request.revision != m_revision || request.description != m_description) return;
    m_deliver(request, std::move(result));
}
} // namespace NativeBrush
