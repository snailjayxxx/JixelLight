#pragma once
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QVector>

struct CatalogVersionSelection { QVector<qsizetype> indices; QString error; };

// Catalog keys are saved identities, not filenames to normalize or case-fold.
// Selection preserves catalog order and never reads sources or modifies records.
inline CatalogVersionSelection selectCatalogVersions(const QStringList &catalogKeys,const QStringList &requestedKeys={}) {
    CatalogVersionSelection result;
    auto fail=[&](const QString &error) { result.indices.clear(); result.error=error; return result; };
    if (requestedKeys.size()>1000) return fail("Choose at most 1000 catalog keys");
    QHash<QString,qsizetype> positions;
    for (qsizetype i=0;i<catalogKeys.size();++i) {
        const auto &key=catalogKeys[i];
        if (key.isEmpty() || positions.contains(key)) return fail("Catalog contains an empty or ambiguous version key");
        positions.insert(key,i);
    }
    QSet<QString> requested;
    for (const auto &key : requestedKeys) {
        if (key.isEmpty()) return fail("Catalog selection contains an empty key");
        if (requested.contains(key)) return fail("Duplicate requested catalog key: "+key);
        if (!positions.contains(key)) return fail("Unknown catalog key: "+key);
        requested.insert(key);
    }
    result.indices.reserve(requestedKeys.isEmpty()?catalogKeys.size():requestedKeys.size());
    for (qsizetype i=0;i<catalogKeys.size();++i)
        if (requestedKeys.isEmpty() || requested.contains(catalogKeys[i])) result.indices.push_back(i);
    return result;
}
