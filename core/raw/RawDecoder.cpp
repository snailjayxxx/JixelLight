#include "core/raw/RawDecoder.h"

#include <QFile>
#include <QFileInfo>
#include <QRgba64>
#include <QtGlobal>

#include <libraw/libraw.h>

#include <algorithm>

namespace {
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

QImage RawDecoder::decode(const QString &path, QString *errorMessage, RawMetadata *metadata) {
    if (errorMessage) errorMessage->clear();
    LibRaw raw;

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

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        if (errorMessage) *errorMessage = libRawError(result);
        return {};
    }

    auto &params = raw.imgdata.params;
    params.use_camera_wb = 1;
    params.use_auto_wb = 0;
    params.no_auto_bright = 1;
    params.output_color = 1;   // sRGB display working output for the alpha pipeline.
    params.output_bps = 16;
    params.user_qual = 3;      // AHD demosaic in the LibRaw/dcraw reference processor.

    result = raw.dcraw_process();
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

    const int colors = processed->colors;
    if (processed->bits == 16) {
        const auto *src = reinterpret_cast<const quint16 *>(processed->data);
        for (int y = 0; y < image.height(); ++y) {
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
    }
    LibRaw::dcraw_clear_mem(processed);
    return image;
}
