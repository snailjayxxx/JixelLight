#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include <QCache>
#include <QMutex>

// CPU preview results only. Sources/plans are immutable worker snapshots;
// exports, exact scopes and diagnostic reference captures bypass this cache.
class RenderedPreviewCache final {
public:
    explicit RenderedPreviewCache(qint64 budgetBytes = 64 * 1024 * 1024);
    QImage render(const QImage &source, const ProcessingPlan &plan, const CancelToken &cancel = {});
    QJsonObject snapshot() const;
private:
    struct Entry {
        QImage image;
        // Keep pointer identities alive even for externally constructed LUTs
        // without a digest. Charge the payload conservatively per entry.
        std::shared_ptr<const LookLut> lut;
    };
    QJsonObject snapshotLocked() const;
    void recordLocked();
    mutable QMutex m_mutex;
    QCache<QString, Entry> m_entries;
    qint64 m_hits = 0, m_misses = 0, m_bypasses = 0, m_evictions = 0, m_cancelled = 0;
};
