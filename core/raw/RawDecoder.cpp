#include "core/raw/RawDecoder.h"
#include "core/raw/RawCameraProfiles.h"
#include "core/raw/RawContainerMetadata.h"
#include "core/raw/RawGeometry.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QSemaphore>
#include <algorithm>
#include <cmath>
#include <memory>

#include <QFile>
#include <QBuffer>
#include <QImageReader>
#include <QFileInfo>
#include <QRgba64>
#include <QtGlobal>

#include <libraw/libraw.h>

namespace {
QSemaphore rawSlots(1);

// LibRaw keeps the camera->RGB derivation helper protected. A camera profile
// sourced independently from public characterization data must enter through
// the same cam_xyz_coeff path used by LibRaw's own Adobe coefficient table.
class JixelLibRaw final : public LibRaw {
public:
    using LibRaw::cam_xyz_coeff;
};

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

bool usableWhiteBalance(const float mul[4]) {
    for (int c = 0; c < 3; ++c)
        if (!std::isfinite(mul[c]) || mul[c] <= 0.00001f || mul[c] > 100000.0f) return false;
    return true;
}

bool usableCameraMatrix(const float matrix[3][4]) {
    double energy = 0.0, identityError = 0.0;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) {
        if (!std::isfinite(matrix[r][c])) return false;
        energy += std::abs(matrix[r][c]);
        identityError += std::abs(matrix[r][c] - (r == c ? 1.0f : 0.0f));
    }
    // LibRaw initializes rgb_cam to identity. Identity therefore means that no
    // camera characterization has been installed, not that the sensor happens
    // to have an identity spectral response.
    return energy > 0.05 && identityError > 0.0001;
}

