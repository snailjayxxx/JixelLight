#include "core/export/PngExporter.h"
#include "core/export/RasterExporter.h"

bool exportPng16(const QImage &source, const AdjustmentState &state, const QString &path,
                 ColorManagement::OutputSpace space, const CancelToken &cancel,
                 QString *error, bool rawSource, float rawBaseExposureStops) {
    return exportRaster(source,state,path,space,RasterFormat::Png16,92,cancel,error,rawSource,rawBaseExposureStops);
}
