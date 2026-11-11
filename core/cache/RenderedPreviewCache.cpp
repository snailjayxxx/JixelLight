#include "core/cache/RenderedPreviewCache.h"
#include "core/pipeline/StageGraph.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QMutexLocker>
#include <QRgba64>
#include <algorithm>
#include <limits>
#ifdef JIXELLIGHT_PREVIEW_STAGE_TEST_PROGRESS
#include "tests/PreviewStageProgressProbe.h"
#endif

RenderedPreviewCache::RenderedPreviewCache(qint64 bytes,std::shared_ptr<BrushRenderBudget> brushBudget)
    : m_entries(int(std::clamp(bytes / 1024, qint64(0), qint64(std::numeric_limits<int>::max())))),
      m_brushBudget(std::move(brushBudget)) {}

QJsonObject RenderedPreviewCache::snapshotLocked() const {
    return {{"schema", 1}, {"backend", "cpu-preview"}, {"key_version", "cpu-preview-v1"},
            {"budget_bytes", qint64(m_entries.maxCost()) * 1024},
            {"charged_bytes", qint64(m_entries.totalCost()) * 1024}, {"entries", m_entries.size()},
            {"hits", m_hits}, {"misses", m_misses}, {"bypasses", m_bypasses},
            {"evictions", m_evictions}, {"cancelled_requests", m_cancelled},
            {"denoise_stage_hits",m_denoiseHits},{"denoise_stage_misses",m_denoiseMisses},
            {"denoise_stage_bypasses",m_denoiseBypasses},{"denoise_key_version","cpu-denoise-preview-v1"},
            {"texture_stage_hits",m_textureHits},{"texture_stage_misses",m_textureMisses},
            {"texture_stage_bypasses",m_textureBypasses},{"texture_key_version","cpu-texture-preview-v1"},
            {"clarity_stage_hits",m_clarityHits},{"clarity_stage_misses",m_clarityMisses},
            {"clarity_stage_bypasses",m_clarityBypasses},{"clarity_key_version","cpu-clarity-preview-v1"},
            {"accounting", "RGBA64 results, FP32 denoise/texture/clarity stages and retained LUT payloads share this budget; rounded to KiB per entry; excludes allocator overhead/live caller images"}};
}
QJsonObject RenderedPreviewCache::snapshot() const {
    QMutexLocker lock(&m_mutex);
    return snapshotLocked();
}
void RenderedPreviewCache::recordLocked() {
    PerformanceRecorder::value("render_cache", snapshotLocked());
}

