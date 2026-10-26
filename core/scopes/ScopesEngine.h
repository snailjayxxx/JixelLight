#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include "core/scopes/ScopePlot.h"
#include <QImage>
#include <QVariantList>
#include <QByteArray>
#include <array>
#include <functional>

struct ScopesResult {
    QVariantList red, green, blue, luma;
    double shadowClipPercent = 0.0, highlightClipPercent = 0.0;
    quint64 pixelCount = 0;
};
class BrushRenderRequest;
struct HistogramCounts {
    static constexpr int Bins = 1024;
    static constexpr int GpuWords = 4*Bins+4;
    std::array<quint64,4*Bins> bins{};
    quint64 shadows=0, highlights=0, pixels=0;
    void add(const HistogramCounts &other);
};
class ScopesEngine {
public:
    static ScopesResult analyze(const QImage &image, int bins = 1024, const CancelToken &cancel = {});
    // An optional collector sees completed center strips after detail halos
    // have been removed. It must not alter the pixels or histogram result.
    using RenderedStripObserver = std::function<void(const QImage &, int)>;
    static ScopesResult analyzeFull(const QImage &source, const ProcessingPlan &plan, const CancelToken &cancel = {},
                                   const RenderedStripObserver &completedStrip = {});
    // Optional plots are explicitly requested and share the same admitted
    // color pass. Results publish only after the entire strip visit succeeds.
    static ScopesResult analyzeAdmitted(BrushRenderRequest &request,const CancelToken &cancel={},
                                       QString *error=nullptr,std::array<ScopePlotResult,3> *plots=nullptr);
    static ScopesResult fromGpu(const QByteArray &bytes, quint64 expectedPixels);
    static ScopesResult result(const HistogramCounts &counts, int bins = 1024);
};
