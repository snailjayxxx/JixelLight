#pragma once
#include "core/commands/EditHistory.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QTemporaryFile>
#include "core/pipeline/ProcessingPlan.h"

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
        if (!state || !m_items.contains(name)) return false;
        *state = AdjustmentState::fromJson(m_items.value(name).toObject()); return true;
    }
    bool save(const QString &name, AdjustmentState state, QString *error) {
        if (!validName(name) || m_items.contains(name) || m_items.size() >= 100)
            return fail(error, "Use a new name of 1–80 characters (maximum 100 presets)");
        if (!snapshot(&state,error)) return false;
        auto next = m_items; next.insert(name, state.toJson()); return write(next, error);
    }
    bool replace(const QString &name, AdjustmentState state, QString *error) {
        if (!m_items.contains(name)) return fail(error,"Preset not found");
        if (!snapshot(&state,error)) return false;
        auto next=m_items; next.insert(name,state.toJson()); return write(next,error);
    }
    bool rename(const QString &name, const QString &replacement, QString *error) {
        if (!m_items.contains(name) || !validName(replacement)) return fail(error,"Choose an existing preset and a valid new name");
        if (name == replacement) return true;
        if (m_items.contains(replacement)) return fail(error,"The new preset name already exists");
        auto next=m_items; const auto state=next.take(name); next.insert(replacement,state); return write(next,error);
    }
    bool exportFile(const QString &name, const QString &destination, QString *error) const {
        if (!m_items.contains(name)) return fail(error,"Preset not found");
        const QFileInfo target(destination);
        if (destination.isEmpty() || target.exists() || !QDir(target.absolutePath()).exists()) return fail(error,"Choose a new preset file in an existing folder");
        const auto bytes=QJsonDocument(QJsonObject{{"format","jixellight-develop-preset"},{"schema",1},
            {"engine",ProcessingPlan::EngineVersion},{"name",name},{"adjustments",m_items.value(name)}}).toJson(QJsonDocument::Compact);
        if (bytes.size()>64*1024*1024) return fail(error,"Preset export exceeds 64 MiB");
        QTemporaryFile file(QDir(target.absolutePath()).filePath(".jixellight-preset-XXXXXX"));
        if (!file.open() || file.write(bytes)!=bytes.size() || !file.flush()) return fail(error,file.errorString());
        file.close();
        // QFile rename refuses an existing destination, including a file which
        // appeared after the initial check. The same-folder move is atomic.
        if (!file.rename(target.absoluteFilePath())) return fail(error,file.errorString());
        file.setAutoRemove(false); return true;
    }
    bool importFile(const QString &source, const QString &replacementName, QString *error) {
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly) || file.size()>64*1024*1024) return fail(error,"Cannot read preset import (64 MiB limit)");
        QJsonParseError parse; const auto doc=QJsonDocument::fromJson(file.readAll(),&parse); const auto root=doc.object();
        if (parse.error!=QJsonParseError::NoError || !doc.isObject() || root.size()!=5
            || root.value("format").toString()!="jixellight-develop-preset" || root.value("schema").toDouble()!=1
            || root.value("engine").toString()!=ProcessingPlan::EngineVersion || !root.value("name").isString()
            || !validName(root.value("name").toString()) || !root.value("adjustments").isObject()) return fail(error,"Invalid or incompatible preset import");
        auto state=AdjustmentState::fromJson(root.value("adjustments").toObject());
        if (!snapshot(&state,error) || state.toJson()!=root.value("adjustments").toObject()) return fail(error,"Invalid imported preset snapshot");
        return save(replacementName.isEmpty() ? root.value("name").toString() : replacementName,state,error);
    }
    bool remove(const QString &name, QString *error) {
        if (!m_items.contains(name)) return fail(error, "Preset not found");
        auto next = m_items; next.remove(name); return write(next, error);
    }
private:
    static bool snapshot(AdjustmentState *state, QString *error) {
        if (!state || !state->look.error.isEmpty()) return fail(error,"Cannot save an invalid Sony Look");
        state->geometry={}; const auto json=state->toJson(); const auto checked=AdjustmentState::fromJson(json);
        if (!checked.look.error.isEmpty() || !LookProfiles::engineCompatible(checked.look) || checked.toJson()!=json)
            return fail(error,"Cannot save an unsupported preset snapshot");
        return true;
    }
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
