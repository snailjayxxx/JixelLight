#include "XmpSidecar.h"
#include "core/look/LookProfiles.h"
#include "core/pipeline/ProcessingPlan.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <optional>

namespace {
const QString Rdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
const QString Xmp = "http://ns.adobe.com/xap/1.0/";
const QString Dc = "http://purl.org/dc/elements/1.1/";
const QString Jixel = "https://github.com/snailjayxxx/JixelLight/ns/1.0/";
constexpr qint64 Limit = 64 * 1024 * 1024;
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool validState(const QJsonObject &json, AdjustmentState *state) {
    *state = AdjustmentState::fromJson(json);
    return !json.isEmpty() && state->look.error.isEmpty()
        && LookProfiles::engineCompatible(state->look) && state->toJson() == json;
}
bool snapshot(const QString &text, XmpSidecar::Document *out, QString *error) {
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(text.toUtf8(), &parse);
    const auto root = doc.object();
    const auto rating = root.value("rating");
    const auto flag = root.value("flag").toString();
    XmpSidecar::Document result;
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || root.size() != 6
        || root.value("schema").toDouble() != 1 || root.value("engine").toString() != ProcessingPlan::EngineVersion
        || !validState(root.value("adjustments").toObject(), &result.adjustments)
        || !CatalogTags::fromJson(root.value("tags").toObject(), &result.tags)
        || !rating.isDouble() || rating.toDouble() != rating.toInt(-1) || rating.toInt(-1) < 0 || rating.toInt() > 5
        || !QStringList{"none","pick","reject"}.contains(flag))
        return fail(error, "Invalid or incompatible JixelLight XMP snapshot");
    result.rating = rating.toInt(); result.flag = flag;
    result.hasAdjustments = result.hasRating = result.hasFlag = true;
    result.hasKeywords = result.hasLabel = result.hasAlbums = true;
    *out = result; return true;
}
QString property(const QString &ns, const QString &name) {
    if (ns == Xmp && (name == "Rating" || name == "Label")) return name;
    if (ns == Dc && name == "subject") return "keywords";
    if (ns == Jixel && name == "Snapshot") return "snapshot";
    return {};
}
}

bool XmpSidecar::read(const QString &path, Document *out, QString *error) {
    if (error) error->clear();
    if (!out) return fail(error, "Missing XMP destination");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > Limit) return fail(error, "Cannot read XMP (64 MiB limit)");
    // Never expand external entities or a document type. Depth and item limits
    // also apply inside unrecognized namespaces; no skipCurrentElement bypass.
    QXmlStreamReader xml(&file); xml.setEntityExpansionLimit(1024);
    QStringList stack;
    QSet<QString> seen;
    QHash<QString,QString> values;
    QString active, text;
    QStringList keywords;
    int propertyDepth = -1, descriptionDepth = -1;
    bool bagSeen = false, rdfSeen = false, descriptionSeen = false, adobeEdits = false;
    auto take = [&](const QString &key, const QString &value) {
        if (seen.contains(key)) return false;
        seen.insert(key); values.insert(key,value); return true;
    };
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (file.pos() > Limit) return fail(error,"XMP exceeds 64 MiB");
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            return fail(error, "XMP document types and entities are not supported");
        if (token == QXmlStreamReader::StartElement) {
            const auto ns = xml.namespaceUri().toString(), name = xml.name().toString();
            const auto qualified = ns + "|" + name;
            const auto parent = stack.isEmpty() ? QString{} : stack.last();
            stack.append(qualified);
            if (stack.size() > 64) return fail(error, "XMP nesting limit exceeded");
            if (ns == Rdf && name == "RDF") {
                if (rdfSeen || stack.size() > 2) return fail(error, "Unsupported XMP RDF structure");
                rdfSeen = true;
            }
            if (ns == Rdf && name == "Description" && parent == Rdf+"|RDF") {
                descriptionSeen = true;
                descriptionDepth = stack.size();
                if (!xml.attributes().value(Rdf,"about").isEmpty()) return fail(error, "XMP must describe the current resource");
                for (const auto &attribute : xml.attributes()) {
                    const auto key = property(attribute.namespaceUri().toString(),attribute.name().toString());
                    if (attribute.namespaceUri() == "http://ns.adobe.com/camera-raw-settings/1.0/") adobeEdits = true;
                    if (key == "keywords") return fail(error,"XMP keywords must use an RDF Bag");
                    if (!key.isEmpty() && !take(key,attribute.value().toString())) return fail(error,"Duplicate XMP property");
                }
            } else if (parent == Rdf+"|Description" && stack.size() == descriptionDepth+1 && active.isEmpty()) {
                if (ns == "http://ns.adobe.com/camera-raw-settings/1.0/") adobeEdits = true;
                const auto key = property(ns,name);
                if (!key.isEmpty()) {
                    if (!xml.attributes().isEmpty() || seen.contains(key)) return fail(error,"Invalid or duplicate XMP property");
                    active = key; propertyDepth = stack.size(); text.clear(); bagSeen = false;
                }
            } else if (!active.isEmpty()) {
                const int depth = stack.size()-propertyDepth;
                if (active != "keywords" || !xml.attributes().isEmpty()
                    || (depth == 1 && (ns != Rdf || name != "Bag" || bagSeen))
                    || (depth == 2 && (ns != Rdf || name != "li" || !bagSeen)) || depth > 2)
                    return fail(error,"Unsupported structured XMP property");
                if (depth == 1) bagSeen = true;
                if (depth == 2) text.clear();
            }
        } else if (token == QXmlStreamReader::Characters && !active.isEmpty()) {
            if (active != "keywords" || stack.size() == propertyDepth+2) text += xml.text();
            else if (!xml.isWhitespace()) return fail(error,"Invalid XMP keyword list");
        } else if (token == QXmlStreamReader::EndElement) {
            if (active == "keywords" && stack.size() == propertyDepth+2) {
                keywords.append(text); text.clear();
                if (keywords.size() > 64) return fail(error,"Too many XMP keywords (maximum 64)");
            }
            if (!active.isEmpty() && stack.size() == propertyDepth) {
                if (active == "keywords" && !bagSeen) return fail(error,"XMP keywords require an RDF Bag");
                if (!take(active,text)) return fail(error,"Duplicate XMP property");
                active.clear(); propertyDepth = -1;
            }
            if (stack.size() == descriptionDepth) descriptionDepth = -1;
            stack.removeLast();
        }
    }
    if (xml.hasError() || !rdfSeen || !descriptionSeen) return fail(error,"Malformed XMP: "+xml.errorString());
    Document result;
    if (values.contains("snapshot") && !snapshot(values.value("snapshot"),&result,error)) return false;
    if (values.contains("Rating")) {
        bool ok = false; const double number = values.value("Rating").trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(number) || number != std::floor(number) || number < -1 || number > 5)
            return fail(error,"Only whole-star XMP ratings -1 through 5 are supported");
        // Public properties are authoritative when another application updates
        // them. Preserve a private star rating on reject, which XMP encodes -1.
        if (number == -1) { result.flag = "reject"; result.hasFlag = true; }
        else {
            result.rating = int(number); result.hasRating = true;
            if (!result.hasFlag || result.flag == "reject") { result.flag = "none"; result.hasFlag = true; }
        }
    }
    if (values.contains("Label")) {
        const auto label = values.value("Label").trimmed().toLower();
        if (label.isEmpty() || CatalogTags::validLabel(label)) {
            result.tags.label = label.isEmpty() ? "none" : label; result.hasLabel = true;
        } else { result.hasLabel = false; result.warnings.append("Custom XMP label was not mapped to a color"); }
    }
    if (values.contains("keywords")) {
        if (!CatalogTags::normalize(&keywords,64)) return fail(error,"Invalid XMP keywords");
        result.tags.keywords = keywords; result.hasKeywords = true;
    }
    if (adobeEdits) result.warnings.append("Adobe Camera Raw adjustments were not applied");
    if (!result.hasAdjustments && !result.hasRating && !result.hasFlag && !result.hasKeywords && !result.hasLabel)
        return fail(error,"No supported metadata or JixelLight adjustments in XMP");
    *out = result; return true;
}

