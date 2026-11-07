#include "core/cache/AnalysisFrameCache.h"
#include "core/look/LookLut.h"
#include <QColorSpace>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

struct AnalysisFrameCache::State {
    struct Accounting {
        explicit Accounting(qint64 bytes) : budget(std::max(qint64(0), bytes)) {}
        const qint64 budget;
        std::atomic<qint64> charged{0}, building{0}, peak{0};
    };
    struct Charge {
        Charge(std::shared_ptr<Accounting> account, qint64 bytes)
            : accounting(std::move(account)), cost(bytes) {
            auto previous=accounting->charged.load(std::memory_order_relaxed);
            while (previous<=accounting->budget && cost<=accounting->budget-previous) {
                if (accounting->charged.compare_exchange_weak(previous,previous+cost)) {
                    reserved=true;
                    accounting->building.fetch_add(cost);
                    auto peak=accounting->peak.load(std::memory_order_relaxed);
                    while (peak<previous+cost && !accounting->peak.compare_exchange_weak(peak,previous+cost)) {}
                    break;
                }
            }
        }
        ~Charge() {
            if (!reserved) return;
            if (building) accounting->building.fetch_sub(cost);
            accounting->charged.fetch_sub(cost);
        }
        void published() noexcept {
            if (building) { accounting->building.fetch_sub(cost); building=false; }
        }
        std::shared_ptr<Accounting> accounting;
        qint64 cost=0;
        bool reserved=false, building=true;
    };
    explicit State(qint64 budget) : accounting(std::make_shared<Accounting>(budget)) {}
    QMutex mutex;
    std::shared_ptr<Accounting> accounting;
    std::shared_ptr<const Entry> latest;
    QString latestKey;
    QHash<QString,quint64> pending;
    quint64 generation=0;
    bool closed=false;
    qint64 hits=0, misses=0, bypasses=0, evictions=0, cancelledRequests=0,
        publications=0, inflightBypasses=0, budgetBypasses=0, oversizeBypasses=0,
        disabledBypasses=0, completedBypasses=0, invalidRequests=0,
        stalePublications=0, allocationFailures=0;
};

struct AnalysisFrameCache::Entry {
    // Charge is destroyed last, after the image and retained LUT are released.
    std::unique_ptr<State::Charge> charge;
    std::shared_ptr<const LookLut> lut;
    QImage image;
};

struct AnalysisFrameCache::Build {
    ~Build() {
        if (!pending) return;
        QMutexLocker lock(&state->mutex);
        const auto it=state->pending.find(key);
        if (it!=state->pending.end() && it.value()==generation) state->pending.erase(it);
        // Entry's charge only updates atomics, never the cache mutex.
    }
    std::shared_ptr<State> state;
    std::shared_ptr<Entry> entry;
    QString key;
    quint64 generation=0;
    int nextY=0;
    bool pending=false, metadataCopied=false;
};

AnalysisFrameCache::Lease::Lease(std::shared_ptr<const Entry> entry) : m_entry(std::move(entry)) {}
AnalysisFrameCache::Lease::operator bool() const noexcept { return bool(m_entry); }
const QImage &AnalysisFrameCache::Lease::image() const noexcept {
    static const QImage empty;
    return m_entry ? m_entry->image : empty;
}

