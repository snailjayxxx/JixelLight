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
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace {
template<class State, class Values>
void recordWriteResult(const std::shared_ptr<State> &state, const QString &group, const Values &values, bool ok, const QString &error) {
    QMutexLocker lock(&state->mutex);
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto key = group + ":" + it.key();
        if (ok) state->failedWrites.remove(key);
        else state->failedWrites.insert(key,error.isEmpty() ? QStringLiteral("Project write failed") : error);
    }
    auto errors = state->failedWrites.values(); errors.removeDuplicates();
    state->error = errors.join('\n');
}
bool validCopyKey(const QString &key) {
    return key.startsWith("jixel-copy:") && !QUuid::fromString(key.mid(11)).isNull();
}
bool backupBeforeTable(QSqlDatabase &db, const QString &folder, const QString &table, QString *error) {
    QSqlQuery query(db); query.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?"); query.addBindValue(table);
    if (!query.exec()) { *error = query.lastError().text(); return false; }
    if (query.next()) return true;
    query.finish();
    if (!QDir().mkpath(QDir(folder).filePath("backups"))) { *error = "Cannot create catalog backup directory"; return false; }
    const auto backup = QDir(folder).filePath("backups/Project-before-" + table + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".db");
    if (!query.prepare("VACUUM INTO ?")) { *error = query.lastError().text(); return false; }
    query.addBindValue(backup);
    if (!query.exec()) { *error = "Catalog backup failed: " + query.lastError().text(); return false; }
    return true;
}
struct StoredIdentity {
    ProjectDatabase::OriginalIdentity identity;
    QString sortKey;
};
bool tableExists(QSqlDatabase &db, const QString &table, bool *exists, QString *error) {
    QSqlQuery query(db);query.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?");query.addBindValue(table);
    if(!query.exec()){*error=query.lastError().text();return false;}
    *exists=query.next();return true;
}
bool readIdentities(QSqlDatabase &db,QHash<QString,StoredIdentity> *identities,bool *exists,QString *error) {
    if(!tableExists(db,"original_identity",exists,error)||!*exists)return error->isEmpty();
    QSqlQuery query(db);
    // Schema 1 includes a nullable sort_key; NULL retains the original source
    // key's ordering. Missing columns/future row schemas fail without migration.
    if(!query.exec("SELECT source_key,schema,size_bytes,sha256,sort_key,typeof(size_bytes) FROM original_identity")){
        *error=query.lastError().text();return false;
    }
    while(query.next()) {
        const auto key=query.value(0).toString();
        const ProjectDatabase::OriginalIdentity identity{query.value(2).toLongLong(),query.value(3).toByteArray()};
        if(key.isEmpty()||key.contains(QChar::Null)||validCopyKey(key)||query.value(1).toDouble()!=1
            ||query.value(5).toString()!="integer"||!identity.isKnown()||identities->contains(key)||identities->size()>=1000000){
            *error="Invalid or unsupported original identity";return false;
        }
        identities->insert(key,{identity,query.value(4).isNull()?key:query.value(4).toString()});
    }
    return true;
}
bool identityForSource(const QHash<QString,StoredIdentity> &identities,const QString &folder,const QString &source,
                       ProjectDatabase::OriginalIdentity *identity,QString *error) {
    *identity={};const auto normalized=ProjectDatabase::normalizedSourcePath(folder,source);
    for(auto it=identities.cbegin();it!=identities.cend();++it) {
        if(ProjectDatabase::normalizedSourcePath(folder,it.key())!=normalized)continue;
        if(identity->isKnown()&&*identity!=it->identity){*error="Conflicting identities for the source family";return false;}
        *identity=it->identity;
    }
    return true;
}
struct SourceFamily {
    QStringList originals;
    QHash<QString,QJsonObject> copies;
    QSet<QString> photoKeys;
    QHash<QString,QJsonObject> allCopies;
};
bool readSourceFamily(QSqlDatabase &db,const QString &folder,const QString &sourceKey,SourceFamily *family,QString *error) {
    const auto normalized=ProjectDatabase::normalizedSourcePath(folder,sourceKey);
    if(normalized.isEmpty()||validCopyKey(sourceKey)){*error="Invalid original source key";return false;}
    QSqlQuery query(db);
    if(!query.exec("SELECT path FROM photos")){*error=query.lastError().text();return false;}
    while(query.next()) {
        const auto key=query.value(0).toString();family->photoKeys.insert(key);
        if(!validCopyKey(key)&&ProjectDatabase::normalizedSourcePath(folder,key)==normalized)family->originals.append(key);
    }
    bool hasCopies=false;if(!tableExists(db,"virtual_sources",&hasCopies,error))return false;
    if(hasCopies) {
        if(!query.exec("SELECT key,json FROM virtual_sources")){*error=query.lastError().text();return false;}
        while(query.next()) {
            const auto key=query.value(0).toString();const auto bytes=query.value(1).toByteArray();
            QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);const auto object=doc.object();
            QStringList name{object.value("name").toString()};
            if(!validCopyKey(key)||!family->photoKeys.contains(key)||bytes.size()>16384||parse.error!=QJsonParseError::NoError
                ||!doc.isObject()||object.value("schema").toDouble()!=1||!object.value("source").isString()
                ||object.value("source").toString().isEmpty()||!CatalogTags::normalize(&name,1)||name.size()!=1
                ||name[0]!=object.value("name").toString()
                ||object!=QJsonObject{{"schema",1},{"source",object.value("source")},{"name",object.value("name")}}){
                *error="Invalid or unsupported virtual copy source";return false;
            }
            family->allCopies.insert(key,object);
            if(ProjectDatabase::normalizedSourcePath(folder,object.value("source").toString())==normalized)family->copies.insert(key,object);
        }
    }
    if(family->originals.size()>1){*error="Ambiguous original source family";return false;}
    if(family->originals.isEmpty()&&family->copies.isEmpty()){*error="Original source is not in the project";return false;}
    return true;
}
bool createIdentityTable(QSqlDatabase &db,QString *error) {
    QSqlQuery query(db);
    if(query.exec("CREATE TABLE IF NOT EXISTS original_identity(source_key TEXT PRIMARY KEY,schema INTEGER NOT NULL CHECK(schema=1),size_bytes INTEGER NOT NULL CHECK(size_bytes>=0),sha256 TEXT NOT NULL,sort_key TEXT)"))return true;
    *error=query.lastError().text();return false;
}
}