bool XmpSidecar::writeNew(const QString &path, const AdjustmentState &state, const CatalogTags &tags,
                          int rating, const QString &flag, QString *error) {
    if (error) error->clear();
    const QFileInfo target(path);
    if (path.isEmpty() || target.suffix().compare("xmp",Qt::CaseInsensitive) || target.exists() || target.isSymLink()
        || !QDir(target.absolutePath()).exists()) return fail(error,"Choose a new .xmp file in an existing folder");
    const auto payload = QJsonDocument(QJsonObject{{"schema",1},{"engine",ProcessingPlan::EngineVersion},
        {"adjustments",state.toJson()},{"tags",tags.toJson()},{"rating",rating},{"flag",flag}}).toJson(QJsonDocument::Compact);
    Document checked;
    if (payload.size() > Limit/2 || !state.look.error.isEmpty() || !snapshot(QString::fromUtf8(payload),&checked,error))
        return fail(error,"Cannot export invalid or oversized XMP snapshot");
    QByteArray bytes; QXmlStreamWriter xml(&bytes); xml.setAutoFormatting(true);
    xml.writeStartDocument(); xml.writeStartElement("x:xmpmeta"); xml.writeNamespace("adobe:ns:meta/","x");
    xml.writeStartElement("rdf:RDF"); xml.writeNamespace(Rdf,"rdf");
    xml.writeStartElement("rdf:Description"); xml.writeAttribute("rdf:about","");
    xml.writeNamespace(Xmp,"xmp"); xml.writeNamespace(Dc,"dc"); xml.writeNamespace(Jixel,"jixel");
    xml.writeTextElement("xmp:Rating",QString::number(flag == "reject" ? -1 : rating));
    auto label = tags.label == "none" ? QString{} : tags.label;
    if (!label.isEmpty()) label[0] = label[0].toUpper();
    xml.writeTextElement("xmp:Label",label);
    xml.writeStartElement("dc:subject"); xml.writeStartElement("rdf:Bag");
    for (const auto &keyword : tags.keywords) xml.writeTextElement("rdf:li",keyword);
    xml.writeEndElement(); xml.writeEndElement();
    xml.writeTextElement("jixel:Snapshot",QString::fromUtf8(payload));
    xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndDocument();
    if (xml.hasError() || bytes.size() > Limit) return fail(error,"Cannot encode XMP snapshot");
    QTemporaryDir staging(QDir(target.absolutePath()).filePath(".jixellight-xmp-XXXXXX"));
    if (!staging.isValid()) return fail(error,"Cannot create XMP staging directory");
    QFile file(staging.filePath("sidecar.xmp"));
    if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(bytes) != bytes.size() || !file.flush())
        return fail(error,file.errorString());
    file.close();
    if (!file.rename(target.absoluteFilePath())) return fail(error,file.errorString());
    return true;
}
