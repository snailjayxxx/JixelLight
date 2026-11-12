#include "XmpMerge.h"
#include <QSet>
#include <algorithm>

namespace {
using XmpMerge::Field;
using XmpMerge::Decision;
bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
QString name(Field f) {
    switch (f) {
    case Field::Develop: return QStringLiteral("adjustments");
    case Field::Rating: return QStringLiteral("rating");
    case Field::Flag: return QStringLiteral("flag");
    case Field::Keywords: return QStringLiteral("keywords");
    case Field::Label: return QStringLiteral("label");
    case Field::Albums: return QStringLiteral("albums");
    }
    return {};
}
constexpr Field fields[] = {Field::Develop, Field::Rating, Field::Flag,
                            Field::Keywords, Field::Label, Field::Albums};
bool finiteValues(const QJsonValue &value) {
    if (value.isDouble()) return std::isfinite(value.toDouble());
    if (value.isArray()) {
        for (const auto &child : value.toArray()) if (!finiteValues(child)) return false;
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto i = object.constBegin(); i != object.constEnd(); ++i)
            if (!finiteValues(i.value())) return false;
    }
    return true;
}
bool validDevelop(const AdjustmentState &state) {
    const auto j = state.toJson();
    return state.look.error.isEmpty()
        && finiteValues(j)
        && (!state.look.lut || state.look.lut->validate())
        && AdjustmentState::validBrushMaskJson(j)
        && AdjustmentState::validLinearMaskJson(j)
        && AdjustmentState::validRadialMaskJson(j)
        && AdjustmentState::validVignetteJson(j)
        && AdjustmentState::validGrainJson(j)
        && AdjustmentState::validSharpenJson(j)
        && AdjustmentState::validDenoiseJson(j)
        &&AdjustmentState::validColorMaskJson(j)&&AdjustmentState::validLuminanceMaskJson(j)&& AdjustmentState::validPointColorJson(j)
        && AdjustmentState::validDehazeJson(j)
        && AdjustmentState::validTextureJson(j)
        && AdjustmentState::validClarityJson(j)
        && AdjustmentState::validBlackAndWhiteJson(j)
        && AdjustmentState::validColorGradingJson(j)
        && AdjustmentState::fromJson(j).toJson() == j;
}
bool snapshot(const XmpMerge::Snapshot &s, QJsonObject *out) {
    CatalogTags tags;
    if (s.rating < 0 || s.rating > 5
        || !QStringList{QStringLiteral("none"), QStringLiteral("pick"), QStringLiteral("reject")}.contains(s.flag)
        || !CatalogTags::fromJson(s.tags.toJson(), &tags) || !validDevelop(s.adjustments)) return false;
    *out = {{"adjustments", s.adjustments.toJson()}, {"rating", s.rating}, {"flag", s.flag},
            {"keywords", QJsonArray::fromStringList(s.tags.keywords)}, {"label", s.tags.label},
            {"albums", QJsonArray::fromStringList(s.tags.albums)}};
    return true;
}
bool incoming(const XmpSidecar::Document &d, QJsonObject *out) {
    QJsonObject value;
    if (d.hasAdjustments) {
        if (!validDevelop(d.adjustments)) return false;
        value.insert("adjustments", d.adjustments.toJson());
    }
    if (d.hasRating) {
        if (d.rating < 0 || d.rating > 5) return false;
        value.insert("rating", d.rating);
    }
    if (d.hasFlag) {
        if (!QStringList{"none", "pick", "reject"}.contains(d.flag)) return false;
        value.insert("flag", d.flag);
    }
    if (d.hasLabel) {
        if (!CatalogTags::validLabel(d.tags.label)) return false;
        value.insert("label", d.tags.label);
    }
    for (const auto f : {Field::Keywords, Field::Albums}) {
        const bool present = f == Field::Keywords ? d.hasKeywords : d.hasAlbums;
        if (!present) continue;
        const auto original = f == Field::Keywords ? d.tags.keywords : d.tags.albums;
        auto normalized = original;
        if (!CatalogTags::normalize(&normalized, f == Field::Keywords ? 64 : 32)
            || normalized != original) return false;
        value.insert(name(f), QJsonArray::fromStringList(original));
    }
    *out = value;
    return true;
}
void includeField(Field f, const QJsonObject &remote, XmpSidecar::Document *patch) {
    const auto value = remote.value(name(f));
    switch (f) {
    case Field::Develop: patch->hasAdjustments = true; patch->adjustments = AdjustmentState::fromJson(value.toObject()); break;
    case Field::Rating: patch->hasRating = true; patch->rating = value.toInt(); break;
    case Field::Flag: patch->hasFlag = true; patch->flag = value.toString(); break;
    case Field::Keywords:
        patch->hasKeywords = true;
        for (const auto &v : value.toArray()) patch->tags.keywords.append(v.toString());
        break;
    case Field::Label: patch->hasLabel = true; patch->tags.label = value.toString(); break;
    case Field::Albums:
        patch->hasAlbums = true;
        for (const auto &v : value.toArray()) patch->tags.albums.append(v.toString());
        break;
    }
}
}