bool ProjectDatabase::OriginalIdentity::isKnown() const {
    if(sizeBytes<0||sha256.size()!=64)return false;
    return std::all_of(sha256.cbegin(),sha256.cend(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
QString ProjectDatabase::normalizedSourcePath(const QString &folder,const QString &sourceKey) {
    if(sourceKey.isEmpty()||sourceKey.contains(QChar::Null))return {};
    auto normalized=QDir::cleanPath(QDir::isAbsolutePath(sourceKey)?sourceKey:QDir(folder).absoluteFilePath(sourceKey));
#ifdef Q_OS_WIN
    normalized=normalized.toCaseFolded();
#endif
    return normalized;
}

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
QString ProjectDatabase::lastError() const {
    if (!m_lastOpenError.isEmpty()) return m_lastOpenError;
    QMutexLocker lock(&m_state->mutex);
    return m_state->error;
}
bool ProjectDatabase::create(const QString &directory, const QString &name) {
    m_lastOpenError.clear();
    const QString safe = name.trimmed().isEmpty() ? QStringLiteral("JixelLight Project") : name.trimmed();
    if (safe == "." || safe == ".." || safe.contains('/') || safe.contains('\\')) {
        m_lastOpenError = QStringLiteral("Invalid project name"); return false;
    }
    if (m_open && !flush()) return false;
    const QString folder = QDir(directory).absoluteFilePath(safe + ".jlp");
    // mkdir reserves a new catalog directory; never reuse an existing project.
    if (!QDir().mkpath(directory) || !QDir(directory).mkdir(safe + ".jlp")) {
        m_lastOpenError = QStringLiteral("Project directory already exists or cannot be created"); return false;
    }
    if (!QDir().mkpath(folder+"/cache/thumbnails") || !QDir().mkpath(folder+"/cache/previews") || !QDir().mkpath(folder+"/backups")) {
        QDir(folder).removeRecursively();
        m_lastOpenError = QStringLiteral("Cannot create project cache directories"); return false;
    }
    bool ok = false; QString error;
    const auto state = m_state;
    const QString candidate = "jixellight-create-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMetaObject::invokeMethod(m_worker, [&, state] {
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", candidate);
            db.setDatabaseName(folder+"/Project.db");
            if (!db.open()) error = db.lastError().text();
            else {
                QSqlQuery query(db);
                ok = query.exec("PRAGMA journal_mode=WAL") && query.exec("PRAGMA busy_timeout=3000")
                    && query.exec("CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT)")
                    && query.exec("CREATE TABLE photos(path TEXT PRIMARY KEY, imported_at TEXT DEFAULT CURRENT_TIMESTAMP, adjustment_json TEXT NOT NULL DEFAULT '{}')")
                    && query.exec("CREATE TABLE virtual_sources(key TEXT PRIMARY KEY,json TEXT NOT NULL)")
                    && query.exec("CREATE TABLE catalog_tags(path TEXT PRIMARY KEY,json TEXT NOT NULL)")
                    && query.exec("CREATE TABLE catalog_dates(path TEXT PRIMARY KEY,json TEXT NOT NULL)")
                    && query.exec("CREATE TABLE curation(path TEXT PRIMARY KEY, rating INTEGER NOT NULL DEFAULT 0 CHECK(rating BETWEEN 0 AND 5), flag TEXT NOT NULL DEFAULT 'none' CHECK(flag IN ('none','pick','reject')))");
                if (ok) {
                    query.prepare("INSERT INTO meta(key,value) VALUES('project_name',?)");
                    query.addBindValue(safe); ok = query.exec();
                }
                if (!ok) error = query.lastError().text();
            }
            if (!ok) db.close();
        }
        if (ok) {
            if (QSqlDatabase::contains(state->connectionName)) {
                { auto previous = QSqlDatabase::database(state->connectionName, false); previous.close(); }
                QSqlDatabase::removeDatabase(state->connectionName);
            }
            state->connectionName = candidate;
            QMutexLocker lock(&state->mutex); state->error.clear(); state->failedWrites.clear();
        } else QSqlDatabase::removeDatabase(candidate);
    }, Qt::BlockingQueuedConnection);
    if (ok) { m_open = true; m_projectPath = folder; m_projectName = safe; }
    else { QDir(folder).removeRecursively(); m_lastOpenError = error; emit writeFailed(error); }
    return ok;
}
// Existing projects are opened without overwriting their metadata, and their
// serialized edits are validated before the active writer connection changes.
bool ProjectDatabase::open(const QString &directory, QVector<SavedPhoto> *photos) {
    return openCatalog(directory,photos,false);
}
bool ProjectDatabase::readSnapshot(const QString &directory, QVector<SavedPhoto> *photos) {
    return openCatalog(directory,photos,true);
}
bool ProjectDatabase::openCatalog(const QString &directory, QVector<SavedPhoto> *photos, bool readOnly) {
    m_lastOpenError.clear();
    if (!photos) { m_lastOpenError = QStringLiteral("Missing project destination"); return false; }
    const QFileInfo folderInfo(directory);
    const QString folder = folderInfo.absoluteFilePath();
    const QFileInfo databaseInfo(QDir(folder).filePath(QStringLiteral("Project.db")));
    if (!folderInfo.isDir() || !folderInfo.fileName().endsWith(QStringLiteral(".jlp"), Qt::CaseInsensitive)
        || !databaseInfo.isFile()) {
        m_lastOpenError = QStringLiteral("Not a JixelLight .jlp directory containing Project.db");
        return false;
    }
    if (!readOnly && m_open && !flush()) return false;

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
                // Keep all catalog tables in one SQLite read snapshot, including
                // when another application is actively saving the WAL writer.
                QSqlQuery timeout(db);
                ready = timeout.exec("PRAGMA busy_timeout=3000") && db.transaction();
                if (!ready) error = db.lastError().text();
            }

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
                QHash<QString, QJsonObject> copies;
                QSqlQuery copyQuery(db);
                ready = ready && copyQuery.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='virtual_sources'");
                if (ready && copyQuery.next()) {
                    ready = copyQuery.exec("SELECT key,json FROM virtual_sources");
                    while (ready && copyQuery.next()) {
                        const auto key = copyQuery.value(0).toString();
                        const auto bytes = copyQuery.value(1).toString().toUtf8();
                        QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse); const auto object = doc.object();
                        QStringList name{object.value("name").toString()};
                        if (!validCopyKey(key) || bytes.size() > 16384 || parse.error != QJsonParseError::NoError || !doc.isObject()
                            || object.value("schema").toDouble() != 1 || !object.value("source").isString()
                            || object.value("source").toString().isEmpty() || !object.value("name").isString()
                            || !CatalogTags::normalize(&name,1) || name.size() != 1 || name[0] != object.value("name").toString()
                            || object != QJsonObject{{"schema",1},{"source",object.value("source")},{"name",object.value("name")}}
                            || copies.size() >= 1000000) {
                            ready = false; error = QStringLiteral("Invalid or unsupported virtual copy source"); break;
                        }
                        copies.insert(key,object);
                    }
                }
                if (!ready && error.isEmpty()) error = copyQuery.lastError().text();
                QHash<QString, CatalogTags> tags;
                QSqlQuery tagQuery(db);
                ready = ready && tagQuery.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='catalog_tags'");
                const bool hasTags = ready && tagQuery.next();
                if (hasTags) {
                    ready = tagQuery.exec("SELECT path,json FROM catalog_tags");
                    while (ready && tagQuery.next()) {
                        const auto path = tagQuery.value(0).toString();
                        const auto bytes = tagQuery.value(1).toString().toUtf8();
                        QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse);
                        CatalogTags item;
                        if (path.isEmpty() || bytes.size() > 16384 || parse.error != QJsonParseError::NoError
                            || !doc.isObject() || !CatalogTags::fromJson(doc.object(), &item) || tags.size() >= 1000000) {
                            ready = false; error = QStringLiteral("Invalid or unsupported catalog annotations"); break;
                        }
                        tags.insert(path, item);
                    }
                }
                if (!ready && error.isEmpty()) error = tagQuery.lastError().text();
                QHash<QString,PhotoTimeline> timelines;
                QSqlQuery dateQuery(db);
                ready = ready && dateQuery.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='catalog_dates'");
                if (ready && dateQuery.next()) {
                    ready = dateQuery.exec("SELECT path,json FROM catalog_dates");
                    while (ready && dateQuery.next()) {
                        const auto key = dateQuery.value(0).toString(); const auto bytes = dateQuery.value(1).toByteArray();
                        QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes,&parse); PhotoTimeline timeline;
                        if (key.isEmpty() || bytes.size() > 1024 || parse.error != QJsonParseError::NoError || !doc.isObject()
                            || !PhotoTimeline::fromJson(doc.object(),&timeline) || timelines.size() >= 1000000) {
                            ready = false; error = QStringLiteral("Invalid or unsupported catalog dates"); break;
                        }
                        timelines.insert(key,timeline);
                    }
                }
                if (!ready && error.isEmpty()) error = dateQuery.lastError().text();
                QHash<QString,StoredIdentity> identities;QHash<QString,OriginalIdentity> identityFamilies;bool hasIdentities=false;
                if(ready)ready=readIdentities(db,&identities,&hasIdentities,&error);
                for(auto it=identities.cbegin();ready&&it!=identities.cend();++it) {
                    const auto family=normalizedSourcePath(folder,it.key());
                    if(identityFamilies.contains(family)&&identityFamilies.value(family)!=it->identity){
                        ready=false;error="Conflicting identities for the source family";break;
                    }
                    identityFamilies.insert(family,it->identity);
                }
                QSqlQuery query(db);
                QString querySql=hasCuration
                    ? QStringLiteral("SELECT p.path,p.adjustment_json,COALESCE(c.rating,0),COALESCE(c.flag,'none'),p.imported_at FROM photos p LEFT JOIN curation c ON c.path=p.path")
                    : QStringLiteral("SELECT p.path,p.adjustment_json,p.imported_at FROM photos p");
                if(hasIdentities)querySql+=" LEFT JOIN original_identity i ON i.source_key=p.path";
                querySql+=" ORDER BY CASE WHEN p.path LIKE 'jixel-copy:%' THEN 1 ELSE 0 END,p.imported_at,";
                querySql+=hasIdentities?"COALESCE(i.sort_key,p.path),p.path":"p.path";
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
                    auto adjustments = AdjustmentState::fromJson(doc.object());
                    if (!AdjustmentState::validBrushMaskJson(doc.object())) {
                        ready=false; error=QStringLiteral("Invalid or unsupported brush mask draft for project photo"); break;
                    }
                    if (!AdjustmentState::validRadialMaskJson(doc.object())) {
                        ready=false; error=QStringLiteral("Invalid or unsupported radial mask for project photo"); break;
                    }
                    if (!AdjustmentState::validLinearMaskJson(doc.object())) {
                        ready=false; error=QStringLiteral("Invalid or unsupported linear mask for project photo"); break;
                    }
                    if (!AdjustmentState::validVignetteJson(doc.object())) {
                        ready=false; error=QStringLiteral("Invalid or unsupported vignette for project photo"); break;
                    }
                    if (!AdjustmentState::validBlackAndWhiteJson(doc.object())) {
                        ready=false; error=QStringLiteral("Invalid or unsupported black-and-white mixer for project photo"); break;
                    }
                    if (!AdjustmentState::validColorGradingJson(doc.object())) {
                        ready=false; error=QStringLiteral("Invalid or unsupported color grading for project photo"); break;
                    }
                    if (doc.object().contains("geometry") && (!doc.object()["geometry"].isObject()
                        || !GeometryState::validJson(doc.object()["geometry"].toObject()))) {
                        ready=false; error=QStringLiteral("Invalid or unsupported geometry for project photo"); break;
                    }
                    if (!adjustments.look.error.isEmpty() || !LookProfiles::engineCompatible(adjustments.look)) {
                        ready=false; error=QStringLiteral("Invalid or incompatible Sony Look for project photo"); break;
                    }
                    EditHistory history;
                    if (doc.object().contains("_history") &&
                        (!doc.object().value("_history").isObject() ||
                         !history.restore(doc.object().value("_history").toObject(), adjustments))) {
                        ready = false;
                        error = QStringLiteral("Invalid or unsupported edit history for project photo");
                        break;
                    }
                    history.initialize(adjustments);
                    // Restore the exact user snapshot rather than stale resolved
                    // As Shot metadata from earlier fusion draft saves.
                    adjustments = history.entries()[history.cursor()].state;
                    const bool isCopy = validCopyKey(path);
                    if (isCopy && !copies.contains(path)) {
                        ready = false; error = QStringLiteral("Virtual copy has no source mapping"); break;
                    }
                    const auto source = isCopy ? copies.take(path) : QJsonObject{};
                    auto timeline = timelines.value(path);
                    if (!timelines.contains(path)) timeline.importedAt = std::max(qint64(0),PhotoTimeline::sqlImportTime(query.value(hasCuration ? 4 : 2).toString()));
                    const auto sourceKey=isCopy?source.value("source").toString():path;
                    staged.push_back({sourceKey, adjustments, rating, flag, history, tags.value(path),
                                      isCopy ? path : QString(), source.value("name").toString(),timeline,sourceKey,
                                      identityFamilies.value(normalizedSourcePath(folder,sourceKey))});
                    if (staged.size() > 1000000) {
                        ready = false;
                        error = QStringLiteral("Project contains too many photos");
                        break;
                    }
                }
                if (ready && !copies.isEmpty()) { ready = false; error = QStringLiteral("Virtual copy source has no edit record"); }
            }

            // Verify the same file is writable before dropping the previous
            // connection. Failed opens leave the old project untouched.
            if (ready && !db.commit()) { ready=false; error=db.lastError().text(); }
            if (ready && !readOnly) {
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
            if (!ready || readOnly) db.close();
        }
        if (ready && !readOnly) {
            if (QSqlDatabase::contains(state->connectionName)) {
                { QSqlDatabase previous = QSqlDatabase::database(state->connectionName, false); previous.close(); }
                QSqlDatabase::removeDatabase(state->connectionName);
            }
            state->connectionName = candidate;
        } else {
            QSqlDatabase::removeDatabase(candidate);
        }
        // A failed candidate open must never change the current writer's
        // error status: otherwise flushing the perfectly healthy old project
        // would fail merely because the attempted replacement was invalid.
    }, Qt::BlockingQueuedConnection);

    if (!ready) { m_lastOpenError = error.isEmpty() ? QStringLiteral("Project validation failed") : error; return false; }
    *photos = std::move(staged);
    if (!readOnly) {
        m_open = true;
        m_projectPath = folder;
        m_projectName = projectName;
    }
    return true;
}

