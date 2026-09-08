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
}
