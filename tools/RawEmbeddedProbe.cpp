#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/look/CameraReference.h"
#include "core/look/LookCalibration.h"
#include "core/metadata/MetadataReader.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/raw/RawDecoder.h"

namespace {
bool writeJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(object).toJson();
    return file.write(bytes) == bytes.size() && file.commit();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 3) {
        qCritical("Usage: JixelLightRawEmbeddedProbe RAW output-directory");
        return 2;
    }
    const QString rawPath = QFileInfo(args[1]).absoluteFilePath();
    const QString outDir = args[2];
    if (!RawDecoder::isRawFile(rawPath) || !QDir().mkpath(outDir)) return 2;

    QString metadataError;
    const QVariantMap metadata = MetadataReader::read(rawPath, &metadataError);
    QString decodeError;
    RawMetadata rawMetadata;
    QImage raw = RawDecoder::decode(rawPath, &decodeError, &rawMetadata);

    CameraReferenceResult reference;
    if (!raw.isNull()) reference = CameraReference::load({rawPath, {}, metadata, 1});

    QJsonObject report{
        {"schema", 1},
        {"commit", JIXELLIGHT_GIT_COMMIT},
        {"engineVersion", ProcessingPlan::EngineVersion},
        {"metadataError", metadataError},
        {"decodeError", decodeError},
        {"make", rawMetadata.make},
        {"model", rawMetadata.model},
        {"width", raw.width()},
        {"height", raw.height()},
        {"probeAdjustMaximumThr", raw.text("JixelLightProbeAdjustMaximumThr")},
        {"probeLibRawHighlight", raw.text("JixelLightProbeLibRawHighlight")},
        {"reference", QJsonObject::fromVariantMap(reference.info)},
        {"referenceError", reference.error}
    };

    if (!raw.isNull() && !reference.image.isNull()) {
        QVariantMap geometry;
        QImage aligned = calibrationSource(raw, reference.image, &geometry);
        if (!aligned.isNull()) {
            QImage small = aligned.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QImage baseline = ImagePipeline::process(small, {}, ImagePipeline::InputEncoding::LinearProPhoto);
            baseline.save(outDir + "/baseline.png");
            reference.image.save(outDir + "/reference.png");
            report["geometry"] = QJsonObject::fromVariantMap(geometry);
            report["baseGain"] = baseline.text("JixelLightProbeRawBaseGain");
        }
        LookCalibrationResult fit = calibrateLook({raw, reference.image, 0, 0, reference.info});
        report["fit"] = QJsonObject::fromVariantMap(fit.report);
        report["fitError"] = fit.error;
        report["fitAccepted"] = bool(fit.lut);
    }

    if (!writeJson(outDir + "/report.json", report)) return 3;
    qInfo().noquote() << QString::fromUtf8(QJsonDocument(report).toJson());
    return raw.isNull() || reference.image.isNull() ? 1 : 0;
}
