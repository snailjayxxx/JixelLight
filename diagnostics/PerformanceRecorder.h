#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>

class PerformanceRecorder {
public:
    static void sample(const QString &stage, double milliseconds, const QJsonObject &detail = {});
    static void count(const QString &name, qint64 delta = 1);
    static void value(const QString &name, const QJsonValue &value);
    static QJsonObject snapshot();
    static qint64 residentBytes();
};
class PerformanceSpan final {
public:
    enum class CpuObservation { Disabled, ProcessAndThread };
    explicit PerformanceSpan(QString stage, QJsonObject detail = {}, CpuObservation cpu = CpuObservation::Disabled);
    ~PerformanceSpan();
private:
    QString m_stage;
    QJsonObject m_detail;
    QElapsedTimer m_timer;
    CpuObservation m_cpuObservation;
    qint64 m_processCpuStart = -1, m_threadCpuStart = -1;
    quintptr m_cpuThread = 0;
};