AnalysisFrameCache::Writer::Writer() noexcept = default;
AnalysisFrameCache::Writer::Writer(std::unique_ptr<Build> build) noexcept : m_build(std::move(build)) {}
AnalysisFrameCache::Writer::~Writer() = default;
AnalysisFrameCache::Writer::Writer(Writer &&) noexcept = default;
AnalysisFrameCache::Writer &AnalysisFrameCache::Writer::operator=(Writer &&) noexcept = default;
AnalysisFrameCache::Writer::operator bool() const noexcept { return bool(m_build); }
void AnalysisFrameCache::Writer::fail(bool wasCancelled, bool malformed, bool allocationFailure) noexcept {
    if (!m_build) return;
    {
        QMutexLocker lock(&m_build->state->mutex);
        if (wasCancelled) ++m_build->state->cancelledRequests;
        if (malformed) ++m_build->state->invalidRequests;
        if (allocationFailure) ++m_build->state->allocationFailures;
    }
    m_build.reset();
}
bool AnalysisFrameCache::Writer::append(const QImage &tile,int y,const CancelToken &cancel) {
    if (!m_build) return false;
    if (cancelled(cancel)) { fail(true); return false; }
    auto &build=*m_build;
    auto &frame=build.entry->image;
    if (tile.isNull() || tile.format()!=QImage::Format_RGBA64 || tile.width()!=frame.width() ||
        y!=build.nextY || tile.height()>frame.height()-build.nextY ||
        (build.metadataCopied && tile.colorSpace()!=frame.colorSpace())) {
        fail(false,true); return false;
    }
    try {
        if (!build.metadataCopied) {
            frame.setColorSpace(tile.colorSpace());
            frame.setDevicePixelRatio(tile.devicePixelRatio());
            frame.setDotsPerMeterX(tile.dotsPerMeterX());
            frame.setDotsPerMeterY(tile.dotsPerMeterY());
            for (const auto &key:tile.textKeys()) frame.setText(key,tile.text(key));
            build.metadataCopied=true;
        }
        const auto bytes=size_t(frame.width())*8;
        for (int row=0;row<tile.height();++row) {
            if (cancelled(cancel)) { fail(true); return false; }
            auto *destination=frame.scanLine(y+row);
            if (!destination) { fail(false,false,true); return false; }
            std::memcpy(destination,tile.constScanLine(row),bytes);
        }
        build.nextY+=tile.height();
    } catch (...) { fail(false,false,true); return false; }
    if (cancelled(cancel)) { fail(true); return false; }
    return true;
}
bool AnalysisFrameCache::Writer::publish(const CancelToken &cancel) {
    if (!m_build) return false;
    if (cancelled(cancel)) { fail(true); return false; }
    if (m_build->nextY!=m_build->entry->image.height()) { fail(false,true); return false; }
    auto &build=*m_build;
    std::shared_ptr<const Entry> retired;
    bool accepted=false;
    try {
        QMutexLocker lock(&build.state->mutex);
        const bool wasCancelled=cancelled(cancel);
        if (wasCancelled) ++build.state->cancelledRequests;
        else if (build.state->closed || build.generation!=build.state->generation)
            ++build.state->stalePublications;
        else {
            // Assign potentially allocating metadata before replacing the entry.
            build.state->latestKey=build.key;
            retired=std::move(build.state->latest);
            if (retired) ++build.state->evictions;
            build.entry->charge->published();
            build.state->latest=build.entry;
            ++build.state->publications;
            accepted=true;
        }
    } catch (...) { fail(false,false,true); return false; }
    m_build.reset();
    return accepted;
}

