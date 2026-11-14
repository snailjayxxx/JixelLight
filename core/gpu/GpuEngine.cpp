#include "core/gpu/GpuEngine.h"
#include "core/color/MonitorColorTransform.h"
#include "core/scopes/ScopesEngine.h"
#include "core/pipeline/FloatFrameFingerprint.h"
#include "core/pipeline/SpatialClarity.h"
#include "core/pipeline/SpatialTexture.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QFile>
#include <QResource>
#include <algorithm>
#include <cstring>

static void initializeShaderResources() { Q_INIT_RESOURCE(pipeline_shaders); }
namespace {
QShader shader(const QString &name) {
    QFile file(QStringLiteral(":/jixellight/") + name + QStringLiteral(".qsb"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QShader::fromSerialized(file.readAll());
}
constexpr int CountsBytes = HistogramCounts::GpuWords * int(sizeof(quint32));
constexpr int DisplayLutSize = 33;
constexpr QSize displayLutAtlasSize() { return QSize(DisplayLutSize * DisplayLutSize, DisplayLutSize); }

bool requiresNumericCpuFallback(QRhi::Implementation backend, const ProcessingPlan &plan,const ImagePipeline::FloatSourceFacts &facts) {
    // Tight Point Color shoulders amplify observed FP32 Lab differences above
    // the unchanged40/65535 bound. Keep the complete CPU reference in this region.
    if(plan.colorMaskNeedsCpuPrecision())return true;
    // CI389 WARP: a broad +3EV color selection reaches42/65535 at an
    // Adobe RGB dark gamut edge. Preserve40 with a common complete reference.
    if(plan.colorMaskActive()&&plan.output==ColorManagement::OutputSpace::AdobeRgb)return true;
    if(plan.pointColorNeedsCpuPrecision())return true;
    // Preserve the CPU guide's tiny/subnormal-alpha semantics. The1e-18 floor
    // also stays safely above its1e-20 normalization epsilon.
    if(plan.spatialInputActive()&&facts.nearZeroAlpha)return true;
    // Signed scene channels expose >40 at a ProPhoto gamut boundary even with
    // denoise off. Keep the oracle and exact alpha rather than widening RGB.
    if(plan.encoding==ImagePipeline::InputEncoding::LinearProPhoto
        &&plan.output==ColorManagement::OutputSpace::ProPhotoRgb&&facts.negativeRgb)return true;
    // Extended FP32 grain fixtures expose >40/65535 at an Adobe RGB gamut
    // edge. Keep the established color math and tolerance; use the exact
    // reference for this optional output combination on every backend.
    if (plan.data[ProcessingPlan::Grain].x>0 && plan.output==ColorManagement::OutputSpace::AdobeRgb) return true;
    // All QRhi backends require the established CPU numeric upload while the
    // M4.4A brush is a research-only CPU kernel. No silent GPU omission.
    if (BrushMaskTileRaster::anyEnabled(plan.state.brushMasks)) return true;
    // Metal stays inside the original 40/65535 bound. D3D11 and OpenGL have
    // a few backend-specific ULP divergences that sRGB encoding amplifies near
    // an Oklab/HSL gamut edge, so only those parameter regions use the CPU
    // reference instead of weakening the acceptance threshold.
    if (backend != QRhi::D3D11 && backend != QRhi::OpenGLES2) return false;
    const auto &state = plan.state;
    bool complexHsl = state.hue != 0;
    for (int i=0; i<8; ++i)
        complexHsl = complexHsl || state.hslHue[i] != 0 || state.hslSaturation[i] != 0 || state.hslLuminance[i] != 0;
    const bool extremeBrightPerceptual =
        plan.encoding == ImagePipeline::InputEncoding::LinearProPhoto && state.exposure+std::max(0.0f,plan.data[ProcessingPlan::Vignette].x)
            +std::max(0.0f,plan.data[ProcessingPlan::LinearMask].x)
            +std::max(0.0f,plan.data[ProcessingPlan::RadialMask].x)
            +std::max(0.0f,plan.data[ProcessingPlan::LuminanceMask].x)
            +std::max(0.0f,plan.data[ProcessingPlan::ColorMask].x) >= 2.5
        && (state.saturation != 0 || state.vibrance != 0);
    return complexHsl || extremeBrightPerceptual;
}
}
GpuEngine::GpuEngine(QRhi *rhi) : m_rhi(rhi) {
    initializeShaderResources();
    m_displayColorLut=MonitorColorTransform::identityLut(DisplayLutSize);
    m_displayColorLutKey=QStringLiteral("identity-srgb");
    m_displayColorProfileName=QStringLiteral("sRGB identity fallback");
}
GpuEngine::~GpuEngine() {
    // No frame-by-frame finish/wait. The only wait is teardown while a callback
    // still references a readback object, so it cannot outlive its QRhi owner.
    if (hasPendingReadback()) m_rhi->finish();
}
void GpuEngine::setDisplayColorLut(const QImage &atlas, const QString &key, const QString &profileName) {
    if (atlas.isNull()) return;
    if (atlas.size()!=displayLutAtlasSize() || atlas.format()!=QImage::Format_RGBA32FPx4) {
        PerformanceRecorder::value("display_monitor_icc_error",QStringLiteral("Rejected invalid display LUT atlas"));
        return;
    }
    const QString normalizedKey=key.isEmpty()?QStringLiteral("identity-srgb"):key;
    if (normalizedKey==m_displayColorLutKey) return;
    m_displayColorLut=atlas;
    m_displayColorLutKey=normalizedKey;
    m_displayColorProfileName=profileName;
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
bool GpuEngine::hasPendingReadback() const {
    return (m_readback && !m_readback->done) || (m_frameReadback && !m_frameReadback->done);
}
bool GpuEngine::captureWorkingFrame(QRhiCommandBuffer *cb, quint64 revision, const FrameReady &ready,
                                   QString *error, qint64 budget) {
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    if (!cb || !ready || !m_output || !m_revision || revision != m_revision)
        return reject(QStringLiteral("Requested GPU revision is not rendered"));
    if (m_frameReadback && !m_frameReadback->done)
        return reject(QStringLiteral("A diagnostic readback is already pending"));
    const qint64 bytes = qint64(m_size.width()) * m_size.height() * 16;
    const qint64 limit = std::min(budget, FloatFrameFingerprint::BudgetBytes);
    if (budget < 0 || bytes > limit)
        return reject(QStringLiteral("Diagnostic frame exceeds readback budget"));
    if (error) error->clear();
    // Reserve the existing final-output capture first. A second texture readback
    // is optional and shares this request's unchanged aggregate budget.
    const bool captureSource = m_source && bytes <= limit - bytes;
    const int count = captureSource ? 2 : 1;
    m_frameReadback = std::make_shared<FrameReadbackState>();
    m_frameReadback->remaining = count;
    const std::weak_ptr<FrameReadbackState> weak = m_frameReadback;
    const auto size = m_size;
    const QJsonObject context{{"parameter_revision", qint64(revision)},
        {"source_cache_key", QString::number(m_sourceKey)}, {"backend", backendName()},
        {"output_space", ColorManagement::key(m_outputSpace)}, {"encoding", "encoded RGB"},
        {"producer", m_lastCpuFallback ? "cpu-reference-upload" : "gpu-compute"},
        {"color_mask",m_outputColorMaskActive?(m_lastCpuFallback?"cpu-reference":"gpu-compute"):"inactive"},
        {"luminance_mask",m_outputLuminanceMaskActive?(m_lastCpuFallback?"cpu-reference":"gpu-compute"):"inactive"},
        {"point_color",m_outputPointColorActive?(m_lastCpuFallback?"cpu-reference":"gpu-compute"):"inactive"},
        {"spatial_dehaze",m_outputDehazeActive?(m_lastCpuFallback?"cpu-reference":"gpu-compute"):"inactive"},
        {"spatial_texture",m_outputTextureActive?(m_lastCpuFallback?"cpu-reference":"gpu-compute"):"inactive"},
        {"spatial_clarity",m_outputClarityActive?(m_lastCpuFallback?"cpu-reference":"gpu-compute"):"inactive"},
        {"spatial_denoise",m_outputDenoiseActive?(m_lastCpuFallback?"cpu-reference":"gpu-compute"):"inactive"},
        {"numeric_source_policy",m_outputNumericPolicy},
        {"stage", "working-output"}, {"monitor_icc", false}};
    const QJsonObject sourceContext{{"parameter_revision",qint64(revision)},
        {"source_cache_key",QString::number(m_sourceKey)}, {"backend",backendName()},
        {"stage","prepared-source-upload"}, {"producer","gpu-texture-upload"},
        {"encoding",m_inputEncoding==ImagePipeline::InputEncoding::LinearProPhoto?"linear-prophoto":"encoded-srgb"},
        {"raw_source",m_inputRawSource}, {"base_exposure_stops",m_inputBaseExposureStops},
        {"processing_frame_rect",QJsonArray{m_inputFrameRect.x,m_inputFrameRect.y,m_inputFrameRect.z,m_inputFrameRect.w}},
        {"alpha","straight"}, {"monitor_icc",false}, {"used_for_compute",!m_lastCpuFallback},
        {"note","Prepared preview upload; not native sensor float or an internal colour/spatial boundary"}};
    if (!captureSource) {
        auto unavailable = sourceContext;
        unavailable.insert("available",false);
        unavailable.insert("reason",m_source?"budget":"source-unavailable");
        unavailable.insert("required_bytes",bytes);
        unavailable.insert("error",m_source?"Source capture exceeds remaining aggregate readback budget":"Prepared source texture is unavailable");
        m_frameReadback->fingerprints[1] = std::move(unavailable);
    }
    const std::array<QJsonObject,2> contexts{context,sourceContext};
    for (int index=0;index<count;++index) {
        m_frameReadback->results[index].completed = [weak,ready,size,bytes,limit,count,index,context=contexts[index]] {
            if (const auto state = weak.lock()) {
                auto &readback = state->results[index];
                QJsonObject result;
                if (readback.format != QRhiTexture::RGBA32F || readback.pixelSize != size
                    || readback.data.size() != bytes) {
                    result = {{"available",false},{"error","Invalid RGBA32F texture readback"}};
                } else {
                    const QImage frame(reinterpret_cast<const uchar *>(readback.data.constData()),
                                       size.width(),size.height(),size.width()*16,QImage::Format_RGBA32FPx4);
                    result = FloatFrameFingerprint::capture(frame);
                }
                for (auto it=context.begin();it!=context.end();++it) result.insert(it.key(),it.value());
                state->fingerprints[index] = std::move(result);
                state->receivedBytes += readback.data.size();
                PerformanceRecorder::count("diagnostic_gpu_readback_bytes",readback.data.size());
                // Each result owns its storage until completion. Keep only small
                // fingerprints and notify once, after both requested reads settle.
                readback.data.clear();
                if (--state->remaining == 0) {
                    state->done = true;
                    auto output = std::move(state->fingerprints[0]);
                    output.insert("source_staging",std::move(state->fingerprints[1]));
                    output.insert("readback_budget_bytes",limit);
                    output.insert("readback_requested_bytes",bytes*count);
                    output.insert("readback_received_bytes",state->receivedBytes);
                    ready(std::move(output));
                }
            }
        };
    }
    auto *updates = m_rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(m_output.get()),&m_frameReadback->results[0]);
    if (captureSource)
        updates->readBackTexture(QRhiReadbackDescription(m_source.get()),&m_frameReadback->results[1]);
    cb->resourceUpdate(updates);
    return true;
}
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
    m_frameReadback.reset();
    m_display.reset(); m_displayBindings.reset();
    m_pipeline.reset(); m_histogram.reset(); m_reduce.reset();
    m_pipelineBindings.reset(); m_histogramBindings.reset(); m_reduceBindings.reset();
    m_detailBaseBindings.reset();m_horizontalBindings.reset();m_detailBindings.reset();
    m_horizontalPipeline.reset();m_detailPipeline.reset();m_detailBase.reset();m_horizontal.reset();
    m_denoisePreparePipeline.reset();m_denoiseFilterPipeline.reset();
    m_denoisePrepareBindings.reset();m_denoiseFilterBindings.reset();m_denoiseColorBindings.reset();m_denoiseDetailBindings.reset();
    m_denoiseGuide.reset();m_denoiseLinear.reset();
    m_denoiseGuideValid=false;m_denoiseFilterValid=false;
    m_dehazePreparePipeline.reset();m_dehazeHorizontalPipeline.reset();m_dehazeVerticalPipeline.reset();m_dehazeFilterPipeline.reset();
    m_dehazePrepareBindings.reset();m_dehazePrepareDenoisedBindings.reset();m_dehazeHorizontalBindings.reset();m_dehazeVerticalBindings.reset();
    m_dehazeFilterBindings.reset();m_dehazeFilterDenoisedBindings.reset();m_dehazeColorBindings.reset();m_dehazeDetailBindings.reset();
    m_dehazeGuide.reset();m_dehazeHorizontal.reset();m_dehazeCoarse.reset();m_dehazeLinear.reset();m_dehazeKernel.reset();
    m_dehazeIdentityValid=false;m_dehazeGuideValid=false;m_dehazeCoarseValid=false;m_dehazeOutputValid=false;
    m_texturePrepareDehazedBindings.reset();m_textureFilterDehazedBindings.reset();
    m_clarityPrepareDehazedBindings.reset();m_clarityFilterDehazedBindings.reset();
    m_texturePreparePipeline.reset();m_textureSmallHorizontalPipeline.reset();m_textureSmallVerticalPipeline.reset();
    m_textureLargeHorizontalPipeline.reset();m_textureFilterPipeline.reset();
    m_texturePrepareBindings.reset();m_texturePrepareDenoisedBindings.reset();m_textureHorizontalBindings.reset();
    m_textureSmallVerticalBindings.reset();m_textureFilterBindings.reset();m_textureFilterDenoisedBindings.reset();
    m_textureColorBindings.reset();m_textureDetailBindings.reset();m_textureKernel.reset();
    m_textureGuide.reset();m_textureHorizontal.reset();m_textureSmallBand.reset();m_textureLinear.reset();
    m_textureGuideValid=false;m_textureNeighborhoodValid=false;m_textureOutputValid=false;
    m_clarityPreparePipeline.reset();m_clarityHorizontalPipeline.reset();m_clarityFilterPipeline.reset();
    m_clarityPrepareBindings.reset();m_clarityPrepareDenoisedBindings.reset();m_clarityPrepareTexturedBindings.reset();m_clarityHorizontalBindings.reset();
    m_clarityFilterBindings.reset();m_clarityFilterDenoisedBindings.reset();m_clarityFilterTexturedBindings.reset();m_clarityColorBindings.reset();m_clarityDetailBindings.reset();
    m_clarityGuide.reset();m_clarityHorizontal.reset();m_clarityLinear.reset();m_clarityKernel.reset();
    m_clarityGuideValid=false;m_clarityHorizontalValid=false;m_clarityOutputValid=false;
    m_source.reset(); m_output.reset(); m_partial.reset(); m_counts.reset();
    m_size = size;
    m_groups = (size.width()*size.height()+16383)/16384;
    m_sourceKey = 0; m_revision = 0; m_histogramRevision = 0;
    const auto textureFlags = QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource;
    m_source.reset(m_rhi->newTexture(QRhiTexture::RGBA32F, size, 1, textureFlags));
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
    PerformanceRecorder::value("gpu_estimated_bytes", qint64(size.width())*size.height()*32 + m_groups*CountsBytes + CountsBytes
                               + qint64(DisplayLutSize)*DisplayLutSize*DisplayLutSize*16);
    return true;
}

bool GpuEngine::process(QRhiCommandBuffer *cb, const QImage &source, ProcessingPlan plan,
                        quint64 revision, const HistogramReady &histogramReady, bool forceHistogram) {
    return processImpl(cb,source,std::move(plan),revision,histogramReady,forceHistogram,nullptr);
}
bool GpuEngine::processAdmittedBrush(QRhiCommandBuffer *cb,const QImage &source,ProcessingPlan plan,
    quint64 revision,const HistogramReady &histogramReady,bool forceHistogram,QString *error,
    std::shared_ptr<BrushRenderBudget> budget) {
    if(error)error->clear();
    const auto refuse=[&](const QString &message){if(error)*error=message;return false;};
    if(!m_error.isEmpty())return refuse(m_error);
    if(!cb||source.isNull()||source.format()!=QImage::Format_RGBA32FPx4)
        return refuse("Admitted GPU brush requires a command buffer and prepared FP32 source");
    if(source.width()>4096||source.height()>4096
        ||std::max(source.width(),source.height())>m_rhi->resourceLimit(QRhi::TextureSizeMax))
        return refuse("Prepared brush preview exceeds GPU texture budget");
    auto request=BrushRenderRequest::createPrepared(source,plan,BrushRenderRequest::Layout::Whole,{},error,std::move(budget));
    if(!request)return false;
    if(!processImpl(cb,source,std::move(plan),revision,histogramReady,forceHistogram,request.get()))
        return refuse(m_error);
    return true;
}
bool GpuEngine::processImpl(QRhiCommandBuffer *cb,const QImage &source,ProcessingPlan plan,
    quint64 revision,const HistogramReady &histogramReady,bool forceHistogram,BrushRenderRequest *admitted) {
    if (!m_error.isEmpty()) return false;
    if (source.isNull() || source.format() != QImage::Format_RGBA32FPx4) return fail(QStringLiteral("GPU source must be linear RGBA32FPx4"));
    if (!BrushMaskDocument::validDocument(plan.state.brushMasks))
        return fail(QStringLiteral("Invalid research brush document"));
    if(source.width()>4096||source.height()>4096
        ||std::max(source.width(),source.height())>m_rhi->resourceLimit(QRhi::TextureSizeMax))
        return fail(QStringLiteral("Preview exceeds GPU texture budget"));
    if(!plan.validSpatialSettings())return fail(QStringLiteral("Invalid spatial settings"));
    const bool newSource=source.cacheKey()!=m_sourceKey;
    // Validate candidate input before a size change releases accepted resources.
    // Rejected source/LUT facts never replace the prior drawable or its identity.
    auto sourceFacts=m_sourceFacts;
    if(newSource&&!ImagePipeline::validFloatSource(source,{},&sourceFacts))
        return fail(QStringLiteral("Invalid non-finite/out-of-range FP32 source or alpha"));
    if(!ImagePipeline::resolveDehazeAirlight(source,plan))return fail(QStringLiteral("Invalid full-frame dehaze context"));
    const auto lut=plan.data[ProcessingPlan::LookStyle].w>0?plan.state.look.lut:nullptr;
    const bool newLut=lut&&lut->digest!=m_lutKey;
    const QImage atlas=newLut?lut->atlas():QImage{};
    if(newLut&&atlas.isNull())return fail(QStringLiteral("Invalid LUT atlas"));
    if (!initialize(source.size())) return false;
    const bool cpuFallback = requiresNumericCpuFallback(m_rhi->backend(), plan,sourceFacts);
    const bool details=plan.data[ProcessingPlan::LookDetail].x>0||plan.data[ProcessingPlan::LookDetail].y>0||plan.data[ProcessingPlan::IndependentDetail].x>0;
    if(details&&!cpuFallback&&!ensureDetail())return false;
    const bool denoise=plan.denoiseActive()&&!cpuFallback;
    if(denoise&&!ensureDenoise())return false;
    const bool dehaze=plan.dehazeActive()&&!cpuFallback;
    if(dehaze&&!ensureDehaze())return false;
    const bool texture=plan.textureActive()&&!cpuFallback;
    if(texture&&!ensureTexture())return false;
    const bool clarity=plan.clarityActive()&&!cpuFallback;
    if(clarity&&!ensureClarity())return false;
    const auto denoiseSettings=plan.denoise(m_size);
    const bool denoiseGuideMiss=denoise&&(!m_denoiseGuideValid||m_denoiseSourceKey!=source.cacheKey()
        ||m_denoiseEncoding!=int(plan.encoding)
        ||std::memcmp(m_denoiseInputRows.data(),plan.data.data()+ProcessingPlan::Input0,sizeof(m_denoiseInputRows))!=0);
    const bool denoiseFilterMiss=denoise&&(denoiseGuideMiss||!m_denoiseFilterValid
        ||std::memcmp(&m_denoiseSettings,&denoiseSettings,sizeof(Float4))!=0);
    const auto dehazeSettings=plan.dehaze(m_size),airlight=plan.data[ProcessingPlan::DehazeAirlight];
    const bool dehazeInputMiss=dehaze&&(!m_dehazeIdentityValid||m_dehazeSourceKey!=source.cacheKey()
        ||m_dehazeEncoding!=int(plan.encoding)
        ||std::memcmp(m_dehazeInputRows.data(),plan.data.data()+ProcessingPlan::Input0,sizeof(m_dehazeInputRows))!=0
        ||std::memcmp(&m_dehazeDenoiseSettings,&denoiseSettings,sizeof(Float4))!=0
        ||std::memcmp(&m_dehazeAirlight,&airlight,sizeof(Float4))!=0);
    const bool positiveDehaze=dehaze&&dehazeSettings.x>0;
    const bool dehazeGuideMiss=positiveDehaze&&(dehazeInputMiss||!m_dehazeGuideValid);
    const bool dehazeCoarseMiss=positiveDehaze&&(dehazeGuideMiss||!m_dehazeCoarseValid||m_dehazeDarkRadius!=dehazeSettings.y);
    const bool dehazeFilterMiss=dehaze&&(dehazeInputMiss||dehazeCoarseMiss||!m_dehazeOutputValid
        ||std::memcmp(&m_dehazeSettings,&dehazeSettings,sizeof(Float4))!=0);
    const auto textureSettings=plan.texture(m_size);
    const bool textureGuideMiss=texture&&(!m_textureGuideValid||m_textureSourceKey!=source.cacheKey()
        ||m_textureEncoding!=int(plan.encoding)
        ||std::memcmp(m_textureInputRows.data(),plan.data.data()+ProcessingPlan::Input0,sizeof(m_textureInputRows))!=0
        ||std::memcmp(&m_textureDenoiseSettings,&denoiseSettings,sizeof(Float4))!=0
        ||std::memcmp(&m_textureDehazeSettings,&dehazeSettings,sizeof(Float4))!=0
        ||std::memcmp(&m_textureDehazeAirlight,&airlight,sizeof(Float4))!=0);
    const bool textureNeighborhoodMiss=texture&&(textureGuideMiss||!m_textureNeighborhoodValid
        ||m_textureSmallRadius!=textureSettings.y||m_textureLargeRadius!=textureSettings.z);
    const bool textureFilterMiss=texture&&(textureNeighborhoodMiss||!m_textureOutputValid
        ||std::memcmp(&m_textureSettings,&textureSettings,sizeof(Float4))!=0);
    const auto claritySettings=plan.clarity(m_size);
    const bool clarityGuideMiss=clarity&&(!m_clarityGuideValid||m_claritySourceKey!=source.cacheKey()
        ||m_clarityEncoding!=int(plan.encoding)
        ||std::memcmp(m_clarityInputRows.data(),plan.data.data()+ProcessingPlan::Input0,sizeof(m_clarityInputRows))!=0
        ||std::memcmp(&m_clarityDenoiseSettings,&denoiseSettings,sizeof(Float4))!=0
        ||std::memcmp(&m_clarityDehazeSettings,&dehazeSettings,sizeof(Float4))!=0
        ||std::memcmp(&m_clarityDehazeAirlight,&airlight,sizeof(Float4))!=0
        ||std::memcmp(&m_clarityTextureSettings,&textureSettings,sizeof(Float4))!=0);
    const bool clarityHorizontalMiss=clarity&&(clarityGuideMiss||!m_clarityHorizontalValid||m_clarityHorizontalRadius!=claritySettings.y);
    const bool clarityFilterMiss=clarity&&(clarityHorizontalMiss||!m_clarityOutputValid
        ||std::memcmp(&m_claritySettings,&claritySettings,sizeof(Float4))!=0);
    if (revision != m_revision || newSource || newLut) {
        PerformanceSpan timing("gpu_submit_cpu_ms", {{"pixels", qint64(source.width())*source.height()}});
        auto *updates = m_rhi->nextResourceUpdateBatch();
        if (newSource) {
            QRhiTextureSubresourceUploadDescription upload(source.constBits(), quint32(source.sizeInBytes()));
            upload.setSourceSize(source.size());
            upload.setDataStride(quint32(source.bytesPerLine()));
            updates->uploadTexture(m_source.get(), {{0, 0, upload}});
        }
        if(newLut) {
            QRhiTextureSubresourceUploadDescription upload(atlas.constBits(),quint32(atlas.sizeInBytes()));
            upload.setSourceSize(atlas.size());upload.setDataStride(quint32(atlas.bytesPerLine()));
            updates->uploadTexture(m_lutTexture.get(),{{0,0,upload}});
        }
        plan.data[ProcessingPlan::Dimensions] = {float(m_size.width()), float(m_size.height()), float(m_groups), 0};
        plan.data[ProcessingPlan::PixelMap] = plan.pixelMap(m_size);
        auto uniforms=plan.data;uniforms[ProcessingPlan::IndependentDetail]=plan.independentDetail(m_size);
        uniforms[ProcessingPlan::Denoise]=plan.denoise(m_size);
        uniforms[ProcessingPlan::Texture]=textureSettings;uniforms[ProcessingPlan::Texture].w=denoise||dehaze?1.f:0.f;
        uniforms[ProcessingPlan::Clarity]=claritySettings;uniforms[ProcessingPlan::Clarity].z=denoise||dehaze||texture?1.f:0.f;
        uniforms[ProcessingPlan::Dimensions].w=denoise||dehaze||texture||clarity?1.f:0.f;
        uniforms[ProcessingPlan::Dehaze]=dehazeSettings;uniforms[ProcessingPlan::Dehaze].w=denoise?1.f:0.f;
        if(positiveDehaze&&dehazeFilterMiss) {
            const auto packed=SpatialDehaze::refinementWeights(dehazeSettings.z);
            updates->updateDynamicBuffer(m_dehazeKernel.get(),0,quint32(sizeof(packed)),packed.data());
        }
        if(textureNeighborhoodMiss) {
            std::array<float,24> packed{};
            const auto small=SpatialTexture::kernelWeights(textureSettings.y),large=SpatialTexture::kernelWeights(textureSettings.z);
            std::copy_n(small.begin(),5,packed.begin());std::copy(large.begin(),large.end(),packed.begin()+8);
            updates->updateDynamicBuffer(m_textureKernel.get(),0,quint32(sizeof(packed)),packed.data());
        }
        if(clarityHorizontalMiss) {
            std::array<float,52> packed{};const auto weights=SpatialClarity::kernelWeights(claritySettings.y);
            std::copy(weights.begin(),weights.end(),packed.begin());
            updates->updateDynamicBuffer(m_clarityKernel.get(),0,quint32(sizeof(packed)),packed.data());
        }
        updates->updateDynamicBuffer(m_uniform.get(), 0, quint32(sizeof(uniforms)), uniforms.data());
        if (cpuFallback) {
            // Render from the exact GPU upload source. An RGBA64 conversion
            // would truncate HDR scene channels (>1) before the safety fallback.
            BrushImageLease admittedPixels;
            if(admitted)admittedPixels=admitted->render();
            const QImage cpuResult = admitted ? admittedPixels.image() : ImagePipeline::processWithPlan(source, plan);
            if (cpuResult.isNull()) { updates->release(); return fail(QStringLiteral("CPU numeric safety fallback failed")); }
            auto uploadFrame=ImagePipeline::floatSource(cpuResult);
            if(admitted)uploadFrame=admitted->retainFloatUpload(std::move(uploadFrame));
            if(uploadFrame.isNull()){updates->release();return fail("CPU fallback upload allocation failed");}
            m_cpuFallbackFrame=std::move(uploadFrame);
            QRhiTextureSubresourceUploadDescription upload(m_cpuFallbackFrame.constBits(), quint32(m_cpuFallbackFrame.sizeInBytes()));
            upload.setSourceSize(m_cpuFallbackFrame.size());
            upload.setDataStride(quint32(m_cpuFallbackFrame.bytesPerLine()));
            updates->uploadTexture(m_output.get(), {{0, 0, upload}});
            cb->resourceUpdate(updates);
            if(admitted)PerformanceRecorder::count("brush_admitted_gpu_uploads");
            PerformanceRecorder::count("gpu_numeric_cpu_fallbacks");
            PerformanceRecorder::value("gpu_last_execution",
                admitted?QStringLiteral("M4.5B admitted brush CPU numeric -> GPU display/scopes"):
                BrushMaskTileRaster::anyEnabled(plan.state.brushMasks)
                    ? QStringLiteral("M4.4A research brush CPU numeric -> GPU display/scopes")
                    : QStringLiteral("CPU numeric safety fallback -> GPU display/scopes"));
        } else {
            m_cpuFallbackFrame={};
            cb->beginComputePass(updates);
            if(denoiseGuideMiss) {
                cb->setComputePipeline(m_denoisePreparePipeline.get());cb->setShaderResources(m_denoisePrepareBindings.get());
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();cb->beginComputePass();
                ++m_denoiseGuideDispatches;PerformanceRecorder::count("gpu_spatial_denoise_guide_dispatches");
                m_denoiseSourceKey=source.cacheKey();m_denoiseEncoding=int(plan.encoding);
                std::copy_n(plan.data.data()+ProcessingPlan::Input0,3,m_denoiseInputRows.data());m_denoiseGuideValid=true;
            } else if(denoise)PerformanceRecorder::count("gpu_spatial_denoise_guide_hits");
            if(denoiseFilterMiss) {
                cb->setComputePipeline(m_denoiseFilterPipeline.get());cb->setShaderResources(m_denoiseFilterBindings.get());
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();cb->beginComputePass();
                ++m_denoiseFilterDispatches;PerformanceRecorder::count("gpu_spatial_denoise_filter_dispatches");
                m_denoiseSettings=denoiseSettings;m_denoiseFilterValid=true;
            } else if(denoise)PerformanceRecorder::count("gpu_spatial_denoise_filter_hits");
            const auto dehazeDispatch=[&](QRhiComputePipeline *pipeline,QRhiShaderResourceBindings *bindings,quint64 &counter,const char *name) {
                cb->setComputePipeline(pipeline);cb->setShaderResources(bindings);
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();cb->beginComputePass();
                ++counter;PerformanceRecorder::count(name);
            };
            if(dehazeInputMiss){m_dehazeGuideValid=false;m_dehazeCoarseValid=false;}
            if(dehazeGuideMiss) {
                dehazeDispatch(m_dehazePreparePipeline.get(),denoise?m_dehazePrepareDenoisedBindings.get():m_dehazePrepareBindings.get(),m_dehazeGuideDispatches,"gpu_spatial_dehaze_guide_dispatches");
                m_dehazeGuideValid=true;
            } else if(positiveDehaze)PerformanceRecorder::count("gpu_spatial_dehaze_guide_hits");
            if(dehazeCoarseMiss) {
                dehazeDispatch(m_dehazeHorizontalPipeline.get(),m_dehazeHorizontalBindings.get(),m_dehazeHorizontalDispatches,"gpu_spatial_dehaze_horizontal_dispatches");
                dehazeDispatch(m_dehazeVerticalPipeline.get(),m_dehazeVerticalBindings.get(),m_dehazeVerticalDispatches,"gpu_spatial_dehaze_vertical_dispatches");
                m_dehazeDarkRadius=dehazeSettings.y;m_dehazeCoarseValid=true;
            } else if(positiveDehaze)PerformanceRecorder::count("gpu_spatial_dehaze_coarse_hits");
            if(dehazeFilterMiss) {
                dehazeDispatch(m_dehazeFilterPipeline.get(),denoise?m_dehazeFilterDenoisedBindings.get():m_dehazeFilterBindings.get(),m_dehazeFilterDispatches,"gpu_spatial_dehaze_filter_dispatches");
                m_dehazeSourceKey=source.cacheKey();m_dehazeEncoding=int(plan.encoding);m_dehazeDenoiseSettings=denoiseSettings;m_dehazeAirlight=airlight;
                std::copy_n(plan.data.data()+ProcessingPlan::Input0,3,m_dehazeInputRows.data());
                m_dehazeSettings=dehazeSettings;m_dehazeIdentityValid=true;m_dehazeOutputValid=true;
            } else if(dehaze)PerformanceRecorder::count("gpu_spatial_dehaze_filter_hits");
            const auto textureDispatch=[&](QRhiComputePipeline *pipeline,QRhiShaderResourceBindings *bindings,quint64 &counter,const char *name) {
                cb->setComputePipeline(pipeline);cb->setShaderResources(bindings);
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();cb->beginComputePass();
                ++counter;PerformanceRecorder::count(name);
            };
            if(textureGuideMiss) {
                textureDispatch(m_texturePreparePipeline.get(),dehaze?m_texturePrepareDehazedBindings.get():denoise?m_texturePrepareDenoisedBindings.get():m_texturePrepareBindings.get(),m_textureGuideDispatches,"gpu_spatial_texture_guide_dispatches");
                m_textureSourceKey=source.cacheKey();m_textureEncoding=int(plan.encoding);m_textureDenoiseSettings=denoiseSettings;m_textureDehazeSettings=dehazeSettings;m_textureDehazeAirlight=airlight;
                std::copy_n(plan.data.data()+ProcessingPlan::Input0,3,m_textureInputRows.data());m_textureGuideValid=true;
            } else if(texture)PerformanceRecorder::count("gpu_spatial_texture_guide_hits");
            if(textureNeighborhoodMiss) {
                textureDispatch(m_textureSmallHorizontalPipeline.get(),m_textureHorizontalBindings.get(),m_textureSmallHorizontalDispatches,"gpu_spatial_texture_small_horizontal_dispatches");
                textureDispatch(m_textureSmallVerticalPipeline.get(),m_textureSmallVerticalBindings.get(),m_textureSmallVerticalDispatches,"gpu_spatial_texture_small_vertical_dispatches");
                textureDispatch(m_textureLargeHorizontalPipeline.get(),m_textureHorizontalBindings.get(),m_textureLargeHorizontalDispatches,"gpu_spatial_texture_large_horizontal_dispatches");
                m_textureSmallRadius=textureSettings.y;m_textureLargeRadius=textureSettings.z;m_textureNeighborhoodValid=true;
            } else if(texture)PerformanceRecorder::count("gpu_spatial_texture_neighborhood_hits");
            if(textureFilterMiss) {
                textureDispatch(m_textureFilterPipeline.get(),dehaze?m_textureFilterDehazedBindings.get():denoise?m_textureFilterDenoisedBindings.get():m_textureFilterBindings.get(),m_textureFilterDispatches,"gpu_spatial_texture_filter_dispatches");
                m_textureSettings=textureSettings;m_textureOutputValid=true;
            } else if(texture)PerformanceRecorder::count("gpu_spatial_texture_filter_hits");
            if(clarityGuideMiss) {
                cb->setComputePipeline(m_clarityPreparePipeline.get());
                cb->setShaderResources(texture?m_clarityPrepareTexturedBindings.get():dehaze?m_clarityPrepareDehazedBindings.get():denoise?m_clarityPrepareDenoisedBindings.get():m_clarityPrepareBindings.get());
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();cb->beginComputePass();
                ++m_clarityGuideDispatches;PerformanceRecorder::count("gpu_spatial_clarity_guide_dispatches");
                m_claritySourceKey=source.cacheKey();m_clarityEncoding=int(plan.encoding);m_clarityDenoiseSettings=denoiseSettings;m_clarityTextureSettings=textureSettings;m_clarityDehazeSettings=dehazeSettings;m_clarityDehazeAirlight=airlight;
                std::copy_n(plan.data.data()+ProcessingPlan::Input0,3,m_clarityInputRows.data());m_clarityGuideValid=true;
            } else if(clarity)PerformanceRecorder::count("gpu_spatial_clarity_guide_hits");
            if(clarityHorizontalMiss) {
                cb->setComputePipeline(m_clarityHorizontalPipeline.get());cb->setShaderResources(m_clarityHorizontalBindings.get());
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();cb->beginComputePass();
                ++m_clarityHorizontalDispatches;PerformanceRecorder::count("gpu_spatial_clarity_horizontal_dispatches");
                m_clarityHorizontalRadius=claritySettings.y;m_clarityHorizontalValid=true;
            } else if(clarity)PerformanceRecorder::count("gpu_spatial_clarity_horizontal_hits");
            if(clarityFilterMiss) {
                cb->setComputePipeline(m_clarityFilterPipeline.get());
                cb->setShaderResources(texture?m_clarityFilterTexturedBindings.get():dehaze?m_clarityFilterDehazedBindings.get():denoise?m_clarityFilterDenoisedBindings.get():m_clarityFilterBindings.get());
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();cb->beginComputePass();
                ++m_clarityFilterDispatches;PerformanceRecorder::count("gpu_spatial_clarity_filter_dispatches");
                m_claritySettings=claritySettings;m_clarityOutputValid=true;
            } else if(clarity)PerformanceRecorder::count("gpu_spatial_clarity_filter_hits");
            cb->setComputePipeline(m_pipeline.get());
            cb->setShaderResources(clarity?(details?m_clarityDetailBindings.get():m_clarityColorBindings.get()):
                texture?(details?m_textureDetailBindings.get():m_textureColorBindings.get()):
                dehaze?(details?m_dehazeDetailBindings.get():m_dehazeColorBindings.get()):
                denoise?(details?m_denoiseDetailBindings.get():m_denoiseColorBindings.get()):(details?m_detailBaseBindings.get():m_pipelineBindings.get()));
            cb->dispatch((m_size.width()+15)/16, (m_size.height()+15)/16, 1);
            cb->endComputePass();
            if(details) {
                cb->beginComputePass();cb->setComputePipeline(m_horizontalPipeline.get());cb->setShaderResources(m_horizontalBindings.get());
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();
                cb->beginComputePass();cb->setComputePipeline(m_detailPipeline.get());cb->setShaderResources(m_detailBindings.get());
                cb->dispatch((m_size.width()+15)/16,(m_size.height()+15)/16,1);cb->endComputePass();
            }
            PerformanceRecorder::value("gpu_last_execution", backendName());
        }
        // These facts describe accepted GPU commands, never a rejected plan or
        // a released resource-update batch. Prior-frame diagnostics stay frozen.
        if(newSource) {
            m_sourceKey=source.cacheKey();m_sourceFacts=sourceFacts;++m_uploads;
            PerformanceRecorder::count("gpu_source_upload_bytes",source.sizeInBytes());
            PerformanceRecorder::count("gpu_source_uploads");
        }
        if(newLut){m_lutKey=lut->digest;PerformanceRecorder::count("look_lut_uploads");}
        m_lastCpuFallback=cpuFallback;
        m_revision = revision;
        m_outputDenoiseActive=plan.denoiseActive();m_outputClarityActive=plan.clarityActive();m_outputTextureActive=plan.textureActive();m_outputDehazeActive=plan.dehazeActive();m_outputPointColorActive=plan.pointColorActive();m_outputLuminanceMaskActive=plan.luminanceMaskActive();m_outputColorMaskActive=plan.colorMaskActive();
        m_outputNumericPolicy=plan.colorMaskNeedsCpuPrecision()?QStringLiteral("color-mask-narrow-range"):
            plan.pointColorNeedsCpuPrecision()?QStringLiteral("point-color-narrow-range"):
            m_sourceFacts.wideRgbSpan&&BrushMaskTileRaster::anyEnabled(plan.state.brushMasks)?QStringLiteral("wide-rgb-span"):
            plan.dehazeActive()&&m_sourceFacts.nearZeroAlpha?QStringLiteral("dehaze-near-zero-alpha"):
            plan.textureActive()&&m_sourceFacts.nearZeroAlpha?QStringLiteral("texture-near-zero-alpha"):
            plan.clarityActive()&&m_sourceFacts.nearZeroAlpha?QStringLiteral("clarity-near-zero-alpha"):
            plan.denoiseActive()&&m_sourceFacts.nearZeroAlpha?QStringLiteral("denoise-near-zero-alpha"):
            plan.encoding==ImagePipeline::InputEncoding::LinearProPhoto&&plan.output==ColorManagement::OutputSpace::ProPhotoRgb&&m_sourceFacts.negativeRgb?QStringLiteral("signed-linear-prophoto"):
            plan.colorMaskActive()&&plan.output==ColorManagement::OutputSpace::AdobeRgb&&plan.data[ProcessingPlan::Grain].x==0
                &&!BrushMaskTileRaster::anyEnabled(plan.state.brushMasks)?QStringLiteral("color-mask-adobe-boundary"):
            cpuFallback?QStringLiteral("existing-color-or-brush"):QStringLiteral("none");
        m_outputSpace = plan.output;
        m_inputEncoding=plan.encoding;m_inputRawSource=plan.rawSource;
        m_inputBaseExposureStops=plan.baseExposureStops;m_inputFrameRect=plan.data[ProcessingPlan::FrameRect];
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

bool GpuEngine::ensureDisplayColorResources() {
    if (!m_displayLutTexture) {
        m_displayLutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,displayLutAtlasSize(),1));
        if (!m_displayLutTexture->create()) return fail(QStringLiteral("Display ICC LUT texture allocation failed"));
        m_uploadedDisplayColorLutKey.clear();
    }
    if (!m_displayLutSampler) {
        m_displayLutSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                    QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_displayLutSampler->create()) return fail(QStringLiteral("Display ICC LUT sampler failed"));
    }
    return true;
}

