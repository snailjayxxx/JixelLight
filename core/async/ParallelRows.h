#pragma once
#include "core/async/LatestJob.h"
#include <QRunnable>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <atomic>

namespace ParallelRows {
inline int threadBudget() {
    bool ok = false;
    const int configured = qEnvironmentVariableIntValue("JIXELLIGHT_CPU_THREADS", &ok);
    return ok ? std::clamp(configured, 1, 32) : std::clamp(QThread::idealThreadCount() - 2, 1, 8);
}
inline QThreadPool &pool() {
    static QThreadPool pool;
    static const bool initialized = [] { pool.setMaxThreadCount(threadBudget() - 1 > 0 ? threadBudget() - 1 : 1); return true; }();
    Q_UNUSED(initialized);
    return pool;
}
// No task submitted here waits on the same pool. tryStart prevents unbounded
// nested queues when preview, full scopes and export share the CPU budget.
template<class Function>
void run(int height, int width, const CancelToken &token, Function function, bool parallel = true) {
    if (height <= 0) return;
    if (!parallel || threadBudget() <= 1 || qint64(height) * width < 262144) {
        for (int y = 0; y < height && !cancelled(token); ++y) function(y);
        return;
    }
    std::atomic_int next{0};
    QSemaphore done;
    auto work = [&] {
        while (!cancelled(token)) {
            const int start = next.fetch_add(32, std::memory_order_relaxed);
            if (start >= height) break;
            for (int y = start; y < std::min(start + 32, height) && !cancelled(token); ++y) function(y);
        }
    };
    int submitted = 0;
    for (int i = 1; i < threadBudget(); ++i) {
        auto *task = QRunnable::create([&] { work(); done.release(); });
        if (pool().tryStart(task)) ++submitted;
        else { delete task; break; }
    }
    work();
    done.acquire(submitted);
}
}
