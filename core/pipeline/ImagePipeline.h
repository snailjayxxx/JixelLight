#pragma once

#include <QImage>
#include "core/pipeline/AdjustmentState.h"

class ImagePipeline {
public:
    static QImage process(const QImage &source, const AdjustmentState &state);
};
