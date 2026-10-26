#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/look/LookProfiles.h"
#include "core/look/SonyLookMetadata.h"
#include "core/look/LookDetail.h"
#include "core/look/LookLut.h"
#include "diagnostics/PerformanceRecorder.h"
#include <algorithm>
#include <cmath>
#include <limits>

struct BrushRenderBudget::Accounting {
    explicit Accounting(qint64 bytes):limit(std::max<qint64>(0,bytes)){}
    const qint64 limit;
    std::atomic<qint64> charged{0},peak{0};
};
struct BrushRenderBudget::Reservation {
    Reservation(std::shared_ptr<Accounting> owner,qint64 count):accounting(std::move(owner)),bytes(count){}
    std::shared_ptr<Accounting> accounting;
    mutable std::atomic<qint64> bytes{0};
    // Transfer already reserved bytes without increasing aggregate charge.
    // A child is allocated before changing the parent, so allocation failure
    // cannot lose a charge or refund memory that is still in use.
    std::shared_ptr<const void> split(qint64 count) const {
        if(count<=0)return {};
        auto child=std::make_shared<Reservation>(accounting,0);
        auto remaining=bytes.load(std::memory_order_relaxed);
        do {if(remaining<count)return {};}
        while(!bytes.compare_exchange_weak(remaining,remaining-count,std::memory_order_relaxed));
        child->bytes.store(count,std::memory_order_relaxed);return child;
    }
    ~Reservation(){accounting->charged.fetch_sub(bytes.load(std::memory_order_relaxed),std::memory_order_relaxed);}
};
BrushRenderBudget::BrushRenderBudget(qint64 bytes):m_accounting(std::make_shared<Accounting>(bytes)){}
BrushRenderBudget::Snapshot BrushRenderBudget::snapshot() const {
    return {m_accounting->limit,m_accounting->charged.load(std::memory_order_relaxed),m_accounting->peak.load(std::memory_order_relaxed)};
}
std::shared_ptr<BrushRenderBudget> BrushRenderBudget::sharedDefault() {
    static const auto budget=std::make_shared<BrushRenderBudget>();return budget;
}
std::shared_ptr<const void> BrushRenderBudget::reserve(qint64 bytes) {
    if(bytes<=0 || bytes>m_accounting->limit)return {};
    auto charged=m_accounting->charged.load(std::memory_order_relaxed);
    do {
        if(charged<0 || bytes>m_accounting->limit-charged)return {};
    }while(!m_accounting->charged.compare_exchange_weak(charged,charged+bytes,std::memory_order_relaxed));
    auto peak=m_accounting->peak.load(std::memory_order_relaxed);
    while(peak<charged+bytes && !m_accounting->peak.compare_exchange_weak(peak,charged+bytes,std::memory_order_relaxed)){}
    try {return std::make_shared<Reservation>(m_accounting,bytes);}
    catch(...) {m_accounting->charged.fetch_sub(bytes,std::memory_order_relaxed);return {};}
}