void applyCameraProfile(JixelLibRaw &raw, const RawCameraProfile &profile) {
    double camXyz[4][3]{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c) {
        const double value = profile.cameraToXyz[std::size_t(r * 3 + c)];
        camXyz[r][c] = value;
        raw.imgdata.color.cam_xyz[r][c] = float(value);
    }
    raw.cam_xyz_coeff(raw.imgdata.color.rgb_cam, camXyz);
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
    auto decoder = std::make_unique<JixelLibRaw>();
    auto &raw = *decoder;
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

    const QString make = QString::fromLatin1(raw.imgdata.idata.make).trimmed();
    const QString model = QString::fromLatin1(raw.imgdata.idata.model).trimmed();
    const auto cameraProfile = RawCameraProfiles::find(make, model);
    const RawContainerInfo containerInfo = cameraProfile ? RawContainerMetadata::read(path) : RawContainerInfo{};

    if (metadata) {
        metadata->make = make;
        metadata->model = model;
        metadata->width = raw.imgdata.sizes.width;
        metadata->height = raw.imgdata.sizes.height;
    }

    if (cancelled(cancel)) return {};
    const quint64 pixels = quint64(raw.imgdata.sizes.raw_width) * raw.imgdata.sizes.raw_height;
    const quint64 limit = quint64(std::max(256, qEnvironmentVariableIntValue("JIXELLIGHT_RAW_MEMORY_MB"))) * 1024 * 1024;
    const quint64 effectiveLimit = qEnvironmentVariableIsSet("JIXELLIGHT_RAW_MEMORY_MB") ? limit : 3ULL*1024*1024*1024;
    if (pixels > effectiveLimit / 32) { if (errorMessage) *errorMessage = QStringLiteral("RAW exceeds the configured decode memory budget"); return {}; }
    { PerformanceSpan timer(QStringLiteral("raw_unpack")); result = raw.unpack(); }
    if (result != LIBRAW_SUCCESS) {
        if (errorMessage) *errorMessage = libRawError(result);
        return {};
    }

    const auto decodeSizes = raw.imgdata.sizes;
    auto &params = raw.imgdata.params;

    int calibratedBlack = -1, calibratedWhite = -1;
    bool profileApplied = false;
    QString profileSource, cropSource;
    if (cameraProfile) {
        applyCameraProfile(raw, *cameraProfile);
        calibratedBlack = containerInfo.blackLevel >= 0 ? containerInfo.blackLevel : cameraProfile->blackLevel;
        calibratedWhite = containerInfo.whiteLevel > 0 ? containerInfo.whiteLevel : cameraProfile->whiteLevel;
        if (calibratedBlack >= 0) {
            params.user_black = calibratedBlack;
            // A model profile supplies one common black level. Clear any stale
            // per-channel/pattern corrections from an unsupported-camera parse
            // so LibRaw subtracts exactly that characterized black point.
            for (int c = 0; c < 4; ++c) params.user_cblack[c] = 0;
        }
        if (calibratedWhite > calibratedBlack) {
            // LibRaw's raw2image path subtracts C.black and reduces C.maximum
            // before dcraw_process applies user_sat. Therefore -S/user_sat is
            // expressed in the post-black domain, not the original sensor code.
            params.user_sat = calibratedBlack >= 0 ? calibratedWhite - calibratedBlack : calibratedWhite;
        }
        profileApplied = true;
        profileSource = cameraProfile->provenance;

        // For cameras not yet characterized by the pinned LibRaw release, use
        // the RAW file's own DNG/TIFF DefaultCrop tags. Never infer a centered
        // crop: the ILCE-7RM6 sample, for example, is intentionally asymmetric.
        if (containerInfo.defaultCrop.isValid()) {
            QRect crop = containerInfo.defaultCrop.translated(-int(decodeSizes.left_margin), -int(decodeSizes.top_margin));
            const QRect visibleBounds(QPoint(0, 0), QSize(decodeSizes.width, decodeSizes.height));
            if (visibleBounds.contains(crop)) {
                params.cropbox[0] = crop.x();
                params.cropbox[1] = crop.y();
                params.cropbox[2] = crop.width();
                params.cropbox[3] = crop.height();
                cropSource = containerInfo.cropSource;
            }
        }
    }

    const bool wbAvailable = usableWhiteBalance(raw.imgdata.color.cam_mul);
    const bool matrixAvailable = profileApplied || usableCameraMatrix(raw.imgdata.color.rgb_cam);
    params.use_camera_wb = wbAvailable ? 1 : 0;
    params.use_auto_wb = 0;
    params.use_camera_matrix = 3;
    params.no_auto_bright = 1;
    params.adjust_maximum_thr = 0.75f;
    params.bright = 1.0f;
    params.highlight = 1;
    params.output_color = 4;
    params.output_bps = 16;
    params.user_qual = 3;
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
    image.setText(QStringLiteral("JixelLightLibRawHighlightMode"), QStringLiteral("1 / unclip"));
    image.setText(QStringLiteral("JixelLightAdjustMaximumThreshold"), QStringLiteral("0.75"));
    if (profileApplied) {
        image.setText(QStringLiteral("JixelLightCameraProfile"), profileSource);
        image.setText(QStringLiteral("JixelLightCalibratedBlackLevel"), QString::number(calibratedBlack));
        image.setText(QStringLiteral("JixelLightCalibratedWhiteLevel"), QString::number(calibratedWhite));
    }
    if (!cropSource.isEmpty()) image.setText(QStringLiteral("JixelLightDefaultCropApplied"), cropSource);

    // Supported cameras may expose an inset without asking LibRaw to crop it.
    // Preserve that geometry for reference alignment. Profiled unsupported
    // cameras above are already physically cropped before demosaic/output.
    if (cropSource.isEmpty() && make.contains(QStringLiteral("SONY"), Qt::CaseInsensitive)) {
        const auto c = decodeSizes.raw_inset_crops[0];
        if (c.cleft < 65535 && c.ctop < 65535 && c.cwidth > 0 && c.cheight > 0
            && quint64(c.cleft) + c.cwidth <= decodeSizes.raw_width && quint64(c.ctop) + c.cheight <= decodeSizes.raw_height) {
            const QRect relative(int(c.cleft) - int(decodeSizes.left_margin), int(c.ctop) - int(decodeSizes.top_margin), c.cwidth, c.cheight);
            const auto roi = RawGeometry::orientCrop(QSize(decodeSizes.width, decodeSizes.height), relative, decodeSizes.flip, image.size());
            if (roi.isValid()) image.setText(QStringLiteral("JixelLightCameraCrop"),
                QStringLiteral("%1,%2,%3,%4").arg(roi.x()).arg(roi.y()).arg(roi.width()).arg(roi.height()));
        }
    }

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
        metadata->cameraWhiteBalanceEnabled = wbAvailable;
        metadata->cameraMatrixAvailable = matrixAvailable;
        metadata->cameraWhiteBalanceAvailable = wbAvailable;
        metadata->cameraProfileApplied = profileApplied;
        metadata->cameraProfileSource = profileSource;
        metadata->calibratedBlackLevel = calibratedBlack;
        metadata->calibratedWhiteLevel = calibratedWhite;
        metadata->defaultCropSource = cropSource;
        metadata->highlightBlendEnabled = false;
        metadata->highlightMode = 1;
        metadata->adjustMaximumThreshold = 0.75f;
    }

    LibRaw::dcraw_clear_mem(processed);
    return image;
}

