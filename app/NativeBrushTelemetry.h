#pragma once
#include <QJsonObject>
#include <atomic>
#include <memory>

// Passive delivery facts contain no QObject, pixels or GPU resource ownership.
// A retained obsolete context can be inspected after its real callbacks retire.
struct NativeBrushTelemetry final {
    std::atomic<quint64> computeSubmissions{0},lastSubmittedRevision{0};
    std::atomic<quint64> enqueueIntents{0},lastEnqueueRevision{0};
    std::atomic<quint64> guiAccepted{0},guiRejected{0};
    std::atomic<quint64> renderRetiredBlocked{0},retiredOwnersReleased{0};
    QJsonObject snapshot() const {
        return {{"compute_submissions",qint64(computeSubmissions.load(std::memory_order_acquire))},
            {"last_submitted_revision",qint64(lastSubmittedRevision.load(std::memory_order_acquire))},
            {"enqueue_intents",qint64(enqueueIntents.load(std::memory_order_acquire))},
            {"last_enqueue_revision",qint64(lastEnqueueRevision.load(std::memory_order_acquire))},
            {"GUI_accepted",qint64(guiAccepted.load(std::memory_order_acquire))},
            {"GUI_rejected",qint64(guiRejected.load(std::memory_order_acquire))},
            {"render_retired_blocked",qint64(renderRetiredBlocked.load(std::memory_order_acquire))},
            {"retired_owners_released",qint64(retiredOwnersReleased.load(std::memory_order_acquire))}};
    }
};
