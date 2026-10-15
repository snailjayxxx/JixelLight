#include "core/raw/RawDecoder.h"
#include "core/raw/RawGeometry.h"
#include "core/raw/SensorMosaicAudit.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QSemaphore>
#include <algorithm>
#include <memory>

#include <QFile>
#include <QBuffer>
#include <QImageReader>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QByteArrayView>
#include <cmath>
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
    // LibRaw includes large fixed tables (> 700 KiB in supported SDKs).
    // Keep them off the limited worker stack; ownership still ends on return.
    auto decoder = std::make_unique<LibRaw>();
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

    if (metadata) {
        metadata->sensorAudit={};
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

    // Capture only when explicitly requested; before dcraw_process() can
    // subtract black, alter maximum, demosaic, or convert to ProPhoto RGB.
    if (metadata && metadata->captureSensorAudit && !cancelled(cancel)) {
        const auto &sizes=raw.imgdata.sizes;
        const auto &color=raw.imgdata.color;
        SensorMosaicAudit::Layout layout;
        layout.rawWidth=sizes.raw_width;layout.rawHeight=sizes.raw_height;
        layout.pitchBytes=int(sizes.raw_pitch);
        layout.active=QRect(sizes.left_margin,sizes.top_margin,sizes.width,sizes.height);
        layout.nominalBlack=color.black;
        layout.nominalWhite=color.maximum;
        auto report=SensorMosaicAudit::capture(raw.imgdata.rawdata.raw_image,layout,cancel);
        QJsonArray perChannelBlack,perChannelLinearWhite,camMul,preMul;
        for(int i=0;i<4;++i) {
            perChannelBlack.append(qint64(color.cblack[i]));
            perChannelLinearWhite.append(qint64(color.linear_max[i]));
            camMul.append(std::isfinite(color.cam_mul[i]) ? QJsonValue(double(color.cam_mul[i])) : QJsonValue());
            preMul.append(std::isfinite(color.pre_mul[i]) ? QJsonValue(double(color.pre_mul[i])) : QJsonValue());
        }
        report["libraw_version"]=QString::fromLatin1(LibRaw::version());
        report["raw_pitch_bytes"]=int(sizes.raw_pitch);
        report["cfa_filters_encoded"]=qint64(raw.imgdata.idata.filters);
        report["cfa_kind"]=raw.imgdata.idata.filters==9 ? QStringLiteral("xtrans_6x6")
            : raw.imgdata.idata.filters>1000 ? QStringLiteral("bayer_like_filters")
            : QStringLiteral("unknown_or_non_mosaic");
        report["camera_flip"]=sizes.flip;
        report["black_channel_corrections"]=perChannelBlack;
        report["linear_white_metadata_not_black_subtracted"]=perChannelLinearWhite;
        report["as_shot_camera_wb_multipliers_unverified"]=camMul;
        report["libraw_pre_multipliers_unverified"]=preMul;
        const auto cols=int(color.cblack[4]),rows=int(color.cblack[5]);
        // cblack dimensions are values from an untrusted camera file.
        // A pattern is not by itself an absolute channel-black level.
        const bool patternValid=cols>0 && rows>0 && qint64(cols)*rows<=4098;
        QJsonArray pattern;
        if (patternValid) for(int i=0;i<cols*rows;++i) pattern.append(qint64(color.cblack[6+i]));
        report["black_pattern_dimensions"]=QJsonArray{cols,rows};
        report["black_pattern_values_uninterpreted"]=pattern;
        report["black_pattern_valid"]=patternValid || (cols==0 && rows==0);
        report["pre_develop_warning"]=QStringLiteral(
            "Unpacked sensor samples and LibRaw black/white metadata are not scene RGB, "
            "do not imply recoverable highlights, and may include unpack-stage linearization");
        metadata->sensorAudit=report;
    }
    const auto decodeSizes=raw.imgdata.sizes;
    auto &params = raw.imgdata.params;
    params.use_camera_wb = 1;
    params.use_auto_wb = 0;
    params.use_camera_matrix = 3; // Prefer embedded/built-in camera color data regardless of WB mode.
    params.no_auto_bright = 1;
    // Keep LibRaw's documented channel-maximum correction enabled. Disabling
    // it restores dcraw-style coloured highlight failures when the nominal
    // white level does not match the photographed channel maximum.
    params.adjust_maximum_thr = 0.75f;
    params.bright = 1.0f;
    // Do not blend highlights inside LibRaw and then recover/compress them a
    // second time in JixelLight. Mode 1 preserves unclipped channel data; the
    // editor owns the visible highlight rendering policy downstream.
    params.highlight = 1;
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
    image.setText(QStringLiteral("JixelLightLibRawHighlightMode"), QStringLiteral("1 / unclip"));
    image.setText(QStringLiteral("JixelLightAdjustMaximumThreshold"), QStringLiteral("0.75"));
    // LibRaw's standard inset is an absolute sensor-space crop. Convert it to
    // the developed/oriented bitmap for reference matching only. Reject bogus
    // metadata and unexplained rescaling; never guess a centre crop.
    if(QString::fromLatin1(raw.imgdata.idata.make).contains("SONY",Qt::CaseInsensitive)) {
        const auto c=decodeSizes.raw_inset_crops[0];
        if(c.cleft<65535&&c.ctop<65535&&c.cwidth>0&&c.cheight>0&&
           quint64(c.cleft)+c.cwidth<=decodeSizes.raw_width&&quint64(c.ctop)+c.cheight<=decodeSizes.raw_height) {
            const QRect relative(int(c.cleft)-int(decodeSizes.left_margin),int(c.ctop)-int(decodeSizes.top_margin),c.cwidth,c.cheight);
            const auto roi=RawGeometry::orientCrop(QSize(decodeSizes.width,decodeSizes.height),relative,decodeSizes.flip,image.size());
            if(roi.isValid())image.setText("JixelLightCameraCrop",QString("%1,%2,%3,%4").arg(roi.x()).arg(roi.y()).arg(roi.width()).arg(roi.height()));
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
        metadata->cameraWhiteBalanceEnabled = true;
        metadata->highlightBlendEnabled = false;
        metadata->highlightMode = 1;
        metadata->adjustMaximumThreshold = 0.75f;
    }

    LibRaw::dcraw_clear_mem(processed);
    return image;
}


SensorLinearMosaic::Tile RawDecoder::calibratedMosaicTile(
        const QString &path,const SensorLinearMosaic::Calibration &calibration,
        const QRect &rawRoi,QString *errorMessage,const CancelToken &cancel) {
    if(errorMessage) errorMessage->clear();
    const auto refuse=[&](const QString &reason) {
        if(errorMessage) *errorMessage=reason;
        SensorLinearMosaic::Tile tile;tile.error=reason;return tile;
    };
    if(cancelled(cancel)) return refuse(QStringLiteral("cancelled"));
    if(!SensorLinearMosaic::validCalibration(calibration))
        return refuse(QStringLiteral("Explicit Bayer calibration and two SHA-256 identities are required"));
    if(!isRawFile(path)) return refuse(QStringLiteral("Calibration probe requires an original RAW file"));
    RawLease lease(cancel);
    if(!lease.acquired) return refuse(QStringLiteral("cancelled"));

    // An external calibration is not evidence unless bound to these exact
    // source bytes. Do not trust a filename, camera model or preview metadata.
    QFile source(path);
    if(!source.open(QIODevice::ReadOnly)) return refuse(QStringLiteral("Cannot open source for SHA-256 verification"));
    const qint64 initialSize=source.size();
    const QDateTime initialModified=QFileInfo(path).lastModified();
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while(!source.atEnd()) {
        if(cancelled(cancel)) return refuse(QStringLiteral("cancelled"));
        const QByteArray chunk=source.read(1024*1024);
        if(chunk.isEmpty()) return refuse(QStringLiteral("Unable to read complete RAW file for SHA-256"));
        digest.addData(chunk);
    }
    source.close();
    if(digest.result().toHex()!=calibration.sourceSha256)
        return refuse(QStringLiteral("Original RAW source SHA-256 does not match calibration"));

    // Heap-allocate LibRaw: the project also decodes on a 512-KiB stack.
    auto instance=std::make_unique<LibRaw>();
    auto &raw=*instance;
    raw.set_progress_handler(rawProgress,const_cast<CancelToken *>(&cancel));
#if defined(Q_OS_WIN)
    int result=raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    const QByteArray encoded=QFile::encodeName(path);
    int result=raw.open_file(encoded.constData());
#endif
    if(result!=LIBRAW_SUCCESS) return refuse(libRawError(result));
    const auto &sizes=raw.imgdata.sizes;
    const quint64 pixels=quint64(sizes.raw_width)*sizes.raw_height;
    const quint64 limit=quint64(std::max(256,qEnvironmentVariableIntValue("JIXELLIGHT_RAW_MEMORY_MB")))*1024*1024;
    const quint64 effectiveLimit=qEnvironmentVariableIsSet("JIXELLIGHT_RAW_MEMORY_MB") ? limit : 3ULL*1024*1024*1024;
    if(pixels>effectiveLimit/32)
        return refuse(QStringLiteral("RAW exceeds configured decode memory budget"));
    if(cancelled(cancel)) return refuse(QStringLiteral("cancelled"));
    result=raw.unpack();
    if(result!=LIBRAW_SUCCESS) return refuse(libRawError(result));
    if(cancelled(cancel)) return refuse(QStringLiteral("cancelled"));

    const auto &idata=raw.imgdata.idata;
    const auto &color=raw.imgdata.color;
    if(idata.is_foveon || idata.filters<=1000 ||
       QString::fromLatin1(idata.make).contains(QStringLiteral("FUJI"),Qt::CaseInsensitive) ||
       idata.cdesc[0]!='R'||idata.cdesc[1]!='G'||idata.cdesc[2]!='B')
        return refuse(QStringLiteral("Unsupported or non-regular RGB Bayer CFA"));
    const int patternCols=int(color.cblack[4]),patternRows=int(color.cblack[5]);
    const bool noPattern=patternCols==0&&patternRows==0;
    const bool pattern2x2=patternCols==2&&patternRows==2;
    if(!noPattern&&!pattern2x2)
        return refuse(QStringLiteral("Unsupported repeating sensor black-level pattern"));
    for(int y=0;y<8;++y) for(int x=0;x<2;++x) {
        const int site=((y&1)<<1)|(x&1);
        const int channel=raw.COLOR(y,x); // row/column relative to visible area
        if(channel!=calibration.cfa[std::size_t(site)] ||
           (channel==3 && idata.cdesc[3]!='G'))
            return refuse(QStringLiteral("External Bayer phase disagrees with LibRaw or is non-2x2"));
        const quint64 black=quint64(color.black)+color.cblack[channel]+
            (pattern2x2 ? color.cblack[6+site] : 0);
        if(black!=calibration.black[std::size_t(site)])
            return refuse(QStringLiteral("External Bayer black calibration disagrees with LibRaw"));
    }
    const SensorLinearMosaic::Layout layout{
        int(sizes.raw_width),int(sizes.raw_height),int(sizes.raw_pitch),
        QRect(sizes.left_margin,sizes.top_margin,sizes.width,sizes.height)};
    auto tile=SensorLinearMosaic::normalizeTile(raw.imgdata.rawdata.raw_image,
                                                layout,calibration,rawRoi,cancel);
    if(!tile.valid()) return refuse(tile.error);
    const QFileInfo after(path);
    if(after.size()!=initialSize||after.lastModified()!=initialModified)
        return refuse(QStringLiteral("Original RAW changed during calibrated mosaic probe"));
    tile.statistics["libraw_nominal_white_code"]=qint64(color.maximum);
    tile.statistics["libraw_version"]=QString::fromLatin1(LibRaw::version());
    tile.statistics["calibrated_white_is_external_not_libraw_maximum"]=true;
    return tile;
}

QImage RawDecoder::thumbnail(const QString &path, const CancelToken &cancel) {
    if (cancelled(cancel)) return {};
    PerformanceSpan timer(QStringLiteral("raw_embedded_preview"));
    // LibRaw includes large fixed tables (> 700 KiB in supported SDKs).
    // Keep them off the limited worker stack; ownership still ends on return.
    auto decoder = std::make_unique<LibRaw>();
    auto &raw = *decoder;
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
        image.setText("JixelLightThumbnailOrientationApplied",reader.transformation()!=QImageIOHandler::TransformationNone?"true":"false");
    }
    else if (thumb->type == LIBRAW_IMAGE_BITMAP && thumb->colors == 3 && thumb->bits == 8)
        image = QImage(thumb->data, thumb->width, thumb->height, thumb->width*3, QImage::Format_RGB888).copy();
    LibRaw::dcraw_clear_mem(thumb);
    if (cancelled(cancel)) return {};
    // A placeholder only. Do not run RAW adjustments or scopes on this image.
    if (image.width() > 2048 || image.height() > 2048) image = image.scaled(2048,2048,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    return image;
}
