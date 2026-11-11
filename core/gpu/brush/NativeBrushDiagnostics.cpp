#include "core/gpu/brush/NativeBrushDomain_p.h"
#include "core/gpu/brush/NativeBrushPrivate.h"
#include "core/pipeline/FloatFrameFingerprint.h"

namespace NativeBrush {
bool RenderDomain::capture(FrameHandle handle,QRhiCommandBuffer *cb,qint64 qtKey,const DiagnosticReady &ready){
    auto *f=find(handle);if(!cb||!f||!ready||!publishable(handle))return false;
    const qint64 bytes=qint64(f->extent.width())*f->extent.height()*16;
    if(bytes<=0||bytes>FloatFrameFingerprint::BudgetBytes)return false;
    try{
        auto charge=BudgetAccess::reserve(f->budget,bytes*2+1024*1024);if(!charge)return false;
        auto d=std::make_unique<Diagnostic>();d->charge=std::move(charge);d->frame=handle;d->stamp=f->stamp;
        d->extent=f->extent;d->qtSourceKey=qtKey;d->deliver=ready;d->completion.bytes(int(bytes));
        auto *job=d.get();m_diagnostics.emplace(++m_nextDiagnostic,std::move(d));++f->gpuUsers;
        auto *updates=m_rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(f->output.get()),&job->completion.result);cb->resourceUpdate(updates);return true;
    }catch(...){return false;}
}
void RenderDomain::collectDiagnostics(){
    for(auto it=m_diagnostics.begin();it!=m_diagnostics.end();){auto &d=*it->second;
        if(!d.completion.done()){++it;continue;}auto *f=find(d.frame);QJsonObject result;
        if(f&&publishable(d.frame)&&d.completion.valid&&d.completion.callbackOwner==m_caps.m_threads.render
            &&d.completion.result.format==QRhiTexture::RGBA32F&&d.completion.result.pixelSize==d.extent){
            const QImage pixels(reinterpret_cast<const uchar*>(d.completion.result.data.constData()),d.extent.width(),d.extent.height(),d.extent.width()*16,QImage::Format_RGBA32FPx4);
            result=FloatFrameFingerprint::capture(pixels);
            QJsonArray samples;
            for(int i=0;i<32;++i){const int x=(i*149+17)%pixels.width(),y=(i*83+29)%pixels.height();
                const auto *row=reinterpret_cast<const float*>(pixels.constScanLine(y));QJsonArray rgba;
                for(int c=0;c<4;++c){const float v=row[x*4+c];rgba.append(std::isfinite(v)?int(std::lround(std::clamp(v,0.f,1.f)*65535.f)):-1);}
                samples.append(QJsonObject{{"x",x},{"y",y},{"rgba16",rgba}});
            }
            result.insert("diagnostic_samples_rgba16",samples);
        }else result={{"available",false},{"error","Native working-output context was retired or readback invalid"}};
        result.insert("parameter_revision",qint64(d.stamp.revision));result.insert("source_cache_key",QString::number(d.qtSourceKey));
        result.insert("input_source_key",QString::fromLatin1(d.stamp.sourceKey.toHex()));result.insert("producer","native-brush-gpu-compute");
        result.insert("stage","working-output");result.insert("monitor_icc",false);result.insert("encoding","encoded RGB");
        if(f)result.insert("output_space",ColorManagement::key(f->outputSpace));
        auto deliver=std::move(d.deliver);const auto stamp=d.stamp;
        if(f&&f->gpuUsers>0)--f->gpuUsers;it=m_diagnostics.erase(it);deliver(stamp,std::move(result));
    }
}
} // namespace NativeBrush
