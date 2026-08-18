#pragma once

#include <QImage>
#include "core/pipeline/AdjustmentState.h"

class ImagePipeline {
public:
    enum class InputEncoding {
        SRgb,
        Linear
    };

    static QImage process(const QImage &source, const AdjustmentState &state,
                          InputEncoding inputEncoding = InputEncoding::SRgb);
};
