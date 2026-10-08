#pragma once
#include "core/pipeline/AdjustmentState.h"
#include <QVector>

// Session-local snapshots preserve shared immutable LUTs without resolving
// As Shot metadata into the user's stored adjustments. Old project JSON stays intact.
class EditHistory {
public:
    struct Entry { AdjustmentState state; QString action; };
    void initialize(const AdjustmentState &state) {
        if (m_entries.isEmpty()) m_entries.push_back({state, QStringLiteral("original")});
    }
    bool record(const AdjustmentState &state, const QString &action, const QString &mergeKey = {}) {
        initialize(state);
        if (m_entries[m_cursor].state.toJson() == state.toJson()) return false;
        const bool merge = !mergeKey.isEmpty() && mergeKey == m_mergeKey && m_cursor > 0 && m_cursor == m_entries.size()-1;
        m_entries.resize(m_cursor + 1); // A new edit invalidates redo.
        if (merge) m_entries[m_cursor] = {state, action};
        else { m_entries.push_back({state, action}); ++m_cursor; }
        if (m_entries.size() > 129) { m_entries.removeFirst(); --m_cursor; }
        m_mergeKey = mergeKey;
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
