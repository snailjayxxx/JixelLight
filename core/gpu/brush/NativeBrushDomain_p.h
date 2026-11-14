#pragma once
#include "core/gpu/brush/NativeBrushFrame.h"
#include "core/scopes/ScopesEngine.h"
#include <rhi/qrhi.h>
#include <cstring>

namespace NativeBrush::detail {
// Callback payloads live in the owner domain until arrival. Arrival, payload
// validity and callback ownership are deliberately separate observations.
struct Completion {
    QRhiReadbackResult result;
    std::atomic_bool arrived{false};
    Qt::HANDLE callbackOwner = nullptr;
    bool valid = false;
    void marker(quint32 expected) {
        result.completed = [this,expected] {
            quint32 value=0;
            if (result.data.size()==4) std::memcpy(&value,result.data.constData(),4);
            valid=result.data.size()==4 && value==expected;
            callbackOwner=QThread::currentThreadId(); arrived.store(true,std::memory_order_release);
        };
    }
    void bytes(int expected) {
        result.completed = [this,expected] {
            valid=result.data.size()==expected;
            callbackOwner=QThread::currentThreadId(); arrived.store(true,std::memory_order_release);
        };
    }
    bool done() const { return arrived.load(std::memory_order_acquire); }
};
}

namespace NativeBrush {
struct RenderDomain::Input {
    std::shared_ptr<const void> gpuCharge, uploadCharge;
    CanonicalSource::Ptr uploadCanonical;
    QImage upload;
    QByteArray key;
    SourceDescription description;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiBuffer> marker;
    detail::Completion completion;
    CancelToken cancel;
    int users=1, gpuUsers=0;
    bool submitted=false, settled=false;
};
struct RenderDomain::Frame {
    std::shared_ptr<BrushRenderBudget> budget;
    std::shared_ptr<const void> outputCharge,workCharge;
    FrameStamp stamp;
    InputHandle input;
    // Weak ID only: settled frames still release their normal input GPU hold.
    InputHandle diagnosticInput;
    QSize extent;
    ProcessingPlan plan;
    ColorManagement::OutputSpace outputSpace=ColorManagement::OutputSpace::SRgb;
    std::vector<detail::DabRecord> packed;
    std::vector<detail::TileSpan> tiles;
    CancelToken cancel;
    std::unique_ptr<QRhiTexture> output,lut;
    std::unique_ptr<QRhiBuffer> marker,uniform,colourUniform,records,spans,mask;
    std::unique_ptr<QRhiShaderResourceBindings> maskBindings,colourBindings;
    std::unique_ptr<QRhiComputePipeline> coverage,colour;
    detail::Completion completion;
    int users=1,gpuUsers=0,columns=0;
    bool issued=false,settled=false;
};
struct RenderDomain::Histogram {
    std::shared_ptr<const void> workCharge,replyCharge;
    FrameHandle frame;
    QSize extent;
    int groups=0;
    std::unique_ptr<QRhiBuffer> uniform,partials,counts;
    std::unique_ptr<QRhiShaderResourceBindings> histogramBindings,reduceBindings;
    std::unique_ptr<QRhiComputePipeline> histogram,reduce;
    detail::Completion completion;
};
struct RenderDomain::Presentation {
    std::shared_ptr<const void> charge;
    FrameHandle frame;
    std::unique_ptr<QRhiBuffer> marker;
    detail::Completion completion;
};
struct RenderDomain::Diagnostic {
    std::shared_ptr<const void> charge;
    FrameHandle frame;
    FrameStamp stamp;
    QSize extent;
    qint64 qtSourceKey=0;
    DiagnosticReady deliver;
    InputHandle source;
    bool sourceRequested=false;
    qint64 readbackBudget=0,requestedBytes=0;
    QJsonObject sourceContext;
    detail::Completion completion,sourceCompletion;
    bool done() const { return completion.done()&&(!sourceRequested||sourceCompletion.done()); }
};
}
