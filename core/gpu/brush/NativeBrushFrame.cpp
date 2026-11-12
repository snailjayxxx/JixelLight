#include "core/gpu/brush/NativeBrushFrame.h"
#include "core/gpu/brush/NativeBrushPrivate.h"

namespace NativeBrush {
namespace {
bool needsExactBackend(bool restrictedBackend, const ProcessingPlan &plan) {
    // Captured by enum comparison on the owner; no backend enum integers or
    // QRhi access are inferred on the worker. Include positive brush EV.
    if (!restrictedBackend) return false;
    const auto &s = plan.state;
    bool complexHsl = s.hue != 0;
    for (int i=0; i<8; ++i) complexHsl = complexHsl || s.hslHue[i] != 0 || s.hslSaturation[i] != 0 || s.hslLuminance[i] != 0;
    double brushEV = 0;
    for (const auto &layer : s.brushMasks.layers) if (layer.enabled) brushEV += std::max(0.0, layer.exposureEv);
    const bool bright = plan.encoding == ImagePipeline::InputEncoding::LinearProPhoto
        && s.exposure + std::max(0.0f, plan.data[ProcessingPlan::Vignette].x)
            + std::max(0.0f, plan.data[ProcessingPlan::LinearMask].x)
            + std::max(0.0f, plan.data[ProcessingPlan::RadialMask].x) + brushEV >= 2.5
        && (s.saturation != 0 || s.vibrance != 0);
    return complexHsl || bright;
}
}

FrameAdmission::Ptr FrameAdmission::prepare(CanonicalSource::Ptr source, const SourceCapabilities &caps,
        const ProcessingPlan &intent, DeliveryTarget target, quint64 revision, FrameFailure *why,
        const CancelToken &cancel, std::size_t indexLimit) {
    auto fail = [why](FrameFailure f) -> Ptr { if (why) *why = f; return {}; };
    if (!caps.threads().permitsCapture()) return fail(FrameFailure::WrongThread);
    if (cancelled(cancel)) return fail(FrameFailure::Cancelled);
    if (!caps.active()) return fail(FrameFailure::Stale);
    if (!source || !target.valid() || !revision) return fail(FrameFailure::InvalidSource);
    if (!BrushRenderRequest::validPreparedPlan(intent) || !BrushMaskDocument::validDocument(intent.state.brushMasks)
        || !BrushMaskTileRaster::anyEnabled(intent.state.brushMasks)) return fail(FrameFailure::CPUPlan);
    const auto &id = source->description(); const auto frame = intent.data[ProcessingPlan::FrameRect];
    if (intent.encoding != id.encoding || intent.rawSource != id.rawSource || intent.baseExposureStops != id.baseExposureStops
        || frame.x != float(id.preparedFrame.x()) || frame.y != float(id.preparedFrame.y())
        || frame.z != float(id.preparedFrame.width()) || frame.w != float(id.preparedFrame.height())) return fail(FrameFailure::InvalidSource);
    const auto look = intent.data[ProcessingPlan::LookDetail];
    if (look.x || look.y || look.z || look.w || intent.state.look.lut
        || intent.data[ProcessingPlan::IndependentDetail].x>0
        || intent.luminanceMaskActive() || intent.pointColorActive() || intent.denoiseActive() || intent.dehazeActive() || intent.textureActive() || intent.clarityActive()
        || (intent.encoding==ImagePipeline::InputEncoding::LinearProPhoto&&intent.output==ColorManagement::OutputSpace::ProPhotoRgb&&source->m_floatFacts.negativeRgb)
        || (intent.data[ProcessingPlan::Grain].x>0 && intent.output==ColorManagement::OutputSpace::AdobeRgb)
        || needsExactBackend(caps.m_numericRestricted, intent)) return fail(FrameFailure::CPUPolicy);
    const auto size = source->size();
    if (!caps.m_compute || caps.m_framesInFlight < 1 || caps.m_framesInFlight > 8 || caps.m_uniformAlignment < 1
        || (size.width()+15)/16 > caps.m_groupMaximum || (size.height()+15)/16 > caps.m_groupMaximum) return fail(FrameFailure::CPUPolicy);
    const auto &doc = intent.state.brushMasks;
    const std::size_t tileCount = std::size_t((size.width()+127)/128) * std::size_t((size.height()+127)/128);
    std::size_t dabs = 0; for (const auto &l : doc.layers) for (const auto &s : l.strokes) dabs += s.dabs.size();
    constexpr std::size_t recordLimit = 16*1024*1024/sizeof(detail::DabRecord);
    const auto maxRecords = std::min(dabs*tileCount, recordLimit);
    const qint64 pixels = qint64(size.width()) * size.height();
    const auto aligned = [&](int bytes) { return (bytes+caps.m_uniformAlignment-1)/caps.m_uniformAlignment*caps.m_uniformAlignment; };
    const int u = aligned(sizeof(detail::CoverageUniform)), c = aligned(sizeof(intent.data));
    const qint64 outputBytes = pixels*16 + 1024*1024;
    const qint64 workBytes = pixels*4 + qint64(maxRecords)*sizeof(detail::DabRecord)*4
        + qint64(tileCount)*sizeof(detail::TileSpan)*4 + qint64(u+c)*(caps.m_framesInFlight+1) + 32*1024*1024;
    auto output = BudgetAccess::reserve(source->m_budget, outputBytes);
    if (!output) return fail(FrameFailure::Budget);
    auto work = BudgetAccess::reserve(source->m_budget, workBytes);
    if (!work) return fail(FrameFailure::Budget);
    try {
        BrushMaskTileRaster::FramePlan original;
        if (!BrushMaskTileRaster::detail::planFrameBounded(doc, size.width(), size.height(), {0,0,size.width(),size.height()},
            &original, cancel.get(), {frame.x,frame.y,frame.z,frame.w}, indexLimit, std::size_t(BrushRenderRequest::MaxSourcePixels)))
            return fail(cancelled(cancel) ? FrameFailure::Cancelled : FrameFailure::CPUWork);
        const auto &p = *original.prepared_;
        if (!p.indexed || p.references > recordLimit) return fail(FrameFailure::CPUIndex);
        for (const auto &l : p.document.layers) if (l.enabled && l.exposureEv != 0) for (const auto &s : l.strokes)
            if (s.style.opacity > 0 && s.style.hardness >= .999) return fail(FrameFailure::CPUPolicy);
        auto a = Ptr(new FrameAdmission);
        a->m_source = std::move(source); a->m_caps = caps; a->m_stamp = {std::move(target), revision, a->m_source->key()};
        a->m_plan = intent; a->m_preparedOn=QThread::currentThreadId(); a->m_cancel = cancel; a->m_outputCharge = std::move(output); a->m_workCharge = std::move(work);
        a->m_outputBytes = outputBytes; a->m_workBytes = workBytes; a->m_uniformBytes = u; a->m_colourUniformBytes = c;
        a->m_candidateCount = original.candidateVisits; a->m_packed.reserve(p.references); a->m_tiles.reserve(p.tiles.size());
        for (std::size_t t=0; t<p.tiles.size(); ++t) {
            if (cancelled(cancel)) return fail(FrameFailure::Cancelled);
            const auto tile = p.tiles[t].rect; const auto &refs = p.tileDabs[t];
            const auto visits = std::size_t(tile.width)*tile.height*refs.size();
            if (visits > BrushMaskTileRaster::MaxDabPixelVisits || visits > BrushMaskTileRaster::MaxFrameDabPixelVisits-a->m_shaderCount)
                return fail(FrameFailure::CPUWork);
            a->m_shaderCount += visits; a->m_tiles.push_back({{int(a->m_packed.size()),int(refs.size()),0,0}});
            for (const auto ref : refs) {
                const auto &l=p.document.layers[ref.layer]; const auto &s=l.strokes[ref.stroke]; const auto &d=s.dabs[ref.dab];
                const auto b=BrushMaskTileRaster::detail::candidateBounds(d,s.style,p.width,p.height,tile,p.frame);
                detail::DabRecord record{{float(b.cx),float(b.cy),float(b.radius),float(s.style.opacity*d.pressure)},
                    {float(s.style.hardness),float(l.exposureEv),0,float(s.style.hardness==1)},
                    {float(b.cx-double(float(b.cx))),float(b.cy-double(float(b.cy))),float(s.style.hardness-double(float(s.style.hardness))),float(b.radius-double(float(b.radius)))},
                    {float(b.radius*s.style.hardness),float(b.radius*s.style.hardness-double(float(b.radius*s.style.hardness))),float(s.style.hardness<1?1/(b.radius*(1-s.style.hardness)):0),0},
                    {b.left,b.top,b.right,b.bottom},{int(ref.layer),int(ref.stroke),int(s.mode==BrushMaskGeometry::StrokeMode::Erase),0}};
                for (const auto &v : {record.centerRadiusPeak,record.shape,record.residual,record.edge})
                    for (float x : v) if (!std::isfinite(x)) return fail(FrameFailure::CPURepresentation);
                if (record.centerRadiusPeak[2] < std::numeric_limits<float>::min() || record.centerRadiusPeak[3] < std::numeric_limits<float>::min())
                    return fail(FrameFailure::CPURepresentation);
                a->m_packed.push_back(record);
            }
        }
        if (a->m_packed.size() != p.references || a->m_tiles.size() != p.tiles.size()) return fail(FrameFailure::CPURepresentation);
        if (!caps.active()) return fail(FrameFailure::Stale);
        if (why) *why = FrameFailure::None;
        return a;
    } catch (...) { return fail(FrameFailure::Allocation); }
}
bool FrameAdmission::discard() {
    int expected=0;
    if (!m_state.compare_exchange_strong(expected,3,std::memory_order_acq_rel)) return false;
    m_packed=std::vector<detail::DabRecord>{};m_tiles=std::vector<detail::TileSpan>{};m_plan={};m_outputCharge.reset();m_workCharge.reset();return true;
}
} // namespace NativeBrush
