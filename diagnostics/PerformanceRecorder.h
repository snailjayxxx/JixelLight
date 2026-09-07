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
    explicit PerformanceSpan(QString stage, QJsonObject detail = {}) : m_stage(std::move(stage)), m_detail(std::move(detail)) { m_timer.start(); }
    ~PerformanceSpan() { PerformanceRecorder::sample(m_stage, m_timer.nsecsElapsed() / 1.0e6, m_detail); }
private:
    QString m_stage;
    QJsonObject m_detail;
    QElapsedTimer m_timer;
};
