#pragma once
#include "XmpSidecar.h"
#include <QVector>
#include <optional>

// Pure planning over parsed XMP/native catalog snapshots. It never reads,
// writes, watches or applies a sidecar. The caller still confirms and commits.
namespace XmpMerge {
enum class Field { Develop, Rating, Flag, Keywords, Label, Albums };
enum class Decision { Absent, Equal, CatalogOnly, SidecarOnly, Conflict };
enum class Choice { KeepCatalog, UseSidecar };
struct Snapshot {
    AdjustmentState adjustments;
    CatalogTags tags;
    int rating = 0;
    QString flag = QStringLiteral("none");
};
struct Delta {
    Field field;
    Decision decision;
    QJsonValue baseline, catalog, sidecar;
};
struct Resolution { Field field; Choice choice; };

class Plan {
public:
    QVector<Delta> fields() const { return m_fields; }
    QString targetVersion() const { return m_target; }
    int conflicts() const;
private:
    bool m_valid = false;
    QString m_target;
    QJsonObject m_catalog, m_sidecar;
    QVector<Delta> m_fields;
    QStringList m_warnings;
    friend bool analyze(const QString &, const std::optional<Snapshot> &,
                        const Snapshot &, const XmpSidecar::Document &, Plan *, QString *);
    friend bool resolve(const Plan &, const QString &, const Snapshot &,
                        const QVector<Resolution> &, XmpSidecar::Document *, QString *);
};

// No baseline means every differing present field needs an explicit choice.
// Develop is a complete immutable intent field, never an inferred slider merge.
bool analyze(const QString &targetVersion, const std::optional<Snapshot> &baseline,
             const Snapshot &catalog, const XmpSidecar::Document &parsedSidecar,
             Plan *out, QString *error = nullptr);

// Choices must cover exactly the conflicts, once each. Missing/unknown/duplicate
// choices or any change to the target/catalog since review rejects atomically.
// The returned patch marks only chosen sidecar fields present; absent fields
// retain catalog data when passed to the existing explicit import application.
bool resolve(const Plan &, const QString &currentTargetVersion,
             const Snapshot &currentCatalog, const QVector<Resolution> &,
             XmpSidecar::Document *out, QString *error = nullptr);
}
