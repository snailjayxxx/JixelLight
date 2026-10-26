#pragma once

#include "core/pipeline/BrushMaskGeometry.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

// Bounded canonical mask intent for projects, presets, history and commands.
// Pixel work still requires the separate production admission boundary.
// An unsupported future schema must never be imported as an empty mask.
namespace BrushMaskDocument {
constexpr int Schema = 1;
constexpr std::size_t MaxLayers = 16;
constexpr std::size_t MaxTotalDabs = 16384;

struct Layer {
    QString id;
    bool enabled = false;
    double exposureEv = 0;
    std::vector<BrushMaskGeometry::Stroke> strokes;
};
struct Document { std::vector<Layer> layers; };

inline bool validId(const QString &id) {
    if (id.isEmpty() || id.size() > 64) return false;
    for (const auto ch : id) {
        const ushort c = ch.unicode();
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
inline bool validDocument(const Document &doc) {
    if (doc.layers.size() > MaxLayers) return false;
    QSet<QString> identities;
    std::size_t totalDabs = 0;
    for (const auto &layer : doc.layers) {
        if (!validId(layer.id) || identities.contains(layer.id)
            || !std::isfinite(layer.exposureEv) || layer.exposureEv < -3
            || layer.exposureEv > 3 || layer.strokes.empty()
            || layer.strokes.size() > BrushMaskGeometry::MaxStrokes) return false;
        identities.insert(layer.id);
        for (const auto &stroke : layer.strokes) {
            if (!BrushMaskGeometry::validStroke(stroke)) return false;
            totalDabs += stroke.dabs.size();
            if (totalDabs > MaxTotalDabs) return false;
        }
    }
    return true;
}

// Returns true only after fully validating, preserving *out on any failure.
// An empty document produces {}, which callers must OMIT rather than persist
// as a redundant optional field. No files or project database are modified.
inline bool toJson(const Document &doc, QJsonObject *out) {
    if (!out || !validDocument(doc)) return false;
    if (doc.layers.empty()) { *out={}; return true; }
    QJsonArray layers;
    for (const auto &layer : doc.layers) {
        QJsonArray strokes;
        for (const auto &stroke : layer.strokes) {
            QJsonArray dabs;
            for (const auto &dab : stroke.dabs)
                dabs.append(QJsonArray{dab.center.x,dab.center.y,dab.pressure});
            strokes.append(QJsonObject{
                {"radius",stroke.style.radius}, {"hardness",stroke.style.hardness},
                {"opacity",stroke.style.opacity}, {"dabs",dabs}});
        }
        layers.append(QJsonObject{
            {"id",layer.id}, {"enabled",layer.enabled},
            {"exposure",layer.exposureEv}, {"strokes",strokes}});
    }
    *out=QJsonObject{{"schema",Schema},{"layers",layers}};
    return true;
}

// An ABSENT optional field is the legacy default. Explicit null, {}, [],
// unknown/extra fields, future versions, malformed numeric triples or empty
// layers fail closed. A failed parse does not touch the existing document.
inline bool fromJson(const QJsonValue &value, Document *out) {
    if (!out) return false;
    if (value.isUndefined()) { *out={}; return true; }
    if (!value.isObject()) return false;
    const auto root=value.toObject();
    if (root.size()!=2 || !root.value("schema").isDouble()
        || root.value("schema").toDouble()!=Schema
        || !root.value("layers").isArray()) return false;
    const auto layers=root.value("layers").toArray();
    if (layers.size()==0 || layers.size()>int(MaxLayers)) return false;
    Document parsed;
    std::size_t totalDabs=0;
    for (const auto &layerValue : layers) {
        if (!layerValue.isObject()) return false;
        const auto source=layerValue.toObject();
        if (source.size()!=4 || !source.value("id").isString()
            || !source.value("enabled").isBool()
            || !source.value("exposure").isDouble()
            || !source.value("strokes").isArray()) return false;
        Layer layer;
        layer.id=source.value("id").toString();
        layer.enabled=source.value("enabled").toBool();
        layer.exposureEv=source.value("exposure").toDouble();
        const auto strokes=source.value("strokes").toArray();
        if (!validId(layer.id) || !std::isfinite(layer.exposureEv)
            || layer.exposureEv < -3 || layer.exposureEv > 3
            || strokes.size()==0 || strokes.size()>int(BrushMaskGeometry::MaxStrokes))
            return false;
        for (const auto &strokeValue : strokes) {
            if (!strokeValue.isObject()) return false;
            const auto strokeObject=strokeValue.toObject();
            if (strokeObject.size()!=4 || !strokeObject.value("radius").isDouble()
                || !strokeObject.value("hardness").isDouble()
                || !strokeObject.value("opacity").isDouble()
                || !strokeObject.value("dabs").isArray()) return false;
            BrushMaskGeometry::Stroke stroke;
            stroke.style={
                strokeObject.value("radius").toDouble(),
                strokeObject.value("hardness").toDouble(),
                strokeObject.value("opacity").toDouble()};
            const auto dabs=strokeObject.value("dabs").toArray();
            if (!BrushMaskGeometry::validStyle(stroke.style)
                || dabs.size()==0 || dabs.size()>int(BrushMaskGeometry::MaxDabsPerStroke))
                return false;
            totalDabs+=std::size_t(dabs.size());
            if (totalDabs>MaxTotalDabs) return false;
            stroke.dabs.reserve(std::size_t(dabs.size()));
            for (const auto &dabValue : dabs) {
                if (!dabValue.isArray()) return false;
                const auto triple=dabValue.toArray();
                if (triple.size()!=3 || !triple[0].isDouble()
                    || !triple[1].isDouble() || !triple[2].isDouble()) return false;
                BrushMaskGeometry::Dab dab{
                    {triple[0].toDouble(),triple[1].toDouble()},
                    triple[2].toDouble()};
                if (!BrushMaskGeometry::validDab(dab)) return false;
                stroke.dabs.push_back(dab);
            }
            layer.strokes.push_back(std::move(stroke));
        }
        parsed.layers.push_back(std::move(layer));
    }
    if (!validDocument(parsed)) return false;
    *out=std::move(parsed);
    return true;
}

// Synthetic reference for later tiled scene-linear compositing:
// exp2(weightedExposureStops) is the intended RGB gain. Intra-stroke dabs
// never build up by sample density; strokes within each layer union alpha.
// Disabled layers retain all intent but have no effect.
inline std::optional<double> weightedExposureStops(const Document &doc,
    BrushMaskGeometry::Point pixel, int width, int height) {
    if (!validDocument(doc) || !BrushMaskGeometry::validFrame(width,height)
        || !BrushMaskGeometry::validPoint(pixel)) return std::nullopt;
    double stops=0;
    for (const auto &layer : doc.layers) {
        if (!layer.enabled || layer.exposureEv==0) continue;
        const auto coverage=BrushMaskGeometry::combinedOpacity(
            layer.strokes,pixel,width,height);
        if (!coverage) return std::nullopt;
        stops+=layer.exposureEv*(*coverage);
    }
    return stops;
}
}
