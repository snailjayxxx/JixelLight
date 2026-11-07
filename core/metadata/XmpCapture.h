#pragma once

#include "XmpSidecar.h"
#include "core/async/LatestJob.h"
#include <QUuid>
#include <memory>

// Owning, read-once sidecar preparation. No catalog/sidecar writes or authority
// to apply a merge. Callers must still recheck the real current target on Apply.
namespace XmpCapture {
inline constexpr qint64 MaximumBytes = 64 * 1024 * 1024;

struct Target {
    QString projectLocator, sourceKey, storageKey, copyKey, sourceIdentity;
    QUuid controller;
    quint64 catalogEpoch = 0, photoEpoch = 0, sequence = 0;
    bool valid() const;
    bool operator==(const Target &) const = default;
};
struct Request {
    Target target;
    QString sidecarPath;
    QString protectedPhotoPath;
};
struct Result;

class Captured {
public:
    bool valid() const noexcept { return bool(m_data); }
    Target target() const;
    QString locator() const;
    QByteArray sha256() const;
    qint64 byteCount() const;
    QByteArray bytes() const;
    // Value copies detach on ordinary writes. The LUT object is separately
    // cloned, so a writable alias to a returned value cannot alter the capture.
    XmpSidecar::Document document() const;
private:
    struct Data;
    std::shared_ptr<const Data> m_data;
    friend struct Result;
    friend Result capture(const Request &, const CancelToken &);
};
struct Result {
    Captured sidecar;
    QString error;
    bool cancelled = false;
    qint64 bytesRead = 0;
    int fileOpens = 0;
    bool workerThread = false;
};

// CPU worker only. Parses precisely the owned bytes being hashed, never a
// second QFile. Detects observed locator/size/time changes; this is not an OS
// filesystem snapshot against an adversary restoring all metadata mid-read.
Result capture(const Request &, const CancelToken &cancel = {});

class Job final {
public:
    using Deliver = std::function<void(const Request &, Result)>;
    explicit Job(Deliver);
    bool submit(Request);
    void cancel();
    void retire();
    bool busy() const { return m_job.busy(); }
private:
    Deliver m_deliver;
    Target m_current;
    quint64 m_lastSequence = 0;
    bool m_retired = false, m_active = false;
    LatestJob<Request, Result> m_job;
};
}
