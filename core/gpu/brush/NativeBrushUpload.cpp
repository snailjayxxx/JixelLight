#include "core/gpu/brush/NativeBrushUpload.h"
#include "core/gpu/brush/NativeBrushPrivate.h"
#include "core/gpu/brush/NativeBrushDomain_p.h"
#include <QCoreApplication>
#include <rhi/qrhi.h>
#include <cstring>

namespace NativeBrush {
namespace { std::atomic<quint64> nextGeneration{0}; constexpr quint32 UploadMarker = 0x4a495845; }

RenderDomain::RenderDomain(QRhi *rhi, ShaderBytes shaders) : m_shaders(std::move(shaders)) {
    if (!rhi || rhi->thread() != QThread::currentThread()) return;
    m_rhi = rhi; m_lifetime = std::make_shared<RhiLifetime>();
    m_caps.m_lifetime = m_lifetime; m_caps.m_cookie = reinterpret_cast<quintptr>(rhi);
    m_caps.m_generation = nextGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    m_caps.m_threads = {QCoreApplication::instance() ? QCoreApplication::instance()->thread() : nullptr, QThread::currentThreadId()};
    m_caps.m_maxTexture = rhi->resourceLimit(QRhi::TextureSizeMax);
    m_caps.m_framesInFlight = rhi->resourceLimit(QRhi::FramesInFlight);
    m_caps.m_uniformAlignment = rhi->ubufAlignment();
    m_caps.m_groupMaximum = rhi->resourceLimit(QRhi::MaxThreadGroupsPerDimension);
    m_caps.m_backend = int(rhi->backend());
    m_caps.m_numericRestricted = rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2;
    m_caps.m_compute = rhi->isFeatureSupported(QRhi::Compute)
        && rhi->resourceLimit(QRhi::MaxThreadsPerThreadGroup)>=256
        && rhi->resourceLimit(QRhi::MaxThreadGroupX)>=16 && rhi->resourceLimit(QRhi::MaxThreadGroupY)>=16;
    m_caps.m_histogramCompute = m_caps.m_compute && rhi->resourceLimit(QRhi::MaxThreadGroupX)>=256;
    if (!m_caps.m_maxTexture || !rhi->isTextureFormatSupported(QRhiTexture::RGBA32F,
            QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource)
        || !rhi->isFeatureSupported(QRhi::ReadBackNonUniformBuffer)) m_lifetime->active = false;
    rhi->addCleanupCallback(this, [this](QRhi *) { clearResources(true); });
}
RenderDomain::~RenderDomain() { shutdown(); }
bool RenderDomain::owner() const {
    return m_rhi && m_caps.m_threads.render == QThread::currentThreadId();
}
RenderDomain::Input *RenderDomain::find(InputHandle h) const {
    if (!owner() || h.generation != generation()) return nullptr;
    const auto it = m_inputs.find(h.input); return it == m_inputs.end() ? nullptr : it->second.get();
}

InputHandle RenderDomain::attach(const UploadAdmission::Ptr &a, SourceFailure *why) {
    auto fail = [why](SourceFailure f) -> InputHandle { if (why) *why = f; return {}; };
    if (!owner()) return fail(SourceFailure::WrongThread);
    if (!a) return fail(SourceFailure::Invalid);
    const auto &caps = a->m_caps;
    if (!m_caps.active() || !caps.active() || caps.m_lifetime != m_lifetime
        || caps.m_generation != generation() || caps.m_cookie != reinterpret_cast<quintptr>(m_rhi)) return fail(SourceFailure::Stale);
    if (cancelled(a->m_cancel)) return fail(SourceFailure::Cancelled);
    int expected = 0;
    if (!a->m_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return fail(SourceFailure::Consumed);
    struct Guard { std::atomic_int &state; bool committed = false;
        ~Guard() { if (!committed) state.store(0, std::memory_order_release); } } guard{a->m_state};
    try {
        auto input = std::make_unique<Input>();
        input->texture.reset(m_rhi->newTexture(QRhiTexture::RGBA32F, a->m_source->size(), 1,
            QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource));
        input->marker.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, 4));
        if (!input->texture->create() || !input->marker->create()) return fail(SourceFailure::Allocation);
        if (!m_caps.active()) return fail(SourceFailure::Stale);
        input->key = a->m_source->key(); input->description=a->m_source->description(); input->cancel = a->m_cancel;
        input->uploadCanonical = a->m_source;
        // Insertion can allocate. Do it before consuming the admission.
        const quint64 id = ++m_nextInput; auto *p = input.get(); m_inputs.emplace(id, std::move(input));
        p->gpuCharge = std::move(a->m_gpuCharge); p->uploadCharge = std::move(a->m_uploadCharge);
        p->upload = std::move(a->m_upload);
        guard.committed = true; a->m_state.store(2, std::memory_order_release);
        if (why) *why = SourceFailure::None;
        return {generation(), id};
    } catch (...) { return fail(SourceFailure::Allocation); }
}

