#include "core/cache/SourceCache.h"
#include "core/raw/RawDecoder.h"
#include "core/metadata/MetadataReader.h"
#include "core/pipeline/ProcessingPlan.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <libraw/libraw.h>

SourceCache::SourceCache(qint64 memoryBytes, QString directory) {
    if (memoryBytes <= 0) {
        bool ok = false; int mb = qEnvironmentVariableIntValue("JIXELLIGHT_CACHE_MB", &ok);
        memoryBytes = qint64(ok ? std::clamp(mb,64,4096) : 512)*1024*1024;
    }
    m_budgetKiB = int(std::clamp(memoryBytes/1024, qint64(1024), qint64(4)*1024*1024));
    m_memory.setMaxCost(m_budgetKiB);
    m_diskDirectory = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/linear-preview-v5" : directory;
    QDir().mkpath(m_diskDirectory);
}
QString SourceCache::fileKey(const QString &path) {
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto field = [&](const QByteArray &value) { hash.addData(value); hash.addData(QByteArrayView("\0",1)); };
    field(QByteArray(ProcessingPlan::EngineVersion));
    field(QByteArrayLiteral("disk-schema-5:RGBA64-native-endian"));
    field(QByteArray(LibRaw::version()));
    field(info.canonicalFilePath().toUtf8());
    field(QByteArray::number(info.size()));
    field(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    field(QByteArrayLiteral("LibRaw:AHD:WBcamera-if-available:highlight1:adjustmax0.75:ProPhoto:16:linear:camera-profile-v1:default-crop-v1"));
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        hash.addData(file.read(65536));
        if (file.size()>65536) { file.seek(std::max<qint64>(65536,file.size()-65536)); hash.addData(file.read(65536)); }
    }
    return QString::fromLatin1(hash.result().toHex());
}
SourceData SourceCache::get(const QString &key) {
    QMutexLocker lock(&m_mutex);
    if (const auto *entry = m_memory.object(key)) { PerformanceRecorder::count("decoded_cache_hit"); return *entry; }
    PerformanceRecorder::count("decoded_cache_miss"); return {};
}
void SourceCache::put(const SourceData &source) {
    if (source.image.isNull() || !source.fullResolution || source.key.isEmpty()) return;
    const auto bytes = source.image.sizeInBytes();
    QMutexLocker lock(&m_mutex);
    if (bytes > qint64(m_budgetKiB)*1024) return;
    m_memory.insert(source.key,new SourceData(source),int((bytes+1023)/1024));
    PerformanceRecorder::value("decoded_cache_bytes",qint64(m_memory.totalCost())*1024);
}
qint64 SourceCache::memoryBytes() const { QMutexLocker lock(&m_mutex); return qint64(m_memory.totalCost())*1024; }
SourceData SourceCache::diskPreview(const QString &key) {
    if (key.size()!=64) return {};
    QMutexLocker lock(&m_diskMutex);
    QFile file(QDir(m_diskDirectory).filePath(key+".jlpv"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    PerformanceSpan timer("disk_preview_read");
    QDataStream stream(&file); stream.setVersion(QDataStream::Qt_6_8);
    quint32 magic=0,w=0,h=0,metaSize=0; quint64 bytes=0;
    stream >> magic >> w >> h >> metaSize >> bytes;
    if (magic!=0x4a4c5035u || !w || !h || w>2048 || h>2048 || metaSize>65536 || bytes!=quint64(w)*h*8 || file.size()!=qint64(24+metaSize+bytes+32)) return {};
    file.seek(0);
    const QByteArray header = file.read(24);
    QByteArray metadata = file.read(metaSize);
    SourceData result; result.image = QImage(int(w),int(h),QImage::Format_RGBA64);
    if (result.image.isNull() || file.read(reinterpret_cast<char *>(result.image.bits()),qint64(bytes))!=qint64(bytes)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(header); hash.addData(metadata); hash.addData(QByteArrayView(reinterpret_cast<const char *>(result.image.constBits()),qsizetype(bytes)));
    if (file.read(32)!=hash.result()) return {};
    result.metadata = QJsonDocument::fromJson(metadata).object().toVariantMap();
    result.key = key; result.fullResolution = false;
    result.image.setText("JixelLightWorkingSpace","Linear ProPhoto RGB");
    PerformanceRecorder::count("disk_preview_hit");
    return result;
}
void SourceCache::storeDiskPreview(const SourceData &source, const CancelToken &token) {
    if (source.key.size()!=64 || source.image.isNull() || cancelled(token)) return;
    PerformanceSpan timer("disk_preview_write");
    QImage preview = source.image;
    if (preview.width()>2048 || preview.height()>2048) preview = preview.scaled(2048,2048,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    preview = preview.convertToFormat(QImage::Format_RGBA64);
    if (cancelled(token)) return;
    const auto metadata = QJsonDocument(QJsonObject::fromVariantMap(source.metadata)).toJson(QJsonDocument::Compact);
    QMutexLocker lock(&m_diskMutex);
    QSaveFile file(QDir(m_diskDirectory).filePath(source.key+".jlpv"));
    if (!file.open(QIODevice::WriteOnly)) return;
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_8);
    stream << quint32(0x4a4c5035) << quint32(preview.width()) << quint32(preview.height()) << quint32(metadata.size()) << quint64(preview.sizeInBytes());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(header); hash.addData(metadata); hash.addData(QByteArrayView(reinterpret_cast<const char *>(preview.constBits()),preview.sizeInBytes()));
    bool ok = file.write(header)==header.size();
    ok &= file.write(metadata)==metadata.size();
    ok &= file.write(reinterpret_cast<const char *>(preview.constBits()),preview.sizeInBytes())==preview.sizeInBytes();
    ok &= file.write(hash.result())==32;
    if (!ok || cancelled(token) || stream.status()!=QDataStream::Ok) { file.cancelWriting(); return; }
    if (file.commit()) trimDisk();
}
void SourceCache::trimDisk() {
    auto files = QDir(m_diskDirectory).entryInfoList({"*.jlpv"},QDir::Files,QDir::Time);
    qint64 total = 0; for (const auto &file : files) total += file.size();
    constexpr qint64 limit = 1024LL*1024*1024;
    for (auto it=files.crbegin(); total>limit && it!=files.crend(); ++it) {
        if (QFile::remove(it->absoluteFilePath())) total-=it->size();
    }
}
SourceData loadSource(SourceCache &cache, const QString &path, const CancelToken &token,
                      const std::function<void(SourceData)> &partial) {
    SourceData out;
    try {
        PerformanceSpan total("source_load");
        out.key = SourceCache::fileKey(path);
        if (out.key.isEmpty()) { out.error = "File not found"; return out; }
        auto cached = cache.get(out.key);
        if (!cached.image.isNull()) return cached;
        const bool raw = RawDecoder::isRawFile(path);
        if (partial) {
            auto disk = cache.diskPreview(out.key);
            if (!disk.image.isNull() && !cancelled(token)) partial(disk);
            else if (raw) {
                auto thumb = RawDecoder::thumbnail(path,token);
                if (!thumb.isNull() && !cancelled(token)) { SourceData p; p.image=thumb; p.key=out.key; p.placeholder=true; partial(p); }
            }
        }
        if (cancelled(token)) return {};
        QString metadataError;
        out.metadata = MetadataReader::read(path,&metadataError);
        if (!metadataError.isEmpty()) out.metadata["metadataWarning"] = metadataError;
        if (raw) {
            RawMetadata meta;
            out.image = RawDecoder::decode(path,&out.error,&meta,token);
            out.metadata["make"] = meta.make;
            out.metadata["model"] = meta.model;
            out.metadata["bitDepth"] = meta.bitsPerChannel;
            out.metadata["demosaic"] = meta.demosaic;
            out.metadata["libRawHighlightMode"] = meta.highlightMode;
            out.metadata["libRawHighlightBlend"] = meta.highlightBlendEnabled;
            out.metadata["libRawAdjustMaximumThreshold"] = meta.adjustMaximumThreshold;
            out.metadata["cameraMatrixAvailable"] = meta.cameraMatrixAvailable;
            out.metadata["cameraWhiteBalanceAvailable"] = meta.cameraWhiteBalanceAvailable;
            out.metadata["cameraProfileApplied"] = meta.cameraProfileApplied;
            if (!meta.cameraProfileSource.isEmpty()) out.metadata["cameraProfileSource"] = meta.cameraProfileSource;
            if (meta.calibratedBlackLevel >= 0) out.metadata["cameraBlackLevel"] = meta.calibratedBlackLevel;
            if (meta.calibratedWhiteLevel > 0) out.metadata["cameraWhiteLevel"] = meta.calibratedWhiteLevel;
            if (!meta.defaultCropSource.isEmpty()) out.metadata["cameraDefaultCropSource"] = meta.defaultCropSource;
            double baseExposure = out.metadata.value("rawBaselineExposure", 0.0).toDouble();
            if (!std::isfinite(baseExposure) || baseExposure < -8.0 || baseExposure > 8.0) baseExposure = 0.0;
            out.metadata["rawBaseExposureStops"] = baseExposure;
            if (!out.metadata.contains("rawBaselineExposureSource"))
                out.metadata["rawBaselineExposureSource"] = QStringLiteral("none / 0 EV camera offset");
            if(!out.image.text("JixelLightCameraCrop").isEmpty())out.metadata["cameraDefaultCrop"]=out.image.text("JixelLightCameraCrop");
        } else {
            QImageReader reader(path); reader.setAutoTransform(true);
            out.image = reader.read();
            if (out.image.isNull()) out.error = reader.errorString();
            else {
                if (!out.image.colorSpace().isValid()) out.image.setColorSpace(QColorSpace(QColorSpace::SRgb));
                const auto working = QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear);
                out.image = out.image.convertedToColorSpace(working,QImage::Format_RGBA64);
            }
        }
        if (out.image.isNull() || cancelled(token)) return out;
        if (SourceCache::fileKey(path)!=out.key) { out.image={}; out.error="Source changed while it was being read"; return out; }
        out.fullResolution = true;
        out.metadata["pixelWidth"] = out.image.width();
        out.metadata["pixelHeight"] = out.image.height();
        out.metadata["workingSpace"] = "Linear ProPhoto RGB";
        out.image.setText("JixelLightWorkingSpace","Linear ProPhoto RGB");
        cache.put(out);
        if (partial && !cancelled(token)) partial(out);
        cache.storeDiskPreview(out,token);
    } catch (const std::exception &e) { out.image={}; out.error=QString::fromUtf8(e.what()); }
    catch (...) { out.image={}; out.error="Unknown source load failure"; }
    return out;
}
