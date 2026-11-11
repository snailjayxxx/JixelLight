#pragma once
#include "core/pipeline/BrushRenderRequest.h"
#include <QThread>

namespace NativeBrush {

enum class SourceFailure { None, Invalid, Cancelled, Budget, Allocation, WrongThread, Stale, Consumed };

// Input identity may be shared by virtual copies. Delivery identity must not be.
struct SourceDescription {
    QString inputIdentity;
    ImagePipeline::InputEncoding encoding = ImagePipeline::InputEncoding::LinearProPhoto;
    bool rawSource = false;
    float baseExposureStops = 0;
    QRectF preparedFrame{0, 0, 1, 1};
    bool valid() const;
    bool operator==(const SourceDescription &) const = default;
};

struct DeliveryTarget {
    QString catalog, photo, version;
    quint64 ownerEpoch = 0;
    bool valid() const;
    bool operator==(const DeliveryTarget &) const = default;
};

// A value captured on the actual RHI owner. No QRhi access occurs on a worker.
struct SourceThreads {
    QThread *gui = nullptr;
    Qt::HANDLE render = nullptr;
    bool permitsCapture() const;
};

class UploadAdmission;
class CanonicalSource final : public std::enable_shared_from_this<CanonicalSource> {
public:
    using Ptr = std::shared_ptr<const CanonicalSource>;
    // Source must remain stable for this synchronous worker call. Enqueuing an
    // externally writable shallow QImage is not an immutable source capture.
    static Ptr capture(const QImage &, SourceDescription, SourceThreads,
                       std::shared_ptr<BrushRenderBudget>, SourceFailure * = nullptr,
                       const CancelToken & = {});
    QSize size() const { return m_pixels.size(); }
    Qt::HANDLE preparationThread() const { return m_preparedOn; }
    quint64 captureSerial() const { return m_captureSerial; }
    QImage::Format format() const { return m_pixels.format(); }
    const QByteArray &key() const { return m_key; }
    const QByteArray &pixelDigest() const { return m_digest; }
    const SourceDescription &description() const { return m_description; }
    qint64 chargedBytes() const { return m_bytes; }
    // Shallow copies keep both pixels and their reservation alive. Writes
    // detach because the backing QImage is exposed as const external storage.
    QImage sharedImage() const;
    std::unique_ptr<BrushRenderRequest> exactRequest(const ProcessingPlan &,
        BrushRenderRequest::Layout = BrushRenderRequest::Layout::Whole,
        const CancelToken & = {}) const;
private:
    friend class UploadAdmission;
    friend class RenderDomain;
    friend class FrameAdmission;
    CanonicalSource() = default;
    std::shared_ptr<BrushRenderBudget> m_budget;
    std::shared_ptr<const void> m_charge;
    QImage m_pixels;
    SourceDescription m_description;
    QByteArray m_key, m_digest;
    qint64 m_bytes = 0;
    Qt::HANDLE m_preparedOn = nullptr;
    quint64 m_captureSerial = 0;
};

} // namespace NativeBrush
