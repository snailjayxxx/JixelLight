#pragma once
#include "core/scopes/ScopesEngine.h"
#include <QCache>
#include <QMutex>
struct ScopeRequest;

// Completed exact histogram results, keyed before geometry allocates a new
// image. No source/rendered frame is retained and no partial counts are stored.
class FullScopesCache final {
public:
    explicit FullScopesCache(qint64 budgetBytes = 4 * 1024 * 1024);
    ScopesResult analyze(const ScopeRequest &request, const CancelToken &cancel = {});
    QJsonObject snapshot() const;
private:
    struct Entry { ScopesResult result; std::shared_ptr<const LookLut> lut; };
    QJsonObject snapshotLocked() const;
    void recordLocked();
    mutable QMutex m_mutex;
    QCache<QString, Entry> m_entries;
    qint64 m_hits=0, m_misses=0, m_bypasses=0, m_evictions=0, m_cancelled=0;
};
