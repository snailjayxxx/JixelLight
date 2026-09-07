#pragma once
#include <QImage>
#include "core/pipeline/AdjustmentState.h"
#include "core/color/ColorManagement.h"
#include "core/async/LatestJob.h"

struct ProcessingPlan;
class ImagePipeline {
public:
    enum class InputEncoding { SRgb, LinearProPhoto };
    static QImage process(const QImage &source, const AdjustmentState &state,
                          InputEncoding encoding = InputEncoding::SRgb,
                          ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb,
                          const CancelToken &cancel = {}, bool parallel = true);
    static QImage processRegion(const QImage &source,const ProcessingPlan &plan,const QRect &region,const CancelToken &cancel = {});
    static QImage processWithPlan(const QImage &source, const ProcessingPlan &plan,
                          const CancelToken &cancel = {}, bool parallel = true);
};