bool ProjectDatabase::originalIdentity(const QString &sourceKey,OriginalIdentity *identity) {
    m_lastOpenError.clear();
    if(!m_open||!identity){m_lastOpenError="Missing open project or identity destination";return false;}
    OriginalIdentity staged;bool ok=false;QString error;const auto state=m_state;const auto folder=m_projectPath;
    QMetaObject::invokeMethod(m_worker,[&,state,folder]{
        auto db=QSqlDatabase::database(state->connectionName,false);
        SourceFamily family;QHash<QString,StoredIdentity> identities;bool exists=false;
        ok=db.isOpen()&&db.transaction();
        if(ok)ok=readSourceFamily(db,folder,sourceKey,&family,&error)
            &&readIdentities(db,&identities,&exists,&error)
            &&identityForSource(identities,folder,sourceKey,&staged,&error);
        if(ok){ok=db.commit();if(!ok)error=db.lastError().text();}
        if(!ok)db.rollback();
    },Qt::BlockingQueuedConnection);
    if(!ok){m_lastOpenError=error.isEmpty()?QString("Cannot read original identity"):error;return false;}
    *identity=staged;return true;
}

bool ProjectDatabase::recordOriginalIdentity(const QString &sourceKey,const OriginalIdentity &identity) {
    m_lastOpenError.clear();
    if(!m_open||!identity.isKnown()){m_lastOpenError="Invalid original identity";return false;}
    if(!flush())return false;
    bool ok=false;QString error;const auto state=m_state;const auto folder=m_projectPath;
    QMetaObject::invokeMethod(m_worker,[&,state,folder]{
        auto db=QSqlDatabase::database(state->connectionName,false);
        SourceFamily family;QHash<QString,StoredIdentity> identities;bool exists=false;OriginalIdentity known;
        auto validate=[&]{
            family={};identities.clear();
            if(!readSourceFamily(db,folder,sourceKey,&family,&error)
                ||!readIdentities(db,&identities,&exists,&error)
                ||!identityForSource(identities,folder,sourceKey,&known,&error))return false;
            if(known.isKnown()&&known!=identity){error="Original identity does not match the recorded baseline";return false;}
            return true;
        };
        ok=db.isOpen()&&validate();
        if(ok&&known.isKnown())return;
        if(ok)ok=backupBeforeTable(db,folder,"original_identity",&error)&&db.transaction();
        if(ok)ok=validate()&&createIdentityTable(db,&error);
        if(ok&&!known.isKnown()) {
            const auto key=family.originals.isEmpty()?sourceKey:family.originals.front();
            QSqlQuery query(db);query.prepare("INSERT INTO original_identity(source_key,schema,size_bytes,sha256,sort_key) VALUES(?,1,?,?,?)");
            query.addBindValue(key);query.addBindValue(identity.sizeBytes);query.addBindValue(QString::fromLatin1(identity.sha256));query.addBindValue(key);
            ok=query.exec();if(!ok)error=query.lastError().text();
        }
        if(ok){ok=db.commit();if(!ok)error=db.lastError().text();}
        if(!ok)db.rollback();
    },Qt::BlockingQueuedConnection);
    if(!ok){m_lastOpenError=error.isEmpty()?QString("Cannot record original identity"):error;emit writeFailed(m_lastOpenError);}
    else emit saved(1);
    return ok;
}

