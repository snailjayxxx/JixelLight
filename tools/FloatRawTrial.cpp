#include "core/raw/FloatRawTrial.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTemporaryFile>
#include <QDebug>

namespace {
constexpr qint64 kMaxJsonInput=2LL*1024*1024;
QJsonObject readObject(const QString &path,QString *error) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>kMaxJsonInput) {
        *error=QStringLiteral("Input JSON absent, empty or over 2 MiB");return {};
    }
    QJsonParseError parse;
    const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()) {
        *error=QStringLiteral("Malformed input JSON");return {};
    }
    return doc.object();
}
// Refuse replacing an existing report. Stage on the same filesystem, publish
// only after the full JSON was written, and clean up failed temp files.
bool writeNewJson(const QString &path,const QJsonObject &json,QString *error) {
    if(QFileInfo::exists(path)){*error=QStringLiteral("Existing report cannot be replaced");return false;}
    const QFileInfo info(path);
    if(!QDir().mkpath(info.absolutePath())) {*error=QStringLiteral("Cannot create report directory");return false;}
    QTemporaryFile stage(QDir(info.absolutePath()).filePath(QStringLiteral(".jix-float-trial-XXXXXX")));
    if(!stage.open()) {*error=QStringLiteral("Cannot stage report");return false;}
    const QByteArray bytes=QJsonDocument(json).toJson(QJsonDocument::Indented);
    if(stage.write(bytes)!=bytes.size()||!stage.flush()) {
        *error=QStringLiteral("Cannot write complete report");return false;
    }
    const QString temp=stage.fileName();
    stage.close();stage.setAutoRemove(false);
    if(!QFile::rename(temp,path)) {
        QFile::remove(temp);
        *error=QStringLiteral("Cannot publish new report (destination may exist)");
        return false;
    }
    return true;
}
QJsonObject readiness(const QJsonObject &evidence,QString *error) {
    if(evidence.value("schema").toInt()!=1||
       evidence.value("evidenceIntegrityPassed")!=QJsonValue(true)||
       evidence.value("cameraCalibrationAccepted")!=QJsonValue(false)||
       evidence.value("defaultRawReplacementAllowed")!=QJsonValue(false)||
       evidence.value("stage6BRealCameraFidelityValidated")!=QJsonValue(false)||
       evidence.value("source").toObject().value("sceneCount").toInt()<2 ||
       evidence.value("source").toObject().value("heldoutCount").toInt()<1) {
        *error=QStringLiteral("Real-sensor source evidence is incomplete or falsely calibrated");
        return {};
    }
    const auto coverage=evidence.value("coverage").toObject();
    if(!coverage.value("remainingEvidence").isArray()||
       coverage.value("remainingEvidence").toArray().isEmpty()) {
        *error=QStringLiteral("Real-camera coverage gaps must remain explicit");
        return {};
    }
    return {{"schema",1},{"stage","F3-Stage6C2-opt-in-real-RAW"},
            {"status","not-executed"},
            {"reason","No separately verified sensor calibration and linear-D50 reference supplied"},
            {"sourceEvidenceIntegrityPassed",true},
            {"sceneCount",evidence.value("source").toObject().value("sceneCount")},
            {"heldoutCount",evidence.value("source").toObject().value("heldoutCount")},
            {"sourceEvidenceCoverage",coverage},
            {"cameraCalibrationAccepted",false},{"defaultRawReplacementAllowed",false},
            {"realRawTrialExecuted",false},
            {"warning","A passing readiness check is not photographic fidelity validation"}};
}
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const QStringList args=app.arguments();
    if(args.size()!=4 || (args[1]!=QStringLiteral("--run") &&
                          args[1]!=QStringLiteral("--readiness"))) {
        qCritical().noquote()<<"Usage: JixelLightFloatRawTrial --run trial-plan.json new-report.json";
        qCritical().noquote()<<"   or: JixelLightFloatRawTrial --readiness sensor-evidence.json new-report.json";
        return 2;
    }
    QString error;
    if(QFileInfo::exists(args[3])) {
        qCritical().noquote()<<"Refusing existing output report:"<<args[3];return 3;
    }
    const auto source=readObject(args[2],&error);
    if(!error.isEmpty()) {qCritical().noquote()<<error;return 3;}
    QJsonObject report;
    if(args[1]==QStringLiteral("--readiness")) {
        report=readiness(source,&error);
    } else {
        FloatRawTrial::Request request;
        if(FloatRawTrial::parse(source,QFileInfo(args[2]).absolutePath(),&request,&error)) {
            const auto outcome=FloatRawTrial::run(request);
            if(outcome.valid())report=outcome.report;
            else error=outcome.error;
        }
    }
    if(!error.isEmpty()||report.isEmpty()) {
        qCritical().noquote()<<(error.isEmpty()?QStringLiteral("Trial produced no valid report"):error);
        return 3;
    }
    if(!writeNewJson(args[3],report,&error)) {
        qCritical().noquote()<<error;return 4;
    }
    qInfo().noquote()<<QJsonDocument(report).toJson(QJsonDocument::Compact);
    return 0;
}
