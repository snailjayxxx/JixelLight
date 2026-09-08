#pragma once

#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

using CancelToken = std::shared_ptr<std::atomic_bool>;
inline bool cancelled(const CancelToken &token) {
    return token && token->load(std::memory_order_relaxed);
}

// GUI-thread facade. At most one running request and ONE replaceable pending
// request. Worker callbacks receive immutable snapshots, never a controller.
// Destruction cancels and joins before any captured state can be destroyed.
template<class Request, class Result>
class LatestJob final {
public:
    using Work = std::function<Result(const Request &, const CancelToken &)>;
    using Deliver = std::function<void(const Request &, Result)>;

    LatestJob(Work work, Deliver deliver) : m_work(std::move(work)), m_deliver(std::move(deliver)) {
        m_pool.setMaxThreadCount(1);
        QObject::connect(&m_watcher, &QFutureWatcher<Result>::finished, &m_context, [this] {
            const bool valid = m_active && m_active->sequence == m_sequence && !cancelled(m_cancel);
            auto active = std::move(m_active);
            m_active.reset();
            m_running = false;
            if (valid) m_deliver(active->request, m_watcher.result());
            if (!m_running && m_pending) start();
        });
    }
    ~LatestJob() {
        QObject::disconnect(&m_watcher, nullptr, &m_context, nullptr);
        cancel();
        m_watcher.waitForFinished();
        m_pool.waitForDone();
    }
    LatestJob(const LatestJob &) = delete;
    LatestJob &operator=(const LatestJob &) = delete;

    void submit(Request request) {
        if (m_cancel) m_cancel->store(true, std::memory_order_relaxed);
        m_pending = Pending{++m_sequence, std::move(request)};
        if (!m_running) start();
    }
    void cancel() {
        ++m_sequence;
        m_pending.reset();
        if (m_cancel) m_cancel->store(true, std::memory_order_relaxed);
    }
    bool busy() const { return m_running || m_pending.has_value(); }
    quint64 sequence() const { return m_sequence; }

private:
    struct Pending { quint64 sequence; Request request; };
    void start() {
        m_active = std::move(m_pending);
        m_pending.reset();
        m_cancel = std::make_shared<std::atomic_bool>(false);
        m_running = true;
        const auto request = m_active->request;
        const auto token = m_cancel;
        const auto work = m_work;
        m_watcher.setFuture(QtConcurrent::run(&m_pool, [work, request, token] { try { return work(request, token); } catch (...) { return Result{}; } }));
    }
    QObject m_context;
    QThreadPool m_pool;
    QFutureWatcher<Result> m_watcher;
    Work m_work;
    Deliver m_deliver;
    std::optional<Pending> m_active, m_pending;
    CancelToken m_cancel;
    quint64 m_sequence = 0;
    bool m_running = false;
};
