#pragma once
#include "core/gpu/brush/NativeBrushSource.h"
#include <atomic>
#include <map>

class QRhi;
class QRhiTexture;
class QRhiCommandBuffer;

namespace NativeBrush {
class RenderDomain;
class FrameAdmission;
enum class FrameFailure;
struct RhiLifetime final { std::atomic_bool active{true}; };

class SourceCapabilities final {
public:
    bool active() const { return m_lifetime && m_lifetime->active.load(std::memory_order_acquire); }
    quint64 generation() const { return m_generation; }
    SourceThreads threads() const { return m_threads; }
private:
    friend class RenderDomain;
    friend class UploadAdmission;
    friend class FrameAdmission;
    quintptr m_cookie = 0;
    quint64 m_generation = 0;
    int m_maxTexture = 0;
    int m_framesInFlight = 0, m_uniformAlignment = 0, m_groupMaximum = 0, m_backend = 0;
    bool m_compute = false, m_histogramCompute = false, m_numericRestricted = false;
    SourceThreads m_threads;
    std::shared_ptr<RhiLifetime> m_lifetime;
};

class UploadAdmission final {
public:
    using Ptr = std::shared_ptr<UploadAdmission>;
    // Canonical CPU pixels survive an obsolete or refused GPU upload.
    static Ptr prepare(CanonicalSource::Ptr, const SourceCapabilities &, SourceFailure * = nullptr,
                       const CancelToken & = {});
    bool ready() const { return m_state.load(std::memory_order_acquire) == 0; }
    bool discard();
    const QByteArray &sourceKey() const { return m_source->key(); }
private:
    friend class RenderDomain;
    UploadAdmission() = default;
    CanonicalSource::Ptr m_source;
    SourceCapabilities m_caps;
    std::shared_ptr<const void> m_gpuCharge, m_uploadCharge;
    QImage m_upload;
    CancelToken m_cancel;
    std::atomic_int m_state{0}; // ready, attaching, consumed, discarded
};

// A GUI handle can only retire the domain atomically. It cannot own/delete any
// QRhi resources. All input handles below are value IDs local to their domain.
class DomainRetirement final {
public:
    void retire() const { if (m_state) m_state->active.store(false, std::memory_order_release); }
    bool active() const { return m_state && m_state->active.load(std::memory_order_acquire); }
private:
    friend class RenderDomain;
    std::shared_ptr<RhiLifetime> m_state;
};

struct InputHandle {
    quint64 generation = 0, input = 0;
    explicit operator bool() const { return generation && input; }
    bool operator==(const InputHandle &) const = default;
};

struct FrameHandle {
    quint64 generation = 0, frame = 0;
    explicit operator bool() const { return generation && frame; }
    bool operator==(const FrameHandle &) const = default;
};
struct FrameStamp {
    DeliveryTarget target;
    quint64 revision = 0;
    QByteArray sourceKey;
    bool operator==(const FrameStamp &) const = default;
};
struct ShaderBytes { QByteArray coverage, colour, histogram, reduce; };
struct HistogramReply {
    std::shared_ptr<const void> charge;
    DomainRetirement owner;
    FrameStamp stamp;
    quint64 generation = 0, pixels = 0;
    QByteArray counts;
};

// Create, call and destroy on the real QRhi owner, including cleanup. The QRhi
// cleanup callback invalidates capabilities before that QRhi is destroyed.
class RenderDomain final {
public:
    struct Trace {
        quint64 sourceUploads=0, coverageDispatches=0, colourDispatches=0, histogramDispatches=0;
        quint64 controlReadbackBytes=0, histogramReadbackBytes=0;
    };
    explicit RenderDomain(QRhi *, ShaderBytes = {});
    ~RenderDomain();
    RenderDomain(const RenderDomain &) = delete;
    RenderDomain &operator=(const RenderDomain &) = delete;
    SourceCapabilities capabilities() const { return m_caps; }
    DomainRetirement retirement() const { DomainRetirement result; result.m_state = m_lifetime; return result; }
    InputHandle attach(const UploadAdmission::Ptr &, SourceFailure * = nullptr);
    bool upload(InputHandle, QRhiCommandBuffer *);
    void collect();
    bool ready(InputHandle) const;
    bool pending() const;
    QRhiTexture *texture(InputHandle) const;
    QByteArray sourceKey(InputHandle) const;
    bool retain(InputHandle);
    void release(InputHandle);
    bool activate(FrameStamp);
    FrameHandle attachFrame(InputHandle, const std::shared_ptr<FrameAdmission> &, FrameFailure * = nullptr);
    bool compute(FrameHandle, QRhiCommandBuffer *);
    bool ready(FrameHandle) const;
    QRhiTexture *texture(FrameHandle) const;
    bool publishable(FrameHandle) const;
    void release(FrameHandle);
    // The drawer only borrows the working texture for this synchronous call.
    // Its original monitor-ICC/premultiplication implementation stays intact.
    // Even a false return retains a fence for any draw already recorded.
    bool present(FrameHandle, QRhiCommandBuffer *, const std::function<bool(QRhiTexture *)> &);
    bool histogram(FrameHandle, QRhiCommandBuffer *);
    using DiagnosticReady=std::function<void(FrameStamp,QJsonObject)>;
    bool capture(FrameHandle,QRhiCommandBuffer *,qint64 qtSourceKey,const DiagnosticReady &);
    std::vector<std::shared_ptr<HistogramReply>> takeHistogramReplies();
    void shutdown();
    quint64 generation() const { return m_caps.generation(); }
    quint64 normalFinishCalls() const { return 0; }
    quint64 teardownFinishCalls() const { return m_teardownFinishes; }
    Trace trace() const { return m_trace; }
private:
    struct Input;
    struct Frame;
    struct Histogram;
    struct Presentation;
    struct Diagnostic;
    bool owner() const;
    Input *find(InputHandle) const;
    Frame *find(FrameHandle) const;
    void collectFrames();
    void collectHistograms();
    void collectPresentations();
    void collectDiagnostics();
    bool workPending() const;
    void clearWorkResources();
    void clearResources(bool rhiCleanup);
    QRhi *m_rhi = nullptr;
    SourceCapabilities m_caps;
    std::shared_ptr<RhiLifetime> m_lifetime;
    std::map<quint64, std::unique_ptr<Input>> m_inputs;
    std::map<quint64, std::unique_ptr<Frame>> m_frames;
    std::map<quint64, std::unique_ptr<Histogram>> m_histograms;
    std::map<quint64, std::unique_ptr<Presentation>> m_presentations;
    std::map<quint64, std::unique_ptr<Diagnostic>> m_diagnostics;
    std::vector<std::shared_ptr<HistogramReply>> m_replies;
    ShaderBytes m_shaders;
    FrameStamp m_current;
    Trace m_trace;
    quint64 m_nextFrame = 0, m_nextHistogram = 0, m_nextPresentation = 0;
    quint64 m_nextDiagnostic = 0;
    quint64 m_nextInput = 0, m_teardownFinishes = 0;
};
} // namespace NativeBrush
