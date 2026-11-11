#pragma once
#include "core/pipeline/ProcessingPlan.h"

// Full-frame 16-bit PNG; separate from the streaming JPEG path.
bool exportPng16(const QImage &source, const AdjustmentState &state, const QString &path,
                 ColorManagement::OutputSpace space, const CancelToken &cancel,
                 QString *error, bool rawSource = false, float rawBaseExposureStops = 0);
