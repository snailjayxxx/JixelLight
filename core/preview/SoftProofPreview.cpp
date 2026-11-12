#include "SoftProofPreview.h"
#include "core/cache/RenderedPreviewCache.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QScopeGuard>
#include <cmath>
namespace {
QImage retain(const QImage &image,std::shared_ptr<const void> charge) {
    if(image.isNull()||!charge)return {};
    struct Owner{QImage image;std::shared_ptr<const void> charge;};
    auto owner=std::make_unique<Owner>(Owner{image,std::move(charge)});const auto *bits=image.constBits();
    QImage view(bits,image.width(),image.height(),image.bytesPerLine(),image.format(),[](void *p){delete static_cast<Owner *>(p);},owner.get());
    if(view.isNull())return {};owner.release();view.setColorSpace(image.colorSpace());
    return view.constBits()==bits?view:QImage{};
}
}
SoftProofPreviewRenderer::SoftProofPreviewRenderer(std::shared_ptr<BrushRenderBudget> budget,std::shared_ptr<RenderedPreviewCache> cache,qint64 maxPixels)
    :m_budget(budget?std::move(budget):BrushRenderBudget::sharedDefault()),m_cache(std::move(cache)),m_maxPixels(maxPixels) {}
SoftProofPreviewResult SoftProofPreviewRenderer::render(const SoftProofPreviewRequest &request,const CancelToken &cancel) {
    QMutexLocker lock(&m_mutex);const auto cleanup=qScopeGuard([&]{if(m_dropCache.exchange(false)){m_scaledSource={};m_scaledOrigin=0;}});SoftProofPreviewResult result;QElapsedTimer timer;timer.start();
    const auto fail=[&](const QString &why,bool budget=false){result.image={};result.error=why;result.retryableBudget=budget;result.cancelled=cancelled(cancel);return result;};
    if(cancelled(cancel))return fail("Soft proof cancelled");
    if(request.source.isNull()||m_maxPixels<1||m_maxPixels>SoftProof::MaxPixels||!request.profile.valid()
        ||request.plan.output!=ColorManagement::OutputSpace::ProPhotoRgb)return fail("Invalid bounded proof source/plan/profile");
    QString planError;if(!BrushRenderRequest::validPreparedImagePlan(request.plan,&planError))return fail("Invalid proof plan: "+planError);
    const auto depth=request.source.depth()/8;if(depth!=8&&depth!=16)return fail("Unsupported proof source format");
    QImage source=request.source;bool scaled=false;
    const qint64 pixels=qint64(source.width())*source.height();
    if(pixels>m_maxPixels){
        scaled=true;
        if(m_scaledOrigin==source.cacheKey()&&!m_scaledSource.isNull()){source=m_scaledSource;PerformanceRecorder::count("soft_proof_scaled_source_hits");}
        else{
            const double ratio=std::sqrt(double(m_maxPixels)/pixels);
            const QSize size(std::max(1,int(std::floor(source.width()*ratio))),std::max(1,int(std::floor(source.height()*ratio))));
            auto charge=m_budget->reserve(qint64(size.width())*size.height()*depth);if(!charge)return fail("Proof source resize memory is busy",true);
            const auto resized=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(resized.isNull())return fail("Cannot allocate bounded proof source");
            if(cancelled(cancel))return fail("Soft proof cancelled");
            auto leased=retain(resized,std::move(charge));if(leased.isNull())return fail("Cannot retain bounded proof source");
            m_scaledOrigin=source.cacheKey();m_scaledSource=leased;source=std::move(leased);PerformanceRecorder::count("soft_proof_scaled_source_misses");
        }
    }else{m_scaledSource={};m_scaledOrigin=0;}
    const qint64 presentationBytes=qint64(source.width())*source.height()*32;
    auto presentation=m_budget->reserve(presentationBytes);if(!presentation)return fail("Proof presentation memory is busy",true);
    QString error;BrushAdmissionFailure reason=BrushAdmissionFailure::None;
    qint64 operationBytes=0;std::shared_ptr<const void> operation;std::unique_ptr<BrushRenderRequest> admission;QImage wide;
    if(BrushMaskTileRaster::anyEnabled(request.plan.state.brushMasks)){
        admission=BrushRenderRequest::createPrepared(source,request.plan,BrushRenderRequest::Layout::Whole,cancel,&error,m_budget,&reason);
        if(!admission)return fail(error,reason==BrushAdmissionFailure::BudgetUnavailable);
        operationBytes=admission->reservedBytes();wide=admission->render(cancel,&error).sharedImage();
    }else{
        operation=BrushRenderRequest::reservePreparedImage(source,request.plan,cancel,&error,m_budget,&operationBytes,&reason);
        if(!operation)return fail(error,reason==BrushAdmissionFailure::BudgetUnavailable);
        wide=m_cache?m_cache->render(source,request.plan,cancel,&error,&reason):ImagePipeline::processWithPlan(source,request.plan,cancel);
    }
    if(wide.isNull())return fail(error,reason==BrushAdmissionFailure::BudgetUnavailable);
    if(cancelled(cancel))return fail("Soft proof cancelled");
    if(timer.elapsed()>60000)return fail("Soft proof deadline exceeded");
    wide.setColorSpace(SoftProof::workingColorSpace());
    auto transformed=SoftProof::transform(wide,request.profile,request.settings,cancel);
    if(transformed.image.isNull())return fail(transformed.error);
    if(cancelled(cancel)||timer.elapsed()>60000)return fail(cancelled(cancel)?"Soft proof cancelled":"Soft proof deadline exceeded");
    result.image=retain(transformed.image,std::move(presentation));if(result.image.isNull())return fail("Cannot retain proof presentation charge");
    result.facts=transformed.facts;result.facts["prepared_source_key"]=QString::number(request.source.cacheKey());result.facts["gpu_staging_source_key"]=QString::number(request.gpuStagingKey);
    result.facts["source_scaled_for_proof"]=scaled;result.facts["proof_width"]=source.width();result.facts["proof_height"]=source.height();
    result.facts["presentation_reserved_bytes"]=presentationBytes;result.facts["cpu_operation_reserved_bytes"]=operationBytes;
    result.facts["payload_accounting"]="four RGBA16 presentation planes; resized source and uncached CPU operation separately admitted; bounded shared caches separate";
    result.facts["elapsed_ms"]=timer.elapsed();return result;
}

void SoftProofPreviewRenderer::clearCachedSource(){m_dropCache=true;if(m_mutex.tryLock()){m_scaledSource={};m_scaledOrigin=0;m_dropCache=false;m_mutex.unlock();}}
