#include "core/raw/FloatRawTrial.h"
#include "core/pipeline/FloatFrameFingerprint.h"
#include "core/pipeline/ImagePipeline.h"
#include <QCryptographicHash>
#include <QColorSpace>
#include <QVector>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr int kMaxManifestText = 131072;
constexpr int kMaxPatches = 64;

bool hexDigest(const QByteArray &bytes) {
    if(bytes.size()!=64) return false;
    for(const auto v:bytes) if(!((v>='0'&&v<='9')||(v>='a'&&v<='f'))) return false;
    return true;
}
QString filePath(const QString &base,const QJsonValue &value) {
    if(!value.isString()||value.toString().isEmpty()||value.toString().contains(QChar::Null)) return {};
    const QString path=value.toString();
    if(path.startsWith(QStringLiteral("http:"),Qt::CaseInsensitive) ||
       path.startsWith(QStringLiteral("https:"),Qt::CaseInsensitive)) return {};
    return QFileInfo(QDir(base).absoluteFilePath(path)).absoluteFilePath();
}
bool scalar(const QJsonValue &value,double *out,double minimum,double maximum) {
    if(!value.isDouble()) return false;
    const double v=value.toDouble();
    if(!std::isfinite(v)||v<minimum||v>maximum) return false;
    *out=v;return true;
}
bool unsignedInt(const QJsonValue &value,int *out,int minimum,int maximum) {
    double v=0;
    if(!scalar(value,&v,minimum,maximum)||std::trunc(v)!=v)return false;
    *out=int(v);return true;
}
bool rect(const QJsonValue &value,QRect *out) {
    if(!value.isArray()||value.toArray().size()!=4)return false;
    const auto a=value.toArray();
    int x=0,y=0,w=0,h=0;
    if(!unsignedInt(a[0],&x,0,32767)||!unsignedInt(a[1],&y,0,32767)||
       !unsignedInt(a[2],&w,1,32768)||!unsignedInt(a[3],&h,1,32768) ||
       qint64(x)+w>32768||qint64(y)+h>32768)
        return false;
    *out=QRect(x,y,w,h);return true;
}
bool array4(const QJsonValue &value,std::array<quint32,4> *out,int ceiling) {
    if(!value.isArray()||value.toArray().size()!=4) return false;
    for(int i=0;i<4;++i) {
        int v=0;
        if(!unsignedInt(value.toArray()[i],&v,0,ceiling))return false;
        (*out)[std::size_t(i)]=quint32(v);
    }
    return true;
}
bool arrayFloat4(const QJsonValue &value,std::array<double,4> *out,double lo,double hi) {
    if(!value.isArray()||value.toArray().size()!=4)return false;
    for(int i=0;i<4;++i)
        if(!scalar(value.toArray()[i],&(*out)[std::size_t(i)],lo,hi))return false;
    return true;
}
bool arrayFloat9(const QJsonValue &value,std::array<double,9> *out) {
    if(!value.isArray()||value.toArray().size()!=9)return false;
    for(int i=0;i<9;++i)
        if(!scalar(value.toArray()[i],&(*out)[std::size_t(i)],-16,16))return false;
    return true;
}
QByteArray requiredSha(const QJsonValue &value) {
    if(!value.isString())return {};
    const QByteArray bytes=value.toString().toLatin1();
    return hexDigest(bytes) ? bytes : QByteArray{};
}
QString readOnlyEvidence(const QJsonValue &value) {
    if(!value.isString()) return {};
    const QString s=value.toString().trimmed();
    // Human-readable provenance is a declaration, never a trusted signature.
    return (s.size()>=8&&s.size()<=512) ? s : QString{};
}
// Cryptographically bind the stated camera calibration identity to concrete
// local provenance bytes. This is file integrity, NOT scientific authentication.
bool verifiedCalibrationEvidence(const QString &path,const QByteArray &digest) {
    if(path.isEmpty()||!hexDigest(digest))return false;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>1024*1024)
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file)&&hash.result().toHex()==digest;
}
QByteArray verifiedFile(const QString &path,const QByteArray &sha,QString *error) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>kMaxManifestText) {
        if(error)*error=QStringLiteral("Reference JSON is absent or exceeds the 128-KiB limit");
        return {};
    }
    const QByteArray bytes=file.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=sha) {
        if(error)*error=QStringLiteral("Independent reference SHA-256 mismatch");
        return {};
    }
    return bytes;
}
QJsonArray boundsArray(const QRect &rect) {
    return QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()};
}
}

