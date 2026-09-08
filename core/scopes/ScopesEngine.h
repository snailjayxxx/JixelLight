#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include <QImage>
#include <QVariantList>
#include <QByteArray>
#include <array>

struct ScopesResult {
    QVariantList red, green, blue, luma;
    double shadowClipPercent = 0.0, highlightClipPercent = 0.0;
    quint64 pixelCount = 0;
};
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
    static ScopesResult analyzeFull(const QImage &source, const ProcessingPlan &plan, const CancelToken &cancel = {});
    static ScopesResult fromGpu(const QByteArray &bytes, quint64 expectedPixels);
    static ScopesResult result(const HistogramCounts &counts, int bins = 1024);
};
