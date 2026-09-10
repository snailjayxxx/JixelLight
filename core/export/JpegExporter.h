#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include <functional>

// Stream 128-row rendered strips to libjpeg; no full-size rendered RGB copy.
bool exportJpegTiled(const QImage &linearSource, const AdjustmentState &state, const QString &path,
                     ColorManagement::OutputSpace space, int quality, const CancelToken &cancel,
                     QString *error, const std::function<void(int)> &progress = {},
                     const std::shared_ptr<std::atomic_bool> &interactive = {},
                     bool rawSource = false, float rawBaseExposureStops = 0.0f);
