#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QImage>
#include <functional>
#include <memory>

// Render-thread-only engine. Source uploads are keyed independently of editing
// revisions. Output stays on the GPU; only 16,400 histogram bytes are read back.
class GpuEngine final {
public:
    using HistogramReady = std::function<void(quint64, QByteArray, quint64)>;
    explicit GpuEngine(QRhi *rhi);
    ~GpuEngine();
    bool process(QRhiCommandBuffer *cb, const QImage &linearFloatSource, ProcessingPlan plan,
                 quint64 revision, const HistogramReady &histogramReady, bool forceHistogram = false);
    bool draw(QRhiCommandBuffer *cb, QRhiRenderTarget *target);
    // Display color management is intentionally downstream of m_output so it
    // cannot alter image pixels used by scopes, export, fitting, or CPU/GPU
    // processing parity. The atlas is a 33^3 encoded-sRGB -> monitor-device LUT.
    void setDisplayColorLut(const QImage &atlas, const QString &key, const QString &profileName);
    QRhiTexture *outputTexture() const { return m_output.get(); }
    QString error() const { return m_error; }
    QString backendName() const;
    bool hasPendingReadback() const;
    quint64 sourceUploads() const { return m_uploads; }
    quint64 renderedRevision() const { return m_revision; }
    bool lastProcessUsedCpuFallback() const { return m_lastCpuFallback; }
private:
    struct ReadbackState { QRhiReadbackResult result; bool done = false; };
    bool initialize(QSize size);
    bool ensureDetail();
    bool createDisplay(QRhiRenderTarget *target);
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
    std::unique_ptr<QRhiTexture> m_source, m_output, m_lutTexture, m_detailBase, m_horizontal, m_displayLutTexture;
    QString m_lutKey;
    std::unique_ptr<QRhiBuffer> m_uniform, m_partial, m_counts, m_vertices;
    std::unique_ptr<QRhiSampler> m_sampler, m_displayLutSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_pipelineBindings, m_histogramBindings, m_reduceBindings, m_displayBindings, m_detailBaseBindings, m_horizontalBindings, m_detailBindings;
    std::unique_ptr<QRhiComputePipeline> m_pipeline, m_histogram, m_reduce, m_horizontalPipeline, m_detailPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_display;
    QRhiRenderPassDescriptor *m_displayPass = nullptr;
    int m_displaySamples = 0;
    bool m_uploadVertices = true;
    bool m_lastCpuFallback = false;
    QImage m_displayColorLut;
    QString m_displayColorLutKey;
    QString m_uploadedDisplayColorLutKey;
    QString m_displayColorProfileName;
    // Keeps CPU safety-fallback upload bytes alive until the following frame.
    QImage m_cpuFallbackFrame;
};