namespace {
QImage retainPixels(const QImage &image,std::shared_ptr<const void> reservation) {
    if(image.isNull()||!reservation)return {};
    struct Owner {std::shared_ptr<const void> reservation;QImage image;};
    auto owner=std::make_unique<Owner>(Owner{std::move(reservation),image});
    const auto *pixels=owner->image.constBits();
    QImage result(pixels,image.width(),image.height(),image.bytesPerLine(),image.format(),
        [](void *data){delete static_cast<Owner *>(data);},owner.get());
    if(result.isNull())return {};
    owner.release();
    result.setColorSpace(image.colorSpace());result.setDevicePixelRatio(image.devicePixelRatio());
    result.setDotsPerMeterX(image.dotsPerMeterX());result.setDotsPerMeterY(image.dotsPerMeterY());
    for(const auto &key:image.textKeys())result.setText(key,image.text(key));
    // Metadata must not have detached the buffer and escaped its reservation.
    return result.constBits()==pixels?result:QImage{};
}
bool fail(QString *error,const QString &message){if(error)*error=message;return false;}
QSize outputSize(const QSize &source,const GeometryState &geometry) {
    if(!GeometryState::validJson(geometry.toJson()))return {};
    auto size=geometry.correctedSize(source);if(size.isEmpty())return {};
    const auto crop=geometry.crop;
    const int left=std::clamp(int(std::floor(crop.x()*size.width())),0,size.width()-1);
    const int top=std::clamp(int(std::floor(crop.y()*size.height())),0,size.height()-1);
    const int right=std::clamp(int(std::ceil(crop.right()*size.width())),left+1,size.width());
    const int bottom=std::clamp(int(std::ceil(crop.bottom()*size.height())),top+1,size.height());
    size={right-left,bottom-top};if(geometry.quarterTurns%2)size.transpose();return size;
}
bool validState(const AdjustmentState &state) {
    if(!BrushMaskDocument::validDocument(state.brushMasks)||!BrushMaskTileRaster::anyEnabled(state.brushMasks)
        ||!state.look.error.isEmpty()||!LookProfiles::engineCompatible(state.look)
        ||!std::isfinite(state.look.strength)||state.look.strength<0||state.look.strength>1
        ||state.look.parameters.size()>SonyLookMetadata::parameterNames().size())return false;
    for(auto it=state.look.parameters.cbegin();it!=state.look.parameters.cend();++it) {
        if(!SonyLookMetadata::parameterNames().contains(it.key())||!std::isfinite(it.value()))return false;
        const auto range=SonyLookMetadata::parameterRange(it.key());if(it.value()<range.first||it.value()>range.second)return false;
    }
    if(state.look.mode!="off"&&state.look.mode!="as-shot"&&state.look.mode!="manual"&&state.look.mode!="calibrated")return false;
    if(state.look.mode=="calibrated"&&!state.look.lut)return false;
    if(state.look.mode=="manual"&&!state.look.lut&&!LookProfiles::commandCodes().contains(state.look.code))return false;
    if(state.look.lut&&!state.look.lut->validate())return false;
    auto plain=state;plain.brushMasks={};plain.look={};
    const auto json=plain.toJson();
    return AdjustmentState::fromJson(json).toJson()==json
        &&GeometryState::validJson(state.geometry.toJson())
        &&AdjustmentState::validRadialMaskJson(json)&&AdjustmentState::validLinearMaskJson(json)
        &&AdjustmentState::validVignetteJson(json)&&AdjustmentState::validBlackAndWhiteJson(json)
        &&AdjustmentState::validColorGradingJson(json);
}
bool samePreparedPlan(const ProcessingPlan &expected,const ProcessingPlan &provided) {
    if(expected.encoding!=provided.encoding||expected.output!=provided.output
        ||expected.rawSource!=provided.rawSource||expected.baseExposureStops!=provided.baseExposureStops
        ||!BrushMaskDocument::validDocument(provided.state.brushMasks)
        ||expected.state.look.lut!=provided.state.look.lut||expected.state.look.error!=provided.state.look.error)return false;
    for(int i=0;i<ProcessingPlan::SlotCount;++i) {
        // These two slots are filled from the actual GPU resource dimensions;
        // neither controls the admitted CPU kernel, which derives its own map.
        if(i==ProcessingPlan::Dimensions||i==ProcessingPlan::PixelMap)continue;
        const auto a=expected.data[std::size_t(i)],b=provided.data[std::size_t(i)];
        if(a.x!=b.x||a.y!=b.y||a.z!=b.z||a.w!=b.w)return false;
    }
    // Also compare the double-valued CPU state and document, not just uniforms.
    // LUT identity was checked above; do not serialize its binary payload.
    auto a=expected.state,b=provided.state;a.look.lut.reset();b.look.lut.reset();
    return a.toJson()==b.toJson();
}
qint64 estimateBytes(const QImage &source,const QRect &region,const QSize &frameSize,
                     const ProcessingPlan &plan,BrushRenderRequest::Layout layout) {
    // All dimensions are bounded before this arithmetic: <=32 Mi pixels and
    // 16 bytes/channel group; qint64 products cannot approach overflow.
    const qint64 sourceBytes=source.sizeInBytes();
    const qint64 pixels=qint64(region.width())*region.height();
    const auto detail=plan.data[ProcessingPlan::LookDetail];
    const int halo=LookDetail::halo({detail.x,detail.y,detail.z,detail.w});
    const auto expanded=region.adjusted(-halo,-halo,halo,halo).intersected(QRect(QPoint{},frameSize));
    const qint64 expandedPixels=qint64(expanded.width())*expanded.height();
    constexpr qint64 fixed=64LL*1024*1024+32LL*384*1024;
    const auto capacity=plan.state.look.lut?plan.state.look.lut->rgb.capacity():std::size_t(0);
    if(capacity>std::size_t((std::numeric_limits<qint64>::max()-fixed)/sizeof(float)))return -1;
    const qint64 retainedLut=qint64(capacity)*sizeof(float);
    qint64 pixelsAndWork=0;
    if(layout==BrushRenderRequest::Layout::Whole) {
        const auto copy=region==QRect(QPoint{},frameSize)?qint64(0):expandedPixels*(source.format()==QImage::Format_RGBA32FPx4?16:8);
        const auto color=2*sourceBytes+copy+24*expandedPixels+8*pixels;
        pixelsAndWork=std::max({3*sourceBytes,color,sourceBytes+24*pixels});
    } else {
        const qint64 stripePixels=qint64(expanded.width())*std::min(expanded.height(),128+2*halo);
        // Max sum of retained center outputs=8P; conservative codec reserve=16P.
        pixelsAndWork=3*sourceBytes+24*pixels+40*stripePixels;
    }
    if(retainedLut>std::numeric_limits<qint64>::max()-fixed-pixelsAndWork)return -1;
    return pixelsAndWork+fixed+retainedLut;
}
}

