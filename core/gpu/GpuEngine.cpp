#include "core/gpu/GpuEngine.h"
#include "core/scopes/ScopesEngine.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QFile>
#include <QResource>
#include <algorithm>

static void initializeShaderResources() { Q_INIT_RESOURCE(pipeline_shaders); }
namespace {
QShader shader(const QString &name) {
    QFile file(QStringLiteral(":/jixellight/") + name + QStringLiteral(".qsb"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QShader::fromSerialized(file.readAll());
}
constexpr int CountsBytes = HistogramCounts::GpuWords * int(sizeof(quint32));
}
GpuEngine::GpuEngine(QRhi *rhi) : m_rhi(rhi) { initializeShaderResources(); }
GpuEngine::~GpuEngine() {
    // No frame-by-frame finish/wait. The only wait is teardown while a callback
    // still references a readback object, so it cannot outlive its QRhi owner.
    if (hasPendingReadback()) m_rhi->finish();
}
bool GpuEngine::fail(const QString &message) { m_error = message; return false; }
QString GpuEngine::backendName() const {
    const char *name = "QRhi";
    switch (m_rhi->backend()) {
    case QRhi::Metal: name = "Metal"; break;
    case QRhi::D3D11: name = "Direct3D 11"; break;
    case QRhi::D3D12: name = "Direct3D 12"; break;
    case QRhi::Vulkan: name = "Vulkan"; break;
    case QRhi::OpenGLES2: name = "OpenGL Compute"; break;
    default: break;
    }
    return QStringLiteral("GPU · %1 · %2").arg(QString::fromLatin1(name), QString::fromUtf8(m_rhi->driverInfo().deviceName));
}
bool GpuEngine::hasPendingReadback() const { return m_readback && !m_readback->done; }
bool GpuEngine::buildCompute(std::unique_ptr<QRhiComputePipeline> &pipeline,
                             QRhiShaderResourceBindings *bindings, const QString &name) {
    const auto code = shader(name);
    if (!code.isValid()) return fail(QStringLiteral("Missing/invalid shader: ") + name);
    pipeline.reset(m_rhi->newComputePipeline());
    pipeline->setShaderStage({QRhiShaderStage::Compute, code});
    pipeline->setShaderResourceBindings(bindings);
    return pipeline->create() || fail(QStringLiteral("Cannot create compute pipeline: ") + name);
}
bool GpuEngine::initialize(QSize size) {
    if (!m_rhi->isFeatureSupported(QRhi::Compute) || !m_rhi->isFeatureSupported(QRhi::ReadBackNonUniformBuffer)
        || !m_rhi->isFeatureSupported(QRhi::TexelFetch)) return fail(QStringLiteral("Compute, storage readback or texel fetch unsupported"));
    if (!m_rhi->isTextureFormatSupported(QRhiTexture::RGBA32F, QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource))
        return fail(QStringLiteral("RGBA32F storage textures unsupported"));
    if (size.width() < 1 || size.height() < 1 || std::max(size.width(), size.height()) > m_rhi->resourceLimit(QRhi::TextureSizeMax)
        || size.width() > 4096 || size.height() > 4096) return fail(QStringLiteral("Preview exceeds GPU texture budget"));
    if (m_rhi->resourceLimit(QRhi::MaxThreadsPerThreadGroup) < 256) return fail(QStringLiteral("256-thread compute groups unsupported"));
    if (m_size == size && m_pipeline) return true;
    if (hasPendingReadback()) m_rhi->finish(); // rare source resize, never a slider update
    m_readback.reset();
    m_display.reset(); m_displayBindings.reset();
    m_pipeline.reset(); m_histogram.reset(); m_reduce.reset();
    m_pipelineBindings.reset(); m_histogramBindings.reset(); m_reduceBindings.reset();
    m_detailBaseBindings.reset();m_horizontalBindings.reset();m_detailBindings.reset();
    m_horizontalPipeline.reset();m_detailPipeline.reset();m_detailBase.reset();m_horizontal.reset();
    m_source.reset(); m_output.reset(); m_partial.reset(); m_counts.reset();
    m_size = size;
    m_groups = (size.width()*size.height()+16383)/16384;
    m_sourceKey = 0; m_revision = 0; m_histogramRevision = 0;
    const auto textureFlags = QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource;
    m_source.reset(m_rhi->newTexture(QRhiTexture::RGBA32F, size, 1, QRhiTexture::UsedWithLoadStore));
    m_output.reset(m_rhi->newTexture(QRhiTexture::RGBA32F, size, 1, textureFlags));
    if (!m_source->create() || !m_output->create()) return fail(QStringLiteral("GPU source/output allocation failed"));
    if (!m_uniform) {
        m_uniform.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, int(sizeof(ProcessingPlan::data))));
        if (!m_uniform->create()) return fail(QStringLiteral("GPU uniform allocation failed"));
    }
    m_partial.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, m_groups*CountsBytes));
    m_counts.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, CountsBytes));
    if (!m_partial->create() || !m_counts->create()) return fail(QStringLiteral("GPU histogram allocation failed"));
    if(!m_lutTexture) {
        m_lutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,QSize(33*33,33),1,QRhiTexture::UsedWithLoadStore));
        if(!m_lutTexture->create())return fail("LUT allocation failed");
    }
    using B = QRhiShaderResourceBinding;
    m_pipelineBindings.reset(m_rhi->newShaderResourceBindings());
    m_pipelineBindings->setBindings({B::uniformBuffer(0, B::ComputeStage, m_uniform.get()),
        B::imageLoad(1, B::ComputeStage, m_source.get(), 0), B::imageStore(2, B::ComputeStage, m_output.get(), 0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
    m_histogramBindings.reset(m_rhi->newShaderResourceBindings());
    m_histogramBindings->setBindings({B::uniformBuffer(0, B::ComputeStage, m_uniform.get()),
        B::imageLoad(1, B::ComputeStage, m_output.get(), 0), B::bufferStore(2, B::ComputeStage, m_partial.get())});
    m_reduceBindings.reset(m_rhi->newShaderResourceBindings());
    m_reduceBindings->setBindings({B::uniformBuffer(0, B::ComputeStage, m_uniform.get()),
        B::bufferLoad(1, B::ComputeStage, m_partial.get()), B::bufferStore(2, B::ComputeStage, m_counts.get())});
    if (!m_pipelineBindings->create() || !m_histogramBindings->create() || !m_reduceBindings->create())
        return fail(QStringLiteral("GPU resource bindings failed"));
    if (!buildCompute(m_pipeline, m_pipelineBindings.get(), "pipeline.comp")
        || !buildCompute(m_histogram, m_histogramBindings.get(), "histogram.comp")
        || !buildCompute(m_reduce, m_reduceBindings.get(), "histogram_reduce.comp")) return false;
    PerformanceRecorder::value("gpu_backend", backendName());
    PerformanceRecorder::value("gpu_working_format", QStringLiteral("RGBA32F; CPU input RGBA64; no FP16 approximation"));
    PerformanceRecorder::value("gpu_estimated_bytes", qint64(size.width())*size.height()*32 + m_groups*CountsBytes + CountsBytes);
    return true;
}

bool GpuEngine::process(QRhiCommandBuffer *cb, const QImage &source, ProcessingPlan plan,
                        quint64 revision, const HistogramReady &histogramReady, bool forceHistogram) {
    if (!m_error.isEmpty()) return false;
    if (source.isNull() || source.format() != QImage::Format_RGBA32FPx4) return fail(QStringLiteral("GPU source must be linear RGBA32FPx4"));
    if (!initialize(source.size())) return false;
    const bool details=plan.data[ProcessingPlan::LookDetail].x>0||plan.data[ProcessingPlan::LookDetail].y>0;
    if(details&&!ensureDetail())return false;
    const auto lut=plan.data[ProcessingPlan::LookStyle].w>0?plan.state.look.lut:nullptr;
    const bool newLut=lut&&lut->digest!=m_lutKey;
    const bool newSource = source.cacheKey() != m_sourceKey;
    if (revision != m_revision || newSource || newLut) {
        PerformanceSpan timing("gpu_submit_cpu_ms", {{"pixels", qint64(source.width())*source.height()}});
        auto *updates = m_rhi->nextResourceUpdateBatch();
        if (newSource) {
            QRhiTextureSubresourceUploadDescription upload(source.constBits(), quint32(source.sizeInBytes()));
            upload.setSourceSize(source.size());
            upload.setDataStride(quint32(source.bytesPerLine()));
            updates->uploadTexture(m_source.get(), {{0, 0, upload}});
            m_sourceKey = source.cacheKey(); ++m_uploads;
            PerformanceRecorder::count("gpu_source_upload_bytes", source.sizeInBytes());
            PerformanceRecorder::count("gpu_source_uploads");
        }
        if(newLut) {
            const QImage atlas=lut->atlas();if(atlas.isNull()) {updates->release();return fail("Invalid LUT atlas");}
            QRhiTextureSubresourceUploadDescription upload(atlas.constBits(),quint32(atlas.sizeInBytes()));
            upload.setSourceSize(atlas.size());upload.setDataStride(quint32(atlas.bytesPerLine()));
            updates->uploadTexture(m_lutTexture.get(),{{0,0,upload}});m_lutKey=lut->digest;
            PerformanceRecorder::count("look_lut_uploads");
        }
        plan.data[ProcessingPlan::Dimensions] = {float(m_size.width()), float(m_size.height()), float(m_groups), 0};
        updates->updateDynamicBuffer(m_uniform.get(), 0, quint32(sizeof(plan.data)), plan.data.data());
        cb->beginComputePass(updates);
        cb->setComputePipeline(m_pipeline.get());
        cb->setShaderResources(details?m_detailBaseBindings.get():m_pipelineBindings.get());
        cb->dispatch((m_size.width()+15)/16, (m_size.height()+15)/16, 1);
        cb->endComputePass();
        if(details) {
            cb->beginComputePass();cb->setComputePipeline(m_horizontalPipeline.get());cb->setShaderResources(m_horizontalBindings.get());
            cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();
            cb->beginComputePass();cb->setComputePipeline(m_detailPipeline.get());cb->setShaderResources(m_detailBindings.get());
            cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();
        }
        m_revision = revision;
    }
    if (histogramReady && !hasPendingReadback() && m_histogramRevision != revision
        && (forceHistogram || !m_histogramClock.isValid() || m_histogramClock.elapsed() >= 70)) {
        m_histogramClock.restart();
        cb->beginComputePass();
        cb->setComputePipeline(m_histogram.get()); cb->setShaderResources(m_histogramBindings.get());
        cb->dispatch(m_groups, 1, 1); cb->endComputePass();
        cb->beginComputePass();
        cb->setComputePipeline(m_reduce.get()); cb->setShaderResources(m_reduceBindings.get());
        cb->dispatch((HistogramCounts::GpuWords+255)/256, 1, 1); cb->endComputePass();
        m_readback = std::make_shared<ReadbackState>();
        const std::weak_ptr<ReadbackState> weak = m_readback;
        const quint64 pixels = quint64(m_size.width())*m_size.height();
        m_readback->result.completed = [weak, histogramReady, revision, pixels] {
            if (const auto state = weak.lock()) {
                state->done = true;
                PerformanceRecorder::count("gpu_readback_bytes", state->result.data.size());
                histogramReady(revision, state->result.data, pixels);
            }
        };
        auto *updates = m_rhi->nextResourceUpdateBatch();
        updates->readBackBuffer(m_counts.get(), 0, CountsBytes, &m_readback->result);
        cb->resourceUpdate(updates);
        m_histogramRevision = revision;
    }
    return true;
}

bool GpuEngine::createDisplay(QRhiRenderTarget *target) {
    if (m_display && m_displayPass == target->renderPassDescriptor() && m_displaySamples == target->sampleCount()) return true;
    m_display.reset(); m_displayBindings.reset();
    if (!m_sampler) {
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_sampler->create()) return fail(QStringLiteral("GPU sampler failed"));
    }
    if (!m_vertices) {
        m_vertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 6*4*int(sizeof(float))));
        if (!m_vertices->create()) return fail(QStringLiteral("GPU vertex allocation failed"));
    }
    m_uploadVertices = true;
    m_displayBindings.reset(m_rhi->newShaderResourceBindings());
    m_displayBindings->setBindings({QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_output.get(), m_sampler.get())});
    if (!m_displayBindings->create()) return fail(QStringLiteral("GPU display bindings failed"));
    m_display.reset(m_rhi->newGraphicsPipeline());
    m_display->setShaderStages({{QRhiShaderStage::Vertex, shader("display.vert")}, {QRhiShaderStage::Fragment, shader("display.frag")}});
    QRhiVertexInputLayout layout;
    layout.setBindings({{4*int(sizeof(float))}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 2*int(sizeof(float))}});
    m_display->setVertexInputLayout(layout);
    m_display->setShaderResourceBindings(m_displayBindings.get());
    m_display->setRenderPassDescriptor(target->renderPassDescriptor());
    m_display->setSampleCount(target->sampleCount());
    if (!m_display->create()) return fail(QStringLiteral("GPU display pipeline failed"));
    m_displayPass = target->renderPassDescriptor(); m_displaySamples = target->sampleCount();
    return true;
}
bool GpuEngine::draw(QRhiCommandBuffer *cb, QRhiRenderTarget *target) {
    if (!m_output || !createDisplay(target)) return false;
    QRhiResourceUpdateBatch *updates = nullptr;
    if (m_uploadVertices) {
        // Top-left data convention on every backend. QQuickRhiItem handles the
        // render-target texture's framebuffer orientation when compositing it.
        const float top = m_rhi->isYUpInNDC() ? 1.f : -1.f, bottom = -top;
        const float vertices[]{-1,top,0,0, -1,bottom,0,1, 1,top,1,0,
                                1,top,1,0, -1,bottom,0,1, 1,bottom,1,1};
        updates = m_rhi->nextResourceUpdateBatch();
        updates->uploadStaticBuffer(m_vertices.get(), vertices);
        m_uploadVertices = false;
    }
    cb->beginPass(target, Qt::black, {1.0f,0}, updates);
    cb->setGraphicsPipeline(m_display.get());
    cb->setShaderResources(m_displayBindings.get());
    cb->setViewport({0,0,float(target->pixelSize().width()),float(target->pixelSize().height())});
    const QRhiCommandBuffer::VertexInput binding(m_vertices.get(), 0);
    cb->setVertexInput(0, 1, &binding);
    cb->draw(6);
    cb->endPass();
    return true;
}

