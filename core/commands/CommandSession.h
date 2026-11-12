#pragma once
#include "core/commands/CommandRegistry.h"
#include <QVector>
#include <functional>
#include <optional>

// A transient offline session. It never writes a catalog or opens an output.
// The owner supplies source facts and publishes exports only after whole-plan
// validation. Editing uses the same registry and history as the desktop.
struct CommandSession {
    struct Export { QString photo,source,destination,space; AdjustmentState state; };
    using SourceContext=std::function<std::optional<CommandRegistry::LookContext>(const QString &,bool metadata,QString *)>;
    QVector<Export> exports;
    int photoCount=0,commandCount=0;
    qint64 chargedStateBytes=0;
    static constexpr qint64 MaximumStateBytes=64*1024*1024;
    static bool compile(const QJsonObject &,const QString &baseDirectory,const SourceContext &,CommandSession *,QString *error=nullptr);
    static QJsonObject schema();
};