QImage RenderedPreviewCache::render(const QImage &source,const ProcessingPlan &plan,const CancelToken &cancel,QString *error,BrushAdmissionFailure *failure) {
    if(error)error->clear();
    if(failure)*failure=BrushAdmissionFailure::None;
    PerformanceSpan timing("preview_render_cached");
    if (cancelled(cancel)) {
        if(failure)*failure=BrushAdmissionFailure::Cancelled;
        QMutexLocker lock(&m_mutex); ++m_cancelled; recordLocked(); return {};
    }
    if (source.isNull()) return {};
    const bool brush=BrushMaskTileRaster::anyEnabled(plan.state.brushMasks);
    if(brush&&!BrushRenderRequest::validPreparedPlan(plan,error)){if(failure)*failure=BrushAdmissionFailure::Invalid;return {};}
    const auto lut = plan.data[ProcessingPlan::LookStyle].w > 0 ? plan.state.look.lut : nullptr;
    const qint64 bytes = qint64(source.width()) * source.height() * sizeof(QRgba64)
        + (lut ? qint64(lut->rgb.capacity()) * sizeof(float) : 0);
    const qint64 cost = (bytes + 1023) / 1024;
    const bool cacheable = m_entries.maxCost() > 0 && cost <= m_entries.maxCost();
    const QString key = cacheable ? StageGraph::renderKey(source, plan) : QString{};
    const qint64 stageCost=(qint64(source.width())*source.height()*16+1023)/1024;
    const bool stageFits=cacheable&&stageCost<=qint64(m_entries.maxCost())-cost;
    const QString spatialKey=!brush&&plan.spatialInputActive()&&stageFits?StageGraph::spatialPreviewKey(source,plan):QString{};
    const bool stageCacheable=!spatialKey.isEmpty();QImage spatialInput;
    {
        QMutexLocker lock(&m_mutex);
        if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
        if (cacheable) {
            if (const auto *entry = m_entries.object(key)) {
                ++m_hits; PerformanceRecorder::count("render_cache_hit"); recordLocked();
                return cancelled(cancel) ? QImage{} : entry->image;
            }
            ++m_misses; PerformanceRecorder::count("render_cache_miss");
        } else {
            ++m_bypasses; PerformanceRecorder::count("render_cache_bypass");
        }
        if(stageCacheable) {
            if(const auto *entry=m_entries.object(spatialKey)) {
                spatialInput=entry->image;
                if(plan.denoiseActive()){++m_denoiseHits;PerformanceRecorder::count("cpu_preview_denoise_hit");}
                if(plan.textureActive()){++m_textureHits;PerformanceRecorder::count("cpu_preview_texture_hit");}
                if(plan.clarityActive()){++m_clarityHits;PerformanceRecorder::count("cpu_preview_clarity_hit");}
            } else {
                if(plan.denoiseActive()){++m_denoiseMisses;PerformanceRecorder::count("cpu_preview_denoise_miss");}
                if(plan.textureActive()){++m_textureMisses;PerformanceRecorder::count("cpu_preview_texture_miss");}
                if(plan.clarityActive()){++m_clarityMisses;PerformanceRecorder::count("cpu_preview_clarity_miss");}
            }
        } else {
            if(plan.denoiseActive()){++m_denoiseBypasses;PerformanceRecorder::count("cpu_preview_denoise_bypass");}
            if(plan.textureActive()){++m_textureBypasses;PerformanceRecorder::count("cpu_preview_texture_bypass");}
            if(plan.clarityActive()){++m_clarityBypasses;PerformanceRecorder::count("cpu_preview_clarity_bypass");}
        }
        recordLocked();
    }
    QImage image;
    if(brush) {
        auto request=BrushRenderRequest::createPrepared(source,plan,BrushRenderRequest::Layout::Whole,cancel,error,m_brushBudget,failure);
        if(request)image=request->render(cancel,error).sharedImage();
    }else if(stageCacheable) {
        if(spatialInput.isNull())spatialInput=ImagePipeline::preparePreviewSpatialInput(source,plan,cancel);
#ifdef JIXELLIGHT_PREVIEW_STAGE_TEST_PROGRESS
        if(!spatialInput.isNull())PreviewStageProgressProbe::notify(spatialInput);
#endif
        if(!spatialInput.isNull())image=ImagePipeline::processPreviewWithSpatialInput(source,spatialInput,plan,cancel);
    }else image=ImagePipeline::processWithPlan(source,plan,cancel);
    if(image.isNull()&&failure&&*failure==BrushAdmissionFailure::None)*failure=cancelled(cancel)?BrushAdmissionFailure::Cancelled:BrushAdmissionFailure::Invalid;
    QMutexLocker lock(&m_mutex);
    if (cancelled(cancel)) { ++m_cancelled; recordLocked(); return {}; }
    if (image.isNull() || !cacheable) return image;
    // Rendering runs outside the lock. Concurrent equivalent misses may finish
    // twice, but only one complete immutable entry is retained.
    const auto retain=[&](const QString &entryKey,const QImage &pixels,qint64 entryCost,std::shared_ptr<const LookLut> retainedLut) {
        if(m_entries.object(entryKey))return;
        const auto previous=m_entries.size();m_entries.insert(entryKey,new Entry{pixels,std::move(retainedLut)},int(entryCost));
        const qint64 evicted=previous+1-m_entries.size();m_evictions+=evicted;
        if(evicted)PerformanceRecorder::count("render_cache_eviction",evicted);
    };
    // Publish only after the whole color render completed without cancellation.
    // The stage and result must fit together in the existing shared LRU budget.
    if(stageCacheable)retain(spatialKey,spatialInput,stageCost,{});
    retain(key,image,cost,lut);
    recordLocked();
    return cancelled(cancel) ? QImage{} : image;
}
