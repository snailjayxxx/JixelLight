#include "core/cache/SourceCache.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QByteArrayView>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

// Explicitly opt-in: today's RAW loadSource still produces RGBA64 and .jlpv
// previews. A real camera-linear FP32 decoder must provide a new decode
// signature before using this persistent source, not merely cast a 16-bit RAW.
namespace {
constexpr quint32 Magic=0x4a4c4631u; // "JLF1", independent of legacy JL P4
constexpr quint32 Version=1;
constexpr qint64 HeaderBytes=32;
constexpr qint64 DigestBytes=32;
constexpr qint64 MaxFrameBytes=1024LL*1024*1024; // 1 GiB per entry, bypass larger RAWs
constexpr qint64 MaxDiskBytes=4LL*1024*1024*1024; // independent of 1 GiB .jlpv budget
constexpr qint64 MaxMetadataBytes=128*1024;
constexpr qint64 MaxIccBytes=512*1024;

bool isSha256(const QString &key) {
    if (key.size()!=64) return false;
    for (const QChar c:key) {
        const ushort v=c.unicode();
        if (!((v>='0'&&v<='9')||(v>='a'&&v<='f'))) return false;
    }
    return true;
}
QString combinedKey(const QString &key,const QString &decoder) {
    if (!isSha256(key) || !isSha256(decoder)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("JixelLight-FP32-scene-cache-v1\0"));
    hash.addData(QByteArray(ProcessingPlan::EngineVersion));
    hash.addData(key.toLatin1());
    hash.addData(decoder.toLatin1());
    return QString::fromLatin1(hash.result().toHex());
}
QString memoryKey(const QString &combined) { return QStringLiteral("float-scene-v1:")+combined; }
QString fileName(const QString &folder,const QString &combined) {
    return QDir(folder).filePath(combined+QStringLiteral(".jlfs"));
}
bool validDimensions(int width,int height,qint64 *bytes=nullptr) {
    if(width<=0 || height<=0 || width>32768 || height>32768) return false;
    const qint64 count=qint64(width)*height*16;
    if(count>MaxFrameBytes) return false;
    if(bytes) *bytes=count;
    return true;
}
QByteArray metadataBytes(const SourceData &source,const QString &decoder) {
    QJsonObject imageText;
    for(const auto &key:source.image.textKeys()) imageText.insert(key,source.image.text(key));
    const QJsonObject document{
        {"schema",1},{"sourceKey",source.key},{"decoderSignature",decoder},
        {"pixelOrder","RGBA IEEE-754 binary32 little-endian, row-major, visible bytes only"},
        {"metadata",QJsonObject::fromVariantMap(source.metadata)},
        {"imageText",imageText},
        {"devicePixelRatio",source.image.devicePixelRatio()},
        {"dotsPerMeterX",source.image.dotsPerMeterX()},
        {"dotsPerMeterY",source.image.dotsPerMeterY()}
    };
    return QJsonDocument(document).toJson(QJsonDocument::Compact);
}
bool readMetadata(const QByteArray &bytes,const QString &sourceKey,const QString &decoder,
                  SourceData *out) {
    QJsonParseError error;
    const QJsonDocument doc=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject obj=doc.object();
    if(obj.value("schema").toInt()!=1 || obj.value("sourceKey").toString()!=sourceKey
       || obj.value("decoderSignature").toString()!=decoder
       || obj.value("pixelOrder").toString()!="RGBA IEEE-754 binary32 little-endian, row-major, visible bytes only"
       || !obj.value("metadata").isObject() || !obj.value("imageText").isObject()
       || !obj.value("devicePixelRatio").isDouble()
       || !obj.value("dotsPerMeterX").isDouble() || !obj.value("dotsPerMeterY").isDouble())
        return false;
    const double dpr=obj.value("devicePixelRatio").toDouble();
    const double dpmX=obj.value("dotsPerMeterX").toDouble(),dpmY=obj.value("dotsPerMeterY").toDouble();
    if(!std::isfinite(dpr)||dpr<=0||dpr>64||!std::isfinite(dpmX)||!std::isfinite(dpmY)
       ||dpmX<0||dpmY<0||dpmX>1000000||dpmY>1000000
       ||std::floor(dpmX)!=dpmX || std::floor(dpmY)!=dpmY) return false;
    out->metadata=obj.value("metadata").toObject().toVariantMap();
    out->image.setDevicePixelRatio(dpr);
    out->image.setDotsPerMeterX(int(dpmX));
    out->image.setDotsPerMeterY(int(dpmY));
    const auto texts=obj.value("imageText").toObject();
    for(auto it=texts.begin();it!=texts.end();++it) {
        if(!it.value().isString()) return false;
        out->image.setText(it.key(),it.value().toString());
    }
    return true;
}
// Hash the exact file representation, not padding or Qt's native-endian image
// storage. 16 bytes/pixel, little-endian IEEE-754 for every R/G/B/A component.
QByteArray canonicalRow(const QImage &image,int row,QByteArray *scratch) {
    const qsizetype bytes=qsizetype(image.width())*16;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    scratch->resize(bytes);
    const auto *src=image.constScanLine(row);
    for(qsizetype i=0;i<bytes;i+=4) {
        quint32 bits;
        std::memcpy(&bits,src+i,4);
        qToLittleEndian(bits,scratch->data()+i);
    }
    return *scratch;
#else
    Q_UNUSED(scratch);
    return QByteArray::fromRawData(reinterpret_cast<const char *>(image.constScanLine(row)),bytes);
#endif
}
void restoreNativeEndianness(QImage &image,int y) {
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    auto *dst=image.scanLine(y);
    const qsizetype bytes=qsizetype(image.width())*16;
    for(qsizetype i=0;i<bytes;i+=4) {
        const quint32 bits=qFromLittleEndian<quint32>(dst+i);
        std::memcpy(dst+i,&bits,4);
    }
#else
    Q_UNUSED(image);Q_UNUSED(y);
#endif
}
}