QImage RawDecoder::thumbnail(const QString &path, const CancelToken &cancel) {
    if (cancelled(cancel)) return {};
    PerformanceSpan timer(QStringLiteral("raw_embedded_preview"));
    auto decoder = std::make_unique<JixelLibRaw>();
    auto &raw = *decoder;
    raw.set_progress_handler(rawProgress, const_cast<CancelToken *>(&cancel));
#if defined(Q_OS_WIN)
    int result = raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    const QByteArray encoded = QFile::encodeName(path);
    int result = raw.open_file(encoded.constData());
#endif
    if (result != LIBRAW_SUCCESS || cancelled(cancel)) return {};

    int selected = -1;
    quint64 largestArea = 0;
    for (int i = 0; i < raw.imgdata.thumbs_list.thumbcount && i < LIBRAW_THUMBNAIL_MAXCOUNT; ++i) {
        const auto &item = raw.imgdata.thumbs_list.thumblist[i];
        if (item.tformat != LIBRAW_INTERNAL_THUMBNAIL_JPEG || !item.twidth || !item.theight || !item.tlength) continue;
        // A corrupt length must not make the preview path allocate/read an
        // arbitrary region. Real full-resolution camera JPEGs are far smaller.
        if (item.tlength > 128u * 1024u * 1024u) continue;
        const quint64 area = quint64(item.twidth) * item.theight;
        if (area > largestArea) { largestArea = area; selected = i; }
    }

    result = selected >= 0 ? raw.unpack_thumb_ex(selected) : raw.unpack_thumb();
    if (result != LIBRAW_SUCCESS && selected >= 0) {
        selected = -1;
        result = raw.unpack_thumb();
    }
    if (result != LIBRAW_SUCCESS || cancelled(cancel)) return {};

    int error = LIBRAW_SUCCESS;
    libraw_processed_image_t *thumb = raw.dcraw_make_mem_thumb(&error);
    if (!thumb || error != LIBRAW_SUCCESS) { if (thumb) LibRaw::dcraw_clear_mem(thumb); return {}; }
    QImage image;
    QSize sourceSize;
    bool orientationApplied = false;
    if (thumb->type == LIBRAW_IMAGE_JPEG) {
        const QByteArray jpeg = QByteArray::fromRawData(reinterpret_cast<const char *>(thumb->data), int(thumb->data_size));
        QBuffer buffer; buffer.setData(jpeg); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPEG");
        reader.setAutoTransform(true);
        sourceSize = reader.size();
        if (sourceSize.isValid() && (sourceSize.width() > 2048 || sourceSize.height() > 2048))
            reader.setScaledSize(sourceSize.scaled(2048, 2048, Qt::KeepAspectRatio));
        image = reader.read();
        orientationApplied = reader.transformation() != QImageIOHandler::TransformationNone;
    } else if (thumb->type == LIBRAW_IMAGE_BITMAP && thumb->colors == 3 && thumb->bits == 8) {
        sourceSize = QSize(int(thumb->width), int(thumb->height));
        image = QImage(thumb->data, thumb->width, thumb->height, thumb->width * 3, QImage::Format_RGB888).copy();
    }
    LibRaw::dcraw_clear_mem(thumb);
    if (cancelled(cancel)) return {};
    if (image.width() > 2048 || image.height() > 2048) image = image.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!image.isNull()) {
        image.setText(QStringLiteral("JixelLightThumbnailOrientationApplied"), orientationApplied ? QStringLiteral("true") : QStringLiteral("false"));
        image.setText(QStringLiteral("JixelLightThumbnailIndex"), QString::number(selected));
        if (sourceSize.isValid()) image.setText(QStringLiteral("JixelLightThumbnailSourceSize"),
            QStringLiteral("%1x%2").arg(sourceSize.width()).arg(sourceSize.height()));
    }
    return image;
}