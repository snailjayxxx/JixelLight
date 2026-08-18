#include "diagnostics/ActionTrace.h"
#include <QDateTime>
#include <QMutexLocker>

ActionTrace &ActionTrace::instance() { static ActionTrace trace; return trace; }

void ActionTrace::record(const QString &action, const QJsonObject &details) {
    QJsonObject row = details;
    row.insert("action", action);
    row.insert("time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    QMutexLocker lock(&m_mutex);
    m_actions.push_back(row);
    while (static_cast<qsizetype>(m_actions.size()) > MaxActions) m_actions.pop_front();
}

QJsonArray ActionTrace::snapshot() const {
    QMutexLocker lock(&m_mutex);
    QJsonArray out;
    for (const auto &row : m_actions) out.append(row);
    return out;
}
