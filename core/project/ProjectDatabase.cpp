#include "core/project/ProjectDatabase.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QDir>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

ProjectDatabase::ProjectDatabase(QObject *parent) : QObject(parent), m_state(std::make_shared<WorkerState>()) {
    m_state->connectionName = "jixellight-writer-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("ProjectDatabaseWriter"));
    m_thread.start();
}
ProjectDatabase::~ProjectDatabase() {
    flush();
    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [state] {
        if (QSqlDatabase::contains(state->connectionName)) {
            { auto db = QSqlDatabase::database(state->connectionName, false); db.close(); }
            QSqlDatabase::removeDatabase(state->connectionName);
        }
    }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}
QString ProjectDatabase::lastError() const { QMutexLocker lock(&m_state->mutex); return m_state->error; }
bool ProjectDatabase::create(const QString &directory, const QString &name) {
    const QString safe = name.trimmed().isEmpty() ? QStringLiteral("JixelLight Project") : name.trimmed();
    if (safe == "." || safe == ".." || safe.contains('/') || safe.contains('\\')) return false;
    flush();
    const QString folder = QDir(directory).filePath(safe + ".jlp");
    if (!QDir().mkpath(folder+"/cache/thumbnails") || !QDir().mkpath(folder+"/cache/previews") || !QDir().mkpath(folder+"/backups")) return false;
    bool ok = false;
    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [&, state] {
        if (QSqlDatabase::contains(state->connectionName)) {
            { auto previous = QSqlDatabase::database(state->connectionName, false); previous.close(); }
            QSqlDatabase::removeDatabase(state->connectionName);
        }
        auto db = QSqlDatabase::addDatabase("QSQLITE", state->connectionName);
        db.setDatabaseName(folder+"/Project.db");
        QString error;
        if (!db.open()) error = db.lastError().text();
        else {
            QSqlQuery query(db);
            ok = query.exec("PRAGMA journal_mode=WAL") && query.exec("PRAGMA busy_timeout=3000")
                && query.exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT)")
                && query.exec("CREATE TABLE IF NOT EXISTS photos(path TEXT PRIMARY KEY, imported_at TEXT DEFAULT CURRENT_TIMESTAMP, adjustment_json TEXT NOT NULL DEFAULT '{}')");
            if (ok) {
                query.prepare("INSERT OR REPLACE INTO meta(key,value) VALUES('project_name',?)");
                query.addBindValue(safe); ok = query.exec();
            }
            if (!ok) error = query.lastError().text();
        }
        QMutexLocker lock(&state->mutex); state->error = error;
    }, Qt::BlockingQueuedConnection);
    m_open = ok;
    if (ok) { m_projectPath = folder; m_projectName = safe; }
    else emit writeFailed(lastError());
    return ok;
}
bool ProjectDatabase::addOrUpdatePhoto(const QString &path, const AdjustmentState &state) { return updateBatch({{path,state}}); }
bool ProjectDatabase::updateAdjustment(const QString &path, const AdjustmentState &state) { return addOrUpdatePhoto(path,state); }
bool ProjectDatabase::updateBatch(const QHash<QString, AdjustmentState> &states) {
    if (!m_open) return false;
    if (states.isEmpty()) return true;
    const auto state = m_state;
    return QMetaObject::invokeMethod(m_worker, [this,state,states] {
        PerformanceSpan timer(QStringLiteral("database_batch"), {{"photos",states.size()}});
        auto db = QSqlDatabase::database(state->connectionName, false);
        bool ok = db.isOpen() && db.transaction();
        QString error = ok ? QString() : db.lastError().text();
        if (ok) {
            QSqlQuery query(db);
            ok = query.prepare("INSERT INTO photos(path,adjustment_json) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET adjustment_json=excluded.adjustment_json");
            for (auto it = states.constBegin(); ok && it != states.constEnd(); ++it) {
                query.bindValue(0,it.key());
                query.bindValue(1,QString::fromUtf8(QJsonDocument(it.value().toJson()).toJson(QJsonDocument::Compact)));
                ok = query.exec();
            }
            if (!ok) error = query.lastError().text();
            if (ok) { ok = db.commit(); if (!ok) error = db.lastError().text(); }
            if (!ok) db.rollback();
        }
        { QMutexLocker lock(&state->mutex); state->error = error; }
        if (ok) emit saved(states.size());
        else emit writeFailed(error);
    }, Qt::QueuedConnection);
}
bool ProjectDatabase::flush() {
    if (!m_thread.isRunning()) return false;
    QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
    return lastError().isEmpty();
}
