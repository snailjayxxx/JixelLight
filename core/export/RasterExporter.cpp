#include "RasterExporter.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QImageWriter>
#include <QSaveFile>

bool exportRaster(const QImage &source, const AdjustmentState &state, const QString &path,
                  ColorManagement::OutputSpace space, RasterFormat format, int quality,
                  const CancelToken &cancel, QString *error, bool rawSource, float rawBaseExposureStops) {
    if (error) error->clear();
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (source.isNull() || cancelled(cancel)) return fail("Cancelled or no source image");
    const QByteArray codec = format == RasterFormat::Png16 ? "png" : format == RasterFormat::Tiff16 ? "tiff" : "webp";
    if (!QImageWriter::supportedImageFormats().contains(codec)) return fail("Required image format plugin is unavailable: "+codec);
    PerformanceSpan timer("raster_export", {{"format",QString::fromLatin1(codec)}, {"space",ColorManagement::key(space)}, {"raw",rawSource}});
    const auto plan = ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,rawSource,rawBaseExposureStops);
    auto rendered = ImagePipeline::processWithPlan(state.geometry.apply(source,cancel),plan,cancel);
    if (rendered.isNull() || cancelled(cancel)) return fail("Cancelled or image allocation failed");
    if (rendered.format() != QImage::Format_RGBA64) return fail("Expected 16-bit RGBA output");
    if (format == RasterFormat::Tiff16 && rendered.sizeInBytes() > qint64(0xffffffff)-16*1024*1024)
        return fail("TIFF exceeds the classic TIFF size limit; BigTIFF is not supported");
    // WebP is an explicitly 8-bit delivery format, converted only after the
    // wide-gamut target transform. TIFF and PNG retain every 16-bit channel.
    if (format == RasterFormat::WebP8) rendered = rendered.convertToFormat(QImage::Format_RGBA8888);
    if (rendered.isNull()) return fail("Cannot allocate output image");
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    QImageWriter writer(&file,codec);
    if (format == RasterFormat::Tiff16) writer.setCompression(1); // Qt TIFF: lossless LZW
    if (format == RasterFormat::WebP8) writer.setQuality(std::clamp(quality,1,100)); // 100 is lossless
    if (!writer.write(rendered)) { file.cancelWriting(); return fail(writer.errorString()); }
    if (cancelled(cancel)) { file.cancelWriting(); return fail("Cancelled"); }
    if (!file.commit()) return fail(file.errorString());
    return true;
}