bool GpuEngine::createDisplay(QRhiRenderTarget *target,bool premultipliedAlpha,QRhiTexture *input) {
    if(!input)return false;
    if (!ensureDisplayColorResources()) return false;
    if (m_display && m_displayPass == target->renderPassDescriptor() && m_displaySamples == target->sampleCount() && m_displayPremultipliedAlpha == premultipliedAlpha && m_displayTextureId==input->globalResourceId()) return true;
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
    m_displayBindings->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, input, m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_displayLutTexture.get(), m_displayLutSampler.get())
    });
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
    if(premultipliedAlpha) {
        // Presentation only: Qt Quick consumes premultiplied device RGB.
        // Keep processing/output texture and ordinary diagnostic draws straight.
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::Zero;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::Zero;
        m_display->setTargetBlends({blend});
    }
    if (!m_display->create()) return fail(QStringLiteral("GPU display pipeline failed"));
    m_displayPass = target->renderPassDescriptor(); m_displaySamples = target->sampleCount(); m_displayPremultipliedAlpha=premultipliedAlpha; m_displayTextureId=input->globalResourceId();
    return true;
}
bool GpuEngine::draw(QRhiCommandBuffer *cb, QRhiRenderTarget *target) {return drawImpl(cb,target,false);}
bool GpuEngine::drawForSceneGraph(QRhiCommandBuffer *cb, QRhiRenderTarget *target) {return drawImpl(cb,target,true);}
bool GpuEngine::drawExternalWorkingTexture(QRhiCommandBuffer *cb,QRhiRenderTarget *target,QRhiTexture *input) {return drawImpl(cb,target,true,input);}
NativeBrush::ShaderBytes GpuEngine::nativeBrushShaders() const {
    const auto bytes=[](QString name){QFile f(QStringLiteral(":/jixellight/")+name+QStringLiteral(".qsb"));return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};};
    return {bytes("brush_mask_coverage.comp"),bytes("brush_pipeline.comp"),bytes("histogram.comp"),bytes("histogram_reduce.comp")};
}
bool GpuEngine::drawImpl(QRhiCommandBuffer *cb, QRhiRenderTarget *target,bool premultipliedAlpha,QRhiTexture *external) {
    auto *input=external?external:m_output.get();
    if (!input || !createDisplay(target,premultipliedAlpha,input)) return false;
    QRhiResourceUpdateBatch *updates = nullptr;
    const auto ensureUpdates=[&]() {
        if (!updates) updates=m_rhi->nextResourceUpdateBatch();
        return updates;
    };
    if (m_uploadVertices) {
        // Top-left data convention on every backend. QQuickRhiItem handles the
        // render-target texture's framebuffer orientation when compositing it.
        const float top = m_rhi->isYUpInNDC() ? 1.f : -1.f, bottom = -top;
        const float vertices[]{-1,top,0,0, -1,bottom,0,1, 1,top,1,0,
                                1,top,1,0, -1,bottom,0,1, 1,bottom,1,1};
        ensureUpdates()->uploadStaticBuffer(m_vertices.get(), vertices);
        m_uploadVertices = false;
    }
    if (m_uploadedDisplayColorLutKey!=m_displayColorLutKey) {
        if (m_displayColorLut.isNull() || m_displayColorLut.size()!=displayLutAtlasSize()
            || m_displayColorLut.format()!=QImage::Format_RGBA32FPx4)
            return fail(QStringLiteral("Display ICC LUT is missing or invalid"));
        QRhiTextureSubresourceUploadDescription upload(m_displayColorLut.constBits(),quint32(m_displayColorLut.sizeInBytes()));
        upload.setSourceSize(m_displayColorLut.size());
        upload.setDataStride(quint32(m_displayColorLut.bytesPerLine()));
        ensureUpdates()->uploadTexture(m_displayLutTexture.get(),{{0,0,upload}});
        m_uploadedDisplayColorLutKey=m_displayColorLutKey;
        PerformanceRecorder::count("display_monitor_lut_uploads");
        PerformanceRecorder::count("display_monitor_lut_upload_bytes",m_displayColorLut.sizeInBytes());
        PerformanceRecorder::value("display_monitor_profile_gpu",m_displayColorProfileName);
    }
    cb->beginPass(target, premultipliedAlpha?Qt::transparent:Qt::black, {1.0f,0}, updates);
    cb->setGraphicsPipeline(m_display.get());
    cb->setShaderResources(m_displayBindings.get());
    cb->setViewport({0,0,float(target->pixelSize().width()),float(target->pixelSize().height())});
    const QRhiCommandBuffer::VertexInput binding(m_vertices.get(), 0);
    cb->setVertexInput(0, 1, &binding);
    cb->draw(6);
    cb->endPass();
    return true;
}

