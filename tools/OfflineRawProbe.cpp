#include "core/raw/RawDecoder.h"
#include "core/look/CameraReference.h"
#include "core/metadata/MetadataReader.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QDebug>

namespace {
bool hex(const QByteArray &s) {
    if(s.size()!=64)return false;
    for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
QString pathValue(const QString &base,const QJsonValue &value) {
    if(!value.isString())return {};
    const auto name=value.toString();
    if(name.isEmpty()||name.contains(QChar::Null)||
       name.startsWith("http:",Qt::CaseInsensitive)||
       name.startsWith("https:",Qt::CaseInsensitive))return {};
    return QFileInfo(QDir(base).absoluteFilePath(name)).absoluteFilePath();
}
QByteArray expectedDigest(const QJsonValue &value) {
    if(!value.isString())return {};
    const auto digest=value.toString().toLatin1();
    return hex(digest)?digest:QByteArray{};
}
bool hashFile(const QString &path,const QByteArray &wanted) {
    if(!hex(wanted))return false;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()<=0||
       file.size()>2LL*1024*1024*1024)return false;
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if(!digest.addData(&file))return false;
    return digest.result().toHex()==wanted;
}
bool validId(const QString &id) {
    if(id.isEmpty()||id.size()>48)return false;
    for(const QChar &c:id) {
        const ushort n=c.unicode();
        if(!((n>='a'&&n<='z')||(n>='A'&&n<='Z')||
             (n>='0'&&n<='9')||n=='-'||n=='_'))return false;
    }
    return true;
}
QJsonObject fail(const char *why) {
    return {{"schema",1},{"available",false},{"reason",QString::fromLatin1(why)},
            {"cameraCalibrationAccepted",false},{"defaultRawReplacementAllowed",false}};
}
bool writeOnce(const QString &path,const QJsonObject &report) {
    if(QFileInfo::exists(path))return false;
    const QFileInfo info(path);
    if(!QDir().mkpath(info.absolutePath()))return false;
    QTemporaryDir temp(QDir(info.absolutePath()).filePath(".jixel-raw-probe-XXXXXX"));
    if(!temp.isValid())return false;
    const QString staged=QDir(temp.path()).filePath("report.json");
    QFile file(staged);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return false;
    const QByteArray bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);
    if(file.write(bytes)!=bytes.size()||!file.flush())return false;
    file.close();
    // Never replace an already published report or any source file.
    return QFile::rename(staged,path);
}
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=3) {
        qCritical("Usage: JixelLightOfflineRawProbe private-manifest.json NEW-report.json");
        return 2;
    }
    const QFileInfo input(args[1]),output(args[2]);
    if(output.exists()||input.absoluteFilePath()==output.absoluteFilePath()) {
        qCritical("Output must be a new file");return 3;
    }
    QFile manifest(input.absoluteFilePath());
    if(!manifest.open(QIODevice::ReadOnly)||manifest.size()<=0||manifest.size()>256*1024) {
        qCritical("Manifest is missing or exceeds 256 KiB");return 3;
    }
    QJsonParseError parse;
    const auto json=QJsonDocument::fromJson(manifest.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError||!json.isObject()) {
        qCritical("Malformed JSON manifest");return 3;
    }
    const auto document=json.object();
    if(document.value("schema").toInt()!=1||
       document.value("purpose").toString()!="offline-local-raw-sensor-audit-v1") {
        qCritical("Explicit offline-local purpose and schema required");return 3;
    }
    const auto entries=document.value("cases").toArray();
    if(entries.size()<2||entries.size()>12) {
        qCritical("Must provide 2-12 RAW cases with distinct train/validation roles");
        return 3;
    }
    const QString base=input.absoluteDir().absolutePath();
    QSet<QString> identifiers;
    QSet<QByteArray> rawDigests,jpegDigests;
    int training=0,heldout=0,pairedCount=0;
    QJsonArray cases;
    for(const QJsonValue &v:entries) {
        if(!v.isObject()){qCritical("Invalid RAW case object");return 3;}
        const auto item=v.toObject();
        const QString id=item.value("id").toString();
        const QString role=item.value("role").toString();
        if(!validId(id)||identifiers.contains(id)||
           (role!="train"&&role!="validation")) {
            qCritical("Invalid/duplicate case ID or role");return 3;
        }
        identifiers.insert(id);
        if(role=="train")++training;else ++heldout;
        const auto source=item.value("source").toObject();
        const QString rawPath=pathValue(base,source.value("file"));
        const QByteArray rawHash=expectedDigest(source.value("sha256"));
        if(rawPath.isEmpty()||!RawDecoder::isRawFile(rawPath)||
           rawHash.isEmpty()||rawDigests.contains(rawHash)) {
            qCritical("RAW file, hash or unique holdout required");return 3;
        }
        rawDigests.insert(rawHash);
        if(rawPath==output.absoluteFilePath()||rawPath==input.absoluteFilePath()) {
            qCritical("Source/output paths must be distinct");return 3;
        }
        QString error;
        const QJsonObject inspection=RawDecoder::inspectUnpacked(rawPath,rawHash,&error);
        if(!inspection.value("available").toBool()||
           !inspection.value("sensorAudit").toObject().value("available").toBool()) {
            qCritical().noquote()<<"RAW evidence inspection rejected:"
                <<(error.isEmpty()?inspection.value("sensorAudit").toObject().value("reason").toString():error);
            return 3;
        }
        const QVariantMap rawMeta=MetadataReader::read(rawPath,&error);
        if(!error.isEmpty()||rawMeta.value("model").toString().isEmpty()) {
            qCritical("Original RAW EXIF metadata unavailable");return 3;
        }
        const auto expected=item.value("expectedCameraModel").toString();
        if(!expected.isEmpty()&&
           (expected.compare(rawMeta.value("model").toString(),Qt::CaseInsensitive)!=0 ||
            expected.compare(inspection.value("model").toString(),Qt::CaseInsensitive)!=0)) {
            qCritical("RAW model differs from pinned camera identity");return 3;
        }
        QJsonObject paired{{"provided",false},{"verified",false}};
        if(item.contains("jpeg")) {
            const auto companion=item.value("jpeg").toObject();
            const auto jpegPath=pathValue(base,companion.value("file"));
            const auto jpegHash=expectedDigest(companion.value("sha256"));
            if(jpegPath.isEmpty()||jpegHash.isEmpty()||
               jpegPath==rawPath||jpegPath==output.absoluteFilePath()||
               jpegPath==input.absoluteFilePath()||
               jpegDigests.contains(jpegHash)||rawDigests.contains(jpegHash)||
               !QStringList{"jpg","jpeg"}.contains(QFileInfo(jpegPath).suffix().toLower())||
               !hashFile(jpegPath,jpegHash)) {
                qCritical("Companion JPEG is missing, duplicate or fails its SHA-256");
                return 3;
            }
            jpegDigests.insert(jpegHash);
            const QVariantMap jpegMeta=MetadataReader::read(jpegPath,&error);
            QString pairReason;
            if(!error.isEmpty()||
               !CameraReference::matches(rawMeta,jpegMeta,&pairReason)) {
                qCritical().noquote()<<"RAW/JPEG pair identity failed:"
                    <<(pairReason.isEmpty()?QStringLiteral("unreadable JPEG metadata"):pairReason);
                return 3;
            }
            ++pairedCount;
            paired={{"provided",true},{"verified",true},
                    {"jpegSourceSha256",QString::fromLatin1(jpegHash)},
                    {"identityBasis","Exiv2 metadata equality, not sensor-linear color truth"},
                    {"jpegExifWidth",jpegMeta.value("pixelWidth").toLongLong()},
                    {"jpegExifHeight",jpegMeta.value("pixelHeight").toLongLong()},
                    {"rawJpegPixelAligned",false}};
        }
        // Deliberately omit source filename, absolute path, Google Drive IDs,
        // previews, GPS, JPEG/RAW pixels and the owner's personal metadata.
        cases.append(QJsonObject{
            {"id",id},{"role",role},
            {"rawSourceSha256",QString::fromLatin1(rawHash)},
            {"cameraModel",inspection.value("model")},
            {"isoText",rawMeta.value("iso").toString()},
            {"shutterText",rawMeta.value("shutter").toString()},
            {"apertureText",rawMeta.value("aperture").toString()},
            {"rawVisibleWidth",inspection.value("active_sensor_bounds").toArray().at(2)},
            {"rawVisibleHeight",inspection.value("active_sensor_bounds").toArray().at(3)},
            {"preDemosaic",inspection},
            {"cameraJpeg",paired}});
    }
    if(training==0||heldout==0) {
        qCritical("Separate training and held-out captures required");return 3;
    }
    const QJsonObject report{
        {"schema",1},{"stage","F3-Stage6C3-offline-private-unpacked-audit"},
        {"evidenceIntegrityPassed",true},
        {"sceneCount",cases.size()},{"trainCount",training},
        {"heldoutCount",heldout},{"pairedCameraJpegCount",pairedCount},
        {"cases",cases},
        {"realRawSourceUnpackPassed",true},{"fullRgbDevelopExecuted",false},
        {"realFP32TrialExecuted",false},{"cameraCalibrationAccepted",false},
        {"defaultRawReplacementAllowed",false},
        {"warning","A SHA-pinned pre-demosaic U16 sensor audit is not a RAW color profile, full RGB development or highlight recovery"}
    };
    if(!writeOnce(output.absoluteFilePath(),report)) {
        qCritical("Could not safely publish a new report");return 4;
    }
    qInfo().noquote()<<QString("Offline RAW sensor audit accepted %1 cases (%2 paired JPEG)")
            .arg(cases.size()).arg(pairedCount);
    return 0;
}
