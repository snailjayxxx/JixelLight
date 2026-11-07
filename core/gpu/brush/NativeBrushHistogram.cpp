#include "core/gpu/brush/NativeBrushDomain_p.h"
#include "core/gpu/brush/NativeBrushPrivate.h"

namespace NativeBrush {
namespace {
constexpr int CountsBytes=HistogramCounts::GpuWords*int(sizeof(quint32));
bool validWire(const QByteArray &bytes,quint64 pixels) {
    if(bytes.size()!=CountsBytes)return false;
    std::array<quint32,HistogramCounts::GpuWords> values{};std::memcpy(values.data(),bytes.constData(),CountsBytes);
    if(values[4098]!=pixels || values[4099]!=0 || values[4096]>pixels || values[4097]>pixels)return false;
    for(int c=0;c<4;++c){quint64 total=0;for(int i=0;i<1024;++i)total+=values[std::size_t(c*1024+i)];if(total!=pixels)return false;}
    return true;
}
}
bool RenderDomain::histogram(FrameHandle handle,QRhiCommandBuffer *cb) {
    auto *f=find(handle);
    if(!f || !cb || !publishable(handle) || !m_caps.m_histogramCompute)return false;
    const auto size=f->extent;const qint64 pixels=qint64(size.width())*size.height();const int groups=int((pixels+16383)/16384),copies=m_caps.m_framesInFlight;
    if(pixels<=0 || pixels>BrushRenderRequest::MaxSourcePixels || groups>m_caps.m_groupMaximum || copies<1 || copies>8)return false;
    try {
        const auto histogramShader=QShader::fromSerialized(m_shaders.histogram),reduceShader=QShader::fromSerialized(m_shaders.reduce);
        if(!histogramShader.isValid() || !reduceShader.isValid())return false;
        const int uniformBytes=m_rhi->ubufAligned(40*int(sizeof(Float4)));
        const qint64 workBytes=qint64(groups)*CountsBytes+CountsBytes+qint64(uniformBytes)*(copies+1)+1024*1024;
        auto work=BudgetAccess::reserve(f->budget,workBytes);if(!work)return false;
        auto reply=BudgetAccess::reserve(f->budget,256*1024);if(!reply)return false;
        auto h=std::make_unique<Histogram>();h->frame=handle;h->extent=size;h->groups=groups;h->workCharge=std::move(work);h->replyCharge=std::move(reply);
        h->uniform.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,uniformBytes));
        h->partials.reset(m_rhi->newBuffer(QRhiBuffer::Static,QRhiBuffer::StorageBuffer,groups*CountsBytes));
        h->counts.reset(m_rhi->newBuffer(QRhiBuffer::Static,QRhiBuffer::StorageBuffer,CountsBytes));
        if(!h->uniform->create() || !h->partials->create() || !h->counts->create())return false;
        using B=QRhiShaderResourceBinding;
        h->histogramBindings.reset(m_rhi->newShaderResourceBindings());h->histogramBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,h->uniform.get()),B::imageLoad(1,B::ComputeStage,f->output.get(),0),B::bufferStore(2,B::ComputeStage,h->partials.get())});
        h->reduceBindings.reset(m_rhi->newShaderResourceBindings());h->reduceBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,h->uniform.get()),B::bufferLoad(1,B::ComputeStage,h->partials.get()),B::bufferStore(2,B::ComputeStage,h->counts.get())});
        h->histogram.reset(m_rhi->newComputePipeline());h->histogram->setShaderStage({QRhiShaderStage::Compute,histogramShader});h->histogram->setShaderResourceBindings(h->histogramBindings.get());
        h->reduce.reset(m_rhi->newComputePipeline());h->reduce->setShaderStage({QRhiShaderStage::Compute,reduceShader});h->reduce->setShaderResourceBindings(h->reduceBindings.get());
        if(!h->histogramBindings->create() || !h->reduceBindings->create() || !h->histogram->create() || !h->reduce->create())return false;
        h->completion.bytes(CountsBytes);auto *job=h.get();m_histograms.emplace(++m_nextHistogram,std::move(h));++f->gpuUsers;
        std::array<Float4,40> params{};params[ProcessingPlan::Dimensions]={float(size.width()),float(size.height()),float(groups),0};
        auto *updates=m_rhi->nextResourceUpdateBatch();updates->updateDynamicBuffer(job->uniform.get(),0,sizeof(params),params.data());
        cb->beginComputePass(updates);cb->setComputePipeline(job->histogram.get());cb->setShaderResources(job->histogramBindings.get());cb->dispatch(groups,1,1);cb->endComputePass();
        cb->beginComputePass();cb->setComputePipeline(job->reduce.get());cb->setShaderResources(job->reduceBindings.get());cb->dispatch((HistogramCounts::GpuWords+255)/256,1,1);cb->endComputePass();
        auto *read=m_rhi->nextResourceUpdateBatch();read->readBackBuffer(job->counts.get(),0,CountsBytes,&job->completion.result);cb->resourceUpdate(read);
        ++m_trace.histogramDispatches;m_trace.histogramReadbackBytes+=CountsBytes;return true;
    }catch(...){return false;}
}

void RenderDomain::collectHistograms() {
    for(auto it=m_histograms.begin();it!=m_histograms.end();) {
        auto &h=*it->second;if(!h.completion.done()){++it;continue;}
        auto *f=find(h.frame);const auto pixels=quint64(h.extent.width())*h.extent.height();
        if(f && publishable(h.frame) && h.completion.valid && h.completion.callbackOwner==m_caps.m_threads.render
            && validWire(h.completion.result.data,pixels)) {
            auto reply=std::make_shared<HistogramReply>();reply->charge=std::move(h.replyCharge);reply->owner=retirement();reply->stamp=f->stamp;
            reply->generation=generation();reply->pixels=pixels;reply->counts=std::move(h.completion.result.data);m_replies.push_back(std::move(reply));
        }
        if(f && f->gpuUsers>0)--f->gpuUsers;
        it=m_histograms.erase(it);
    }
}
std::vector<std::shared_ptr<HistogramReply>> RenderDomain::takeHistogramReplies() {
    if(!owner())return {};auto replies=std::move(m_replies);m_replies.clear();return replies;
}
} // namespace NativeBrush
