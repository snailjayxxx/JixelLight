#include "diagnostics/PerformanceRecorder.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <deque>
#include <algorithm>
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
            {"note", "CPU timings measure elapsed work; GPU submit/readback are not presentation latency. No original RAW bytes included."}};
}
