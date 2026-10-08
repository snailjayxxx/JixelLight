#pragma once
#include "core/library/CatalogTags.h"
#include "core/pipeline/AdjustmentState.h"

// An explicit import patch: absent standard properties leave existing values
// alone. JixelLight snapshots carry the complete state, never Adobe CRS edits.
namespace XmpSidecar {
struct Document {
    AdjustmentState adjustments;
    CatalogTags tags;
    int rating = 0;
    QString flag = "none";
    bool hasAdjustments = false, hasRating = false, hasFlag = false;
    bool hasKeywords = false, hasLabel = false, hasAlbums = false;
    QStringList warnings;
};
bool read(const QString &path, Document *out, QString *error);
bool writeNew(const QString &path, const AdjustmentState &state, const CatalogTags &tags,
              int rating, const QString &flag, QString *error);
}