int XmpMerge::Plan::conflicts() const {
    return int(std::count_if(m_fields.cbegin(), m_fields.cend(),
        [](const Delta &d) { return d.decision == Decision::Conflict; }));
}

bool XmpMerge::analyze(const QString &target, const std::optional<Snapshot> &base,
                       const Snapshot &catalog, const XmpSidecar::Document &sidecar,
                       Plan *out, QString *error) {
    if (error) error->clear();
    if (!out || target.isEmpty()) return fail(error, QStringLiteral("A saved target version is required"));
    Plan next;
    QJsonObject baseline;
    if (!snapshot(catalog, &next.m_catalog) || (base && !snapshot(*base, &baseline))
        || !incoming(sidecar, &next.m_sidecar))
        return fail(error, QStringLiteral("Invalid parsed sidecar or catalog snapshot"));
    for (const auto f : ::fields) {
        const auto key = name(f);
        const auto local = next.m_catalog.value(key), remote = next.m_sidecar.value(key);
        const auto ancestor = base ? baseline.value(key) : QJsonValue(QJsonValue::Undefined);
        Decision decision;
        if (!next.m_sidecar.contains(key)) decision = Decision::Absent;
        else if (local == remote) decision = Decision::Equal;
        else if (!base) decision = Decision::Conflict;
        else if (local == ancestor) decision = Decision::SidecarOnly;
        else if (remote == ancestor) decision = Decision::CatalogOnly;
        else decision = Decision::Conflict;
        next.m_fields.append({f, decision, ancestor, local, remote});
    }
    next.m_target = target;
    next.m_warnings = sidecar.warnings;
    next.m_valid = true;
    *out = next;
    return true;
}

bool XmpMerge::resolve(const Plan &plan, const QString &target, const Snapshot &catalog,
                       const QVector<Resolution> &choices, XmpSidecar::Document *out, QString *error) {
    if (error) error->clear();
    QJsonObject actual;
    if (!out || !plan.m_valid || target != plan.m_target || !snapshot(catalog, &actual)
        || actual != plan.m_catalog)
        return fail(error, QStringLiteral("The target or catalog changed; review the sidecar again"));
    QSet<int> seen;
    for (const auto &choice : choices) {
        const int id = int(choice.field);
        const auto delta = std::find_if(plan.m_fields.cbegin(), plan.m_fields.cend(),
            [&](const Delta &d) { return d.field == choice.field; });
        if (name(choice.field).isEmpty() || seen.contains(id) || delta == plan.m_fields.cend()
            || delta->decision != Decision::Conflict
            || (choice.choice != Choice::KeepCatalog && choice.choice != Choice::UseSidecar))
            return fail(error, QStringLiteral("Each conflict requires exactly one explicit choice"));
        seen.insert(id);
    }
    if (seen.size() != plan.conflicts())
        return fail(error, QStringLiteral("Resolve every conflict before applying"));
    XmpSidecar::Document patch;
    patch.warnings = plan.m_warnings;
    for (const auto &d : plan.m_fields) {
        bool take = d.decision == Decision::SidecarOnly;
        if (d.decision == Decision::Conflict)
            take = std::find_if(choices.cbegin(), choices.cend(), [&](const Resolution &r) { return r.field == d.field; })->choice == Choice::UseSidecar;
        if (take) includeField(d.field, plan.m_sidecar, &patch);
    }
    *out = patch;
    return true;
}
