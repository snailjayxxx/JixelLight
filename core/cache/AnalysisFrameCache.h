#pragma once
#include "core/async/LatestJob.h"
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <memory>

class LookLut;

// Optional completed, pre-monitor RGBA64 analysis frames. Admission never waits
// for another producer. The budget includes reservations and evicted entries
// still held by consumers; it is independent of the other image caches.
class AnalysisFrameCache final {
    struct Entry;
    struct State;
    struct Build;
public:
    class Lease {
    public:
        Lease() = default;
        explicit operator bool() const noexcept;
        // Keep this lease alive throughout use. Do not retain a shallow QImage
        // copy/reference beyond the lease: that would escape budget ownership.
        const QImage &image() const noexcept;
    private:
        explicit Lease(std::shared_ptr<const Entry> entry);
        std::shared_ptr<const Entry> m_entry;
        friend class AnalysisFrameCache;
    };
    class Writer {
    public:
        Writer() noexcept;
        ~Writer();
        Writer(Writer &&) noexcept;
        Writer &operator=(Writer &&) noexcept;
        Writer(const Writer &) = delete;
        Writer &operator=(const Writer &) = delete;
        explicit operator bool() const noexcept;
        bool append(const QImage &rgba64Tile, int y, const CancelToken &cancel = {});
        bool publish(const CancelToken &cancel = {});
    private:
        explicit Writer(std::unique_ptr<Build> build) noexcept;
        void fail(bool wasCancelled = false, bool malformed = false,
                  bool allocationFailure = false) noexcept;
        std::unique_ptr<Build> m_build;
        friend class AnalysisFrameCache;
    };

    explicit AnalysisFrameCache(qint64 budgetBytes = 320 * 1024 * 1024);
    ~AnalysisFrameCache();
    AnalysisFrameCache(const AnalysisFrameCache &) = delete;
    AnalysisFrameCache &operator=(const AnalysisFrameCache &) = delete;
    Lease find(const QString &key, const CancelToken &cancel = {});
    Writer begin(const QString &key, const QSize &postGeometrySize,
                 const std::shared_ptr<const LookLut> &lut = {},
                 const CancelToken &cancel = {});
    QJsonObject snapshot() const;
private:
    std::shared_ptr<State> m_state;
};
