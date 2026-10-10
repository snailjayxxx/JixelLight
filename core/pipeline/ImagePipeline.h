#pragma once
#include <QImage>
#include <QJsonObject>
#include "core/pipeline/AdjustmentState.h"
#include "core/color/ColorManagement.h"
#include "core/async/LatestJob.h"

struct ProcessingPlan;
class ImagePipeline {
public:
    enum class InputEncoding { SRgb, LinearProPhoto };
    struct DiagnosticResult { QImage image; QJsonObject stages; };
    // Preserve unassociated channels, including hidden RGB at zero alpha.
    // Qt's generic integer/float conversion can round through premultiplied
    // RGBA64; staging/readback must use the same channel values as the oracle.
    static QImage floatSource(const QImage &source,const CancelToken &cancel = {});
    static QImage rgba64Source(const QImage &source,const CancelToken &cancel = {});
    static QImage process(const QImage &source, const AdjustmentState &state,
                          InputEncoding encoding = InputEncoding::SRgb,
                          ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb,
                          const CancelToken &cancel = {}, bool parallel = true);
    static QImage processRegion(const QImage &source,const ProcessingPlan &plan,const QRect &region,const CancelToken &cancel = {});
    static QImage processWithPlan(const QImage &source, const ProcessingPlan &plan,
                          const CancelToken &cancel = {}, bool parallel = true);
    // Explicit CPU reference capture only. Normal rendering never allocates
    // stage buffers or hashes pixels. The source remains RGBA64, not float RAW.
    static DiagnosticResult diagnoseWithPlan(const QImage &source, const ProcessingPlan &plan,
                                            const CancelToken &cancel = {});
};