bool ProjectDatabase::relinkSource(const RelinkRequest &request) {
    m_lastOpenError.clear();
    if(!m_open||request.sourceKey.isEmpty()||validCopyKey(request.sourceKey)
        ||request.candidatePath.isEmpty()||request.candidatePath.contains(QChar::Null)
        ||!QDir::isAbsolutePath(request.candidatePath)||!request.candidateIdentity.isKnown()){
        m_lastOpenError="Invalid source relink request";return false;
    }
    if(!flush())return false;
    bool ok=false;QString error;int affected=0;const auto state=m_state;const auto folder=m_projectPath;
    // A Windows family key is case-folded; retain the verified candidate's
    // spelling in photos/virtual_sources/identity and the eventual display.
    const auto candidate=QDir::cleanPath(QDir::fromNativeSeparators(request.candidatePath));
    const auto candidateFamily=normalizedSourcePath(folder,candidate);
    QMetaObject::invokeMethod(m_worker,[&,state,folder,candidate,candidateFamily]{
        auto db=QSqlDatabase::database(state->connectionName,false);
        SourceFamily family;QHash<QString,StoredIdentity> identities;bool exists=false;OriginalIdentity known;
        const auto source=normalizedSourcePath(folder,request.sourceKey);
        auto validate=[&]{
            family={};identities.clear();
            if(!readSourceFamily(db,folder,request.sourceKey,&family,&error)
                ||!readIdentities(db,&identities,&exists,&error)
                ||!identityForSource(identities,folder,request.sourceKey,&known,&error))return false;
            if(known.isKnown()&&known!=request.candidateIdentity){error="Candidate does not match the recorded original identity";return false;}
            if(!known.isKnown()&&!request.acknowledgeUnknownIdentity){error="Unknown original identity requires explicit confirmation";return false;}
            for(const auto &key:family.photoKeys) {
                if(!validCopyKey(key)&&normalizedSourcePath(folder,key)==candidateFamily&&!family.originals.contains(key)){
                    error="Relink destination belongs to another source family";return false;
                }
            }
            for(auto it=family.allCopies.cbegin();it!=family.allCopies.cend();++it) {
                if(normalizedSourcePath(folder,it->value("source").toString())==candidateFamily&&!family.copies.contains(it.key())){
                    error="Relink destination belongs to another virtual source family";return false;
                }
            }
            for(auto it=identities.cbegin();it!=identities.cend();++it) {
                const auto normalized=normalizedSourcePath(folder,it.key());
                if(normalized==candidateFamily&&normalized!=source){error="Relink destination has another recorded identity";return false;}
            }
            // Adjunct rows can exist in legacy catalogs without a photo row.
            // Never overwrite or silently adopt those annotations either.
            for(const auto &table:{"photos","curation","catalog_tags","catalog_dates"}) {
                bool tablePresent=false;if(!tableExists(db,table,&tablePresent,&error))return false;
                if(!tablePresent)continue;
                QSqlQuery query(db);query.prepare("SELECT path FROM "+QString::fromLatin1(table));
                if(!query.exec()){error=query.lastError().text();return false;}
                while(query.next()) {
                    const auto key=query.value(0).toString();
                    if(normalizedSourcePath(folder,key)==candidateFamily&&!family.originals.contains(key)){
                        error="Relink destination already has a catalog record";return false;
                    }
                }
            }
            return true;
        };
        ok=db.isOpen()&&validate();
        if(ok)ok=backupBeforeTable(db,folder,"original_identity",&error)&&db.transaction();
        if(ok)ok=validate()&&createIdentityTable(db,&error);
        QString sortKey=request.sourceKey;
        if(ok&&!family.originals.isEmpty()) {
            const auto old=family.originals.front();
            sortKey=identities.contains(old)?identities.value(old).sortKey:old;
            for(const auto &table:{"photos","curation","catalog_tags","catalog_dates"}) {
                bool tablePresent=false;if(!tableExists(db,table,&tablePresent,&error)){ok=false;break;}
                if(!tablePresent)continue;
                QSqlQuery query(db);query.prepare("UPDATE "+QString::fromLatin1(table)+" SET path=? WHERE path=?");
                query.addBindValue(candidate);query.addBindValue(old);
                ok=query.exec();
                if(ok&&QString::fromLatin1(table)=="photos")ok=query.numRowsAffected()==1;
                if(!ok){error=query.lastError().text();if(error.isEmpty())error="Original source record changed during relink";break;}
            }
        }
        for(auto it=family.copies.cbegin();ok&&it!=family.copies.cend();++it) {
            auto mapping=it.value();mapping.insert("source",candidate);
            QSqlQuery query(db);query.prepare("UPDATE virtual_sources SET json=? WHERE key=?");
            query.addBindValue(QString::fromUtf8(QJsonDocument(mapping).toJson(QJsonDocument::Compact)));query.addBindValue(it.key());
            ok=query.exec()&&query.numRowsAffected()==1;if(!ok)error=query.lastError().text();
        }
        for(auto it=identities.cbegin();ok&&it!=identities.cend();++it) {
            if(normalizedSourcePath(folder,it.key())!=source)continue;
            QSqlQuery query(db);query.prepare("DELETE FROM original_identity WHERE source_key=?");query.addBindValue(it.key());
            ok=query.exec();if(!ok)error=query.lastError().text();
        }
        if(ok) {
            QSqlQuery query(db);query.prepare("INSERT INTO original_identity(source_key,schema,size_bytes,sha256,sort_key) VALUES(?,1,?,?,?)");
            query.addBindValue(candidate);query.addBindValue(request.candidateIdentity.sizeBytes);
            query.addBindValue(QString::fromLatin1(request.candidateIdentity.sha256));query.addBindValue(sortKey);
            ok=query.exec();if(!ok)error=query.lastError().text();
        }
        if(ok){ok=db.commit();if(!ok)error=db.lastError().text();}
        if(!ok)db.rollback();
        else affected=family.originals.size()+family.copies.size();
    },Qt::BlockingQueuedConnection);
    if(!ok){m_lastOpenError=error.isEmpty()?QString("Cannot relink original source"):error;emit writeFailed(m_lastOpenError);}
    else emit saved(affected);
    return ok;
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
        recordWriteResult(state,"curation",changes,ok,error);
        if (ok) emit saved(changes.size());
        else emit writeFailed(error);
    }, Qt::QueuedConnection);
}

