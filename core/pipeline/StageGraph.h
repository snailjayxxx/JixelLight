#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/preview/PreviewTasks.h"
#include "core/scopes/ScopePlot.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QColorSpace>
#include <QByteArrayView>

// Keys for the existing engine boundaries. Real camera RAW still decodes to
// RGBA64; optional explicit FP32 scene inputs now preserve float precision
// through manual geometry, tone processing, and GPU staging. This is not a
// native floating-point LibRaw decoder.
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
        if (!BrushMaskDocument::validDocument(plan.state.brushMasks)) return {};
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
        if (BrushMaskTileRaster::anyEnabled(plan.state.brushMasks)) {
            QJsonObject mask;
            if (!BrushMaskDocument::toJson(plan.state.brushMasks,&mask)) return {};
            stream << QStringLiteral("brush-cpu-research-m44a")
                   << QJsonDocument(mask).toJson(QJsonDocument::Compact);
        }
        return digest(bytes);
    }
    static QJsonObject describe(const QString &sourceKey, const PrepareRequest &request,
                                const AdjustmentState &state, bool raw, float baseExposure) {
        const QString prepared = prepareKey(request);
        const bool sourceFp32=request.image.format()==QImage::Format_RGBA32FPx4;
        const QByteArray renderDependencies = QJsonDocument(QJsonObject{
            {"prepare", prepared}, {"adjustments", state.toJson()}, {"raw", raw},
            {"baseExposure", baseExposure}, {"output", "srgb"},
            {"engine", QString::fromLatin1(ProcessingPlan::EngineVersion)}}).toJson(QJsonDocument::Compact);
        const QString rendered = digest(renderDependencies);
        return {{"schema", 1}, {"source", sourceKey}, {"prepare", prepared}, {"render", rendered},
                {"scopes", digest((rendered + ":1024:rgb-luma:pre-monitor").toUtf8())},
                {"source_storage", sourceFp32
                     ? "RGBA32FPx4 explicit scene; CPU geometry/resampling FP32; GPU staging FP32"
                     : "RGBA64 camera decoded source; GPU staging FP32"},
                {"preview_resize",QJsonObject{
                    {"backend",sourceFp32?"cpu-fp32-reference":"qt-image-smooth"},
                    {"filter",sourceFp32
                        ? "separable normalized area-weighted downsample / bilinear upscale; scene-linear associated alpha"
                        : "Qt SmoothTransformation (legacy integer input)"},
                    {"precision",sourceFp32
                        ? "RGBA32FPx4 unassociated, RGB unclipped; double-precision weighted accumulation"
                        : "historical QImage integer presentation"},
                    {"gpu_max_edge",4096},{"fast_max_edge",1024},{"monitor_icc",false}}},
                {"geometry",QJsonObject{{"backend","cpu"},{"settings",request.geometry.toJson()},
                    {"order","manual lens/CA -> perspective -> straighten -> crop -> orientation -> color"},
                    {"corrections_active",request.geometry.hasCorrections()},{"input_fp32",sourceFp32},
                     {"sampling",sourceFp32
                          ? "associated-alpha bilinear FP32 (double accumulation); RGB unassociated, unclipped; conservative inscribed trim"
                          : "premultiplied-alpha bilinear RGBA64; conservative inscribed trim"},
                    {"lens_profile",false},{"monitor_icc",false}}},
                {"black_white",QJsonObject{{"schema",1},{"enabled",state.blackWhite},
                     {"mix_hundredth_stops",AdjustmentState::toJsonArray(state.bwMix)},
                     {"backend","CPU reference / QRhi color compute"},
                     {"order","perceptual color -> Sony styling -> B&W mix -> target gamut"},
                     {"monitor_icc",false}}},
                 {"color_grading",QJsonObject{{"schema",1},{"enabled",state.gradingEnabled},
                     {"hue_degrees",AdjustmentState::toJsonArray(state.gradingHue)},
                     {"saturation",AdjustmentState::toJsonArray(state.gradingSaturation)},
                     {"balance",state.gradingBalance},{"backend","CPU reference / QRhi color compute"},
                     {"operation","Oklab a/b: three smoothly weighted tone zones; no monitor ICC"},
                     {"order","perceptual look -> optional B&W -> grading -> output transform"}}},
                 {"brush_draft",QJsonObject{{"schema",BrushMaskDocument::Schema},
                     {"layer_count",int(state.brushMasks.layers.size())},
                     {"active_rendering",BrushMaskTileRaster::anyEnabled(state.brushMasks)},
                     {"state",BrushMaskTileRaster::anyEnabled(state.brushMasks)
                         ? "research-in-memory-only; cannot save or enable in UI"
                         : "stored-disabled-draft-only"},
                     {"backend",BrushMaskTileRaster::anyEnabled(state.brushMasks)
                         ? "bounded CPU tile research; GPU CPU numeric fallback"
                         : "not enabled; opt-in C++ draft storage"},
                     {"coordinates","normalized post-geometry/crop/orientation"},
                     {"warning","active brush is restricted to research in-memory source; project/XMP/CLI reject enabled brush until production acceptance"}}},
                 {"radial_mask",QJsonObject{{"schema",1},
                    {"enabled",state.radialMaskEnabled},{"inverted",state.radialMaskInverted},
                    {"exposure_ev",state.radialMaskExposure},
                    {"center_x",state.radialMaskCenterX},{"center_y",state.radialMaskCenterY},
                    {"radius_x",state.radialMaskRadiusX},{"radius_y",state.radialMaskRadiusY},
                    {"feather",state.radialMaskFeather},
                    {"backend","CPU reference / QRhi compute"},
                    {"order","camera WB/global EV -> linear mask -> radial mask -> vignette"},
                    {"coordinates","normalized post-geometry/crop/orientation with full-frame viewport anchoring"},
                    {"operation","elliptic distance -> interior smoothstep, optional invert -> scene-linear EV"},
                    {"alpha","unchanged"},{"monitor_icc",false},{"automatic_selection",false}}},
                {"linear_mask",QJsonObject{{"schema",1},
                    {"enabled",state.linearMaskEnabled},{"exposure_ev",state.linearMaskExposure},
                    {"center",state.linearMaskCenter},{"feather",state.linearMaskFeather},
                    {"angle_degrees",state.linearMaskAngle},
                    {"backend","CPU reference / QRhi compute"},
                    {"order","camera WB and global EV -> linear mask -> vignette -> highlight recovery"},
                    {"coordinates","normalized post-geometry/crop/orientation with full-frame viewport anchoring"},
                    {"alpha","unchanged"},{"limitations","one linear gradient only, plus independent radial gradient; no AI/brush/multi-layer combination"}}},
                {"vignette",QJsonObject{{"schema",1},{"amount_ev",state.vignetteAmount},
                    {"midpoint",state.vignetteMidpoint},{"feather",state.vignetteFeather},
                    {"active",state.vignetteAmount!=0},{"backend","CPU reference / QRhi color compute"},
                    {"order","WB/exposure -> scene-linear vignette -> highlight recovery -> tone"},
                    {"coordinates","normalized post-geometry, committed crop and orientation; viewport preserves full-image position"},
                    {"operation","RGB * exp2(amount * smoothstep); alpha unchanged"},
                    {"lens_profile",false},{"monitor_icc",false}}},
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
    static QString scopePlotsKey(const ScopePlotRequest &request) {
        QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_8);
        stream << QStringLiteral("scope-plots-v1:wave256x1024:parade768x1024:chroma512x512:pre-monitor")
            << renderKey(request.source,request.plan) << QJsonDocument(request.geometry.toJson()).toJson(QJsonDocument::Compact) << request.fullResolution;
        return digest(bytes); // Mode selects ink from the same completed batch; revisions are delivery-only.
    }
};
