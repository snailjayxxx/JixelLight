#include "core/gpu/brush/NativeBrushDomain_p.h"
#include "core/gpu/brush/NativeBrushPrivate.h"
#include "core/pipeline/FloatFrameFingerprint.h"
#include "diagnostics/PerformanceRecorder.h"

namespace NativeBrush {
bool RenderDomain::capture(FrameHandle handle,QRhiCommandBuffer *cb,qint64 qtKey,const DiagnosticReady &ready,qint64 budget){
    auto *f=find(handle);if(!cb||!f||!ready||!publishable(handle))return false;
    const qint64 bytes=qint64(f->extent.width())*f->extent.height()*16;
    const qint64 limit=std::min(budget,FloatFrameFingerprint::BudgetBytes);
    if(bytes<=0||budget<0||bytes>limit)return false;
    try{
        auto *input=find(f->diagnosticInput);
        const bool sourceReady=input&&this->ready(f->diagnosticInput)&&input->key==f->stamp.sourceKey;
        bool captureSource=sourceReady&&bytes<=limit-bytes;
        QString unavailable=sourceReady?QStringLiteral("budget"):QStringLiteral("source-retired");
        // Account for staging and host bytes, preserving the original final-only
        // reservation if the optional second read cannot fit the native budget.
        auto charge=BudgetAccess::reserve(f->budget,bytes*(captureSource?4:2)+1024*1024);
        if(!charge&&captureSource){captureSource=false;unavailable=QStringLiteral("resource-budget");charge=BudgetAccess::reserve(f->budget,bytes*2+1024*1024);}
        if(!charge)return false;
        auto d=std::make_unique<Diagnostic>();d->charge=std::move(charge);d->frame=handle;d->stamp=f->stamp;
        d->extent=f->extent;d->qtSourceKey=qtKey;d->deliver=ready;d->completion.bytes(int(bytes));
        d->sourceRequested=captureSource;d->readbackBudget=limit;d->requestedBytes=bytes*(captureSource?2:1);
        d->sourceContext={{"parameter_revision",qint64(f->stamp.revision)},{"source_cache_key",QString::number(qtKey)},
            {"input_source_key",QString::fromLatin1(f->stamp.sourceKey.toHex())},{"stage","prepared-source-upload"},
            {"producer","gpu-texture-upload"},{"alpha","straight"},{"monitor_icc",false},{"used_for_compute",true},
            {"note","Prepared native-brush upload; not native sensor float or an internal colour boundary"}};
        if(sourceReady){
            const auto &info=input->description;const auto &rect=info.preparedFrame;
            d->sourceContext.insert("encoding",info.encoding==ImagePipeline::InputEncoding::LinearProPhoto?"linear-prophoto":"encoded-srgb");
            d->sourceContext.insert("raw_source",info.rawSource);d->sourceContext.insert("base_exposure_stops",info.baseExposureStops);
            d->sourceContext.insert("processing_frame_rect",QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()});
        }
        if(captureSource){d->source=f->diagnosticInput;d->sourceCompletion.bytes(int(bytes));}
        else{
            d->sourceContext.insert("available",false);d->sourceContext.insert("reason",unavailable);d->sourceContext.insert("required_bytes",bytes);
            d->sourceContext.insert("error",unavailable=="source-retired"?"Prepared input was released before diagnostic capture":
                unavailable=="resource-budget"?"Optional source capture exceeds remaining native resource budget":"Optional source capture exceeds aggregate readback budget");
        }
        auto *job=d.get();m_diagnostics.emplace(++m_nextDiagnostic,std::move(d));++f->gpuUsers;
        if(captureSource)++input->gpuUsers;
        auto *updates=m_rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(f->output.get()),&job->completion.result);
        if(captureSource)updates->readBackTexture(QRhiReadbackDescription(input->texture.get()),&job->sourceCompletion.result);
        cb->resourceUpdate(updates);return true;
    }catch(...){return false;}
}
void RenderDomain::collectDiagnostics(){
    for(auto it=m_diagnostics.begin();it!=m_diagnostics.end();){auto &d=*it->second;
        if(!d.done()){++it;continue;}auto *f=find(d.frame);QJsonObject result;
        const bool current=f&&publishable(d.frame);
        const auto valid=[&](const detail::Completion &c){return current&&c.valid&&c.callbackOwner==m_caps.m_threads.render
            &&c.result.format==QRhiTexture::RGBA32F&&c.result.pixelSize==d.extent;};
        if(valid(d.completion)){
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
        auto source=d.sourceContext;
        if(d.sourceRequested){
            QJsonObject fingerprint;
            if(valid(d.sourceCompletion)){
                const QImage pixels(reinterpret_cast<const uchar*>(d.sourceCompletion.result.data.constData()),d.extent.width(),d.extent.height(),d.extent.width()*16,QImage::Format_RGBA32FPx4);
                fingerprint=FloatFrameFingerprint::capture(pixels);
            }else fingerprint={{"available",false},{"error","Native prepared-source context was retired or readback invalid"}};
            for(auto field=fingerprint.begin();field!=fingerprint.end();++field)source.insert(field.key(),field.value());
        }
        result.insert("source_staging",source);result.insert("readback_budget_bytes",d.readbackBudget);result.insert("readback_requested_bytes",d.requestedBytes);
        const qint64 received=d.completion.result.data.size()+(d.sourceRequested?d.sourceCompletion.result.data.size():0);
        result.insert("readback_received_bytes",received);PerformanceRecorder::count("diagnostic_gpu_readback_bytes",received);
        result.insert("parameter_revision",qint64(d.stamp.revision));result.insert("source_cache_key",QString::number(d.qtSourceKey));
        result.insert("input_source_key",QString::fromLatin1(d.stamp.sourceKey.toHex()));result.insert("producer","native-brush-gpu-compute");
        result.insert("stage","working-output");result.insert("monitor_icc",false);result.insert("encoding","encoded RGB");
        if(f)result.insert("output_space",ColorManagement::key(f->outputSpace));
        auto deliver=std::move(d.deliver);const auto stamp=d.stamp;
        if(f&&f->gpuUsers>0)--f->gpuUsers;
        if(d.sourceRequested)if(auto *input=find(d.source);input&&input->gpuUsers>0)--input->gpuUsers;
        // Release both payloads/reservations before notifying; only fingerprints survive.
        it=m_diagnostics.erase(it);deliver(stamp,std::move(result));
    }
}
} // namespace NativeBrush