bool ProjectDatabase::addVirtualCopy(const QString &key, const QString &source, const QString &name,
                                     const AdjustmentState &adjustments, const EditHistory &history,
                                     const CatalogTags &tags, const PhotoCuration &curation, const PhotoTimeline &timeline) {
    m_lastOpenError.clear();
    QStringList checkedName{name}; CatalogTags checkedTags; EditHistory checkedHistory;
    if (!m_open || !validCopyKey(key) || source.isEmpty() || !CatalogTags::normalize(&checkedName,1)
        || checkedName.size() != 1 || checkedName[0] != name || !CatalogTags::fromJson(tags.toJson(),&checkedTags)
        || !BrushMaskDocument::validDocument(adjustments.brushMasks)
        || !AdjustmentState::validBrushMaskJson(adjustments.toJson())
        || !checkedHistory.restore(history.toJson(),adjustments) || curation.rating < 0 || curation.rating > 5
        || (curation.flag != "none" && curation.flag != "pick" && curation.flag != "reject")) return false;
    PhotoTimeline checkedDates; if (!PhotoTimeline::fromJson(timeline.toJson(),&checkedDates) || !flush()) return false;
    const auto state = m_state; const auto folder = m_projectPath; bool ok = false; QString error;
    QMetaObject::invokeMethod(m_worker,[&,state,folder] {
        auto db = QSqlDatabase::database(state->connectionName,false);
        ok = db.isOpen() && backupBeforeTable(db,folder,"virtual_sources",&error)
            && backupBeforeTable(db,folder,"catalog_tags",&error)
            && backupBeforeTable(db,folder,"catalog_dates",&error)
            && backupBeforeTable(db,folder,"curation",&error);
        if (ok) ok = db.transaction();
        QSqlQuery query(db);
        if (ok) ok = query.exec("CREATE TABLE IF NOT EXISTS virtual_sources(key TEXT PRIMARY KEY,json TEXT NOT NULL)")
            && query.exec("CREATE TABLE IF NOT EXISTS catalog_tags(path TEXT PRIMARY KEY,json TEXT NOT NULL)")
            && query.exec("CREATE TABLE IF NOT EXISTS catalog_dates(path TEXT PRIMARY KEY,json TEXT NOT NULL)")
            && query.exec("CREATE TABLE IF NOT EXISTS curation(path TEXT PRIMARY KEY,rating INTEGER NOT NULL DEFAULT 0 CHECK(rating BETWEEN 0 AND 5),flag TEXT NOT NULL DEFAULT 'none' CHECK(flag IN ('none','pick','reject')))");
        auto insert = [&](const QString &sql,const QVariantList &values) {
            if (!ok) return;
            ok = query.prepare(sql); for (const auto &value : values) query.addBindValue(value);
            if (ok) ok = query.exec();
        };
        insert("INSERT INTO virtual_sources(key,json) VALUES(?,?)",{key,QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"source",source},{"name",name}}).toJson(QJsonDocument::Compact))});
        auto json = adjustments.toJson(); json.insert("_history",history.toJson());
        insert("INSERT INTO photos(path,adjustment_json) VALUES(?,?)",{key,QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))});
        insert("INSERT INTO catalog_tags(path,json) VALUES(?,?)",{key,QString::fromUtf8(QJsonDocument(tags.toJson()).toJson(QJsonDocument::Compact))});
        insert("INSERT INTO catalog_dates(path,json) VALUES(?,?)",{key,QString::fromUtf8(QJsonDocument(timeline.toJson()).toJson(QJsonDocument::Compact))});
        insert("INSERT INTO curation(path,rating,flag) VALUES(?,?,?)",{key,curation.rating,curation.flag});
        if (!ok && error.isEmpty()) error = query.lastError().text().isEmpty() ? db.lastError().text() : query.lastError().text();
        if (ok) { ok = db.commit(); if (!ok) error = db.lastError().text(); }
        if (!ok) db.rollback();
    },Qt::BlockingQueuedConnection);
    if (!ok) { m_lastOpenError = error; emit writeFailed(error); }
    else emit saved(1);
    return ok;
}

