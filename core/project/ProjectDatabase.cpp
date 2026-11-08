#include "core/project/ProjectDatabase.h"
#include "diagnostics/ActionTrace.h"
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
#include <QCryptographicHash>
#include <QPointer>
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
                    ? QStringLiteral("SELECT p.path,p.adjustment_json,c.rating,c.flag,p.imported_at,c.path,typeof(c.rating),typeof(c.flag) FROM photos p LEFT JOIN curation c ON c.path=p.path")
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
                    int rating = 0;
                    QString flag = QStringLiteral("none");
                    // An absent LEFT JOIN row keeps legacy defaults. A present
                    // row must retain its exact stored intent: never narrow a
                    // wide integer, coalesce NULL or sanitize an unknown flag.
                    if (hasCuration && !query.value(5).isNull()) {
                        bool validInteger = false;
                        const qint64 storedRating = query.value(2).toLongLong(&validInteger);
                        const QString storedFlag = query.value(3).toString();
                        if (query.value(6).toString() != QStringLiteral("integer") || !validInteger
                            || storedRating < 0 || storedRating > 5
                            || query.value(7).toString() != QStringLiteral("text")
                            || (storedFlag != QStringLiteral("none") && storedFlag != QStringLiteral("pick")
                                && storedFlag != QStringLiteral("reject"))) {
                            ready = false;
                            error = QStringLiteral("Invalid stored rating/flag for project photo");
                            break;
                        }
                        rating = int(storedRating);
                        flag = storedFlag;
                    }
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

