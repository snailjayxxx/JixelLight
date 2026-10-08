#pragma once
#include "core/pipeline/AdjustmentState.h"
#include "core/look/LookProfiles.h"
#include <QVector>
#include <QHash>

// Snapshots preserve shared immutable LUTs without resolving
// As Shot metadata into the user's stored adjustments. Old project JSON stays intact.
class EditHistory {
public:
    struct Entry { AdjustmentState state; QString action; };
    void initialize(const AdjustmentState &state) {
        if (m_entries.isEmpty()) m_entries.push_back({state, QStringLiteral("original")});
    }
    bool record(const AdjustmentState &state, const QString &action, const QString &mergeKey = {}) {
        initialize(state);
        const auto &previous = m_entries[m_cursor].state;
        // Do not serialize a potentially large calibration LUT on every slider tick.
        auto a = previous, b = state;
        a.look.lut.reset(); b.look.lut.reset();
        if (previous.look.lut == state.look.lut && previous.look.error == state.look.error && a.toJson() == b.toJson()) return false;
        const bool merge = !mergeKey.isEmpty() && mergeKey == m_mergeKey && m_cursor > 0 && m_cursor == m_entries.size()-1;
        m_entries.resize(m_cursor + 1); // A new edit invalidates redo.
        if (merge) m_entries[m_cursor] = {state, action};
        else { m_entries.push_back({state, action}); ++m_cursor; }
        if (m_entries.size() > 129) { m_entries.removeFirst(); --m_cursor; }
        m_mergeKey = mergeKey;
        return true;
    }
    QJsonObject toJson() const {
        QJsonArray entries, luts;
        QHash<const LookLut *, int> references;
        for (const auto &entry : m_entries) {
            auto snapshot = entry.state;
            const auto lut = snapshot.look.lut;
            snapshot.look.lut.reset();
            auto state = snapshot.toJson();
            if (lut) {
                if (!references.contains(lut.get())) {
                    references.insert(lut.get(), luts.size()); luts.append(lut->toJson());
                }
                auto look = state.value("look").toObject();
                look.insert("lutRef", references.value(lut.get())); state.insert("look", look);
            }
            entries.append(QJsonObject{{"state", state}, {"action", entry.action}});
        }
        return {{"schema", 1}, {"cursor", m_cursor}, {"entries", entries}, {"luts", luts}};
    }
    // Validate before replacing a live history. Unknown versions and inconsistent
    // current snapshots must never silently destroy a project's redo branch.
    bool restore(const QJsonObject &json, const AdjustmentState &current) {
        const auto entries = json.value("entries").toArray();
        const auto cursorValue = json.value("cursor");
        const int cursor = cursorValue.toInt(-1);
        if (json.value("schema").toDouble() != 1 || entries.isEmpty() || entries.size() > 129
            || !cursorValue.isDouble() || cursorValue.toDouble() != cursor
            || cursor < 0 || cursor >= entries.size()) return false;
        if (json.contains("luts") && !json.value("luts").isArray()) return false;
        QVector<std::shared_ptr<const LookLut>> luts;
        for (const auto &value : json.value("luts").toArray()) {
            if (luts.size() >= 129 || !value.isObject()) return false;
            QString error;
            auto lut = LookLut::fromJson(value.toObject(), &error);
            if (!lut || !error.isEmpty()) return false;
            luts.push_back(lut);
        }
        QVector<Entry> restored;
        for (const auto &value : entries) {
            const auto object = value.toObject();
            if (!object.value("state").isObject() || !object.value("action").isString()
                || object.value("action").toString().size() > 256) return false;
            auto stateJson = object.value("state").toObject();
            auto look = stateJson.value("look").toObject();
            std::shared_ptr<const LookLut> sharedLut;
            if (look.contains("lutRef")) {
                const auto value = look.take("lutRef");
                const int index = value.toInt(-1);
                if (!value.isDouble() || value.toDouble() != index || index < 0 || index >= luts.size()) return false;
                if (look.contains("lut")) return false;
                sharedLut = luts[index];
                stateJson.insert("look", look);
            }
            auto parseJson = stateJson;
            if (sharedLut && look.value("mode").toString() == "calibrated") {
                auto parseLook = look; parseLook.insert("mode", "off"); parseJson.insert("look", parseLook);
            }
            auto state = AdjustmentState::fromJson(parseJson);
            if (sharedLut) {
                state.look.lut = sharedLut;
                if (look.value("mode").toString() == "calibrated") state.look.mode = "calibrated";
            }
            // Compare scalar snapshots without repeatedly decoding/serializing LUTs.
            auto checked = state;
            if (sharedLut) checked.look.lut.reset();
            if (!state.look.error.isEmpty() || !LookProfiles::engineCompatible(state.look) || checked.toJson() != stateJson) return false;
            restored.push_back({state, object.value("action").toString()});
        }
        if (restored[cursor].state.toJson() != current.toJson()) return false;
        m_entries = restored; m_cursor = cursor; finish();
        return true;
    }
    void finish() { m_mergeKey.clear(); }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor + 1 < m_entries.size(); }
    const AdjustmentState &undo() { finish(); if (canUndo()) --m_cursor; return m_entries[m_cursor].state; }
    const AdjustmentState &redo() { finish(); if (canRedo()) ++m_cursor; return m_entries[m_cursor].state; }
    const QVector<Entry> &entries() const { return m_entries; }
    int cursor() const { return m_cursor; }
private:
    QVector<Entry> m_entries;
    int m_cursor = 0;
    QString m_mergeKey;
};