bool FloatRawTrial::parse(const QJsonObject &manifest,const QString &directory,
                          Request *request,QString *error) {
    if(error)error->clear();
    const auto fail=[&](const char *reason) {
        if(error)*error=QString::fromLatin1(reason);
        return false;
    };
    if(!request) return fail("Missing output request");
    // In case caller reuses a Request, discard all older profile fields.
    *request=Request{};
    if(manifest.value("schema").toInt()!=1 ||
       manifest.value("purpose").toString()!=QStringLiteral("experimental-research-only"))
        return fail("Experimental trial schema and purpose are required");
    const auto source=manifest.value("source").toObject();
    request->rawPath=filePath(directory,source.value("file"));
    request->sensor.sourceSha256=requiredSha(source.value("sha256"));
    if(request->rawPath.isEmpty()||request->sensor.sourceSha256.isEmpty() ||
       !RawDecoder::isRawFile(request->rawPath))
        return fail("Explicit original RAW file and SHA-256 required");
    const auto sensor=manifest.value("sensorCalibration").toObject();
    request->sensor.calibrationSha256=requiredSha(sensor.value("sha256"));
    const QString sensorProof=filePath(directory,sensor.value("evidenceFile"));
    if(readOnlyEvidence(sensor.value("provenance")).isEmpty() ||
       sensorProof==request->rawPath ||
       !verifiedCalibrationEvidence(sensorProof,request->sensor.calibrationSha256) ||
       !array4(sensor.value("black"),&request->sensor.black,65535) ||
       !array4(sensor.value("white"),&request->sensor.white,65535))
        return fail("Sensor black/white and SHA-verified provenance file are required");
    std::array<quint32,4> channels{};
    if(!array4(sensor.value("cfa"),&channels,3))
        return fail("2x2 Bayer phase required");
    for(int i=0;i<4;++i)request->sensor.cfa[std::size_t(i)]=quint8(channels[std::size_t(i)]);
    if(!SensorLinearMosaic::validCalibration(request->sensor))
        return fail("Invalid bound Bayer sensor calibration");
    const auto color=manifest.value("colorCalibration").toObject();
    request->color.profileSha256=requiredSha(color.value("sha256"));
    const QString colorProof=filePath(directory,color.value("evidenceFile"));
    if(readOnlyEvidence(color.value("provenance")).isEmpty()||
       colorProof==sensorProof || colorProof==request->rawPath ||
       request->color.profileSha256==request->sensor.calibrationSha256 ||
       !verifiedCalibrationEvidence(colorProof,request->color.profileSha256) ||
       !arrayFloat4(color.value("whiteBalanceBySite"),&request->color.whiteBalanceBySite,0.000001,64)||
       !arrayFloat9(color.value("cameraToProPhoto"),&request->color.cameraToProPhoto)||
       !SensorFloatDevelop::validProfile(request->color))
        return fail("Invalid research color profile or its SHA-verified provenance file");
    if(!rect(manifest.value("sampleRoi"),&request->sampleRoi) ||
       !rect(manifest.value("outputRoi"),&request->outputRoi) ||
       !request->sampleRoi.contains(request->outputRoi) ||
       qint64(request->sampleRoi.width())*request->sampleRoi.height()>1024LL*1024 ||
       qint64(request->outputRoi.width())*request->outputRoi.height()>1024LL*1024)
        return fail("Invalid/oversize source and output ROI");
    const auto reference=manifest.value("reference");
    if(!reference.isUndefined()&&!reference.isNull()) {
        if(!reference.isObject())return fail("Reference entry must be an object");
        const auto obj=reference.toObject();
        request->referencePath=filePath(directory,obj.value("file"));
        request->referenceSha256=requiredSha(obj.value("sha256"));
        if(request->referencePath.isEmpty()||request->referenceSha256.isEmpty()||
           request->referenceSha256==request->sensor.sourceSha256 ||
           obj.value("domain").toString()!=QStringLiteral("linear-ProPhoto-D50-patch-means-v1"))
            return fail("Separately hashed linear-D50 patch reference is required");
    }
    return true;
}