bool ProjectDatabase::renameVirtualCopy(const QString &key, const QString &name) {
    QStringList checked{name};
    if (!CatalogTags::normalize(&checked,1) || checked.size() != 1 || checked[0] != name) return false;
    return modifyVirtualCopy(key,name,false);
}
bool ProjectDatabase::removeVirtualCopy(const QString &key) { return modifyVirtualCopy(key,{},true); }
bool ProjectDatabase::modifyVirtualCopy(const QString &key, const QString &name, bool remove) {
    // Original paths never enter this transaction. Source files stay read-only.
    if (!m_open || !validCopyKey(key) || !flush()) return false;
    bool ok = false; QString error; const auto state = m_state;
    QMetaObject::invokeMethod(m_worker,[&,state] {
        auto db = QSqlDatabase::database(state->connectionName,false);
        ok = db.isOpen() && db.transaction(); QSqlQuery query(db); QJsonObject mapping;
        if (ok) {
            ok = query.prepare("SELECT json FROM virtual_sources WHERE key=?"); query.addBindValue(key);
            ok = ok && query.exec() && query.next();
            if (ok) {
                const auto bytes = query.value(0).toByteArray(); QJsonParseError parse;
                const auto doc = QJsonDocument::fromJson(bytes,&parse); mapping = doc.object();
                QStringList checked{mapping.value("name").toString()};
                ok = bytes.size() <= 16384 && parse.error == QJsonParseError::NoError && doc.isObject()
                    && mapping.value("schema").toDouble() == 1 && mapping.value("source").isString()
                    && !mapping.value("source").toString().isEmpty() && CatalogTags::normalize(&checked,1)
                    && checked.size() == 1 && checked[0] == mapping.value("name").toString()
                    && mapping == QJsonObject{{"schema",1},{"source",mapping.value("source")},{"name",mapping.value("name")}};
            }
            if (!ok) error = QStringLiteral("Virtual copy is missing or invalid");
            query.finish();
        }
        if (ok) {
            query.prepare("SELECT path FROM photos WHERE path=?"); query.addBindValue(key);
            ok = query.exec() && query.next(); query.finish();
            if (!ok) error = QStringLiteral("Virtual copy has no edit record");
        }
        if (ok && remove) {
            for (const auto &table : {QStringLiteral("photos"),QStringLiteral("curation"),QStringLiteral("catalog_tags"),QStringLiteral("catalog_dates")}) {
                query.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?"); query.addBindValue(table);
                ok = query.exec(); const bool exists = ok && query.next(); query.finish();
                if (ok && exists) {
                    ok = query.prepare("DELETE FROM " + table + " WHERE path=?"); query.addBindValue(key);
                    ok = ok && query.exec();
                    if (ok && table == "photos") ok = query.numRowsAffected() == 1;
                }
                if (!ok) break;
            }
            if (ok) {
                query.prepare("DELETE FROM virtual_sources WHERE key=?"); query.addBindValue(key);
                ok = query.exec() && query.numRowsAffected() == 1;
            }
        } else if (ok) {
            mapping.insert("name",name);
            query.prepare("UPDATE virtual_sources SET json=? WHERE key=?");
            query.addBindValue(QString::fromUtf8(QJsonDocument(mapping).toJson(QJsonDocument::Compact))); query.addBindValue(key);
            ok = query.exec() && query.numRowsAffected() == 1;
        }
        if (!ok && error.isEmpty()) error = query.lastError().text().isEmpty() ? db.lastError().text() : query.lastError().text();
        if (ok) { ok = db.commit(); if (!ok) error = db.lastError().text(); }
        if (!ok) db.rollback();
    },Qt::BlockingQueuedConnection);
    if (!ok) { m_lastOpenError = error; emit writeFailed(error); }
    else { m_lastOpenError.clear(); emit saved(1); }
    return ok;
}

