#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>

// Catalog-only annotations never enter Develop state, RAW metadata or exports.
struct CatalogTags {
    QStringList keywords, albums;
    QString label = QStringLiteral("none");
    static bool validLabel(const QString &s) {
        return QStringList{"none", "red", "yellow", "green", "blue", "purple"}.contains(s);
    }
    static bool normalize(QStringList *items, int maximum) {
        QStringList normalized;
        for (const auto &raw : *items) {
            const auto item = raw.trimmed();
            if (item.isEmpty()) continue;
            if (item.size() > 80) return false;
            for (const auto c : item) if (c.category() == QChar::Other_Control) return false;
            if (!normalized.contains(item)) normalized.append(item);
        }
        if (normalized.size() > maximum) return false;
        normalized.sort(); *items = normalized; return true;
    }
    QJsonObject toJson() const {
        return {{"schema",1}, {"keywords",QJsonArray::fromStringList(keywords)},
                {"albums",QJsonArray::fromStringList(albums)}, {"label",label}};
    }
    static bool fromJson(const QJsonObject &json, CatalogTags *out) {
        if (json.value("schema").toDouble() != 1 || !json.value("keywords").isArray()
            || !json.value("albums").isArray() || !json.value("label").isString()) return false;
        CatalogTags tags; tags.label = json.value("label").toString();
        for (const auto &v : json.value("keywords").toArray()) { if (!v.isString()) return false; tags.keywords.append(v.toString()); }
        for (const auto &v : json.value("albums").toArray()) { if (!v.isString()) return false; tags.albums.append(v.toString()); }
        if (!validLabel(tags.label) || !normalize(&tags.keywords,64) || !normalize(&tags.albums,32)
            || tags.toJson() != json) return false;
        *out = tags; return true;
    }
};
