#include "core/gpu/brush/NativeBrushSource.h"
#include "core/gpu/brush/NativeBrushPrivate.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>

namespace NativeBrush {
namespace { std::atomic<quint64> nextCaptureSerial{0}; }

bool SourceDescription::valid() const {
    return !inputIdentity.isEmpty() && inputIdentity.size() <= 4096
        && (encoding == ImagePipeline::InputEncoding::SRgb || encoding == ImagePipeline::InputEncoding::LinearProPhoto)
        && std::isfinite(baseExposureStops) && baseExposureStops >= -8 && baseExposureStops <= 8
        && BrushMaskTileRaster::validNormalizedFrame({preparedFrame.x(), preparedFrame.y(), preparedFrame.width(), preparedFrame.height()});
}
bool DeliveryTarget::valid() const {
    return !catalog.isEmpty() && !photo.isEmpty() && !version.isEmpty() && ownerEpoch
        && catalog.size() <= 4096 && photo.size() <= 4096 && version.size() <= 4096;
}
bool SourceThreads::permitsCapture() const {
    const auto *app = QCoreApplication::instance();
    return QThread::currentThreadId() != render && QThread::currentThread() != gui
        && (!app || QThread::currentThread() != app->thread());
}

CanonicalSource::Ptr CanonicalSource::capture(const QImage &image, SourceDescription description,
        SourceThreads threads, std::shared_ptr<BrushRenderBudget> budget, SourceFailure *why, const CancelToken &cancel) {
    auto fail = [why](SourceFailure f) -> Ptr { if (why) *why = f; return {}; };
    if (!threads.permitsCapture()) return fail(SourceFailure::WrongThread);
    if (cancelled(cancel)) return fail(SourceFailure::Cancelled);
    const auto pixels = qint64(image.width()) * image.height();
    if (!budget || image.isNull() || !description.valid()
        || !BrushMaskGeometry::validFrame(image.width(), image.height()) || pixels > BrushRenderRequest::MaxSourcePixels
        || image.sizeInBytes() <= 0 || image.sizeInBytes() > BrushRenderRequest::MaxSourcePixels * 16
        || (image.format() != QImage::Format_RGBA64 && image.format() != QImage::Format_RGBA32FPx4)) return fail(SourceFailure::Invalid);
    const auto bytes = image.sizeInBytes() + 512 * 1024;
    auto charge = BudgetAccess::reserve(budget, bytes);
    if (!charge) return fail(SourceFailure::Budget);
    try {
        auto source = std::shared_ptr<CanonicalSource>(new CanonicalSource);
        source->m_budget = std::move(budget); source->m_charge = std::move(charge); source->m_bytes = bytes;
        source->m_description = std::move(description); source->m_preparedOn=QThread::currentThreadId(); source->m_pixels = image.copy();
        if (source->m_pixels.isNull()) return fail(SourceFailure::Allocation);
        if (image.format() == QImage::Format_RGBA32FPx4 && !ImagePipeline::validFloatSource(source->m_pixels, cancel))
            return fail(cancelled(cancel) ? SourceFailure::Cancelled : SourceFailure::Invalid);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        const int rowBytes = image.width() * (image.format() == QImage::Format_RGBA64 ? 8 : 16);
        for (int y = 0; y < image.height(); ++y) {
            if (cancelled(cancel)) return fail(SourceFailure::Cancelled);
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(source->m_pixels.constScanLine(y)), rowBytes));
        }
        source->m_digest = hash.result();
        QByteArray header; QDataStream stream(&header, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian); stream.setVersion(QDataStream::Qt_6_8);
        const auto &d = source->m_description;
        stream << QStringLiteral("native-canonical-v1") << d.inputIdentity << qint32(d.encoding)
            << d.rawSource << d.baseExposureStops << d.preparedFrame << image.size() << qint32(image.format()) << source->m_digest;
        source->m_key = QCryptographicHash::hash(header, QCryptographicHash::Sha256);
        if (cancelled(cancel)) return fail(SourceFailure::Cancelled);
        source->m_captureSerial=nextCaptureSerial.fetch_add(1,std::memory_order_relaxed)+1;
        if (why) *why = SourceFailure::None;
        return source;
    } catch (...) { return fail(SourceFailure::Allocation); }
}

QImage CanonicalSource::sharedImage() const {
    using Keeper = std::shared_ptr<const CanonicalSource>;
    auto keeper = std::make_unique<Keeper>(shared_from_this());
    QImage view(m_pixels.constBits(), m_pixels.width(), m_pixels.height(), m_pixels.bytesPerLine(), m_pixels.format(),
        [](void *p) { delete static_cast<Keeper *>(p); }, keeper.get());
    if (view.isNull()) return {};
    keeper.release(); view.setColorSpace(m_pixels.colorSpace()); view.setDevicePixelRatio(m_pixels.devicePixelRatio());
    view.setDotsPerMeterX(m_pixels.dotsPerMeterX()); view.setDotsPerMeterY(m_pixels.dotsPerMeterY());
    for (const auto &key : m_pixels.textKeys()) view.setText(key, m_pixels.text(key));
    return view.constBits() == m_pixels.constBits() ? view : QImage{};
}

std::unique_ptr<BrushRenderRequest> CanonicalSource::exactRequest(const ProcessingPlan &plan,
        BrushRenderRequest::Layout layout, const CancelToken &cancel) const {
    const auto &d = m_description; const auto f = plan.data[ProcessingPlan::FrameRect];
    if (plan.encoding != d.encoding || plan.rawSource != d.rawSource || plan.baseExposureStops != d.baseExposureStops
        || f.x != float(d.preparedFrame.x()) || f.y != float(d.preparedFrame.y())
        || f.z != float(d.preparedFrame.width()) || f.w != float(d.preparedFrame.height())) return {};
    return BrushRenderRequest::createPrepared(m_pixels, plan, layout, cancel, nullptr, m_budget);
}
} // namespace NativeBrush