bool GpuEngine::ensureDenoise() {
    using B=QRhiShaderResourceBinding;
    if(!m_denoisePreparePipeline) {
        m_denoiseGuide.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        m_denoiseLinear.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        if(!m_denoiseGuide->create()||!m_denoiseLinear->create())return fail("Denoise allocation failed");
        m_denoisePrepareBindings.reset(m_rhi->newShaderResourceBindings());
        m_denoisePrepareBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageStore(2,B::ComputeStage,m_denoiseGuide.get(),0)});
        m_denoiseFilterBindings.reset(m_rhi->newShaderResourceBindings());
        m_denoiseFilterBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageLoad(2,B::ComputeStage,m_denoiseGuide.get(),0),B::imageStore(3,B::ComputeStage,m_denoiseLinear.get(),0)});
        m_denoiseColorBindings.reset(m_rhi->newShaderResourceBindings());
        m_denoiseColorBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_denoiseLinear.get(),0),B::imageStore(2,B::ComputeStage,m_output.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
        if(!m_denoisePrepareBindings->create()||!m_denoiseFilterBindings->create()||!m_denoiseColorBindings->create())return fail("Denoise bindings failed");
        if(!buildCompute(m_denoisePreparePipeline,m_denoisePrepareBindings.get(),"denoise_prepare.comp")||!buildCompute(m_denoiseFilterPipeline,m_denoiseFilterBindings.get(),"denoise_filter.comp"))return false;
        PerformanceRecorder::value("denoise_extra_gpu_bytes",qint64(m_size.width())*m_size.height()*32);
    }
    // Detail may become active after an earlier denoise-only frame.
    if(m_detailBase&&!m_denoiseDetailBindings) {
        m_denoiseDetailBindings.reset(m_rhi->newShaderResourceBindings());
        m_denoiseDetailBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_denoiseLinear.get(),0),B::imageStore(2,B::ComputeStage,m_detailBase.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
        if(!m_denoiseDetailBindings->create())return fail("Denoise detail bindings failed");
    }
    return true;
}
bool GpuEngine::ensureDehaze() {
    using B=QRhiShaderResourceBinding;
    if(!m_dehazeFilterPipeline) {
        m_dehazePreparePipeline.reset();m_dehazeHorizontalPipeline.reset();m_dehazeVerticalPipeline.reset();
        for(auto *texture:{&m_dehazeGuide,&m_dehazeHorizontal,&m_dehazeCoarse,&m_dehazeLinear}) {
            texture->reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
            if(!(*texture)->create())return fail("Dehaze allocation failed");
        }
        m_dehazeKernel.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,8*sizeof(float)));
        if(!m_dehazeKernel->create())return fail("Dehaze kernel allocation failed");
        m_dehazePrepareBindings.reset(m_rhi->newShaderResourceBindings());
        m_dehazePrepareBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageStore(2,B::ComputeStage,m_dehazeGuide.get(),0)});
        m_dehazeHorizontalBindings.reset(m_rhi->newShaderResourceBindings());
        m_dehazeHorizontalBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_dehazeGuide.get(),0),B::imageStore(2,B::ComputeStage,m_dehazeHorizontal.get(),0)});
        m_dehazeVerticalBindings.reset(m_rhi->newShaderResourceBindings());
        m_dehazeVerticalBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_dehazeHorizontal.get(),0),B::imageStore(2,B::ComputeStage,m_dehazeCoarse.get(),0)});
        m_dehazeFilterBindings.reset(m_rhi->newShaderResourceBindings());
        m_dehazeFilterBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageLoad(2,B::ComputeStage,m_dehazeGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_dehazeCoarse.get(),0),B::imageStore(4,B::ComputeStage,m_dehazeLinear.get(),0),B::uniformBuffer(5,B::ComputeStage,m_dehazeKernel.get())});
        m_dehazeColorBindings.reset(m_rhi->newShaderResourceBindings());
        m_dehazeColorBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_dehazeLinear.get(),0),B::imageStore(2,B::ComputeStage,m_output.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
        if(!m_dehazePrepareBindings->create()||!m_dehazeHorizontalBindings->create()||!m_dehazeVerticalBindings->create()||!m_dehazeFilterBindings->create()||!m_dehazeColorBindings->create())return fail("Dehaze bindings failed");
        if(!buildCompute(m_dehazePreparePipeline,m_dehazePrepareBindings.get(),"dehaze_prepare.comp")
            ||!buildCompute(m_dehazeHorizontalPipeline,m_dehazeHorizontalBindings.get(),"dehaze_horizontal.comp")
            ||!buildCompute(m_dehazeVerticalPipeline,m_dehazeVerticalBindings.get(),"dehaze_vertical.comp")
            ||!buildCompute(m_dehazeFilterPipeline,m_dehazeFilterBindings.get(),"dehaze_filter.comp"))return false;
        PerformanceRecorder::value("dehaze_extra_gpu_bytes",qint64(m_size.width())*m_size.height()*64+qint64(8*sizeof(float)));
    }
    if(m_denoiseLinear&&!m_dehazePrepareDenoisedBindings) {
        m_dehazePrepareDenoisedBindings.reset(m_rhi->newShaderResourceBindings());
        m_dehazePrepareDenoisedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_denoiseLinear.get(),0),B::imageStore(2,B::ComputeStage,m_dehazeGuide.get(),0)});
        m_dehazeFilterDenoisedBindings.reset(m_rhi->newShaderResourceBindings());
        m_dehazeFilterDenoisedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_denoiseLinear.get(),0),B::imageLoad(2,B::ComputeStage,m_dehazeGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_dehazeCoarse.get(),0),B::imageStore(4,B::ComputeStage,m_dehazeLinear.get(),0),B::uniformBuffer(5,B::ComputeStage,m_dehazeKernel.get())});
        if(!m_dehazePrepareDenoisedBindings->create()||!m_dehazeFilterDenoisedBindings->create())return fail("Denoised dehaze bindings failed");
    }
    if(m_detailBase&&!m_dehazeDetailBindings) {
        m_dehazeDetailBindings.reset(m_rhi->newShaderResourceBindings());
        m_dehazeDetailBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_dehazeLinear.get(),0),B::imageStore(2,B::ComputeStage,m_detailBase.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
        if(!m_dehazeDetailBindings->create())return fail("Dehaze detail bindings failed");
    }
    return true;
}
bool GpuEngine::ensureTexture() {
    using B=QRhiShaderResourceBinding;
    if(!m_textureFilterPipeline) {
        m_texturePreparePipeline.reset();m_textureSmallHorizontalPipeline.reset();m_textureSmallVerticalPipeline.reset();m_textureLargeHorizontalPipeline.reset();
        m_textureGuide.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        m_textureHorizontal.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        m_textureSmallBand.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        m_textureLinear.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        m_textureKernel.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,24*sizeof(float)));
        if(!m_textureGuide->create()||!m_textureHorizontal->create()||!m_textureSmallBand->create()||!m_textureLinear->create()||!m_textureKernel->create())return fail("Texture allocation failed");
        m_texturePrepareBindings.reset(m_rhi->newShaderResourceBindings());
        m_texturePrepareBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageStore(2,B::ComputeStage,m_textureGuide.get(),0)});
        m_textureHorizontalBindings.reset(m_rhi->newShaderResourceBindings());
        m_textureHorizontalBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_textureGuide.get(),0),B::imageStore(2,B::ComputeStage,m_textureHorizontal.get(),0),B::uniformBuffer(3,B::ComputeStage,m_textureKernel.get())});
        m_textureSmallVerticalBindings.reset(m_rhi->newShaderResourceBindings());
        m_textureSmallVerticalBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_textureGuide.get(),0),B::imageLoad(2,B::ComputeStage,m_textureHorizontal.get(),0),B::imageStore(3,B::ComputeStage,m_textureSmallBand.get(),0),B::uniformBuffer(4,B::ComputeStage,m_textureKernel.get())});
        m_textureFilterBindings.reset(m_rhi->newShaderResourceBindings());
        m_textureFilterBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageLoad(2,B::ComputeStage,m_textureGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_textureHorizontal.get(),0),B::imageLoad(4,B::ComputeStage,m_textureSmallBand.get(),0),B::imageStore(5,B::ComputeStage,m_textureLinear.get(),0),B::uniformBuffer(6,B::ComputeStage,m_textureKernel.get())});
        m_textureColorBindings.reset(m_rhi->newShaderResourceBindings());
        m_textureColorBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_textureLinear.get(),0),B::imageStore(2,B::ComputeStage,m_output.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
        if(!m_texturePrepareBindings->create()||!m_textureHorizontalBindings->create()||!m_textureSmallVerticalBindings->create()||!m_textureFilterBindings->create()||!m_textureColorBindings->create())return fail("Texture bindings failed");
        if(!buildCompute(m_texturePreparePipeline,m_texturePrepareBindings.get(),"texture_prepare.comp")
            ||!buildCompute(m_textureSmallHorizontalPipeline,m_textureHorizontalBindings.get(),"texture_small_horizontal.comp")
            ||!buildCompute(m_textureSmallVerticalPipeline,m_textureSmallVerticalBindings.get(),"texture_small_vertical.comp")
            ||!buildCompute(m_textureLargeHorizontalPipeline,m_textureHorizontalBindings.get(),"texture_large_horizontal.comp")
            ||!buildCompute(m_textureFilterPipeline,m_textureFilterBindings.get(),"texture_filter.comp"))return false;
        PerformanceRecorder::value("texture_extra_gpu_bytes",qint64(m_size.width())*m_size.height()*64+qint64(24*sizeof(float)));
    }
    if(m_denoiseLinear&&!m_texturePrepareDenoisedBindings) {
        m_texturePrepareDenoisedBindings.reset(m_rhi->newShaderResourceBindings());
        m_texturePrepareDenoisedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_denoiseLinear.get(),0),B::imageStore(2,B::ComputeStage,m_textureGuide.get(),0)});
        m_textureFilterDenoisedBindings.reset(m_rhi->newShaderResourceBindings());
        m_textureFilterDenoisedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_denoiseLinear.get(),0),B::imageLoad(2,B::ComputeStage,m_textureGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_textureHorizontal.get(),0),B::imageLoad(4,B::ComputeStage,m_textureSmallBand.get(),0),B::imageStore(5,B::ComputeStage,m_textureLinear.get(),0),B::uniformBuffer(6,B::ComputeStage,m_textureKernel.get())});
        if(!m_texturePrepareDenoisedBindings->create()||!m_textureFilterDenoisedBindings->create())return fail("Denoised texture bindings failed");
    }
    if(m_dehazeLinear&&!m_texturePrepareDehazedBindings) {
        m_texturePrepareDehazedBindings.reset(m_rhi->newShaderResourceBindings());
        m_texturePrepareDehazedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_dehazeLinear.get(),0),B::imageStore(2,B::ComputeStage,m_textureGuide.get(),0)});
        m_textureFilterDehazedBindings.reset(m_rhi->newShaderResourceBindings());
        m_textureFilterDehazedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_dehazeLinear.get(),0),B::imageLoad(2,B::ComputeStage,m_textureGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_textureHorizontal.get(),0),B::imageLoad(4,B::ComputeStage,m_textureSmallBand.get(),0),B::imageStore(5,B::ComputeStage,m_textureLinear.get(),0),B::uniformBuffer(6,B::ComputeStage,m_textureKernel.get())});
        if(!m_texturePrepareDehazedBindings->create()||!m_textureFilterDehazedBindings->create())return fail("Dehazed texture bindings failed");
    }
    if(m_detailBase&&!m_textureDetailBindings) {
        m_textureDetailBindings.reset(m_rhi->newShaderResourceBindings());
        m_textureDetailBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_textureLinear.get(),0),B::imageStore(2,B::ComputeStage,m_detailBase.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
        if(!m_textureDetailBindings->create())return fail("Texture detail bindings failed");
    }
    return true;
}
bool GpuEngine::ensureClarity() {
    using B=QRhiShaderResourceBinding;
    if(!m_clarityPreparePipeline) {
        m_clarityGuide.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        m_clarityHorizontal.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        m_clarityLinear.reset(m_rhi->newTexture(QRhiTexture::RGBA32F,m_size,1,QRhiTexture::UsedWithLoadStore));
        m_clarityKernel.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,52*sizeof(float)));
        if(!m_clarityGuide->create()||!m_clarityHorizontal->create()||!m_clarityLinear->create()||!m_clarityKernel->create())return fail("Clarity allocation failed");
        m_clarityPrepareBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityPrepareBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageStore(2,B::ComputeStage,m_clarityGuide.get(),0)});
        m_clarityHorizontalBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityHorizontalBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_clarityGuide.get(),0),B::imageStore(2,B::ComputeStage,m_clarityHorizontal.get(),0),B::uniformBuffer(3,B::ComputeStage,m_clarityKernel.get())});
        m_clarityFilterBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityFilterBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_source.get(),0),B::imageLoad(2,B::ComputeStage,m_clarityGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_clarityHorizontal.get(),0),B::imageStore(4,B::ComputeStage,m_clarityLinear.get(),0),B::uniformBuffer(5,B::ComputeStage,m_clarityKernel.get())});
        m_clarityColorBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityColorBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_clarityLinear.get(),0),B::imageStore(2,B::ComputeStage,m_output.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
        if(!m_clarityPrepareBindings->create()||!m_clarityHorizontalBindings->create()||!m_clarityFilterBindings->create()||!m_clarityColorBindings->create())return fail("Clarity bindings failed");
        if(!buildCompute(m_clarityPreparePipeline,m_clarityPrepareBindings.get(),"clarity_prepare.comp")
            ||!buildCompute(m_clarityHorizontalPipeline,m_clarityHorizontalBindings.get(),"clarity_horizontal.comp")
            ||!buildCompute(m_clarityFilterPipeline,m_clarityFilterBindings.get(),"clarity_filter.comp"))return false;
        PerformanceRecorder::value("clarity_extra_gpu_bytes",qint64(m_size.width())*m_size.height()*48+qint64(52*sizeof(float)));
    }
    if(m_denoiseLinear&&!m_clarityPrepareDenoisedBindings) {
        m_clarityPrepareDenoisedBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityPrepareDenoisedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_denoiseLinear.get(),0),B::imageStore(2,B::ComputeStage,m_clarityGuide.get(),0)});
        m_clarityFilterDenoisedBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityFilterDenoisedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_denoiseLinear.get(),0),B::imageLoad(2,B::ComputeStage,m_clarityGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_clarityHorizontal.get(),0),B::imageStore(4,B::ComputeStage,m_clarityLinear.get(),0),B::uniformBuffer(5,B::ComputeStage,m_clarityKernel.get())});
        if(!m_clarityPrepareDenoisedBindings->create()||!m_clarityFilterDenoisedBindings->create())return fail("Denoised clarity bindings failed");
    }
    if(m_dehazeLinear&&!m_clarityPrepareDehazedBindings) {
        m_clarityPrepareDehazedBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityPrepareDehazedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_dehazeLinear.get(),0),B::imageStore(2,B::ComputeStage,m_clarityGuide.get(),0)});
        m_clarityFilterDehazedBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityFilterDehazedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_dehazeLinear.get(),0),B::imageLoad(2,B::ComputeStage,m_clarityGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_clarityHorizontal.get(),0),B::imageStore(4,B::ComputeStage,m_clarityLinear.get(),0),B::uniformBuffer(5,B::ComputeStage,m_clarityKernel.get())});
        if(!m_clarityPrepareDehazedBindings->create()||!m_clarityFilterDehazedBindings->create())return fail("Dehazed clarity bindings failed");
    }
    if(m_textureLinear&&!m_clarityPrepareTexturedBindings) {
        m_clarityPrepareTexturedBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityPrepareTexturedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_textureLinear.get(),0),B::imageStore(2,B::ComputeStage,m_clarityGuide.get(),0)});
        m_clarityFilterTexturedBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityFilterTexturedBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_textureLinear.get(),0),B::imageLoad(2,B::ComputeStage,m_clarityGuide.get(),0),B::imageLoad(3,B::ComputeStage,m_clarityHorizontal.get(),0),B::imageStore(4,B::ComputeStage,m_clarityLinear.get(),0),B::uniformBuffer(5,B::ComputeStage,m_clarityKernel.get())});
        if(!m_clarityPrepareTexturedBindings->create()||!m_clarityFilterTexturedBindings->create())return fail("Textured clarity bindings failed");
    }
    if(m_detailBase&&!m_clarityDetailBindings) {
        m_clarityDetailBindings.reset(m_rhi->newShaderResourceBindings());
        m_clarityDetailBindings->setBindings({B::uniformBuffer(0,B::ComputeStage,m_uniform.get()),B::imageLoad(1,B::ComputeStage,m_clarityLinear.get(),0),B::imageStore(2,B::ComputeStage,m_detailBase.get(),0),B::imageLoad(3,B::ComputeStage,m_lutTexture.get(),0)});
        if(!m_clarityDetailBindings->create())return fail("Clarity detail bindings failed");
    }
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
