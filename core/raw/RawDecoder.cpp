#include "core/raw/RawDecoder.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QSemaphore>
#include <algorithm>

#include <QFile>
#include <QBuffer>
#include <QImageReader>
#include <QFileInfo>
#include <QRgba64>
#include <QtGlobal>

#include <libraw/libraw.h>

namespace {
QSemaphore rawSlots(1);
struct RawLease {
    bool acquired = false;
    explicit RawLease(const CancelToken &token) {
        while (!cancelled(token)) if (rawSlots.tryAcquire(1, 20)) { acquired = true; break; }
    }
    ~RawLease() { if (acquired) rawSlots.release(); }
};
int rawProgress(void *context, enum LibRaw_progress, int, int) {
    return cancelled(*static_cast<const CancelToken *>(context)) ? 1 : 0;
}

QString libRawError(int code) {
    const char *message = LibRaw::strerror(code);
    return message ? QString::fromLatin1(message) : QStringLiteral("Unknown LibRaw error");
}
}

bool RawDecoder::isRawFile(const QString &path) {
    static const QStringList extensions = {
        QStringLiteral("arw"), QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("crw"),
        QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("raf"), QStringLiteral("rw2"),
        QStringLiteral("orf"), QStringLiteral("dng"), QStringLiteral("pef"), QStringLiteral("srw"),
        QStringLiteral("rwl"), QStringLiteral("3fr"), QStringLiteral("erf"), QStringLiteral("kdc"),
        QStringLiteral("mos"), QStringLiteral("mrw"), QStringLiteral("x3f"), QStringLiteral("iiq"),
        QStringLiteral("raw")
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

QImage RawDecoder::decode(const QString &path, QString *errorMessage, RawMetadata *metadata, const CancelToken &cancel) {
    RawLease lease(cancel);
    if (!lease.acquired) return {};
    PerformanceSpan total(QStringLiteral("raw_total"));
    if (errorMessage) errorMessage->clear();
    LibRaw raw;
    raw.set_progress_handler(rawProgress, const_cast<CancelToken *>(&cancel));

    int result = LIBRAW_SUCCESS;
#if defined(Q_OS_WIN)
    result = raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    const QByteArray encodedPath = QFile::encodeName(path);
    result = raw.open_file(encodedPath.constData());
#endif
    if (result != LIBRAW_SUCCESS) {
        if (errorMessage) *errorMessage = libRawError(result);
        return {};
    }

    if (metadata) {
        metadata->make = QString::fromLatin1(raw.imgdata.idata.make).trimmed();
        metadata->model = QString::fromLatin1(raw.imgdata.idata.model).trimmed();
        metadata->width = raw.imgdata.sizes.width;
        metadata->height = raw.imgdata.sizes.height;
    }

    if (cancelled(cancel)) return {};
    const quint64 pixels = quint64(raw.imgdata.sizes.raw_width) * raw.imgdata.sizes.raw_height;
    // Reject unreasonable allocations before unpack; this is an application resource limit, not a camera-format claim.
    const quint64 limit = quint64(std::max(256, qEnvironmentVariableIntValue("JIXELLIGHT_RAW_MEMORY_MB"))) * 1024 * 1024;
    const quint64 effectiveLimit = qEnvironmentVariableIsSet("JIXELLIGHT_RAW_MEMORY_MB") ? limit : 3ULL*1024*1024*1024;
    if (pixels > effectiveLimit / 32) { if (errorMessage) *errorMessage = QStringLiteral("RAW exceeds the configured decode memory budget"); return {}; }
    { PerformanceSpan timer(QStringLiteral("raw_unpack")); result = raw.unpack(); }
    if (result != LIBRAW_SUCCESS) {
        if (errorMessage) *errorMessage = libRawError(result);
        return {};
    }

    auto &params = raw.imgdata.params;
    params.use_camera_wb = 1;
    params.use_auto_wb = 0;
    params.use_camera_matrix = 3; // Prefer embedded/built-in camera color data regardless of WB mode.
    params.no_auto_bright = 1;
    params.adjust_maximum_thr = 0.0f;
    params.bright = 1.0f;
    params.highlight = 2;         // LibRaw highlight blend before our scene-linear tone stage.
    params.output_color = 4;      // ProPhoto RGB primaries (D50), kept linear with gamm below.
    params.output_bps = 16;
    params.user_qual = 3;         // AHD demosaic reference path.
    params.gamm[0] = 1.0;
    params.gamm[1] = 1.0;

    if (cancelled(cancel)) return {};
    { PerformanceSpan timer(QStringLiteral("raw_develop")); result = raw.dcraw_process(); }
    if (result != LIBRAW_SUCCESS) {
        if (errorMessage) *errorMessage = libRawError(result);
        return {};
    }

    int memoryError = LIBRAW_SUCCESS;
    libraw_processed_image_t *processed = raw.dcraw_make_mem_image(&memoryError);
    if (!processed || memoryError != LIBRAW_SUCCESS) {
        if (errorMessage) *errorMessage = libRawError(memoryError);
        if (processed) LibRaw::dcraw_clear_mem(processed);
        return {};
    }

    if (processed->type != LIBRAW_IMAGE_BITMAP || processed->colors < 3 || processed->width == 0 || processed->height == 0) {
        if (errorMessage) *errorMessage = QStringLiteral("LibRaw returned an unsupported bitmap layout");
        LibRaw::dcraw_clear_mem(processed);
        return {};
    }

    QImage image(static_cast<int>(processed->width), static_cast<int>(processed->height), QImage::Format_RGBA64);
    if (image.isNull()) {
        if (errorMessage) *errorMessage = QStringLiteral("Unable to allocate RAW image buffer");
        LibRaw::dcraw_clear_mem(processed);
        return {};
    }
    image.setText(QStringLiteral("JixelLightWorkingSpace"), QStringLiteral("Linear ProPhoto RGB"));
    image.setText(QStringLiteral("JixelLightSource"), QStringLiteral("RAW"));

    const int colors = processed->colors;
    if (processed->bits == 16) {
        const auto *src = reinterpret_cast<const quint16 *>(processed->data);
        for (int y = 0; y < image.height(); ++y) {
            if (cancelled(cancel)) { LibRaw::dcraw_clear_mem(processed); return {}; }
            auto *dst = reinterpret_cast<QRgba64 *>(image.scanLine(y));
            const qsizetype rowBase = static_cast<qsizetype>(y) * image.width() * colors;
            for (int x = 0; x < image.width(); ++x) {
                const qsizetype i = rowBase + static_cast<qsizetype>(x) * colors;
                dst[x] = QRgba64::fromRgba64(src[i], src[i + 1], src[i + 2], 65535);
            }
        }
    } else if (processed->bits == 8) {
        const auto *src = reinterpret_cast<const quint8 *>(processed->data);
        for (int y = 0; y < image.height(); ++y) {
            if (cancelled(cancel)) { LibRaw::dcraw_clear_mem(processed); return {}; }
            auto *dst = reinterpret_cast<QRgba64 *>(image.scanLine(y));
            const qsizetype rowBase = static_cast<qsizetype>(y) * image.width() * colors;
            for (int x = 0; x < image.width(); ++x) {
                const qsizetype i = rowBase + static_cast<qsizetype>(x) * colors;
                dst[x] = QRgba64::fromRgba64(src[i] * 257u, src[i + 1] * 257u, src[i + 2] * 257u, 65535);
            }
        }
    } else {
        if (errorMessage) *errorMessage = QStringLiteral("Unsupported LibRaw output bit depth: %1").arg(processed->bits);
        LibRaw::dcraw_clear_mem(processed);
        return {};
    }

    if (metadata) {
        metadata->width = image.width();
        metadata->height = image.height();
        metadata->bitsPerChannel = processed->bits;
        metadata->workingSpace = QStringLiteral("Linear ProPhoto RGB");
        metadata->demosaic = QStringLiteral("AHD");
        metadata->cameraMatrixEnabled = true;
        metadata->cameraWhiteBalanceEnabled = true;
        metadata->highlightBlendEnabled = true;
    }

    LibRaw::dcraw_clear_mem(processed);
    return image;
}

QImage RawDecoder::thumbnail(const QString &path, const CancelToken &cancel) {
    if (cancelled(cancel)) return {};
    PerformanceSpan timer(QStringLiteral("raw_embedded_preview"));
    LibRaw raw;
    raw.set_progress_handler(rawProgress, const_cast<CancelToken *>(&cancel));
#if defined(Q_OS_WIN)
    int result = raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    const QByteArray encoded = QFile::encodeName(path);
    int result = raw.open_file(encoded.constData());
#endif
    if (result != LIBRAW_SUCCESS || cancelled(cancel) || raw.unpack_thumb() != LIBRAW_SUCCESS) return {};
    int error = LIBRAW_SUCCESS;
    libraw_processed_image_t *thumb = raw.dcraw_make_mem_thumb(&error);
    if (!thumb || error != LIBRAW_SUCCESS) { if (thumb) LibRaw::dcraw_clear_mem(thumb); return {}; }
    QImage image;
    if (thumb->type == LIBRAW_IMAGE_JPEG) {
        const QByteArray jpeg=QByteArray::fromRawData(reinterpret_cast<const char *>(thumb->data),int(thumb->data_size));
        QBuffer buffer;buffer.setData(jpeg);buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer,"JPEG");reader.setAutoTransform(true);image=reader.read();
    }
    else if (thumb->type == LIBRAW_IMAGE_BITMAP && thumb->colors == 3 && thumb->bits == 8)
        image = QImage(thumb->data, thumb->width, thumb->height, thumb->width*3, QImage::Format_RGB888).copy();
    LibRaw::dcraw_clear_mem(thumb);
    if (cancelled(cancel)) return {};
    // A placeholder only. Do not run RAW adjustments or scopes on this image.
    if (image.width() > 2048 || image.height() > 2048) image = image.scaled(2048,2048,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    return image;
}
