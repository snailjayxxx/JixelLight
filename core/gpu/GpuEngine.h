#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/gpu/brush/NativeBrushUpload.h"
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QImage>
#include <functional>
#include <memory>

// Render-thread-only engine. Source uploads are keyed independently of editing
// revisions. Normal interaction reads back only 16,400 histogram bytes. An
// explicit diagnostic request can capture the pre-monitor working output.
class GpuEngine final {
public:
    using HistogramReady = std::function<void(quint64, QByteArray, quint64)>;
    using FrameReady = std::function<void(QJsonObject)>;
    explicit GpuEngine(QRhi *rhi);
    ~GpuEngine();
    bool process(QRhiCommandBuffer *cb, const QImage &linearFloatSource, ProcessingPlan plan,
                 quint64 revision, const HistogramReady &histogramReady, bool forceHistogram = false);
    // Explicit large prepared/viewport brush route. CPU-reference pixels are
    // uploaded for GPU display/scopes; this does not enable native GPU brushes.
    // Admission refusal leaves the engine usable and reports through error.
    bool processAdmittedBrush(QRhiCommandBuffer *cb,const QImage &linearFloatSource,ProcessingPlan plan,
        quint64 revision,const HistogramReady &histogramReady,bool forceHistogram=false,
        QString *error=nullptr,std::shared_ptr<BrushRenderBudget> budget={});
    bool draw(QRhiCommandBuffer *cb, QRhiRenderTarget *target);
    // Qt Quick composites premultiplied monitor-device RGB. Keep draw() and
    // the working output texture straight for diagnostic/scopes/export users.
    bool drawForSceneGraph(QRhiCommandBuffer *cb, QRhiRenderTarget *target);
    bool drawExternalWorkingTexture(QRhiCommandBuffer *,QRhiRenderTarget *,QRhiTexture *);
    NativeBrush::ShaderBytes nativeBrushShaders() const;
    bool captureWorkingFrame(QRhiCommandBuffer *cb, quint64 revision, const FrameReady &ready,
                             QString *error = nullptr, qint64 budget = 64 * 1024 * 1024);
    // Display color management is intentionally downstream of m_output so it
    // cannot alter image pixels used by scopes, export, fitting, or CPU/GPU
    // processing parity. The atlas is a 33^3 encoded-sRGB -> monitor-device LUT.
    void setDisplayColorLut(const QImage &atlas, const QString &key, const QString &profileName);
    QRhiTexture *outputTexture() const { return m_output.get(); }
    QString error() const { return m_error; }
    QString backendName() const;
    bool hasPendingReadback() const;
    quint64 sourceUploads() const { return m_uploads; }
    quint64 denoiseGuideDispatches() const { return m_denoiseGuideDispatches; }
    quint64 denoiseFilterDispatches() const { return m_denoiseFilterDispatches; }
    quint64 dehazeGuideDispatches() const {return m_dehazeGuideDispatches;}
    quint64 dehazeHorizontalDispatches() const {return m_dehazeHorizontalDispatches;}
    quint64 dehazeVerticalDispatches() const {return m_dehazeVerticalDispatches;}
    quint64 dehazeFilterDispatches() const {return m_dehazeFilterDispatches;}
    quint64 textureGuideDispatches() const {return m_textureGuideDispatches;}
    quint64 textureSmallHorizontalDispatches() const {return m_textureSmallHorizontalDispatches;}
    quint64 textureSmallVerticalDispatches() const {return m_textureSmallVerticalDispatches;}
    quint64 textureLargeHorizontalDispatches() const {return m_textureLargeHorizontalDispatches;}
    quint64 textureFilterDispatches() const {return m_textureFilterDispatches;}
    quint64 clarityGuideDispatches() const {return m_clarityGuideDispatches;}
    quint64 clarityHorizontalDispatches() const {return m_clarityHorizontalDispatches;}
    quint64 clarityFilterDispatches() const {return m_clarityFilterDispatches;}
    quint64 renderedRevision() const { return m_revision; }
    bool lastProcessUsedCpuFallback() const { return m_lastCpuFallback; }
private:
    bool processImpl(QRhiCommandBuffer *cb,const QImage &source,ProcessingPlan plan,quint64 revision,
        const HistogramReady &histogramReady,bool forceHistogram,BrushRenderRequest *admitted);
    struct ReadbackState { QRhiReadbackResult result; bool done = false; };
    bool initialize(QSize size);
    bool ensureDetail();
    bool ensureDenoise();
    bool ensureDehaze();
    bool ensureTexture();
    bool ensureClarity();
    bool createDisplay(QRhiRenderTarget *target,bool premultipliedAlpha,QRhiTexture *input);
    bool drawImpl(QRhiCommandBuffer *cb,QRhiRenderTarget *target,bool premultipliedAlpha,QRhiTexture *input=nullptr);
    bool ensureDisplayColorResources();
    bool buildCompute(std::unique_ptr<QRhiComputePipeline> &pipeline,
                      QRhiShaderResourceBindings *bindings, const QString &name);
    bool fail(const QString &message);
    QRhi *m_rhi;
    QString m_error;
    QSize m_size;
    qint64 m_sourceKey = 0;
    quint64 m_revision = 0, m_histogramRevision = 0, m_uploads = 0;
    int m_groups = 0;
    QElapsedTimer m_histogramClock;
    std::shared_ptr<ReadbackState> m_readback;
    std::shared_ptr<ReadbackState> m_frameReadback;
    ColorManagement::OutputSpace m_outputSpace = ColorManagement::OutputSpace::SRgb;
    std::unique_ptr<QRhiTexture> m_source, m_output, m_lutTexture, m_detailBase, m_horizontal, m_displayLutTexture;
    std::unique_ptr<QRhiTexture> m_denoiseGuide,m_denoiseLinear;
    std::unique_ptr<QRhiShaderResourceBindings> m_denoisePrepareBindings,m_denoiseFilterBindings,m_denoiseColorBindings,m_denoiseDetailBindings;
    std::unique_ptr<QRhiComputePipeline> m_denoisePreparePipeline,m_denoiseFilterPipeline;
    std::unique_ptr<QRhiTexture> m_dehazeGuide,m_dehazeHorizontal,m_dehazeCoarse,m_dehazeLinear;
    std::unique_ptr<QRhiBuffer> m_dehazeKernel;
    std::unique_ptr<QRhiShaderResourceBindings> m_dehazePrepareBindings,m_dehazePrepareDenoisedBindings,
        m_dehazeHorizontalBindings,m_dehazeVerticalBindings,m_dehazeFilterBindings,m_dehazeFilterDenoisedBindings,
        m_dehazeColorBindings,m_dehazeDetailBindings;
    std::unique_ptr<QRhiComputePipeline> m_dehazePreparePipeline,m_dehazeHorizontalPipeline,m_dehazeVerticalPipeline,m_dehazeFilterPipeline;
    qint64 m_dehazeSourceKey=0;
    int m_dehazeEncoding=-1;
    std::array<Float4,3> m_dehazeInputRows{};
    Float4 m_dehazeDenoiseSettings{},m_dehazeAirlight{},m_dehazeSettings{};
    float m_dehazeDarkRadius=-1;
    bool m_dehazeIdentityValid=false,m_dehazeGuideValid=false,m_dehazeCoarseValid=false,m_dehazeOutputValid=false;
    quint64 m_dehazeGuideDispatches=0,m_dehazeHorizontalDispatches=0,m_dehazeVerticalDispatches=0,m_dehazeFilterDispatches=0;
    std::unique_ptr<QRhiTexture> m_textureGuide,m_textureHorizontal,m_textureSmallBand,m_textureLinear;
    std::unique_ptr<QRhiBuffer> m_textureKernel;
    std::unique_ptr<QRhiShaderResourceBindings> m_texturePrepareBindings,m_texturePrepareDenoisedBindings,m_texturePrepareDehazedBindings,
        m_textureHorizontalBindings,m_textureSmallVerticalBindings,m_textureFilterBindings,m_textureFilterDenoisedBindings,m_textureFilterDehazedBindings,
        m_textureColorBindings,m_textureDetailBindings;
    std::unique_ptr<QRhiComputePipeline> m_texturePreparePipeline,m_textureSmallHorizontalPipeline,
        m_textureSmallVerticalPipeline,m_textureLargeHorizontalPipeline,m_textureFilterPipeline;
    qint64 m_textureSourceKey=0;
    int m_textureEncoding=-1;
    std::array<Float4,3> m_textureInputRows{};
    Float4 m_textureDenoiseSettings{},m_textureDehazeSettings{},m_textureDehazeAirlight{},m_textureSettings{};
    float m_textureSmallRadius=-1,m_textureLargeRadius=-1;
    bool m_textureGuideValid=false,m_textureNeighborhoodValid=false,m_textureOutputValid=false;
    quint64 m_textureGuideDispatches=0,m_textureSmallHorizontalDispatches=0,m_textureSmallVerticalDispatches=0,
        m_textureLargeHorizontalDispatches=0,m_textureFilterDispatches=0;
    std::unique_ptr<QRhiTexture> m_clarityGuide,m_clarityHorizontal,m_clarityLinear;
    std::unique_ptr<QRhiBuffer> m_clarityKernel;
    std::unique_ptr<QRhiShaderResourceBindings> m_clarityPrepareBindings,m_clarityPrepareDenoisedBindings,m_clarityPrepareTexturedBindings,m_clarityPrepareDehazedBindings,
        m_clarityHorizontalBindings,m_clarityFilterBindings,m_clarityFilterDenoisedBindings,m_clarityFilterTexturedBindings,m_clarityFilterDehazedBindings,
        m_clarityColorBindings,m_clarityDetailBindings;
    std::unique_ptr<QRhiComputePipeline> m_clarityPreparePipeline,m_clarityHorizontalPipeline,m_clarityFilterPipeline;
    qint64 m_claritySourceKey=0;
    int m_clarityEncoding=-1;
    std::array<Float4,3> m_clarityInputRows{};
    Float4 m_clarityDenoiseSettings{},m_clarityDehazeSettings{},m_clarityDehazeAirlight{},m_clarityTextureSettings{},m_claritySettings{};
    float m_clarityHorizontalRadius=-1;
    bool m_clarityGuideValid=false,m_clarityHorizontalValid=false,m_clarityOutputValid=false;
    quint64 m_clarityGuideDispatches=0,m_clarityHorizontalDispatches=0,m_clarityFilterDispatches=0;
    QString m_lutKey;
    std::unique_ptr<QRhiBuffer> m_uniform, m_partial, m_counts, m_vertices;
    std::unique_ptr<QRhiSampler> m_sampler, m_displayLutSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_pipelineBindings, m_histogramBindings, m_reduceBindings, m_displayBindings, m_detailBaseBindings, m_horizontalBindings, m_detailBindings;
    std::unique_ptr<QRhiComputePipeline> m_pipeline, m_histogram, m_reduce, m_horizontalPipeline, m_detailPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_display;
    QRhiRenderPassDescriptor *m_displayPass = nullptr;
    int m_displaySamples = 0;
    bool m_displayPremultipliedAlpha = false;
    quint64 m_displayTextureId = 0;
    bool m_uploadVertices = true;
    bool m_lastCpuFallback = false;
    bool m_outputDenoiseActive = false,m_outputClarityActive=false,m_outputTextureActive=false,m_outputDehazeActive=false,m_outputPointColorActive=false,m_outputLuminanceMaskActive=false;
    ImagePipeline::FloatSourceFacts m_sourceFacts;
    QString m_outputNumericPolicy;
    // GPU-resident stage cache; downstream color/output edits do not change it.
    qint64 m_denoiseSourceKey = 0;
    int m_denoiseEncoding = -1;
    std::array<Float4,3> m_denoiseInputRows{};
    Float4 m_denoiseSettings{};
    bool m_denoiseGuideValid = false,m_denoiseFilterValid = false;
    quint64 m_denoiseGuideDispatches = 0,m_denoiseFilterDispatches = 0;
    QImage m_displayColorLut;
    QString m_displayColorLutKey;
    QString m_uploadedDisplayColorLutKey;
    QString m_displayColorProfileName;
    // Keeps CPU safety-fallback upload bytes alive until the following frame.
    // Admitted uploads carry a payload reservation in their QImage cleanup.
    QImage m_cpuFallbackFrame;
};
