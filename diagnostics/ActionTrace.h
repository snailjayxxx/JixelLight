#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVariantMap>
#include <deque>
#include <type_traits>

class ActionTrace {
public:
    static ActionTrace &instance();
    void record(const QString &action, const QJsonObject &details = {});

    // Only participates in overload resolution when the caller already has a
    // concrete QVariantMap variable. Braced initializer lists cannot deduce T,
    // so the normal QJsonObject overload remains unambiguous everywhere else.
    template <typename T, std::enable_if_t<std::is_same_v<T, QVariantMap>, int> = 0>
    void record(const QString &action, const T &details) {
        record(action, QJsonObject::fromVariantMap(details));
    }

    [[nodiscard]] QJsonArray snapshot() const;

private:
    mutable QMutex m_mutex;
    std::deque<QJsonObject> m_actions;
    static constexpr qsizetype MaxActions = 1000;
};