bool ProjectDatabase::updateTagsBatch(const QHash<QString, CatalogTags> &changes) {
    if (!m_open) return false;
    if (changes.isEmpty()) return true;
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        CatalogTags checked;
        if (it.key().isEmpty() || !CatalogTags::fromJson(it.value().toJson(), &checked)) return false;
    }
    const auto state = m_state;
    const auto folder = m_projectPath;
    return QMetaObject::invokeMethod(m_worker, [this,state,folder,changes] {
        auto db = QSqlDatabase::database(state->connectionName, false);
        QSqlQuery query(db); QString error;
        // Backup before the additive migration, including committed WAL content.
        bool ok = db.isOpen() && backupBeforeTable(db,folder,"catalog_tags",&error);
        if (ok) ok = db.transaction();
        if (ok) ok = query.exec("CREATE TABLE IF NOT EXISTS catalog_tags(path TEXT PRIMARY KEY,json TEXT NOT NULL)");
        if (ok) ok = query.prepare("INSERT INTO catalog_tags(path,json) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET json=excluded.json");
        for (auto it = changes.begin(); ok && it != changes.end(); ++it) {
            query.bindValue(0,it.key()); query.bindValue(1,QString::fromUtf8(QJsonDocument(it.value().toJson()).toJson(QJsonDocument::Compact)));
            ok = query.exec();
        }
        if (!ok && error.isEmpty()) error = query.lastError().text().isEmpty() ? db.lastError().text() : query.lastError().text();
        if (ok) { ok = db.commit(); if (!ok) error = db.lastError().text(); }
        if (!ok) db.rollback();
        recordWriteResult(state,"tags",changes,ok,error);
        if (ok) emit saved(changes.size()); else emit writeFailed(error);
    }, Qt::QueuedConnection);
}