namespace {
using XmpRecord = ProjectDatabase::XmpReviewRecord;
constexpr qsizetype XmpJsonLimit = 64 * 1024 * 1024;
bool xmpError(QString *error, const QString &text) { *error=text; return false; }
QByteArray xmpBytes(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
bool xmpHash(const QByteArray &h) {
    return h.size()==64 && std::all_of(h.cbegin(),h.cend(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
bool xmpObject(const QByteArray &bytes,QJsonObject *out,QString *error) {
    if(bytes.isEmpty()||bytes.size()>XmpJsonLimit)return xmpError(error,"Invalid or oversized XMP catalog JSON");
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject())return xmpError(error,"Invalid XMP catalog JSON");
    *out=doc.object();return true;
}
QJsonObject xmpSnapshotJson(const XmpMerge::Snapshot &s) {
    return {{"schema",1},{"adjustments",s.adjustments.toJson()},{"tags",s.tags.toJson()},{"rating",s.rating},{"flag",s.flag}};
}
bool xmpSnapshotParse(const QJsonObject &j,XmpMerge::Snapshot *out,QString *error) {
    const auto rating=j.value("rating");XmpMerge::Snapshot s;
    if(j.size()!=5||!j.value("schema").isDouble()||j.value("schema").toDouble()!=1
        ||!j.value("adjustments").isObject()||!j.value("tags").isObject()
        ||!rating.isDouble()||rating.toDouble()!=rating.toInt(-1)||rating.toInt(-1)<0||rating.toInt()>5
        ||!j.value("flag").isString()||!CatalogTags::fromJson(j.value("tags").toObject(),&s.tags))
        return xmpError(error,"Invalid XMP ancestor snapshot");
    s.adjustments=AdjustmentState::fromJson(j.value("adjustments").toObject());s.rating=rating.toInt();s.flag=j.value("flag").toString();
    XmpMerge::Plan check;
    if(s.adjustments.toJson()!=j.value("adjustments").toObject()||!LookProfiles::engineCompatible(s.adjustments.look)
        ||!XmpMerge::analyze("validate-saved-ancestor",s,s,{},&check,error)||xmpSnapshotJson(s)!=j)
        return xmpError(error,"Invalid or unsupported XMP ancestor intent");
    *out=std::move(s);return true;
}
QJsonObject xmpTargetJson(const XmpCapture::Target &t) {
    return {{"project",t.projectLocator},{"source",t.sourceKey},{"storage",t.storageKey},{"copy",t.copyKey},
        {"source_identity",t.sourceIdentity},{"controller",t.controller.toString(QUuid::WithoutBraces)},
        {"catalog_epoch",QString::number(t.catalogEpoch)},{"photo_epoch",QString::number(t.photoEpoch)},
        {"sequence",QString::number(t.sequence)}};
}
class XmpTransaction {
public:
    explicit XmpTransaction(QSqlDatabase &db):m_db(db){}
    ~XmpTransaction(){if(m_active)m_db.rollback();}
    bool begin(QString *error){m_active=m_db.transaction();return m_active||xmpError(error,m_db.lastError().text());}
    bool commit(QString *error){if(!m_db.commit())return xmpError(error,m_db.lastError().text());m_active=false;return true;}
private:
    QSqlDatabase &m_db;bool m_active=false;
};
bool xmpAncestor(QSqlDatabase &db,const XmpRecord &record,std::optional<XmpMerge::Snapshot> *out,QJsonValue *token,QString *error) {
    bool exists=false;if(!tableExists(db,"xmp_ancestry",&exists,error))return false;
    *out={};*token=QJsonValue(QJsonValue::Null);if(!exists)return true;
    QSqlQuery shape(db);if(!shape.exec("PRAGMA table_info(xmp_ancestry)"))return xmpError(error,shape.lastError().text());
    const QStringList names{"version_key","locator","schema","source_key","size_bytes","source_sha256","sidecar_sha256","snapshot_json"};
    const QStringList types{"TEXT","TEXT","INTEGER","TEXT","INTEGER","TEXT","TEXT","TEXT"};int index=0;
    while(shape.next()) {
        if(index>=names.size()||shape.value(1).toString()!=names[index]||shape.value(2).toString().toUpper()!=types[index]
            ||shape.value(5).toInt()!=(index<2?index+1:0))return xmpError(error,"Unsupported XMP ancestry table schema");
        ++index;
    }
    if(index!=names.size())return xmpError(error,"Unsupported XMP ancestry table schema");
    QSqlQuery q(db);q.prepare("SELECT schema,source_key,size_bytes,source_sha256,sidecar_sha256,snapshot_json,typeof(schema),typeof(size_bytes) FROM xmp_ancestry WHERE version_key=? AND locator=?");
    q.addBindValue(record.storageKey);q.addBindValue(record.sidecarLocator);
    if(!q.exec())return xmpError(error,q.lastError().text());if(!q.next())return true;
    QJsonObject snapshot;
    if(q.value(6).toString()!="integer"||q.value(7).toString()!="integer"||q.value(0).toLongLong()!=1
        ||q.value(1).toString()!=record.sourceKey||q.value(2).toLongLong()!=record.photo.identity.sizeBytes
        ||q.value(3).toByteArray()!=record.photo.identity.sha256||!xmpHash(q.value(4).toByteArray())
        ||!xmpObject(q.value(5).toByteArray(),&snapshot,error))
        return xmpError(error,"XMP ancestry source/version is invalid or changed");
    XmpMerge::Snapshot ancestor;if(!xmpSnapshotParse(snapshot,&ancestor,error))return false;
    *out=std::move(ancestor);*token=QJsonObject{{"schema",1},{"source",record.sourceKey},
        {"size",QString::number(record.photo.identity.sizeBytes)},{"source_sha256",QString::fromLatin1(record.photo.identity.sha256)},
        {"sidecar_sha256",q.value(4).toString()},{"snapshot",snapshot}};
    if(q.next())return xmpError(error,"Ambiguous XMP ancestry row");return true;
}
bool xmpReadRecord(QSqlDatabase &db,const QString &folder,const QString &writer,
                   const XmpCapture::Captured &capture,XmpRecord *out,QString *error) {
    if(!db.isOpen()||!capture.valid()||!capture.target().valid())return xmpError(error,"Missing captured XMP or writable catalog context");
    const auto t=capture.target();
    if(ProjectDatabase::normalizedSourcePath(folder,t.projectLocator)!=ProjectDatabase::normalizedSourcePath(folder,folder))
        return xmpError(error,"The reviewed catalog changed");
    XmpRecord next;next.writerId=writer;next.projectLocator=folder;next.storageKey=t.storageKey;next.sourceKey=t.sourceKey;
    next.sidecarLocator=capture.locator();next.sidecarSha256=capture.sha256();next.target=t;
    if(next.sidecarLocator.isEmpty()||!xmpHash(next.sidecarSha256))return xmpError(error,"Invalid captured sidecar provenance");
    SourceFamily family;if(!readSourceFamily(db,folder,t.sourceKey,&family,error)||!family.photoKeys.contains(t.storageKey))
        return xmpError(error,"The reviewed source/version was removed");
    QJsonObject mapping;
    if(validCopyKey(t.storageKey)) {
        if(t.copyKey!=t.storageKey||!family.copies.contains(t.storageKey))return xmpError(error,"Virtual copy source mapping changed");
        mapping=family.copies.value(t.storageKey);
        if(mapping.value("source").toString()!=t.sourceKey)return xmpError(error,"Virtual copy source changed");
    } else if(!t.copyKey.isEmpty()||t.storageKey!=t.sourceKey||!family.originals.contains(t.storageKey))
        return xmpError(error,"Original photo source mapping changed");
    QHash<QString,StoredIdentity> identities;bool hasIdentity=false;ProjectDatabase::OriginalIdentity identity;
    if(!readIdentities(db,&identities,&hasIdentity,error)||!identityForSource(identities,folder,t.sourceKey,&identity,error)
        ||!identity.isKnown()||QString::fromLatin1(identity.sha256)!=t.sourceIdentity)
        return xmpError(error,"A verified saved original identity is required for XMP review");
    QSqlQuery q(db);q.prepare("SELECT adjustment_json,imported_at FROM photos WHERE path=?");q.addBindValue(t.storageKey);
    if(!q.exec()||!q.next())return xmpError(error,"The reviewed photo no longer exists");
    QJsonObject raw;if(!xmpObject(q.value(0).toByteArray(),&raw,error))return false;
    const QString imported=q.value(1).toString();if(q.next())return xmpError(error,"Ambiguous photo version");q.finish();
    auto develop=raw;const auto historyValue=develop.take("_history");auto state=AdjustmentState::fromJson(develop);
    if((!develop.isEmpty()&&state.toJson()!=develop)||!state.look.error.isEmpty()||!LookProfiles::engineCompatible(state.look))
        return xmpError(error,"Unsupported stored Develop intent; refusing to discard fields");
    EditHistory history;
    if(!historyValue.isUndefined()&&(!historyValue.isObject()||!history.restore(historyValue.toObject(),state)))
        return xmpError(error,"Invalid stored edit history");
    history.initialize(state);state=history.entries()[history.cursor()].state;
    CatalogTags tags;PhotoTimeline timeline;timeline.importedAt=std::max(qint64(0),PhotoTimeline::sqlImportTime(imported));
    int rating=0;QString flag="none";bool exists=false;QJsonObject tagsJson=tags.toJson(),datesJson=timeline.toJson();
    if(!tableExists(db,"catalog_tags",&exists,error))return false;
    if(exists) {
        q.prepare("SELECT json FROM catalog_tags WHERE path=?");q.addBindValue(t.storageKey);
        if(!q.exec())return xmpError(error,q.lastError().text());
        if(q.next()&&(!xmpObject(q.value(0).toByteArray(),&tagsJson,error)||!CatalogTags::fromJson(tagsJson,&tags)))return false;q.finish();
    }
    if(!tableExists(db,"catalog_dates",&exists,error))return false;
    if(exists) {
        q.prepare("SELECT json FROM catalog_dates WHERE path=?");q.addBindValue(t.storageKey);
        if(!q.exec())return xmpError(error,q.lastError().text());
        if(q.next()&&(!xmpObject(q.value(0).toByteArray(),&datesJson,error)||!PhotoTimeline::fromJson(datesJson,&timeline)))return false;q.finish();
    }
    if(!tableExists(db,"curation",&exists,error))return false;
    if(exists) {
        q.prepare("SELECT rating,flag,typeof(rating) FROM curation WHERE path=?");q.addBindValue(t.storageKey);
        if(!q.exec())return xmpError(error,q.lastError().text());
        if(q.next()) {
            bool validInteger=false;const qint64 storedRating=q.value(0).toLongLong(&validInteger);flag=q.value(1).toString();
            if(q.value(2).toString()!="integer"||!validInteger||storedRating<0||storedRating>5||!QStringList{"none","pick","reject"}.contains(flag))
                return xmpError(error,"Invalid stored rating/flag");
            rating=int(storedRating);
        }q.finish();
    }
    next.photo={t.sourceKey,state,rating,flag,history,tags,t.copyKey,mapping.value("name").toString(),timeline,t.sourceKey,identity};
    QJsonValue ancestorToken;if(!xmpAncestor(db,next,&next.ancestor,&ancestorToken,error))return false;
    const QJsonObject token{{"writer",writer},{"project",folder},{"target",xmpTargetJson(t)},
        {"sidecar_locator",next.sidecarLocator},{"sidecar_sha256",QString::fromLatin1(next.sidecarSha256)},
        {"sidecar_bytes",QString::number(capture.byteCount())},{"stored_develop_history",raw},{"imported",imported},
        {"mapping",mapping},{"tags",tagsJson},{"timeline",datesJson},{"rating",rating},{"flag",flag},
        {"original_size",QString::number(identity.sizeBytes)},{"original_sha256",QString::fromLatin1(identity.sha256)},{"ancestor",ancestorToken}};
    next.recordToken=QCryptographicHash::hash(xmpBytes(token),QCryptographicHash::Sha256).toHex();
    const XmpMerge::Snapshot catalog{state,tags,rating,flag};
    if(!XmpMerge::analyze(QString::fromLatin1(next.recordToken),next.ancestor,catalog,capture.document(),&next.plan,error))return false;
    next.valid=true;*out=std::move(next);return true;
}
bool xmpSameReview(const XmpRecord &expected,const XmpRecord &current) {
    return expected.valid&&expected.writerId==current.writerId&&expected.projectLocator==current.projectLocator
        &&expected.storageKey==current.storageKey&&expected.sourceKey==current.sourceKey
        &&expected.target==current.target&&expected.sidecarLocator==current.sidecarLocator
        &&expected.sidecarSha256==current.sidecarSha256&&xmpHash(expected.recordToken)&&expected.recordToken==current.recordToken;
}
bool xmpCommit(QSqlDatabase &db,const QString &folder,const QString &writer,const XmpCapture::Captured &capture,
               const XmpRecord &expected,const QVector<XmpMerge::Resolution> &choices,XmpRecord *saved,QString *error) {
    XmpRecord current;
    {XmpTransaction read(db);if(!read.begin(error)||!xmpReadRecord(db,folder,writer,capture,&current,error)||!read.commit(error))return false;}
    if(!xmpSameReview(expected,current))return xmpError(error,"The catalog, source, sidecar or history changed; review again");
    const XmpMerge::Snapshot catalog{current.photo.adjustments,current.photo.tags,current.photo.rating,current.photo.flag};
    XmpSidecar::Document patch;
    if(!XmpMerge::resolve(current.plan,QString::fromLatin1(current.recordToken),catalog,choices,&patch,error))return false;
    auto next=current.photo;
    if(patch.hasRating)next.rating=patch.rating;if(patch.hasFlag)next.flag=patch.flag;
    if(patch.hasKeywords)next.tags.keywords=patch.tags.keywords;if(patch.hasLabel)next.tags.label=patch.tags.label;
    if(patch.hasAlbums)next.tags.albums=patch.tags.albums;
    const bool developChanged=patch.hasAdjustments&&patch.adjustments.toJson()!=next.adjustments.toJson();
    if(developChanged) {next.adjustments=patch.adjustments;next.history.record(next.adjustments,"xmp_merge");next.history.finish();next.timeline.editedAt=QDateTime::currentMSecsSinceEpoch();}
    const auto incoming=capture.document();const bool full=incoming.hasAdjustments&&incoming.hasRating&&incoming.hasFlag&&incoming.hasKeywords&&incoming.hasLabel&&incoming.hasAlbums;
    QStringList tables{"curation","catalog_tags"};if(developChanged)tables<<"catalog_dates";if(full)tables<<"xmp_ancestry";
    // Consistent backups run outside a transaction. Re-read and recheck the
    // complete token inside the write transaction after any migration backup.
    for(const auto &table:tables)if(!backupBeforeTable(db,folder,table,error))return false;
    XmpTransaction transaction(db);if(!transaction.begin(error))return false;XmpRecord atWrite;
    if(!xmpReadRecord(db,folder,writer,capture,&atWrite,error))return false;
    if(!xmpSameReview(expected,atWrite))return xmpError(error,"The catalog changed while preparing its backup; review again");
    QSqlQuery q(db);
    auto exec=[&](const QString &sql,const QVariantList &values=QVariantList{}) {
        if(!q.prepare(sql))return xmpError(error,q.lastError().text());
        for(const auto &value:values)q.addBindValue(value);
        return q.exec()||xmpError(error,q.lastError().text());
    };
    if(!exec("CREATE TABLE IF NOT EXISTS curation(path TEXT PRIMARY KEY,rating INTEGER NOT NULL DEFAULT 0 CHECK(rating BETWEEN 0 AND 5),flag TEXT NOT NULL DEFAULT 'none' CHECK(flag IN ('none','pick','reject')))")
        ||!exec("CREATE TABLE IF NOT EXISTS catalog_tags(path TEXT PRIMARY KEY,json TEXT NOT NULL)"))return false;
    if(developChanged) {
        auto photoJson=next.adjustments.toJson();photoJson.insert("_history",next.history.toJson());
        if(!exec("UPDATE photos SET adjustment_json=? WHERE path=?",{QString::fromUtf8(xmpBytes(photoJson)),current.storageKey}))return false;
        if(q.numRowsAffected()!=1)return xmpError(error,"The reviewed photo was removed");
        if(!exec("CREATE TABLE IF NOT EXISTS catalog_dates(path TEXT PRIMARY KEY,json TEXT NOT NULL)")
            ||!exec("INSERT INTO catalog_dates(path,json) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET json=excluded.json",{current.storageKey,QString::fromUtf8(xmpBytes(next.timeline.toJson()))}))return false;
    }
    if(!exec("INSERT INTO curation(path,rating,flag) VALUES(?,?,?) ON CONFLICT(path) DO UPDATE SET rating=excluded.rating,flag=excluded.flag",{current.storageKey,next.rating,next.flag})
        ||!exec("INSERT INTO catalog_tags(path,json) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET json=excluded.json",{current.storageKey,QString::fromUtf8(xmpBytes(next.tags.toJson()))}))return false;
    if(full) {
        const auto snapshot=xmpSnapshotJson({incoming.adjustments,incoming.tags,incoming.rating,incoming.flag});XmpMerge::Snapshot checked;
        if(!xmpSnapshotParse(snapshot,&checked,error))return false;
        if(!exec("CREATE TABLE IF NOT EXISTS xmp_ancestry(version_key TEXT NOT NULL,locator TEXT NOT NULL,schema INTEGER NOT NULL CHECK(schema=1),source_key TEXT NOT NULL,size_bytes INTEGER NOT NULL,source_sha256 TEXT NOT NULL,sidecar_sha256 TEXT NOT NULL,snapshot_json TEXT NOT NULL,PRIMARY KEY(version_key,locator))")
            ||!exec("INSERT INTO xmp_ancestry(version_key,locator,schema,source_key,size_bytes,source_sha256,sidecar_sha256,snapshot_json) VALUES(?,?,1,?,?,?,?,?) ON CONFLICT(version_key,locator) DO UPDATE SET schema=excluded.schema,source_key=excluded.source_key,size_bytes=excluded.size_bytes,source_sha256=excluded.source_sha256,sidecar_sha256=excluded.sidecar_sha256,snapshot_json=excluded.snapshot_json",
                {current.storageKey,current.sidecarLocator,current.sourceKey,next.identity.sizeBytes,QString::fromLatin1(next.identity.sha256),QString::fromLatin1(capture.sha256()),QString::fromUtf8(xmpBytes(snapshot))}))return false;
    }
    q.finish();if(!transaction.commit(error))return false;
    // The durable result remains true even if a later read fails; callers must
    // reload instead of treating a committed merge as an uncommitted retry.
    XmpTransaction reload(db);
    if(!reload.begin(error)||!xmpReadRecord(db,folder,writer,capture,saved,error)||!reload.commit(error)) {
        *saved={};*error="XMP merge committed; reload the catalog: "+*error;
    }
    return true;
}
}

bool ProjectDatabase::reviewCapturedXmp(const XmpCapture::Captured &capture,QObject *context,XmpReviewDone done) {
    if(QThread::currentThread()!=thread()||!m_open||!context||context->thread()!=thread()||!done||!capture.valid())return false;
    const auto state=m_state;const auto folder=m_projectPath;const QPointer<QObject> guard(context);
    return QMetaObject::invokeMethod(m_worker,[this,state,folder,capture,guard,done=std::move(done)] {
        XmpRecord record;QString error;bool pendingError=false;
        {QMutexLocker lock(&state->mutex);pendingError=!state->error.isEmpty();}
        auto db=QSqlDatabase::database(state->connectionName,false);
        if(pendingError)error="Resolve pending catalog save failures before reviewing XMP";
        else {
            XmpTransaction transaction(db);
            const bool ok=transaction.begin(&error)&&xmpReadRecord(db,folder,state->connectionName,capture,&record,&error)&&transaction.commit(&error);
            if(!ok){record={};if(error.isEmpty())error="Cannot read a valid XMP review snapshot";}
        }
        QMetaObject::invokeMethod(this,[guard,done,record=std::move(record),error] {if(guard)done(record,error);},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
bool ProjectDatabase::commitCapturedXmp(const XmpCapture::Captured &capture,const XmpReviewRecord &expected,
                                        const QVector<XmpMerge::Resolution> &choices,QObject *context,XmpCommitDone done) {
    if(QThread::currentThread()!=thread()||!m_open||!context||context->thread()!=thread()||!done||!capture.valid()||!expected.valid)return false;
    const auto state=m_state;const auto folder=m_projectPath;const QPointer<QObject> guard(context);
    return QMetaObject::invokeMethod(m_worker,[this,state,folder,capture,expected,choices,guard,done=std::move(done)] {
        XmpRecord record;QString error;bool committed=false,pendingError=false;
        {QMutexLocker lock(&state->mutex);pendingError=!state->error.isEmpty();}
        auto db=QSqlDatabase::database(state->connectionName,false);
        if(pendingError)error="Resolve pending catalog save failures before merging XMP";
        else committed=xmpCommit(db,folder,state->connectionName,capture,expected,choices,&record,&error);
        if(!committed&&error.isEmpty())error="XMP merge was not committed";
        if(committed) {
            ActionTrace::instance().record("xmp_merge_sql_committed",{{"project",folder},{"storage_key",capture.target().storageKey},
                {"source_identity",capture.target().sourceIdentity},{"sequence",QString::number(capture.target().sequence)},
                {"controller",capture.target().controller.toString(QUuid::WithoutBraces)},
                {"sidecar_sha256",QString::fromLatin1(capture.sha256())},{"record_valid_after_commit",record.valid},
                {"SQL_thread",QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()),16)}});
            emit saved(1);
        }
        // Refusal belongs to this explicit merge callback. The generic
        // autosave writeFailed handler would requeue old GUI rows and could
        // overwrite the very concurrent change which invalidated the review.
        QMetaObject::invokeMethod(this,[guard,done,committed,record=std::move(record),error] {if(guard)done(committed,record,error);},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
