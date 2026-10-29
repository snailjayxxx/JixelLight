#pragma once
#include <QKeySequence>
#include <QStringList>
#include <QVariantMap>

// Match the existing QtQuick Shortcut bindings: Open uses the primary sequence;
// Undo/Redo use all platform bindings through their sequences property.
inline QVariantMap nativeShortcutLabels() {
    const auto labels = [](QKeySequence::StandardKey key, bool primaryOnly) {
        QStringList result;
        for (const auto &sequence : QKeySequence::keyBindings(key)) {
            const auto text = sequence.toString(QKeySequence::NativeText);
            if (!text.isEmpty() && !result.contains(text)) result.append(text);
            if (primaryOnly) break;
        }
        return result;
    };
    return {{"open", labels(QKeySequence::Open, true)},
            {"undo", labels(QKeySequence::Undo, false)},
            {"redo", labels(QKeySequence::Redo, false)}};
}
