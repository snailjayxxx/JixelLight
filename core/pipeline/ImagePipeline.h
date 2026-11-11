#pragma once
#include <QImage>
#include <QJsonObject>
#include "core/pipeline/AdjustmentState.h"
#include "core/color/ColorManagement.h"
#include "core/async/LatestJob.h"

struct ProcessingPlan;
class BrushRenderRequest;
class RenderedPreviewCache;
class ImagePipeline {
public:
    enum class InputEncoding { SRgb, LinearProPhoto };
    struct DiagnosticResult { QImage image; QJsonObject stages; };
    // Preserve unassociated channels, including hidden RGB at zero alpha.
    // Qt's generic integer/float conversion can round through premultiplied
    // RGBA64; staging/readback must use the same channel values as the oracle.
    static QImage floatSource(const QImage &source,const CancelToken &cancel = {});
    // A bounded, finite, unassociated linear FP32 scene source may exceed the
    // display range [0,1], or carry negative out-of-gamut color channels.
    // Alpha must be finite and within [0,1]; no pre-tone RGB clipping.
    static constexpr float MaxLinearInputMagnitude = 65536.0f;
    struct FloatSourceFacts {bool nearZeroAlpha=false,negativeRgb=false;};
    static bool validFloatSource(const QImage &source,const CancelToken &cancel = {},FloatSourceFacts *facts=nullptr);
    static QImage rgba64Source(const QImage &source,const CancelToken &cancel = {});
    static QImage process(const QImage &source, const AdjustmentState &state,
                          InputEncoding encoding = InputEncoding::SRgb,
                          ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb,
                          const CancelToken &cancel = {}, bool parallel = true);
    // Whole-operation research guard for row-streamed callers. Charge every
    // expanded strip, including repeated detail halos, before source color
    // conversion or output creation. A single bounded processRegion remains
    // an independent ROI request; inactive brushes retain ordinary large images.
    static bool preflightTiledResearchRequest(const QSize &frameSize,const ProcessingPlan &plan,
                          int stripeHeight,const CancelToken &cancel = {},QString *error = nullptr);
    static QImage processRegion(const QImage &source,const ProcessingPlan &plan,const QRect &region,const CancelToken &cancel = {});
    static QImage processWithPlan(const QImage &source, const ProcessingPlan &plan,
                          const CancelToken &cancel = {}, bool parallel = true);
    // Explicit CPU reference capture only. Normal rendering never allocates
    // stage buffers or hashes pixels. Real camera RAW still decodes RGBA64;
    // explicit RGBA32FPx4 scene sources retain over-range values until tone mapping.
    static DiagnosticResult diagnoseWithPlan(const QImage &source, const ProcessingPlan &plan,
                                            const CancelToken &cancel = {});
private:
    friend class BrushRenderRequest;
    friend class RenderedPreviewCache;
    // Immutable preview-only stage reuse. Brush admission, exports, exact
    // scopes and diagnostic captures always keep their reference paths.
    static QImage preparePreviewSpatialInput(const QImage &,const ProcessingPlan &,const CancelToken &);
    static QImage processPreviewWithSpatialInput(const QImage &,const QImage &,const ProcessingPlan &,const CancelToken &);
    static bool preflightAdmittedBrush(const QSize &frameSize,const ProcessingPlan &plan,const QRect &region,
                                      int stripeHeight,const CancelToken &cancel,QString *error);
    static QImage renderAdmittedBrush(const QImage &source,const ProcessingPlan &plan,const QRect &region,
                                     const CancelToken &cancel,bool parallel);
};
