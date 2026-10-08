#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include "core/preview/PreviewTasks.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>

// Keys for the existing engine boundaries. These describe the actual RGBA64
// decode / FP32 GPU staging path, not an unimplemented float RAW decoder.
struct StageGraph {
    static QString digest(const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    static QString prepareKey(const PrepareRequest &request) {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << QStringLiteral("prepare-v1") << QString::fromLatin1(ProcessingPlan::EngineVersion)
               << request.image.cacheKey() << request.image.size() << int(request.image.format())
               << QJsonDocument(request.geometry.toJson()).toJson(QJsonDocument::Compact)
               << request.viewport << request.zoom << request.centerX << request.centerY << request.fullResolution;
        return digest(bytes);
    }
    static QJsonObject describe(const QString &sourceKey, const PrepareRequest &request,
                                const AdjustmentState &state, bool raw, float baseExposure) {
        const QString prepared = prepareKey(request);
        const QByteArray renderDependencies = QJsonDocument(QJsonObject{
            {"prepare", prepared}, {"adjustments", state.toJson()}, {"raw", raw},
            {"baseExposure", baseExposure}, {"output", "srgb"},
            {"engine", QString::fromLatin1(ProcessingPlan::EngineVersion)}}).toJson(QJsonDocument::Compact);
        const QString rendered = digest(renderDependencies);
        return {{"schema", 1}, {"source", sourceKey}, {"prepare", prepared}, {"render", rendered},
                {"scopes", digest((rendered + ":1024:rgb-luma:pre-monitor").toUtf8())},
                {"source_storage", "RGBA64; GPU staging FP32"},
                {"monitor_icc", "presentation-only; excluded from render/scopes keys"}};
    }
};
