#pragma once
#include "core/library/CatalogTags.h"
#include "core/pipeline/AdjustmentState.h"
#include <QStringList>
#include <QByteArray>
#include <atomic>

// An explicit import patch: absent standard properties leave existing values
// alone. JixelLight snapshots carry the complete state, never Adobe CRS edits.
namespace XmpSidecar {
// Read-only sibling detection; it never imports, applies or rewrites XMP.
// A unique match may preselect a file in the existing explicit import dialog.
struct Companion {
    QString status = "none"; // none | unique | ambiguous | unsafe
    QString selectedPath;
    QStringList candidates;
};
Companion discoverCompanion(const QString &photoPath);
struct Document {
    AdjustmentState adjustments;
    CatalogTags tags;
    int rating = 0;
    QString flag = "none";
    bool hasAdjustments = false, hasRating = false, hasFlag = false;
    bool hasKeywords = false, hasLabel = false, hasAlbums = false;
    QStringList warnings;
};
// Field-level import impact for explicit confirmation. Absent XMP properties
// never overwrite catalog or Develop values.
struct ImportImpact {
    bool adjustments = false;
    bool rating = false;
    bool flag = false;
    bool keywords = false;
    bool label = false;
    bool albums = false;
    bool any() const noexcept {
        return adjustments || rating || flag || keywords || label || albums;
    }
    bool catalogChanges() const noexcept {
        return rating || flag || keywords || label || albums;
    }
};
ImportImpact previewChanges(const Document &incoming, const AdjustmentState &current,
                            const CatalogTags &tags, int rating, const QString &flag);
bool read(const QString &path, Document *out, QString *error);
// Parse the same bounded bytes captured on a worker, without a second file read.
bool readBytes(const QByteArray &bytes, Document *out, QString *error,
               const std::atomic_bool *cancel = nullptr);
bool writeNew(const QString &path, const AdjustmentState &state, const CatalogTags &tags,
              int rating, const QString &flag, QString *error);
}