bool GpuEngine::ensureDetail() {
    if(m_detailPipeline)return true;
    m_detailBase.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
    m_horizontal.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
    if(!m_detailBase->create()||!m_horizontal->create())return fail("Look detail allocation failed");
    using B=QRhiShaderResourceBinding;
    m_detailBaseBindings.reset(m_rhi->newShaderResourceBindings());
    m_detailBaseBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),
        B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageStore(2,B::ComputeStage,m_detailBase.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
    m_horizontalBindings.reset(m_rhi->newShaderResourceBindings());
    m_horizontalBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),
        B::imageLoad(1,B::ComputeStage,m_detailBase.get(),0),B::imageStore(2,B::ComputeStage,m_horizontal.get(),0)});
    m_detailBindings.reset(m_rhi->newShaderResourceBindings());
    m_detailBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),
        B::imageLoad(1,B::ComputeStage,m_detailBase.get(),0),B::imageLoad(2,B::ComputeStage,m_horizontal.get(),0),B::imageStore(3,B::ComputeStage,m_output.get(),0)});
    if(!m_detailBaseBindings->create()||!m_horizontalBindings->create()||!m_detailBindings->create())return fail("Look detail bindings failed");
    PerformanceRecorder::value("look_detail_extra_gpu_bytes",qint64(m_size.width())*m_size.height()*32);
    return buildCompute(m_horizontalPipeline,m_horizontalBindings.get(),"look_horizontal.comp")
        &&buildCompute(m_detailPipeline,m_detailBindings.get(),"look_detail.comp");
}
