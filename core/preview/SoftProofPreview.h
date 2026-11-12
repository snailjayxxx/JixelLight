#pragma once
#include "core/color/SoftProof.h"
#include "core/pipeline/BrushRenderRequest.h"
#include <QMutex>
class RenderedPreviewCache;
struct SoftProofPreviewRequest {
    QImage source;ProcessingPlan plan;SoftProof::Profile profile;SoftProof::Settings settings;
    quint64 photo=0,preparation=0,revision=0,generation=0;
    qint64 gpuStagingKey=0;
};
struct SoftProofPreviewResult {QImage image;QJsonObject facts;QString error;bool cancelled=false,retryableBudget=false;};
// One serialized worker, a single bounded resized-source slot, and the existing
// CPU spatial caches. Returned read-only images retain their payload charge.
class SoftProofPreviewRenderer final {
public:
    explicit SoftProofPreviewRenderer(std::shared_ptr<BrushRenderBudget> budget,
        std::shared_ptr<RenderedPreviewCache> cache,qint64 maxPixels=SoftProof::MaxPixels);
    SoftProofPreviewResult render(const SoftProofPreviewRequest &,const CancelToken &);
    void clearCachedSource();
private:
    QMutex m_mutex;
    std::shared_ptr<BrushRenderBudget> m_budget;
    std::shared_ptr<RenderedPreviewCache> m_cache;
    qint64 m_maxPixels;
    QImage m_scaledSource;
    qint64 m_scaledOrigin=0;
    std::atomic_bool m_dropCache=false;
};
