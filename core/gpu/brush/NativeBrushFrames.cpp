#include "core/gpu/brush/NativeBrushDomain_p.h"
#include "core/gpu/brush/NativeBrushPrivate.h"

namespace NativeBrush {
namespace { constexpr quint32 FrameMarker=0x4652414d, DrawMarker=0x44524157; }

bool RenderDomain::activate(FrameStamp stamp) {
    if (!owner() || !m_caps.active() || !stamp.target.valid() || !stamp.revision || stamp.sourceKey.size()!=32) return false;
    if (m_current.revision && stamp.revision<=m_current.revision && stamp!=m_current) return false;
    m_current=std::move(stamp); return true;
}
RenderDomain::Frame *RenderDomain::find(FrameHandle h) const {
    if (!owner() || h.generation!=generation()) return nullptr;
    const auto it=m_frames.find(h.frame); return it==m_frames.end()?nullptr:it->second.get();
}
FrameHandle RenderDomain::attachFrame(InputHandle input,const std::shared_ptr<FrameAdmission> &a,FrameFailure *why) {
    auto fail=[why](FrameFailure f)->FrameHandle{if(why)*why=f;return {};};
    if (!owner()) return fail(FrameFailure::WrongThread);
    if (!a || !ready(input) || sourceKey(input)!=a->m_stamp.sourceKey) return fail(FrameFailure::InvalidSource);
    if (!m_caps.active() || !a->m_caps.active() || a->m_caps.m_lifetime!=m_lifetime
        || a->m_caps.generation()!=generation() || a->m_stamp!=m_current) return fail(FrameFailure::Stale);
    if (cancelled(a->m_cancel)) return fail(FrameFailure::Cancelled);
    int expected=0;
    if (!a->m_state.compare_exchange_strong(expected,1,std::memory_order_acq_rel)) return fail(FrameFailure::Consumed);
    struct Guard{std::atomic_int &state;bool committed=false;~Guard(){if(!committed)state.store(0,std::memory_order_release);}} guard{a->m_state};
    try {
        const auto coverageShader=QShader::fromSerialized(m_shaders.coverage), colourShader=QShader::fromSerialized(m_shaders.colour);
        if (!coverageShader.isValid() || !colourShader.isValid()) return fail(FrameFailure::Allocation);
        auto frame=std::make_unique<Frame>(); const auto size=a->m_source->size(); const auto pixels=qint64(size.width())*size.height();
        frame->uniform.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,a->m_uniformBytes));
        frame->colourUniform.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,a->m_colourUniformBytes));
        frame->records.reset(m_rhi->newBuffer(QRhiBuffer::Static,QRhiBuffer::StorageBuffer,int(std::max<std::size_t>(1,a->m_packed.size())*sizeof(detail::DabRecord))));
        frame->spans.reset(m_rhi->newBuffer(QRhiBuffer::Static,QRhiBuffer::StorageBuffer,int(a->m_tiles.size()*sizeof(detail::TileSpan))));
        frame->mask.reset(m_rhi->newBuffer(QRhiBuffer::Static,QRhiBuffer::StorageBuffer,int(pixels*4)));
        frame->marker.reset(m_rhi->newBuffer(QRhiBuffer::Static,QRhiBuffer::StorageBuffer,4));
        frame->output.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,size,1,QRhiTexture::UsedWithLoadStore|QRhiTexture::UsedAsTransferSource));
        frame->lut.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,{1,1},1,QRhiTexture::UsedWithLoadStore));
        if (!frame->uniform->create() || !frame->colourUniform->create() || !frame->records->create() || !frame->spans->create()
            || !frame->mask->create() || !frame->marker->create() || !frame->output->create() || !frame->lut->create()) return fail(FrameFailure::Allocation);
        using B=QRhiShaderResourceBinding;
        frame->maskBindings.reset(m_rhi->newShaderResourceBindings());
        frame->maskBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,frame->uniform.get()),B::bufferLoad(1,B::ComputeStage,frame->records.get()),B::bufferStore(2,B::ComputeStage,frame->mask.get()),B::bufferLoad(3,B::ComputeStage,frame->spans.get())});
        frame->colourBindings.reset(m_rhi->newShaderResourceBindings());
        frame->colourBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,frame->colourUniform.get()),B::imageLoad(1,B::ComputeStage,texture(input),0),B::imageStore(2,B::ComputeStage,frame->output.get(),0),B::imageLoad(3,B::ComputeStage,frame->lut.get(),0),B::bufferLoad(4,B::ComputeStage,frame->mask.get())});
        frame->coverage.reset(m_rhi->newComputePipeline());frame->coverage->setShaderStage({QRhiShaderStage::Compute,coverageShader});frame->coverage->setShaderResourceBindings(frame->maskBindings.get());
        frame->colour.reset(m_rhi->newComputePipeline());frame->colour->setShaderStage({QRhiShaderStage::Compute,colourShader});frame->colour->setShaderResourceBindings(frame->colourBindings.get());
        if (!frame->maskBindings->create() || !frame->colourBindings->create() || !frame->coverage->create() || !frame->colour->create()) return fail(FrameFailure::Allocation);
        if (!m_caps.active()) return fail(FrameFailure::Stale);
        frame->budget=a->m_source->m_budget;frame->outputSpace=a->m_plan.output;frame->stamp=a->m_stamp;frame->input=input;frame->extent=size;frame->columns=(size.width()+127)/128;frame->cancel=a->m_cancel;
        const auto id=++m_nextFrame;auto *f=frame.get();m_frames.emplace(id,std::move(frame));
        ++find(input)->gpuUsers;
        f->outputCharge=std::move(a->m_outputCharge);f->workCharge=std::move(a->m_workCharge);
        f->plan=std::move(a->m_plan);f->packed=std::move(a->m_packed);f->tiles=std::move(a->m_tiles);
        a->m_state.store(2,std::memory_order_release);guard.committed=true;
        if (why) *why=FrameFailure::None;
        return {generation(),id};
    } catch (...) { return fail(FrameFailure::Allocation); }
}