QJsonObject FloatRawTrial::comparePatchMeans(const QImage &frame,const QRect &rawRoi,
                                             const QJsonObject &reference,
                                             const QByteArray &sourceSha256,QString *error) {
    if(error)error->clear();
    const auto fail=[&](const QString &message) {
        if(error)*error=message;return QJsonObject{};
    };
    const auto requiredSpace=QColorSpace(QColorSpace::ProPhotoRgb)
        .withTransferFunction(QColorSpace::TransferFunction::Linear);
    if(frame.isNull()||frame.format()!=QImage::Format_RGBA32FPx4||
       frame.colorSpace()!=requiredSpace ||
       frame.size()!=rawRoi.size()||!rawRoi.isValid()||
       !ImagePipeline::validFloatSource(frame))
        return fail(QStringLiteral("Expected a finite scene-linear FP32 frame matching sensor ROI"));
    if(reference.value("schema").toInt()!=1||
       reference.value("domain").toString()!=QStringLiteral("linear-ProPhoto-D50-patch-means-v1") ||
       reference.value("sourceRawSha256").toString()!=QString::fromLatin1(sourceSha256)||
       readOnlyEvidence(reference.value("measurementMethod")).isEmpty())
        return fail(QStringLiteral("Reference domain, source or provenance is not independently bound"));
    const auto patches=reference.value("patches").toArray();
    if(patches.size()<2||patches.size()>kMaxPatches)
        return fail(QStringLiteral("Expected 2-64 independently measured patch regions"));
    QVector<QRect> used;used.reserve(patches.size());
    QJsonArray patchResults;
    long double squared=0,absolute=0;
    double maxAbs=0;
    for(const auto &element:patches) {
        if(!element.isObject())return fail(QStringLiteral("Invalid reference patch"));
        const auto patch=element.toObject();
        QRect area;
        if(!rect(patch.value("rawRoi"),&area)||!rawRoi.contains(area))
            return fail(QStringLiteral("Reference patch ROI outside candidate sensor ROI"));
        for(const auto &previous:used)
            if(previous.intersects(area))return fail(QStringLiteral("Overlapping reference patch regions"));
        used.append(area);
        const auto expected=patch.value("meanRgb").toArray();
        if(expected.size()!=3)return fail(QStringLiteral("Invalid reference linear RGB mean"));
        std::array<double,3> referenceRgb{};
        for(int c=0;c<3;++c)
            if(!scalar(expected[c],&referenceRgb[std::size_t(c)],-65536,65536))
                return fail(QStringLiteral("Non-finite independent reference RGB"));
        std::array<long double,3> sums{};
        for(int y=area.top();y<=area.bottom();++y) {
            const auto *row=reinterpret_cast<const float *>(frame.constScanLine(y-rawRoi.top()));
            for(int x=area.left();x<=area.right();++x)
                for(int c=0;c<3;++c)
                    sums[std::size_t(c)]+=row[4*(x-rawRoi.left())+c];
        }
        const long double pixels=qint64(area.width())*area.height();
        QJsonArray observed,delta;
        for(int c=0;c<3;++c) {
            const double mean=double(sums[std::size_t(c)]/pixels);
            const double diff=mean-referenceRgb[std::size_t(c)];
            observed.append(mean);delta.append(diff);
            squared+=static_cast<long double>(diff)*diff;
            absolute+=std::abs(diff);
            maxAbs=std::max(maxAbs,std::abs(diff));
        }
        patchResults.append(QJsonObject{{"rawRoi",boundsArray(area)},
                                        {"observedMeanRgb",observed},
                                        {"referenceMeanRgb",expected},{"deltaRgb",delta}});
    }
    return {{"schema",1},{"available",true},{"kind","research-only-linear-ProPhoto-patch-means"},
            {"patchCount",patches.size()},{"rgbRmse",double(std::sqrt(squared/(patches.size()*3)))},
            {"rgbMeanAbsolute",double(absolute/(patches.size()*3))},
            {"rgbMaximumAbsolute",maxAbs},{"patches",patchResults},
            {"independentlyVerified",false},{"cameraCalibrationAccepted",false},
            {"warning","Reference provenance is supplied by the experimenter; no external scientific verification is performed"}};
}

FloatRawTrial::Result FloatRawTrial::run(const Request &request,const CancelToken &cancel) {
    Result result;
    if(cancelled(cancel)){result.error=QStringLiteral("cancelled");return result;}
    QString reason;
    const auto frame=RawDecoder::calibratedFloatTile(
        request.rawPath,request.sensor,request.color,
        request.sampleRoi,request.outputRoi,&reason,cancel);
    if(!frame.valid()) {
        result.error=reason.isEmpty()?frame.error:reason;return result;
    }
    QJsonObject comparison;
    const bool hasReference=!request.referencePath.isEmpty();
    if(hasReference) {
        const auto data=verifiedFile(request.referencePath,request.referenceSha256,&reason);
        if(data.isEmpty()){result.error=reason;return result;}
        QJsonParseError parse;
        const auto doc=QJsonDocument::fromJson(data,&parse);
        if(parse.error!=QJsonParseError::NoError||!doc.isObject()) {
            result.error=QStringLiteral("Invalid separately hashed reference JSON");return result;
        }
        comparison=comparePatchMeans(frame.image,request.outputRoi,doc.object(),
                                     request.sensor.sourceSha256,&reason);
        if(comparison.isEmpty()){result.error=reason;return result;}
    }
    result.report=QJsonObject{
        {"schema",1},{"stage","F3-Stage6C2-experimental-real-RAW"},
        {"status",hasReference?"research-patch-comparison":"research-no-independent-reference"},
        {"sourceRawSha256",QString::fromLatin1(request.sensor.sourceSha256)},
        {"sensorCalibrationSha256",QString::fromLatin1(request.sensor.calibrationSha256)},
        {"colorCalibrationSha256",QString::fromLatin1(request.color.profileSha256)},
        {"referenceSha256",hasReference?QString::fromLatin1(request.referenceSha256):QString{}},
        {"sampleRoi",boundsArray(request.sampleRoi)},
        {"outputRoi",boundsArray(request.outputRoi)},
        {"decoderSignature",QString::fromLatin1(frame.decoderSignature)},
        {"floatFingerprint",FloatFrameFingerprint::capture(frame.image)},
        {"floatDiagnostics",frame.diagnostics},
        {"patchComparison",comparison},
        {"realRawTrialExecuted",true},{"cameraCalibrationAccepted",false},
        {"defaultRawReplacementAllowed",false},
        {"warning","Experimental calibration and reference are externally asserted, not independently verified by JixelLight"}};
    return result;
}