QString SourceCache::floatDecoderSignature(const QByteArray &configuration) {
    if(configuration.isEmpty() || configuration.size()>8192) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("JixelLight-float-camera-decode-v1"));
    hash.addData(QByteArray(ProcessingPlan::EngineVersion));
    hash.addData(configuration);
    return QString::fromLatin1(hash.result().toHex());
}
bool SourceCache::storeFloatScene(const SourceData &source,const QString &decoderSignature,
                                  const CancelToken &token) {
    const QString combined=combinedKey(source.key,decoderSignature);
    if(combined.isEmpty() || !source.fullResolution || source.placeholder
       || source.image.isNull() || source.image.format()!=QImage::Format_RGBA32FPx4
       || cancelled(token)) return false;
    qint64 payloadBytes=0;
    if(!validDimensions(source.image.width(),source.image.height(),&payloadBytes)
       || !ImagePipeline::validFloatSource(source.image,token)) return false;
    PerformanceSpan timer("float_scene_cache_write",{{"bytes",payloadBytes}});
    const QByteArray meta=metadataBytes(source,decoderSignature);
    const QByteArray icc=source.image.colorSpace().iccProfile();
    if(meta.isEmpty()||meta.size()>MaxMetadataBytes||icc.size()>MaxIccBytes
       ||(source.image.colorSpace().isValid()&&icc.isEmpty())) return false;
    QByteArray header;
    QDataStream stream(&header,QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_8);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << Magic << Version
           << quint32(source.image.width()) << quint32(source.image.height())
           << quint32(meta.size()) << quint32(icc.size()) << quint64(payloadBytes);
    if(stream.status()!=QDataStream::Ok || header.size()!=HeaderBytes) return false;
    if(cancelled(token)) return false;
    {
        QMutexLocker lock(&m_diskMutex);
        QSaveFile file(fileName(m_diskDirectory,combined));
        file.setDirectWriteFallback(false);
        if(!file.open(QIODevice::WriteOnly)) return false;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        auto writePart=[&](QByteArrayView data) {
            if(file.write(data.data(),data.size())!=data.size()) return false;
            hash.addData(data);
            return true;
        };
        if(!writePart(header)||!writePart(meta)||!writePart(icc)) {
            file.cancelWriting(); return false;
        }
        QByteArray scratch;
        for(int y=0;y<source.image.height();++y) {
            if(cancelled(token)) {file.cancelWriting();return false;}
            const auto row=canonicalRow(source.image,y,&scratch);
            if(!writePart(row)) {file.cancelWriting();return false;}
        }
        if(cancelled(token) || file.write(hash.result())!=DigestBytes) {
            file.cancelWriting();return false;
        }
        if(!file.commit()) return false;
        trimFloatDisk();
        // Invalidate any old decoded snapshot only after an atomic commit.
        // Do not insert the caller's QImage here: it may wrap borrowed or
        // padded external memory with a shorter lifetime than SourceCache.
        // A subsequent floatScene() performs an owned, validated disk read.
        QMutexLocker memoryLock(&m_mutex);
        m_memory.remove(memoryKey(combined));
        PerformanceRecorder::value("decoded_cache_bytes",qint64(m_memory.totalCost())*1024);
    }
    PerformanceRecorder::count("float_scene_cache_write_success");
    return true;
}
SourceData SourceCache::floatScene(const QString &sourceKey,const QString &decoderSignature,
                                   const CancelToken &token) {
    const QString combined=combinedKey(sourceKey,decoderSignature);
    if(combined.isEmpty()||cancelled(token)) return {};
    {
        QMutexLocker lock(&m_mutex);
        if(const auto *existing=m_memory.object(memoryKey(combined))) {
            PerformanceRecorder::count("float_scene_cache_memory_hit");
            return cancelled(token)?SourceData{}:*existing;
        }
    }
    PerformanceSpan timer("float_scene_cache_read");
    SourceData result;
    {
        QMutexLocker lock(&m_diskMutex);
        QFile file(fileName(m_diskDirectory,combined));
        if(!file.open(QIODevice::ReadOnly)) return {};
        const QByteArray header=file.read(HeaderBytes);
        if(header.size()!=HeaderBytes) return {};
        QDataStream stream(header);
        stream.setVersion(QDataStream::Qt_6_8);
        stream.setByteOrder(QDataStream::LittleEndian);
        quint32 magic=0,version=0,w=0,h=0,metaSize=0,iccSize=0;
        quint64 bytes=0;
        stream >> magic >> version >> w >> h >> metaSize >> iccSize >> bytes;
        qint64 expected=0;
        if(stream.status()!=QDataStream::Ok || magic!=Magic || version!=Version
           || w>32768||h>32768||!validDimensions(int(w),int(h),&expected)
           || bytes!=quint64(expected)||metaSize>MaxMetadataBytes
           ||iccSize>MaxIccBytes
           ||file.size()!=HeaderBytes+qint64(metaSize)+iccSize+expected+DigestBytes)
            return {};
        const QByteArray meta=file.read(metaSize),icc=file.read(iccSize);
        if(meta.size()!=metaSize||icc.size()!=iccSize||cancelled(token)) return {};
        result.image=QImage(int(w),int(h),QImage::Format_RGBA32FPx4);
        if(result.image.isNull()||!readMetadata(meta,sourceKey,decoderSignature,&result)) return {};
        if(!icc.isEmpty()) {
            const auto color=QColorSpace::fromIccProfile(icc);
            if(!color.isValid()) return {};
            result.image.setColorSpace(color);
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(header);hash.addData(meta);hash.addData(icc);
        auto *base=result.image.bits();
        const qsizetype rowBytes=qsizetype(w)*16, stride=result.image.bytesPerLine();
        for(int y=0;y<int(h);++y) {
            if(cancelled(token)) return {};
            char *row=reinterpret_cast<char *>(base+qsizetype(y)*stride);
            if(file.read(row,rowBytes)!=rowBytes) return {};
            hash.addData(QByteArrayView(row,rowBytes));
            restoreNativeEndianness(result.image,y);
        }
        if(file.read(DigestBytes)!=hash.result() || !file.atEnd() || cancelled(token)
           || !ImagePipeline::validFloatSource(result.image,token))
            return {};
        result.key=sourceKey;
        result.fullResolution=true;
        result.placeholder=false;
    }
    if(cancelled(token)) return {};
    if(result.image.sizeInBytes()<=qint64(m_budgetKiB)*1024) {
        QMutexLocker lock(&m_mutex);
        m_memory.insert(memoryKey(combined),new SourceData(result),
                        int((result.image.sizeInBytes()+1023)/1024));
        PerformanceRecorder::value("decoded_cache_bytes",qint64(m_memory.totalCost())*1024);
    }
    PerformanceRecorder::count("float_scene_cache_disk_hit");
    return result;
}
void SourceCache::trimFloatDisk() {
    // Called with m_diskMutex held. Float files have a separate 4-GiB quota
    // and never evict the legacy RGBA64 .jlpv preview cache.
    auto files=QDir(m_diskDirectory).entryInfoList({"*.jlfs"},QDir::Files,QDir::Time);
    qint64 total=0;for(const auto &f:files)total+=f.size();
    for(auto it=files.crbegin();total>MaxDiskBytes&&it!=files.crend();++it)
        if(QFile::remove(it->absoluteFilePath()))total-=it->size();
}
