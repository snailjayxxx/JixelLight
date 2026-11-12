#include "diagnostics/PerformanceRecorder.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <deque>
#include <algorithm>
#include <cerrno>
#include <limits>
#include <time.h>
#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif
namespace {
QMutex mutex;
std::deque<QJsonObject> samples;
QJsonObject counters, values;

qint64 cpuNanoseconds(bool process) {
#if defined(Q_OS_WIN)
    FILETIME created{}, exited{}, kernel{}, user{};
    const auto previousError = GetLastError();
    const bool ok = process ? GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)
                            : GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
    SetLastError(previousError);
    if (!ok) return -1;
    const auto ticks = [](const FILETIME &time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    const auto kernelTicks = ticks(kernel), userTicks = ticks(user);
    constexpr auto limit = quint64((std::numeric_limits<qint64>::max)() / 100);
    if (kernelTicks > limit || userTicks > limit - kernelTicks) return -1;
    return qint64((kernelTicks + userTicks) * 100);
#elif defined(Q_OS_UNIX) && defined(CLOCK_PROCESS_CPUTIME_ID) && defined(CLOCK_THREAD_CPUTIME_ID)
    timespec value{};
    const auto previousError = errno;
    const auto result = clock_gettime(process ? CLOCK_PROCESS_CPUTIME_ID : CLOCK_THREAD_CPUTIME_ID, &value);
    errno = previousError;
    if (result != 0
        || value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec >= 1000000000) return -1;
    if (quint64(value.tv_sec) > quint64(((std::numeric_limits<qint64>::max)() - value.tv_nsec) / 1000000000)) return -1;
    return qint64(value.tv_sec) * 1000000000 + value.tv_nsec;
#else
    Q_UNUSED(process);
    return -1;
#endif
}
QJsonValue cpuDelta(qint64 start, qint64 end) {
    return start >= 0 && end >= start ? QJsonValue(double(end - start) / 1.0e6) : QJsonValue(QJsonValue::Null);
}
}
PerformanceSpan::PerformanceSpan(QString stage, QJsonObject detail, CpuObservation cpu)
    : m_stage(std::move(stage)), m_detail(std::move(detail)), m_cpuObservation(cpu) {
    m_timer.start();
    if (cpu == CpuObservation::ProcessAndThread) {
        m_cpuThread = reinterpret_cast<quintptr>(QThread::currentThreadId());
        m_processCpuStart = cpuNanoseconds(true);
        m_threadCpuStart = cpuNanoseconds(false);
    }
}
PerformanceSpan::~PerformanceSpan() {
    const double elapsed = m_timer.nsecsElapsed() / 1.0e6;
    if (m_cpuObservation == CpuObservation::ProcessAndThread) {
        m_detail["process_cpu_ms"] = cpuDelta(m_processCpuStart, cpuNanoseconds(true));
        const auto sameThread = m_cpuThread == reinterpret_cast<quintptr>(QThread::currentThreadId());
        m_detail["thread_cpu_ms"] = cpuDelta(m_threadCpuStart, sameThread ? cpuNanoseconds(false) : -1);
        m_detail["cpu_time_scope"] = "process-all-threads-and-span-thread";
    }
    PerformanceRecorder::sample(m_stage, elapsed, m_detail);
}
void PerformanceRecorder::sample(const QString &stage, double ms, const QJsonObject &detail) {
    QJsonObject entry = detail;
    entry["stage"] = stage;
    entry["duration_ms"] = ms;
    entry["utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QMutexLocker lock(&mutex);
    samples.push_back(entry);
    if (samples.size() > 2048) samples.pop_front();
}
void PerformanceRecorder::count(const QString &name, qint64 delta) {
    QMutexLocker lock(&mutex);
    counters[name] = counters.value(name).toDouble() + double(delta);
}
void PerformanceRecorder::value(const QString &name, const QJsonValue &value) {
    QMutexLocker lock(&mutex);
    values[name] = value;
}
qint64 PerformanceRecorder::residentBytes() {
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS c{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c)) ? qint64(c.WorkingSetSize) : 0;
#elif defined(Q_OS_MACOS)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t size = MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &size) == KERN_SUCCESS ? qint64(info.resident_size) : 0;
#else
    QFile file(QStringLiteral("/proc/self/statm"));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const auto columns = file.readAll().simplified().split(' ');
    return columns.size() > 1 ? columns.at(1).toLongLong() * sysconf(_SC_PAGESIZE) : 0;
#endif
}
QJsonObject PerformanceRecorder::snapshot() {
    const qint64 rss = residentBytes();
    QMutexLocker lock(&mutex);
    QJsonArray array;
    for (const auto &entry : samples) array.append(entry);
    return {{"schema", 1}, {"timings", array}, {"counters", counters}, {"values", values},
            {"resident_bytes_at_snapshot", rss}, {"logical_processors", QThread::idealThreadCount()},
            {"note", "duration_ms measures elapsed work. Optional process_cpu_ms includes concurrent process threads; thread_cpu_ms covers the span thread. Null means unavailable. CPU/elapsed gaps do not isolate a waiting cause. GPU submit/readback are not presentation latency. No original RAW bytes included."}};
}
