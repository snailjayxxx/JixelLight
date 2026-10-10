#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include "core/preview/PreviewTasks.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QColorSpace>
#include <QByteArrayView>

// Keys for the existing engine boundaries. These describe the actual RGBA64
// decode / FP32 GPU staging path, not an unimplemented float RAW decoder.
struct StageGraph {
    static QString digest(const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    // Diagnostic-only: stream the visible pixel bytes without allocating a
    // full-frame copy or hashing uninitialized row padding. No slider-path cost.
    static QJsonObject outputFingerprint(const QImage &image) {
        if (image.isNull()) return {{"available",false}};
        QByteArray descriptor; QDataStream stream(&descriptor,QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << QStringLiteral("stage-pixels-v1") << QString::fromLatin1(ProcessingPlan::EngineVersion)
               << image.width() << image.height() << int(image.format()) << image.depth();
        QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(descriptor);
        const qsizetype rowBytes=(qsizetype(image.width())*image.depth()+7)/8;
        for (int y=0;y<image.height();++y) hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),rowBytes));
        return {{"available",true},{"pixel_sha256",QString::fromLatin1(hash.result().toHex())},
            {"icc_sha256",digest(image.colorSpace().iccProfile())},{"width",image.width()},{"height",image.height()},
            {"format",int(image.format())},{"depth",image.depth()},{"pixel_order","native QImage; row padding excluded"}};
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
    // The CPU kernel reads both packed float slots and selected double-valued
    // state. Never substitute state.toJson(): it serializes entire LUT payloads.
    // Geometry is already applied to source; monitor ICC and request revisions
    // are presentation/delivery concerns, not point-color dependencies.
    static QString renderKey(const QImage &source, const ProcessingPlan &plan) {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << QStringLiteral("cpu-preview-v1") << QString::fromLatin1(ProcessingPlan::EngineVersion)
               << source.cacheKey() << source.size() << int(source.format())
               << int(plan.encoding) << int(plan.output) << plan.rawSource << plan.baseExposureStops;
        for (const auto &slot : plan.data) stream << slot.x << slot.y << slot.z << slot.w;
        stream << plan.state.hue << plan.state.saturation << plan.state.vibrance;
        for (const auto *curve : {&plan.state.masterCurve, &plan.state.redCurve, &plan.state.greenCurve, &plan.state.blueCurve})
            for (double value : *curve) stream << value;
        const auto lut = plan.data[ProcessingPlan::LookStyle].w > 0 ? plan.state.look.lut : nullptr;
        stream << quint64(reinterpret_cast<quintptr>(lut.get()));
        if (lut) stream << lut->size << lut->digest;
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
    static QString fullScopesKey(const ScopeRequest &request) {
        QByteArray bytes;
        QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_8);
        stream << QStringLiteral("full-scopes-v1:1024:rgb-luma:pre-monitor")
               << renderKey(request.image,request.plan)
               << QJsonDocument(request.geometry.toJson()).toJson(QJsonDocument::Compact);
        return digest(bytes);
    }
};
