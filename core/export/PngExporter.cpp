#include "core/export/PngExporter.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QImageWriter>
#include <QSaveFile>

bool exportPng16(const QImage &source, const AdjustmentState &state, const QString &path,
                 ColorManagement::OutputSpace space, const CancelToken &cancel,
                 QString *error, bool rawSource, float rawBaseExposureStops) {
    if (error) error->clear();
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (source.isNull() || cancelled(cancel)) return fail("Cancelled or no source image");
    PerformanceSpan timer("png16_export", {{"space", ColorManagement::key(space)}, {"raw", rawSource}});
    const auto plan = ProcessingPlan::compile(state, ImagePipeline::InputEncoding::LinearProPhoto,
                                              space, rawSource, rawBaseExposureStops);
    const auto rendered = ImagePipeline::processWithPlan(state.geometry.apply(source), plan, cancel);
    if (rendered.isNull() || cancelled(cancel)) return fail("Cancelled or image allocation failed");
    if (rendered.format() != QImage::Format_RGBA64) return fail("Expected 16-bit RGBA output");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    QImageWriter writer(&file, "png");
    if (!writer.write(rendered)) { file.cancelWriting(); return fail(writer.errorString()); }
    if (cancelled(cancel)) { file.cancelWriting(); return fail("Cancelled"); }
    if (!file.commit()) return fail(file.errorString());
    return true;
}
