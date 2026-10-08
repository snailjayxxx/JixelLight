#include "core/project/ProjectDatabase.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

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
                && query.exec("CREATE TABLE IF NOT EXISTS photos(path TEXT PRIMARY KEY, imported_at TEXT DEFAULT CURRENT_TIMESTAMP, adjustment_json TEXT NOT NULL DEFAULT '{}')")
                && query.exec("CREATE TABLE IF NOT EXISTS curation(path TEXT PRIMARY KEY, rating INTEGER NOT NULL DEFAULT 0 CHECK(rating BETWEEN 0 AND 5), flag TEXT NOT NULL DEFAULT 'none' CHECK(flag IN ('none','pick','reject')))");
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
// Existing projects are opened without overwriting their metadata, and their
// serialized edits are validated before the active writer connection changes.
bool ProjectDatabase::open(const QString &directory, QVector<SavedPhoto> *photos) {
    if (!photos) return false;
    const QFileInfo folderInfo(directory);
    const QString folder = folderInfo.absoluteFilePath();
    const QFileInfo databaseInfo(QDir(folder).filePath(QStringLiteral("Project.db")));
    if (!folderInfo.isDir() || !folderInfo.fileName().endsWith(QStringLiteral(".jlp"), Qt::CaseInsensitive)
        || !databaseInfo.isFile()) {
        QMutexLocker lock(&m_state->mutex);
        m_state->error = QStringLiteral("Not a JixelLight .jlp directory containing Project.db");
        return false;
    }
    if (m_open && !flush()) return false;

    const auto state = m_state;
    QVector<SavedPhoto> staged;
    QString projectName = folderInfo.fileName().chopped(4);
    QString error;
    bool ready = false;
    QMetaObject::invokeMethod(m_worker, [&, state] {
        const QString candidate = QStringLiteral("jixellight-writer-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), candidate);
            db.setDatabaseName(databaseInfo.absoluteFilePath());
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            ready = db.open();
            if (!ready) error = db.lastError().text();

            if (ready) {
                QSqlQuery title(db);
                if (!title.exec(QStringLiteral("SELECT value FROM meta WHERE key='project_name'"))) {
                    ready = false;
                    error = title.lastError().text();
                } else if (title.next() && !title.value(0).toString().isEmpty()) {
                    projectName = title.value(0).toString();
                }
            }
            if (ready) {
                QSqlQuery tableQuery(db);
                if (!tableQuery.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='curation'"))) {
                    ready = false;
                    error = tableQuery.lastError().text();
                }
                const bool hasCuration = ready && tableQuery.next();
                QSqlQuery query(db);
                const QString querySql = hasCuration
                    ? QStringLiteral("SELECT p.path,p.adjustment_json,COALESCE(c.rating,0),COALESCE(c.flag,'none') FROM photos p LEFT JOIN curation c ON c.path=p.path ORDER BY p.imported_at,p.path")
                    : QStringLiteral("SELECT path,adjustment_json FROM photos ORDER BY imported_at,path");
                if (ready && !query.exec(querySql)) {
                    ready = false;
                    error = query.lastError().text();
                }
                while (ready && query.next()) {
                    const QString path = query.value(0).toString();
                    const QByteArray json = query.value(1).toString().toUtf8();
                    QJsonParseError parseError;
                    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
                    if (path.isEmpty() || parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                        ready = false;
                        error = QStringLiteral("Invalid adjustment JSON for project photo");
                        break;
                    }
                    const int rating = hasCuration ? std::clamp(query.value(2).toInt(), 0, 5) : 0;
                    QString flag = hasCuration ? query.value(3).toString() : QStringLiteral("none");
                    if (flag != QStringLiteral("pick") && flag != QStringLiteral("reject")) flag = QStringLiteral("none");
                    staged.push_back({path, AdjustmentState::fromJson(doc.object()), rating, flag});
                    if (staged.size() > 1000000) {
                        ready = false;
                        error = QStringLiteral("Project contains too many photos");
                        break;
                    }
                }
            }

            // Verify the same file is writable before dropping the previous
            // connection. Failed opens leave the old project untouched.
            if (ready) {
                db.close();
                db.setConnectOptions(QString());
                ready = db.open();
                if (!ready) error = db.lastError().text();
                else {
                    QSqlQuery pragma(db);
                    ready = pragma.exec(QStringLiteral("PRAGMA busy_timeout=3000"))
                        && pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
                    if (!ready) error = pragma.lastError().text();
                }
            }
            if (!ready) db.close();
        }
        if (ready) {
            if (QSqlDatabase::contains(state->connectionName)) {
                { QSqlDatabase previous = QSqlDatabase::database(state->connectionName, false); previous.close(); }
                QSqlDatabase::removeDatabase(state->connectionName);
            }
            state->connectionName = candidate;
        } else {
            QSqlDatabase::removeDatabase(candidate);
        }
        { QMutexLocker lock(&state->mutex); state->error = error; }
    }, Qt::BlockingQueuedConnection);

    if (!ready) return false;
    *photos = std::move(staged);
    m_open = true;
    m_projectPath = folder;
    m_projectName = projectName;
    return true;
}

bool ProjectDatabase::addOrUpdatePhoto(const QString &path, const AdjustmentState &state) { return updateBatch({{path,state}}); }
bool ProjectDatabase::updateAdjustment(const QString &path, const AdjustmentState &state) { return addOrUpdatePhoto(path,state); }
bool ProjectDatabase::updateCurationBatch(const QHash<QString, PhotoCuration> &changes) {
    if (!m_open) return false;
    if (changes.isEmpty()) return true;
    const auto state = m_state;
    return QMetaObject::invokeMethod(m_worker, [this, state, changes] {
        auto db = QSqlDatabase::database(state->connectionName, false);
        bool ok = db.isOpen() && db.transaction();
        QString error = ok ? QString() : db.lastError().text();
        if (ok) {
            QSqlQuery query(db);
            // Old projects have no curation table. Add it transactionally
            // on first write, not just on an open.
            ok = query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS curation(path TEXT PRIMARY KEY, rating INTEGER NOT NULL DEFAULT 0 CHECK(rating BETWEEN 0 AND 5), flag TEXT NOT NULL DEFAULT 'none' CHECK(flag IN ('none','pick','reject')))"));
            if (ok) ok = query.prepare(QStringLiteral("INSERT INTO curation(path,rating,flag) VALUES(?,?,?) ON CONFLICT(path) DO UPDATE SET rating=excluded.rating,flag=excluded.flag"));
            for (auto it = changes.constBegin(); ok && it != changes.constEnd(); ++it) {
                const auto &item = it.value();
                if (item.rating < 0 || item.rating > 5
                    || (item.flag != QStringLiteral("none") && item.flag != QStringLiteral("pick") && item.flag != QStringLiteral("reject"))) {
                    error = QStringLiteral("Invalid rating/flag");
                    ok = false;
                    break;
                }
                query.bindValue(0, it.key());
                query.bindValue(1, item.rating);
                query.bindValue(2, item.flag);
                ok = query.exec();
            }
            if (!ok && error.isEmpty()) error = query.lastError().text();
            if (ok) { ok = db.commit(); if (!ok) error = db.lastError().text(); }
            if (!ok) db.rollback();
        }
        { QMutexLocker lock(&state->mutex); state->error = error; }
        if (ok) emit saved(changes.size());
        else emit writeFailed(error);
    }, Qt::QueuedConnection);
}

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
