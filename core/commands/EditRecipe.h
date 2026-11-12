#pragma once
#include "core/pipeline/AdjustmentState.h"

namespace EditRecipe {
inline constexpr qsizetype MaximumTextLength=1048576;
enum class Error { None, UnsupportedLook, UnrepresentableState, TooLong };
struct Result {
    Error error=Error::None;
    QString text;
    qsizetype commandCount=0;
    explicit operator bool() const { return error==Error::None; }
};
// A complete replacement recipe using the existing public editing commands.
// As-shot remains symbolic and resolves from the destination RAW's metadata.
Result encode(const AdjustmentState &state,bool rawSource);
}
