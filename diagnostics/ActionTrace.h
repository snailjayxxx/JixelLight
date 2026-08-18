#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVariantMap>
#include <deque>

class ActionTrace {
public:
    static ActionTrace &instance();
    void record(const QString &action, const QJsonObject &details = {});
    void record(const QString &action, const QVariantMap &details) {
        record(action, QJsonObject::fromVariantMap(details));
    }
    [[nodiscard]] QJsonArray snapshot() const;

private:
    mutable QMutex m_mutex;
    std::deque<QJsonObject> m_actions;
    static constexpr qsizetype MaxActions = 1000;
};