bool RenderDomain::compute(FrameHandle handle,QRhiCommandBuffer *cb) {
    auto *f=find(handle);
    if (!f || !cb || !m_caps.active() || f->issued || f->settled || f->stamp!=m_current || cancelled(f->cancel) || !ready(f->input)) return false;
    detail::CoverageUniform uniform{{0,0,f->extent.width(),f->extent.height()},{int(f->packed.size()),f->columns,0,0}};
    f->plan.data[ProcessingPlan::Dimensions]={float(f->extent.width()),float(f->extent.height()),1,1};
    f->plan.data[ProcessingPlan::PixelMap]=f->plan.pixelMap(f->extent);
    auto *updates=m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(f->uniform.get(),0,sizeof(uniform),&uniform);
    updates->updateDynamicBuffer(f->colourUniform.get(),0,sizeof(f->plan.data),f->plan.data.data());
    const detail::DabRecord empty{};
    updates->uploadStaticBuffer(f->records.get(),f->packed.empty()?static_cast<const void *>(&empty):f->packed.data());
    updates->uploadStaticBuffer(f->spans.get(),f->tiles.data());updates->uploadStaticBuffer(f->marker.get(),&FrameMarker);
    const std::array<float,4> black{0,0,0,1};QRhiTextureSubresourceUploadDescription zero(black.data(),sizeof(black));zero.setSourceSize({1,1});updates->uploadTexture(f->lut.get(),{{0,0,zero}});
    cb->beginComputePass(updates);cb->setComputePipeline(f->coverage.get());cb->setShaderResources(f->maskBindings.get());cb->dispatch((f->extent.width()+15)/16,(f->extent.height()+15)/16,1);cb->endComputePass();
    cb->beginComputePass();cb->setComputePipeline(f->colour.get());cb->setShaderResources(f->colourBindings.get());cb->dispatch((f->extent.width()+15)/16,(f->extent.height()+15)/16,1);cb->endComputePass();
    f->completion.marker(FrameMarker);f->issued=true;
    auto *fence=m_rhi->nextResourceUpdateBatch();fence->readBackBuffer(f->marker.get(),0,4,&f->completion.result);cb->resourceUpdate(fence);
    ++m_trace.coverageDispatches;++m_trace.colourDispatches;m_trace.controlReadbackBytes+=4;return true;
}

