#pragma once

#include <QJsonArray>
#include <QMutex>
#include <QString>
#include <deque>

class ActionTrace {
public:
    static ActionTrace &instance();
    void record(const QString &action, const QJsonObject &details = {});
    [[nodiscard]] QJsonArray snapshot() const;

private:
    mutable QMutex m_mutex;
    std::deque<QJsonObject> m_actions;
    static constexpr qsizetype MaxActions = 1000;
};
