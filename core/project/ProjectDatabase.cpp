#include "core/project/ProjectDatabase.h"

#include <QDir>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QDebug>

ProjectDatabase::ProjectDatabase()
    : m_connectionName("jixellight-project-" + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

ProjectDatabase::~ProjectDatabase() {
    if (QSqlDatabase::contains(m_connectionName)) {
        auto db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen()) db.close();
        db = {};
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool ProjectDatabase::create(const QString &projectDirectory, const QString &projectName) {
    const QString safeName = projectName.trimmed().isEmpty() ? QStringLiteral("JixelLight Project") : projectName.trimmed();
    QDir base(projectDirectory);
    if (!base.exists() && !QDir().mkpath(projectDirectory)) return false;
    const QString folder = base.filePath(safeName + ".jlp");
    if (!QDir().mkpath(folder + "/cache/thumbnails") || !QDir().mkpath(folder + "/cache/previews") || !QDir().mkpath(folder + "/backups")) return false;

    auto db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(folder + "/Project.db");
    if (!db.open()) { qWarning() << "Project DB open failed" << db.lastError(); return false; }
    QSqlQuery q(db);
    if (!q.exec("PRAGMA journal_mode=WAL") ||
        !q.exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)") ||
        !q.exec("CREATE TABLE IF NOT EXISTS photos(path TEXT PRIMARY KEY, imported_at TEXT DEFAULT CURRENT_TIMESTAMP, adjustment_json TEXT NOT NULL DEFAULT '{}')")) {
        qWarning() << "Project schema failed" << q.lastError(); return false;
    }
    q.prepare("INSERT OR REPLACE INTO meta(key,value) VALUES('project_name',?)"); q.addBindValue(safeName); q.exec();
    m_projectPath = folder; m_projectName = safeName;
    return true;
}

bool ProjectDatabase::addOrUpdatePhoto(const QString &path, const AdjustmentState &state) {
    if (!isOpen()) return false;
    auto db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("INSERT INTO photos(path,adjustment_json) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET adjustment_json=excluded.adjustment_json");
    q.addBindValue(path);
    q.addBindValue(QString::fromUtf8(QJsonDocument(state.toJson()).toJson(QJsonDocument::Compact)));
    return q.exec();
}

bool ProjectDatabase::updateAdjustment(const QString &path, const AdjustmentState &state) {
    return addOrUpdatePhoto(path, state);
}

bool ProjectDatabase::isOpen() const {
    return QSqlDatabase::contains(m_connectionName) && QSqlDatabase::database(m_connectionName, false).isOpen();
}
