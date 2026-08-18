#pragma once

#include <QImage>
#include <QString>
#include "core/pipeline/AdjustmentState.h"

class DiagnosticBundle {
public:
    static QString create(const QImage &preview, const QString &currentFile,
                          const QString &projectPath, const AdjustmentState &state,
                          double shadowClip, double highlightClip);
};