bool ProjectDatabase::updateBatch(const QHash<QString, AdjustmentState> &states, const QHash<QString, EditHistory> &histories, const QHash<QString, PhotoTimeline> &timelines) {
    if (!m_open) return false;
    if (states.isEmpty()) return timelines.isEmpty();
    // Fail before scheduling any database writes: malformed or enabled
    // draft masks cannot silently disappear from a serialized photo.
    for (auto it=states.cbegin();it!=states.cend();++it) {
        if (!BrushMaskDocument::validDocument(it.value().brushMasks)
            || !AdjustmentState::validBrushMaskJson(it.value().toJson())) return false;
    }
    for (auto it=histories.cbegin();it!=histories.cend();++it) {
        for (const auto &entry:it.value().entries()) {
            if (!BrushMaskDocument::validDocument(entry.state.brushMasks)
                || !AdjustmentState::validBrushMaskJson(entry.state.toJson())) return false;
        }
    }
    for (auto it = timelines.begin(); it != timelines.end(); ++it) {
        PhotoTimeline checked; if (!states.contains(it.key()) || !PhotoTimeline::fromJson(it.value().toJson(),&checked)) return false;
    }
    const auto state = m_state;
    const auto folder = m_projectPath;
    return QMetaObject::invokeMethod(m_worker, [this,state,states,histories,timelines,folder] {
        PerformanceSpan timer(QStringLiteral("database_batch"), {{"photos",states.size()}});
        auto db = QSqlDatabase::database(state->connectionName, false);
        QString error;
        bool ok = db.isOpen() && (timelines.isEmpty() || backupBeforeTable(db,folder,"catalog_dates",&error)) && db.transaction();
        if (!ok && error.isEmpty()) error = db.lastError().text();
        if (ok) {
            QSqlQuery query(db);
            ok = query.prepare("INSERT INTO photos(path,adjustment_json) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET adjustment_json=excluded.adjustment_json");
            for (auto it = states.constBegin(); ok && it != states.constEnd(); ++it) {
                if (validCopyKey(it.key())) {
                    QSqlQuery source(db); source.prepare("SELECT key FROM virtual_sources WHERE key=?"); source.addBindValue(it.key());
                    if (!source.exec() || !source.next()) { ok = false; error = QStringLiteral("Virtual copy has no source mapping"); break; }
                }
                query.bindValue(0,it.key());
                auto json = it.value().toJson();
                if (histories.contains(it.key())) json.insert("_history", histories.value(it.key()).toJson());
                query.bindValue(1,QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
                ok = query.exec();
            }
            if (ok && !timelines.isEmpty()) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS catalog_dates(path TEXT PRIMARY KEY,json TEXT NOT NULL)")
                    && query.prepare("INSERT INTO catalog_dates(path,json) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET json=excluded.json");
                for (auto it = timelines.begin(); ok && it != timelines.end(); ++it) {
                    query.bindValue(0,it.key()); query.bindValue(1,QString::fromUtf8(QJsonDocument(it.value().toJson()).toJson(QJsonDocument::Compact)));
                    ok = query.exec();
                }
            }
            if (!ok && error.isEmpty()) error = query.lastError().text();
            if (ok) { ok = db.commit(); if (!ok) error = db.lastError().text(); }
            if (!ok) db.rollback();
        }
        recordWriteResult(state,"adjustments",states,ok,error);
        if (ok) emit saved(states.size());
        else emit writeFailed(error);
    }, Qt::QueuedConnection);
}
bool ProjectDatabase::flush() {
    if (!m_thread.isRunning()) return false;
    QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
    QMutexLocker lock(&m_state->mutex);
    return m_state->error.isEmpty();
}
