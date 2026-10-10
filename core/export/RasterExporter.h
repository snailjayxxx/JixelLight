#pragma once
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"

enum class RasterFormat { Png16, Tiff16, WebP8 };
bool exportRaster(const QImage &source, const AdjustmentState &state, const QString &path,
                  ColorManagement::OutputSpace space, RasterFormat format, int quality,
                  const CancelToken &cancel, QString *error, bool rawSource = false, float rawBaseExposureStops = 0);
