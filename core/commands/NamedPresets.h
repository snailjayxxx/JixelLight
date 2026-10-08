#pragma once
#include "core/commands/EditHistory.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QJsonDocument>

// User-local color presets. Crop/orientation and catalog curation stay per photo.
// A failed read never replaces an unknown/corrupt preset file with an empty one.
class NamedPresets {
public:
    explicit NamedPresets(QString path) : m_path(std::move(path)) {}
    bool load(QString *error) {
        m_items = {}; m_writable = false;
        if (!QFileInfo::exists(m_path)) { m_writable = true; return true; }
        QFile file(m_path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
            return fail(error, "Cannot read preset file (64 MiB limit)");
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
        const auto root = doc.object();
        if (parse.error != QJsonParseError::NoError || !doc.isObject()
            || root.value("schema").toDouble() != 1 || !root.value("presets").isObject())
            return fail(error, "Invalid or unsupported preset file");
        const auto items = root.value("presets").toObject();
        if (items.size() > 100) return fail(error, "Too many presets (maximum 100)");
        for (auto it = items.begin(); it != items.end(); ++it) {
            const auto state = AdjustmentState::fromJson(it.value().toObject());
            if (!validName(it.key()) || !it.value().isObject() || state.toJson() != it.value().toObject()
                || !state.look.error.isEmpty() || !LookProfiles::engineCompatible(state.look) || state.geometry.toJson() != GeometryState{}.toJson())
                return fail(error, "Invalid preset snapshot");
        }
        m_items = items; m_writable = true; return true;
    }
    QStringList names() const { return m_items.keys(); }
    bool get(const QString &name, AdjustmentState *state) const {
        if (!m_items.contains(name)) return false;
        *state = AdjustmentState::fromJson(m_items.value(name).toObject()); return true;
    }
    bool save(const QString &name, AdjustmentState state, QString *error) {
        if (!validName(name) || m_items.contains(name) || m_items.size() >= 100)
            return fail(error, "Use a new name of 1–80 characters (maximum 100 presets)");
        if (!state.look.error.isEmpty()) return fail(error, "Cannot save an invalid Sony Look");
        state.geometry = {};
        const auto snapshot = state.toJson();
        const auto checked = AdjustmentState::fromJson(snapshot);
        if (!checked.look.error.isEmpty() || !LookProfiles::engineCompatible(checked.look) || checked.toJson() != snapshot)
            return fail(error, "Cannot save an unsupported preset snapshot");
        auto next = m_items; next.insert(name, state.toJson()); return write(next, error);
    }
    bool remove(const QString &name, QString *error) {
        if (!m_items.contains(name)) return fail(error, "Preset not found");
        auto next = m_items; next.remove(name); return write(next, error);
    }
private:
    static bool validName(const QString &name) {
        if (name.isEmpty() || name.size() > 80 || name != name.trimmed()) return false;
        for (const auto c : name) if (c.category() == QChar::Other_Control) return false;
        return true;
    }
    static bool fail(QString *error, const QString &text) { if (error) *error = text; return false; }
    bool write(const QJsonObject &next, QString *error) {
        if (!m_writable) return fail(error, "Preset file is unreadable; existing data is protected");
        const auto bytes = QJsonDocument(QJsonObject{{"schema", 1}, {"presets", next}}).toJson(QJsonDocument::Compact);
        if (bytes.size() > 64 * 1024 * 1024) return fail(error, "Preset file exceeds 64 MiB");
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) return fail(error, "Cannot create preset directory");
        QSaveFile file(m_path); file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return fail(error, file.errorString());
        m_items = next; return true;
    }
    QString m_path;
    QJsonObject m_items;
    bool m_writable = false;
};
