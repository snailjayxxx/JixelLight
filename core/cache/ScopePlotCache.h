#pragma once
#include "core/scopes/ScopePlot.h"
#include <QCache>
#include <QMutex>

// Retains only the three completed UI-ink plots and active LUT, never source,
// rendered tiles or full encoded frames. A miss shares one color-render pass.
class ScopePlotCache final {
public:
    explicit ScopePlotCache(qint64 budgetBytes=16*1024*1024);
    ScopePlotResult render(const ScopePlotRequest &request,const CancelToken &cancel={});
    QJsonObject snapshot() const;
private:
    struct Entry { std::array<ScopePlotResult,3> results; std::shared_ptr<const LookLut> lut; };
    QJsonObject snapshotLocked() const;
    void recordLocked();
    mutable QMutex m_mutex;
    QCache<QString,Entry> m_entries;
    qint64 m_hits=0,m_misses=0,m_bypasses=0,m_evictions=0,m_cancelled=0;
};
