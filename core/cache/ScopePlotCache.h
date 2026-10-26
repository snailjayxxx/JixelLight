#pragma once
#include "core/scopes/ScopePlot.h"
#include <QCache>
#include <QMutex>
class AnalysisFrameCache;
class BrushRenderBudget;

// Plot entries retain only completed UI ink and active LUT. An optional shared
// analysis cache supplies completed color under its own frame/lease budget.
// Otherwise a miss shares one bounded color-render pass across three plots.
class ScopePlotCache final {
public:
    explicit ScopePlotCache(qint64 budgetBytes=16*1024*1024,
                            std::shared_ptr<AnalysisFrameCache> analysisFrames = {},
                            std::shared_ptr<BrushRenderBudget> brushBudget = {});
    ScopePlotResult render(const ScopePlotRequest &request,const CancelToken &cancel={});
    QJsonObject snapshot() const;
private:
    struct Entry { std::array<ScopePlotResult,3> results; std::shared_ptr<const LookLut> lut; };
    QJsonObject snapshotLocked() const;
    void recordLocked();
    mutable QMutex m_mutex;
    QCache<QString,Entry> m_entries;
    std::shared_ptr<AnalysisFrameCache> m_analysisFrames;
    std::shared_ptr<BrushRenderBudget> m_brushBudget;
    qint64 m_hits=0,m_misses=0,m_bypasses=0,m_evictions=0,m_cancelled=0;
};