void RenderDomain::collectFrames() {
    for (auto it=m_frames.begin();it!=m_frames.end();) {
        auto &f=*it->second;const bool arrived=f.completion.done();
        const bool unwanted=!m_caps.active() || cancelled(f.cancel) || f.users==0
            || (arrived && (!f.completion.valid || f.completion.callbackOwner!=m_caps.m_threads.render));
        if (!f.settled && ((f.issued && arrived) || (!f.issued && unwanted))) {
            f.coverage.reset();f.colour.reset();f.maskBindings.reset();f.colourBindings.reset();f.uniform.reset();f.colourUniform.reset();f.records.reset();f.spans.reset();f.mask.reset();f.lut.reset();
            f.packed=std::vector<detail::DabRecord>{};f.tiles=std::vector<detail::TileSpan>{};f.plan={};f.workCharge.reset();
            if (auto *input=find(f.input);input && input->gpuUsers>0) --input->gpuUsers;
            f.input={};f.settled=true;
        }
        if (unwanted && (!f.issued || arrived) && f.gpuUsers==0) it=m_frames.erase(it); else ++it;
    }
}
bool RenderDomain::ready(FrameHandle h) const {
    const auto *f=find(h);return f && m_caps.active() && f->issued && f->settled && f->completion.done()
        && f->completion.valid && f->completion.callbackOwner==m_caps.m_threads.render && !cancelled(f->cancel);
}
QRhiTexture *RenderDomain::texture(FrameHandle h) const { auto *f=find(h);return ready(h)?f->output.get():nullptr; }
bool RenderDomain::publishable(FrameHandle h) const { auto *f=find(h);return ready(h) && f->stamp==m_current; }
void RenderDomain::release(FrameHandle h) {auto *f=find(h);if(f && f->users>0)--f->users;collect();}

bool RenderDomain::present(FrameHandle handle,QRhiCommandBuffer *cb,const std::function<bool(QRhiTexture *)> &draw) {
    auto *f=find(handle);if(!f || !cb || !draw || !publishable(handle))return false;
    try {
        auto charge=BudgetAccess::reserve(f->budget,256*1024);if(!charge)return false;
        auto p=std::make_unique<Presentation>();p->charge=std::move(charge);p->frame=handle;
        p->marker.reset(m_rhi->newBuffer(QRhiBuffer::Static,QRhiBuffer::StorageBuffer,4));if(!p->marker->create())return false;
        p->completion.marker(DrawMarker);auto *use=p.get();m_presentations.emplace(++m_nextPresentation,std::move(p));++f->gpuUsers;
        bool drawn=false;try{drawn=draw(f->output.get());}catch(...){drawn=false;}
        auto *marker=m_rhi->nextResourceUpdateBatch();marker->uploadStaticBuffer(use->marker.get(),&DrawMarker);cb->resourceUpdate(marker);
        auto *fence=m_rhi->nextResourceUpdateBatch();fence->readBackBuffer(use->marker.get(),0,4,&use->completion.result);cb->resourceUpdate(fence);m_trace.controlReadbackBytes+=4;
        return drawn;
    }catch(...){return false;}
}
void RenderDomain::collectPresentations() {
    for(auto it=m_presentations.begin();it!=m_presentations.end();){auto &p=*it->second;
        if(!p.completion.done()){++it;continue;}if(auto *f=find(p.frame);f && f->gpuUsers>0)--f->gpuUsers;it=m_presentations.erase(it);}
}
bool RenderDomain::workPending() const {
    for(const auto &[id,f]:m_frames)if(f->issued && !f->completion.done())return true;
    for(const auto &[id,h]:m_histograms)if(!h->completion.done())return true;
    for(const auto &[id,p]:m_presentations)if(!p->completion.done())return true;
    for(const auto &[id,d]:m_diagnostics)if(!d->completion.done())return true;
    return false;
}
void RenderDomain::clearWorkResources() {
    collectDiagnostics();
    for(auto &[id,d]:m_diagnostics)if(d->deliver)d->deliver(d->stamp,{{"available",false},{"error","Native RHI retired before diagnostic completion"}});
    m_diagnostics.clear();m_replies.clear();m_presentations.clear();m_histograms.clear();m_frames.clear();
}
} // namespace NativeBrush
