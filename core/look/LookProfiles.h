#pragma once
#include "core/pipeline/AdjustmentState.h"
#include <QVariantMap>
#include <QVariantList>
namespace LookProfiles {
QVariantList catalog();
AdjustmentState resolveAsShot(AdjustmentState state,const QVariantMap &metadata,bool raw);
AdjustmentState effective(const AdjustmentState &state);
std::array<float,4> style(const LookState &look); // mono, sepia, fade, enabled LUT
std::array<float,4> detail(const LookState &look); // amount, clarity amount, radius, clarity radius
bool active(const LookState &look);
// Fitted evidence must match the current RAW rendering engine.
bool engineCompatible(const LookState &look);
// Portable commands share the existing GUI normalization, without accepting
// arbitrary LookState/LUT JSON or mutating recorded camera metadata.
QStringList commandCodes();
QVariantList commandParameters();
bool normalizeCommandCode(const QString &input, QString *code, QString *error = nullptr);
bool normalizeCommandStrength(double input, double *strength, QString *error = nullptr);
bool normalizeCommandParameter(const QString &name, double input, double *value, QString *error = nullptr);
}