AnalysisFrameCache::AnalysisFrameCache(qint64 budgetBytes) : m_state(std::make_shared<State>(budgetBytes)) {}
AnalysisFrameCache::~AnalysisFrameCache() {
    std::shared_ptr<const Entry> retired;
    QMutexLocker lock(&m_state->mutex);
    m_state->closed=true;
    ++m_state->generation;
    retired=std::move(m_state->latest);
}
AnalysisFrameCache::Lease AnalysisFrameCache::find(const QString &key,const CancelToken &cancel) {
    QMutexLocker lock(&m_state->mutex);
    if (cancelled(cancel)) { ++m_state->cancelledRequests; return {}; }
    if (!key.isEmpty() && m_state->latest && m_state->latestKey==key) {
        ++m_state->hits;
        return Lease(m_state->latest);
    }
    ++m_state->misses;
    return {};
}
AnalysisFrameCache::Writer AnalysisFrameCache::begin(const QString &key,const QSize &size,
        const std::shared_ptr<const LookLut> &lut,const CancelToken &cancel) {
    std::unique_ptr<Build> build;
    std::shared_ptr<const Entry> retired;
    try {
        build=std::make_unique<Build>();
        build->state=m_state;
        build->key=key;
        {
            QMutexLocker lock(&m_state->mutex);
            if (cancelled(cancel)) { ++m_state->cancelledRequests; return {}; }
            if (key.isEmpty() || size.width()<=0 || size.height()<=0 || m_state->closed) {
                ++m_state->invalidRequests; ++m_state->bypasses; return {};
            }
            if (m_state->pending.contains(key)) {
                ++m_state->inflightBypasses; ++m_state->bypasses; return {};
            }
            if (m_state->latest && m_state->latestKey==key) {
                ++m_state->completedBypasses; ++m_state->bypasses; return {};
            }
            build->generation=++m_state->generation;
            if (m_state->accounting->budget==0) {
                ++m_state->disabledBypasses; ++m_state->bypasses; return {};
            }
            // Checked arithmetic precedes any QImage allocation.
            const qint64 maximum=std::numeric_limits<qint64>::max();
            const qint64 rowBytes=qint64(size.width())*8;
            const qint64 lutSize=lut ? lut->rgb.capacity():0;
            const qint64 overhead=sizeof(Entry)+(lut ? sizeof(LookLut):0);
            if (lutSize>(maximum-overhead)/qint64(sizeof(float)) ||
                qint64(size.height())>(maximum-overhead-lutSize*qint64(sizeof(float)))/rowBytes) {
                ++m_state->oversizeBypasses; ++m_state->bypasses; return {};
            }
            const qint64 cost=rowBytes*size.height()+overhead+lutSize*qint64(sizeof(float));
            if (cost>m_state->accounting->budget) {
                ++m_state->oversizeBypasses; ++m_state->bypasses; return {};
            }
            m_state->pending.insert(key,build->generation);
            build->pending=true;
            // Reclaim our completed ownership before reserving the next frame.
            // Outstanding leases continue to charge the retired entry.
            retired=std::move(m_state->latest);
            m_state->latestKey.clear();
            if (retired) ++m_state->evictions;
        }
        retired.reset();
        const qint64 cost=qint64(size.width())*8*size.height()+sizeof(Entry)+
            (lut ? qint64(sizeof(LookLut))+qint64(lut->rgb.capacity())*sizeof(float):0);
        auto charge=std::make_unique<State::Charge>(m_state->accounting,cost);
        if (!charge->reserved) {
            QMutexLocker lock(&m_state->mutex);
            ++m_state->budgetBypasses; ++m_state->bypasses;
            return {};
        }
        build->entry=std::make_shared<Entry>();
        build->entry->charge=std::move(charge);
        build->entry->lut=lut;
        build->entry->image=QImage(size,QImage::Format_RGBA64);
        if (build->entry->image.isNull() || build->entry->image.sizeInBytes()!=qint64(size.width())*8*size.height()) {
            QMutexLocker lock(&m_state->mutex);
            ++m_state->allocationFailures; ++m_state->bypasses;
            return {};
        }
        if (cancelled(cancel)) {
            QMutexLocker lock(&m_state->mutex); ++m_state->cancelledRequests; return {};
        }
        return Writer(std::move(build));
    } catch (...) {
        QMutexLocker lock(&m_state->mutex);
        ++m_state->allocationFailures; ++m_state->bypasses;
        return {};
    }
}
QJsonObject AnalysisFrameCache::snapshot() const {
    QMutexLocker lock(&m_state->mutex);
    const auto charged=m_state->accounting->charged.load();
    const auto building=m_state->accounting->building.load();
    const auto cached=m_state->latest ? m_state->latest->charge->cost:0;
    const auto leased=std::max(qint64(0),charged-building-cached)+
        (m_state->latest && m_state->latest.use_count()>1 ? cached:0);
    return {{"schema",1},{"backend","cpu-analysis-frame"},{"budget_bytes",m_state->accounting->budget},
        {"charged_bytes",charged},{"cached_bytes",cached},{"building_bytes",building},
        {"leased_estimate_bytes",leased},{"peak_charged_bytes",m_state->accounting->peak.load()},
        {"entries",m_state->latest ? 1:0},{"inflight_entries",m_state->pending.size()},
        {"hits",m_state->hits},{"misses",m_state->misses},{"bypasses",m_state->bypasses},
        {"evictions",m_state->evictions},{"cancelled_requests",m_state->cancelledRequests},
        {"publications",m_state->publications},{"inflight_bypasses",m_state->inflightBypasses},
        {"budget_bypasses",m_state->budgetBypasses},{"oversize_bypasses",m_state->oversizeBypasses},
        {"disabled_bypasses",m_state->disabledBypasses},{"invalid_requests",m_state->invalidRequests},
        {"completed_bypasses",m_state->completedBypasses},
        {"stale_publications",m_state->stalePublications},{"allocation_failures",m_state->allocationFailures},
        {"accounting","RGBA64 payload, Entry, retained LUT object/numeric allocated capacity; reservations and evicted leases remain charged; allocator/key/metadata/source/geometry excluded; leased estimate overlaps cached bytes when borrowed"}};
}
