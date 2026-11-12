#pragma once
#include "core/pipeline/AdjustmentState.h"
#include <QByteArray>

namespace DiagnosticReplay {
class Snapshot {
public:
    const QByteArray &session() const {return m_session;}
    const QJsonObject &metadata() const {return m_metadata;}
    bool available() const {return !m_session.isEmpty();}
private:
    QByteArray m_session;
    QJsonObject m_metadata{{"schema",1},{"available",false},{"reason","stored_intent_not_supplied"}};
    friend Snapshot capture(const AdjustmentState &,bool,const QString &,const QString &,quint64,quint64);
};
// No image, metadata or filesystem reads. Capture at the initial diagnostic
// request, not after an asynchronous GPU response or a selection change.
Snapshot capture(const AdjustmentState &storedIntent,bool rawSource,const QString &source,
                 const QString &versionKey,quint64 photoEpoch,quint64 revision);
}
