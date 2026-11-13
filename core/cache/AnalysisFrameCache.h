#pragma once
#include "core/async/LatestJob.h"
#include "core/pipeline/AnalysisDomain.h"
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <memory>
#include <utility>

class LookLut;

// Optional completed, pre-monitor encoded RGBA64 or linear FP32 analysis frames.
// Encoded and linear handles are distinct. Admission never waits
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
    // Separate handles prevent linear frames from entering existing encoded
    // consumers. Both storage types share the original accounting/lifetime.
    class LinearLease {
    public:
        LinearLease()=default;
        explicit operator bool()const noexcept{return bool(m_lease);}
        const QImage &image()const noexcept{return m_lease.image();}
    private:
        explicit LinearLease(Lease lease):m_lease(std::move(lease)){}
        Lease m_lease;
        friend class AnalysisFrameCache;
    };
    class LinearWriter {
    public:
        LinearWriter()=default;
        LinearWriter(LinearWriter &&)noexcept=default;
        LinearWriter &operator=(LinearWriter &&)noexcept=default;
        explicit operator bool()const noexcept{return bool(m_writer);}
        bool append(const QImage &tile,int y,const CancelToken &cancel={}){return m_writer.append(tile,y,cancel);}
        bool publish(const CancelToken &cancel={}){return m_writer.publish(cancel);}
    private:
        explicit LinearWriter(Writer writer):m_writer(std::move(writer)){}
        Writer m_writer;
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
    LinearLease findLinear(const QString &key,AnalysisDomain domain,const CancelToken &cancel={});
    LinearWriter beginLinear(const QString &key,const QSize &size,AnalysisDomain domain,
        const std::shared_ptr<const LookLut> &lut={},const CancelToken &cancel={});
    QJsonObject snapshot() const;
private:
    Lease findDomain(const QString &,AnalysisDomain,const CancelToken &);
    Writer beginDomain(const QString &,const QSize &,AnalysisDomain,const std::shared_ptr<const LookLut> &,const CancelToken &);
    std::shared_ptr<State> m_state;
};
