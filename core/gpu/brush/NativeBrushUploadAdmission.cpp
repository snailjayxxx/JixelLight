#include "core/gpu/brush/NativeBrushUpload.h"
#include "core/gpu/brush/NativeBrushPrivate.h"

namespace NativeBrush {
UploadAdmission::Ptr UploadAdmission::prepare(CanonicalSource::Ptr source, const SourceCapabilities &caps,
        SourceFailure *why, const CancelToken &cancel) {
    auto fail = [why](SourceFailure f) -> Ptr { if (why) *why = f; return {}; };
    if (!caps.threads().permitsCapture()) return fail(SourceFailure::WrongThread);
    if (cancelled(cancel)) return fail(SourceFailure::Cancelled);
    if (!caps.active()) return fail(SourceFailure::Stale);
    if (!source || source->size().width() > caps.m_maxTexture || source->size().height() > caps.m_maxTexture)
        return fail(SourceFailure::Invalid);
    const auto size = source->size(); const qint64 pixels = qint64(size.width()) * size.height();
    const qint64 gpuBytes = pixels * 16 + 512 * 1024;
    const qint64 pitch = ((qint64(size.width()) * 16 + 255) / 256) * 256;
    const qint64 uploadBytes = (source->format() == QImage::Format_RGBA64 ? pixels * 16 : 0) + pitch * size.height() + 64 * 1024;
    auto gpu = BudgetAccess::reserve(source->m_budget, gpuBytes);
    if (!gpu) return fail(SourceFailure::Budget);
    auto upload = BudgetAccess::reserve(source->m_budget, uploadBytes);
    if (!upload) return fail(SourceFailure::Budget);
    try {
        auto a = Ptr(new UploadAdmission); a->m_source = std::move(source); a->m_caps = caps;
        a->m_cancel = cancel; a->m_gpuCharge = std::move(gpu); a->m_uploadCharge = std::move(upload);
        a->m_upload = ImagePipeline::floatSource(a->m_source->m_pixels, cancel);
        if (cancelled(cancel)) return fail(SourceFailure::Cancelled);
        if (a->m_upload.isNull()) return fail(SourceFailure::Allocation);
        if (!caps.active()) return fail(SourceFailure::Stale);
        if (why) *why = SourceFailure::None;
        return a;
    } catch (...) { return fail(SourceFailure::Allocation); }
}

bool UploadAdmission::discard() {
    int expected = 0;
    if (!m_state.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) return false;
    m_upload = {}; m_uploadCharge.reset(); m_gpuCharge.reset();
    return true;
}

} // namespace NativeBrush
