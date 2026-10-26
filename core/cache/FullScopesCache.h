#pragma once
#include "core/scopes/ScopesEngine.h"
#include <QCache>
#include <QMutex>
struct ScopeRequest;
class AnalysisFrameCache;

// Completed exact histogram results, keyed before geometry allocates a new
// image. Histogram entries retain no frames or partial counts. An optional
// shared analysis-frame cache has its own separate allocation/lease budget.
class FullScopesCache final {
public:
    explicit FullScopesCache(qint64 budgetBytes = 4 * 1024 * 1024,
                             std::shared_ptr<AnalysisFrameCache> analysisFrames = {});
    ScopesResult analyze(const ScopeRequest &request, const CancelToken &cancel = {});
    QJsonObject snapshot() const;
private:
    struct Entry { ScopesResult result; std::shared_ptr<const LookLut> lut; };
    QJsonObject snapshotLocked() const;
    void recordLocked();
    mutable QMutex m_mutex;
    QCache<QString, Entry> m_entries;
    std::shared_ptr<AnalysisFrameCache> m_analysisFrames;
    qint64 m_hits=0, m_misses=0, m_bypasses=0, m_evictions=0, m_cancelled=0;
};
