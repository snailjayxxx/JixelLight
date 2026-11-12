#pragma once

#include <QImage>
#include <QString>
#include <QJsonObject>
#include "core/pipeline/AdjustmentState.h"
#include "diagnostics/DiagnosticReplay.h"

class DiagnosticBundle {
public:
    static QString create(const QImage &preview, const QString &currentFile,
                          const QString &projectPath, const AdjustmentState &state,
                          double shadowClip, double highlightClip,
                          const QString &pipelineDescription = QString(), const QJsonObject &scopeContext = {}, const QJsonObject &stageOutputs = {},
                          const DiagnosticReplay::Snapshot &replay = {});
};
