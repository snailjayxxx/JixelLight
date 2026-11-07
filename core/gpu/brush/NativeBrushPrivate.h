#pragma once
#include "core/gpu/brush/NativeBrushSource.h"

namespace NativeBrush {
// Only this internal adapter can reserve from the original shared account.
class BudgetAccess final {
public:
    static std::shared_ptr<const void> reserve(const std::shared_ptr<BrushRenderBudget> &b, qint64 bytes) {
        return b && bytes > 0 ? b->reserve(bytes) : nullptr;
    }
};
}