QImage BrushImageLease::sharedImage() const {
    try{return retainPixels(m_image,m_reservation);}catch(...){return {};}
}
bool BrushRenderRequest::validPreparedPlan(const ProcessingPlan &plan,QString *error) {
    if(error)error->clear();
    if(!plan.m_brushIntent)return fail(error,"Prepared brush plan has no original intent");
    try {
        if((plan.encoding!=ImagePipeline::InputEncoding::SRgb&&plan.encoding!=ImagePipeline::InputEncoding::LinearProPhoto)
            ||int(plan.output)<0||int(plan.output)>int(ColorManagement::OutputSpace::ProPhotoRgb)
            ||!std::isfinite(plan.baseExposureStops)||plan.baseExposureStops<-8||plan.baseExposureStops>8)
            return fail(error,"Invalid prepared brush specification");
        if(!validState(*plan.m_brushIntent))return fail(error,"Invalid original brush intent");
        const auto frame=plan.data[ProcessingPlan::FrameRect];
        if(!BrushMaskTileRaster::validNormalizedFrame({frame.x,frame.y,frame.z,frame.w}))return fail(error,"Invalid prepared viewport");
        auto expected=ProcessingPlan::compile(*plan.m_brushIntent,plan.encoding,plan.output,plan.rawSource,plan.baseExposureStops);
        expected.setFrameRect({frame.x,frame.y,frame.z,frame.w});
        return samePreparedPlan(expected,plan)||fail(error,"Prepared brush plan differs from its original intent");
    }catch(...){return fail(error,"Cannot validate prepared brush intent");}
}
std::unique_ptr<BrushRenderRequest> BrushRenderRequest::create(const QImage &source,const AdjustmentState &state,
    const Spec &spec,const CancelToken &cancel,QString *error,std::shared_ptr<BrushRenderBudget> budget,BrushAdmissionFailure *failure) {
    return createImpl(source,state,spec,cancel,error,std::move(budget),nullptr,failure);
}
std::unique_ptr<BrushRenderRequest> BrushRenderRequest::createPrepared(const QImage &source,const ProcessingPlan &plan,
    Layout layout,const CancelToken &cancel,QString *error,std::shared_ptr<BrushRenderBudget> budget,BrushAdmissionFailure *failure) {
    if(error)error->clear();
    if(failure)*failure=BrushAdmissionFailure::Invalid;
    if(cancelled(cancel)){if(failure)*failure=BrushAdmissionFailure::Cancelled;fail(error,"Cancelled");return {};}
    if(!plan.m_brushIntent){fail(error,"Prepared brush plan has no original intent");return {};}
    Spec spec;spec.sourceStage=SourceStage::Prepared;spec.layout=layout;
    spec.encoding=plan.encoding;spec.output=plan.output;spec.rawSource=plan.rawSource;spec.rawBaseExposureStops=plan.baseExposureStops;
    const auto frame=plan.data[ProcessingPlan::FrameRect];spec.normalizedFrame={frame.x,frame.y,frame.z,frame.w};
    return createImpl(source,*plan.m_brushIntent,spec,cancel,error,std::move(budget),&plan,failure);
}
std::unique_ptr<BrushRenderRequest> BrushRenderRequest::createOriginal(const QImage &source,const ProcessingPlan &plan,
    const GeometryState &geometry,Layout layout,const CancelToken &cancel,QString *error,
    std::shared_ptr<BrushRenderBudget> budget,BrushAdmissionFailure *failure) {
    if(failure)*failure=BrushAdmissionFailure::Invalid;
    if(cancelled(cancel)){if(failure)*failure=BrushAdmissionFailure::Cancelled;fail(error,"Cancelled");return {};}
    if(!validPreparedPlan(plan,error))return {};
    try {
        auto state=*plan.m_brushIntent;state.geometry=geometry;
        Spec spec;spec.layout=layout;spec.encoding=plan.encoding;spec.output=plan.output;
        spec.rawSource=plan.rawSource;spec.rawBaseExposureStops=plan.baseExposureStops;
        const auto frame=plan.data[ProcessingPlan::FrameRect];spec.normalizedFrame={frame.x,frame.y,frame.z,frame.w};
        return createImpl(source,state,spec,cancel,error,std::move(budget),nullptr,failure);
    }catch(...){fail(error,"Cannot capture original brush request");return {};}
}
std::unique_ptr<BrushRenderRequest> BrushRenderRequest::createImpl(const QImage &source,const AdjustmentState &state,
    const Spec &spec,const CancelToken &cancel,QString *error,std::shared_ptr<BrushRenderBudget> budget,
    const ProcessingPlan *expectedPlan,BrushAdmissionFailure *failure) {
    if(error)error->clear();
    if(failure)*failure=BrushAdmissionFailure::Invalid;
    const auto refuse=[&](const QString &message)->std::unique_ptr<BrushRenderRequest>{
        if(failure&&cancelled(cancel))*failure=BrushAdmissionFailure::Cancelled;
        fail(error,message);return {};
    };
    try {
        if(source.isNull()||cancelled(cancel))return refuse("Cancelled or no source");
        if(!BrushMaskDocument::validDocument(state.brushMasks)||!BrushMaskTileRaster::anyEnabled(state.brushMasks))return refuse("Admitted brush requests require a valid enabled document");
        if(source.format()!=QImage::Format_RGBA64&&source.format()!=QImage::Format_RGBA32FPx4)return refuse("Admitted brush sources require RGBA64 or RGBA32FPx4");
        if(!BrushMaskGeometry::validFrame(source.width(),source.height())
            ||qint64(source.width())*source.height()>MaxSourcePixels)return refuse("Admitted brush source exceeds 32 Mi pixels");
        if(source.sizeInBytes()<=0||source.sizeInBytes()>MaxSourcePixels*16)return refuse("Unsupported source stride or byte size");
        if((spec.layout!=Layout::Whole&&spec.layout!=Layout::Stripes128)
            ||(spec.sourceStage!=SourceStage::Original&&spec.sourceStage!=SourceStage::Prepared)
            ||(spec.encoding!=ImagePipeline::InputEncoding::SRgb&&spec.encoding!=ImagePipeline::InputEncoding::LinearProPhoto)
            ||int(spec.output)<0||int(spec.output)>int(ColorManagement::OutputSpace::ProPhotoRgb)
            ||!std::isfinite(spec.rawBaseExposureStops)||spec.rawBaseExposureStops<-8||spec.rawBaseExposureStops>8)
            return refuse("Invalid admitted brush render specification");
        if(!BrushMaskTileRaster::validNormalizedFrame({spec.normalizedFrame.x(),spec.normalizedFrame.y(),spec.normalizedFrame.width(),spec.normalizedFrame.height()}))
            return refuse("Invalid admitted brush viewport");
        const auto geometry=spec.sourceStage==SourceStage::Original?state.geometry:GeometryState{};
        const auto size=outputSize(source.size(),geometry);
        if(size.isEmpty())return refuse("Invalid admitted brush geometry");
        const auto region=spec.region==QRect()?QRect(QPoint{},size):spec.region;
        if(region.isEmpty()||!QRect(QPoint{},size).contains(region))return refuse("Invalid admitted brush region");
        // Frozen compile: a caller cannot attach an admission to another plan.
        auto plan=ProcessingPlan::compile(state,spec.encoding,spec.output,spec.rawSource,spec.rawBaseExposureStops);
        plan.setFrameRect(spec.normalizedFrame);
        for(const auto &v:plan.data)for(const auto x:{v.x,v.y,v.z,v.w})if(!std::isfinite(x))return refuse("Non-finite admitted brush plan");
        const auto bytes=estimateBytes(source,region,size,plan,spec.layout);
        if(bytes<=0)return refuse("Invalid admitted brush memory estimate");
        if(!budget)budget=BrushRenderBudget::sharedDefault();
        auto reservation=budget->reserve(bytes);
        if(!reservation){if(failure)*failure=BrushAdmissionFailure::BudgetUnavailable;return refuse("Admitted brush memory budget is exhausted");}
        if(!validState(state))return refuse("Invalid active brush or adjustment state");
        if(expectedPlan&&!samePreparedPlan(plan,*expectedPlan))return refuse("Prepared brush plan differs from its original intent");
        if(!ImagePipeline::preflightAdmittedBrush(size,plan,region,spec.layout==Layout::Whole?region.height():128,cancel,error)) {
            if(failure&&cancelled(cancel))*failure=BrushAdmissionFailure::Cancelled;return {};
        }
        // QImage can wrap caller-owned memory. A deep pixel snapshot prevents
        // later writes through an external buffer from changing this request.
        auto frozenSource=source.copy();
        if(frozenSource.isNull())return refuse("Cannot allocate admitted source snapshot");
        if(frozenSource.format()==QImage::Format_RGBA32FPx4&&!ImagePipeline::validFloatSource(frozenSource,cancel))return refuse("Cancelled or invalid full FP32 source");
        if(cancelled(cancel))return refuse("Cancelled");
        auto request=std::unique_ptr<BrushRenderRequest>(new BrushRenderRequest);
        request->m_source=std::move(frozenSource);request->m_plan=std::move(plan);request->m_geometry=geometry;request->m_spec=spec;
        request->m_frameSize=size;request->m_region=region;request->m_reservedBytes=bytes;
        request->m_budget=std::move(budget);request->m_reservation=std::move(reservation);
        PerformanceRecorder::count("brush_admitted_requests");
        PerformanceRecorder::value("brush_admitted_reserved_bytes",bytes);
        PerformanceRecorder::value("brush_admitted_budget_bytes",request->m_budget->snapshot().limit);
        if(failure)*failure=BrushAdmissionFailure::None;
        return request;
    }catch(const std::exception &e){return refuse(QString::fromUtf8(e.what()));}
    catch(...){return refuse("Admitted brush allocation failed");}
}
bool BrushRenderRequest::begin(Layout expected,const CancelToken &cancel,QString *error) {
    if(error)error->clear();
    if(m_spec.layout!=expected)return fail(error,"Wrong layout for admitted brush request");
    if(m_used.exchange(true,std::memory_order_relaxed))return fail(error,"Admitted brush request has already executed");
    return !cancelled(cancel)||fail(error,"Cancelled");
}
QImage BrushRenderRequest::prepare(const CancelToken &cancel,QString *error) const {
    auto image=m_geometry.apply(m_source,cancel);
    if(image.isNull()||image.size()!=m_frameSize){fail(error,cancelled(cancel)?"Cancelled":"Admitted geometry size or allocation failed");return {};}
    return image;
}
BrushImageLease BrushRenderRequest::render(const CancelToken &cancel,QString *error) {
    if(!begin(Layout::Whole,cancel,error))return {};
    try {
        const auto source=prepare(cancel,error);if(source.isNull())return {};
        auto rendered=ImagePipeline::renderAdmittedBrush(source,m_plan,m_region,cancel,m_spec.parallel);
        if(rendered.isNull()||cancelled(cancel)){fail(error,cancelled(cancel)?"Cancelled":"Admitted brush rendering failed");return {};}
        BrushImageLease lease;
        lease.m_reservation=std::static_pointer_cast<const BrushRenderBudget::Reservation>(m_reservation)->split(rendered.sizeInBytes());
        if(!lease.m_reservation){fail(error,"Cannot retain admitted output payload");return {};}
        lease.m_image=std::move(rendered);m_renderCompleted=true;return lease;
    }catch(...){fail(error,"Admitted brush rendering failed");return {};}
}
bool BrushRenderRequest::visitStrips(const StripConsumer &consumer,const CancelToken &cancel,QString *error) {
    if(!consumer)return fail(error,"Missing admitted strip consumer");
    if(!begin(Layout::Stripes128,cancel,error))return false;
    try {
        const auto source=prepare(cancel,error);if(source.isNull())return false;
        for(int y=m_region.y();y<=m_region.bottom();y+=128) {
            if(cancelled(cancel))return fail(error,"Cancelled");
            const QRect region(m_region.x(),y,m_region.width(),std::min(128,m_region.bottom()-y+1));
            BrushImageLease lease;
            lease.m_image=ImagePipeline::renderAdmittedBrush(source,m_plan,region,cancel,m_spec.parallel);
            if(lease.m_image.isNull())return fail(error,cancelled(cancel)?"Cancelled":"Admitted strip rendering failed");
            lease.m_reservation=std::static_pointer_cast<const BrushRenderBudget::Reservation>(m_reservation)->split(lease.m_image.sizeInBytes());
            if(!lease.m_reservation)return fail(error,"Cannot retain admitted strip payload");
            if(!consumer(lease,y-m_region.y()))return fail(error,"Admitted strip consumer refused output");
        }
        return !cancelled(cancel)||fail(error,"Cancelled");
    }catch(...){return fail(error,"Admitted strip rendering or consumer failed");}
}
QImage BrushRenderRequest::retainFloatUpload(QImage image) {
    if(!m_renderCompleted||m_uploadRetained||m_spec.layout!=Layout::Whole
        ||image.format()!=QImage::Format_RGBA32FPx4||image.size()!=size())return {};
    try {
        auto charge=std::static_pointer_cast<const BrushRenderBudget::Reservation>(m_reservation)->split(image.sizeInBytes());
        auto retained=retainPixels(image,std::move(charge));
        if(!retained.isNull())m_uploadRetained=true;
        return retained;
    }catch(...){return {};}
}