bool RenderDomain::upload(InputHandle handle, QRhiCommandBuffer *cb) {
    auto *p = find(handle);
    if (!p || !cb || !m_caps.active() || p->submitted || p->settled || cancelled(p->cancel)) return false;
    const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(p->upload.constBits()), p->upload.sizeInBytes());
    QRhiTextureSubresourceUploadDescription data(bytes);
    data.setSourceSize(p->upload.size()); data.setDataStride(quint32(p->upload.bytesPerLine()));
    auto *updates = m_rhi->nextResourceUpdateBatch();
    updates->uploadTexture(p->texture.get(), {{0, 0, data}}); updates->uploadStaticBuffer(p->marker.get(), &UploadMarker);
    cb->resourceUpdate(updates); p->submitted = true;
    ++m_trace.sourceUploads;m_trace.controlReadbackBytes+=4;
    // No resource is destroyed inside the callback. Arrival and validity are
    // separate so an invalid completed marker is not an eternal pending job.
    p->completion.marker(UploadMarker);
    auto *fence = m_rhi->nextResourceUpdateBatch();
    fence->readBackBuffer(p->marker.get(), 0, 4, &p->completion.result); cb->resourceUpdate(fence);
    return true;
}

void RenderDomain::collect() {
    if (!owner()) return;
    collectDiagnostics(); collectPresentations(); collectHistograms(); collectFrames();
    for (auto it = m_inputs.begin(); it != m_inputs.end();) {
        auto &p = *it->second;
        const bool arrived = p.completion.done();
        if (arrived && !p.settled) {
            p.upload = {}; p.uploadCanonical.reset(); p.uploadCharge.reset(); p.settled = true;
        }
        const bool pending = p.submitted && !arrived;
        const bool unwanted = p.users == 0 || !m_caps.active() || cancelled(p.cancel)
            || (arrived && (!p.completion.valid || p.completion.callbackOwner != m_caps.m_threads.render));
        if (unwanted && !pending && p.gpuUsers==0) it = m_inputs.erase(it); else ++it;
    }
}
bool RenderDomain::ready(InputHandle h) const {
    const auto *p = find(h);
    return p && m_caps.active() && p->settled && p->completion.valid && p->completion.callbackOwner == m_caps.m_threads.render && !cancelled(p->cancel);
}
bool RenderDomain::pending() const {
    if (!owner()) return false;
    for (const auto &[id, p] : m_inputs) if (p->submitted && !p->completion.done()) return true;
    return workPending();
}
QRhiTexture *RenderDomain::texture(InputHandle h) const { const auto *p = find(h); return ready(h) ? p->texture.get() : nullptr; }
QByteArray RenderDomain::sourceKey(InputHandle h) const { const auto *p = find(h); return p ? p->key : QByteArray{}; }
bool RenderDomain::retain(InputHandle h) { auto *p = find(h); if (!p || !m_caps.active()) return false; ++p->users; return true; }
void RenderDomain::release(InputHandle h) { auto *p = find(h); if (p && p->users > 0) --p->users; collect(); }
void RenderDomain::clearResources(bool rhiCleanup) {
    if (!m_rhi) return;
    if (!owner()) qFatal("Native brush source resources must be destroyed on their QRhi owner");
    if (m_lifetime) m_lifetime->active.store(false, std::memory_order_release);
    if (pending()) { m_rhi->finish(); ++m_teardownFinishes; }
    clearWorkResources();
    m_inputs.clear();
    if (!rhiCleanup) m_rhi->removeCleanupCallback(this);
    m_rhi = nullptr;
}
void RenderDomain::shutdown() { clearResources(false); }
} // namespace NativeBrush
