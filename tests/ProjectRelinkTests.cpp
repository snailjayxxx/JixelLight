#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QUuid>
#include "core/project/ProjectDatabase.h"

namespace {
using Identity=ProjectDatabase::OriginalIdentity;
bool fileFixture(const QString &path,const QByteArray &bytes) {
    QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
}
Identity identityOf(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))return {};
    return {file.size(),hash.result().toHex()};
}
template<class Visitor> bool sqlVisit(const QString &folder,Visitor visitor) {
    const auto connection="relink-test-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool ok=false;
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(QDir(folder).filePath("Project.db"));
        if(db.open()){ok=visitor(db);db.close();}
    }
    QSqlDatabase::removeDatabase(connection);return ok;
}
bool sqlExec(const QString &folder,const QString &statement,const QVariantList &values={}) {
    return sqlVisit(folder,[&](QSqlDatabase &db){QSqlQuery query(db);if(!query.prepare(statement))return false;
        for(const auto &value:values)query.addBindValue(value);return query.exec();});
}
QVariantList sqlRow(const QString &folder,const QString &statement,const QVariantList &values={}) {
    QVariantList row;
    sqlVisit(folder,[&](QSqlDatabase &db){QSqlQuery query(db);if(!query.prepare(statement))return false;
        for(const auto &value:values)query.addBindValue(value);
        if(!query.exec()||!query.next())return false;
        for(int i=0;i<query.record().count();++i)row.append(query.value(i));return true;});
    return row;
}
QJsonObject databaseRows(const QString &folder) {
    QJsonObject result;
    const bool ok=sqlVisit(folder,[&](QSqlDatabase &db){
        for(const auto &table:QStringList{"photos","curation","catalog_tags","catalog_dates","virtual_sources","original_identity"}) {
            QSqlQuery exists(db);exists.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?");exists.addBindValue(table);
            if(!exists.exec())return false;if(!exists.next())continue;
            QSqlQuery query(db);if(!query.exec("SELECT * FROM "+table+" ORDER BY 1"))return false;
            QJsonArray rows;
            while(query.next()){QJsonArray row;for(int i=0;i<query.record().count();++i)row.append(QJsonValue::fromVariant(query.value(i)));rows.append(row);}
            result.insert(table,rows);
        }
        return true;
    });
    if(!ok)result.insert("error",true);return result;
}
QString copyKey(){return "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);}
EditHistory redoHistory(AdjustmentState *current,double later) {
    EditHistory history;history.initialize(*current);auto next=*current;next.exposure=later;
    history.record(next,"exposure");history.undo();*current=history.entries()[history.cursor()].state;return history;
}
}

class ProjectRelinkTests : public QObject {
    Q_OBJECT
private slots:
    void knownMoveUpdatesWholeFamilyAndPreservesHistoryRowsAndCatalogOrder() {
        QTemporaryDir dir;ProjectDatabase db;QVERIFY(db.create(dir.path(),"Move"));const auto folder=db.projectPath();
        const auto old=QDir(folder).filePath("a.raw"),peer=QDir(folder).filePath("b.raw"),moved=QDir(folder).filePath("z.raw");
        QVERIFY(fileFixture(old,QByteArray(200000,'a')+"original"));QVERIFY(fileFixture(peer,"peer"));const auto identity=identityOf(old);QVERIFY(identity.isKnown());
        AdjustmentState original;original.exposure=.25;auto originalHistory=redoHistory(&original,.75);
        AdjustmentState alternate;alternate.exposure=-.5;auto copyHistory=redoHistory(&alternate,-1.25);
        const auto one=copyKey(),two=copyKey();CatalogTags tags;tags.keywords={"keep"};tags.albums={"Album"};tags.label="blue";
        PhotoTimeline dates;dates.captureTime="2025-01-02 03:04:05";dates.captureChecked=true;dates.importedAt=1000;dates.editedAt=2000;
        QVERIFY(db.updateBatch({{old,original},{peer,{}}},{{old,originalHistory}},{{old,dates}}));
        QVERIFY(db.updateTagsBatch({{old,tags}}));QVERIFY(db.updateCurationBatch({{old,{4,"pick"}}}));QVERIFY(db.flush());
        QVERIFY(db.addVirtualCopy(one,"a.raw","Relative",alternate,copyHistory,tags,{3,"reject"},dates));
        QVERIFY(db.addVirtualCopy(two,old,"Absolute",alternate,copyHistory,tags,{2,"none"},dates));
        QVERIFY(sqlExec(folder,"UPDATE photos SET imported_at='2026-01-01 00:00:00'"));
        QVERIFY(db.recordOriginalIdentity("a.raw",identity));
        const auto originalRaw=sqlRow(folder,"SELECT rowid,imported_at,adjustment_json FROM photos WHERE path=?",{old});
        const auto copyRaw=sqlRow(folder,"SELECT rowid,imported_at,adjustment_json FROM photos WHERE path=?",{one});
        QVERIFY(!originalRaw.isEmpty());QVERIFY(!copyRaw.isEmpty());
        QVector<ProjectDatabase::SavedPhoto> before;QVERIFY(db.readSnapshot(folder,&before));QCOMPARE(before.size(),4);
        QCOMPARE(before[0].path,old);QCOMPARE(before[1].path,peer);
        QVERIFY(QFile::rename(old,moved));QVERIFY(!QFileInfo::exists(old));
        // Missing paths remain readable catalog records, without source I/O.
        QVector<ProjectDatabase::SavedPhoto> missing;QVERIFY(db.readSnapshot(folder,&missing));QCOMPARE(missing.size(),4);
        QVERIFY2(db.relinkSource({"a.raw",moved,identity,false}),qPrintable(db.lastError()));
        QCOMPARE(sqlRow(folder,"SELECT rowid,imported_at,adjustment_json FROM photos WHERE path=?",{moved}),originalRaw);
        QCOMPARE(sqlRow(folder,"SELECT rowid,imported_at,adjustment_json FROM photos WHERE path=?",{one}),copyRaw);
        QVERIFY(sqlRow(folder,"SELECT path FROM photos WHERE path=?",{old}).isEmpty());
        QVector<ProjectDatabase::SavedPhoto> after;QVERIFY(db.readSnapshot(folder,&after));QCOMPARE(after.size(),before.size());
        QCOMPARE(after[0].path,moved);QCOMPARE(after[1].path,peer); // rename did not reorder same-second originals
        for(int i=0;i<after.size();++i) {
            QCOMPARE(after[i].copyKey,before[i].copyKey);QCOMPARE(after[i].versionName,before[i].versionName);
            QCOMPARE(after[i].history.toJson(),before[i].history.toJson());QCOMPARE(after[i].adjustments.toJson(),before[i].adjustments.toJson());
            QCOMPARE(after[i].tags.toJson(),before[i].tags.toJson());QCOMPARE(after[i].rating,before[i].rating);QCOMPARE(after[i].flag,before[i].flag);
            QCOMPARE(after[i].timeline.toJson(),before[i].timeline.toJson());
            if(i!=1){QCOMPARE(after[i].path,moved);QCOMPARE(after[i].sourceKey,moved);QVERIFY(after[i].identity==identity);}
        }
        const auto again=QDir(folder).filePath("zz.raw");QVERIFY(QFile::rename(moved,again));
        QVERIFY(db.relinkSource({moved,again,identity,false}));QVERIFY(db.readSnapshot(folder,&after));QCOMPARE(after[0].path,again);QCOMPARE(after[1].path,peer);
        QCOMPARE(identityOf(again).sha256,identity.sha256);
    }

    void virtualOnlyUnknownIdentityRequiresConfirmationAndEstablishesBaseline() {
        QTemporaryDir dir;ProjectDatabase db;QVERIFY(db.create(dir.path(),"VirtualOnly"));const auto folder=db.projectPath();
        const auto candidate=QDir(folder).filePath("recovered.raw");QVERIFY(fileFixture(candidate,"actual candidate bytes"));const auto identity=identityOf(candidate);
        AdjustmentState state;EditHistory history;history.initialize(state);const auto key=copyKey();
        QVERIFY(db.addVirtualCopy(key,"offline.raw","Only",state,history,{},{}));
        Identity known;QVERIFY(db.originalIdentity("offline.raw",&known));QVERIFY(!known.isKnown());
        const auto before=databaseRows(folder);
        QVERIFY(!db.relinkSource({"offline.raw",candidate,identity,false}));QVERIFY(!db.lastError().isEmpty());QCOMPARE(databaseRows(folder),before);QVERIFY(db.flush());
        QVERIFY(db.relinkSource({"offline.raw",candidate,identity,true}));
        QVector<ProjectDatabase::SavedPhoto> photos;QVERIFY(db.readSnapshot(folder,&photos));QCOMPARE(photos.size(),1);
        QCOMPARE(photos[0].copyKey,key);QCOMPARE(photos[0].versionName,QString("Only"));QCOMPARE(photos[0].path,candidate);QVERIFY(photos[0].identity==identity);
        QVERIFY(sqlRow(folder,"SELECT path FROM photos WHERE path=?",{candidate}).isEmpty());
        QVERIFY(db.originalIdentity(candidate,&known));QVERIFY(known==identity);
        const auto baseline=databaseRows(folder);auto wrong=identity;wrong.sha256[0]=wrong.sha256[0]=='a'?'b':'a';
        QVERIFY(!db.relinkSource({candidate,QDir(folder).filePath("wrong.raw"),wrong,true}));QCOMPARE(databaseRows(folder),baseline);
    }

    void relativeOriginalAndMixedCopySourcesRelinkWithoutDuplicatingRows() {
        QTemporaryDir dir;ProjectDatabase db;QVERIFY(db.create(dir.path(),"Relative"));const auto folder=db.projectPath();
        const auto old=QDir(folder).filePath("old.raw"),candidate=QDir(folder).filePath("new.raw");
        QVERIFY(fileFixture(old,"relative original"));const auto identity=identityOf(old);
        AdjustmentState state;state.exposure=.6;EditHistory history;history.initialize(state);const auto key=copyKey();
        QVERIFY(db.updateAdjustment("old.raw",state));QVERIFY(db.flush());QVERIFY(db.addVirtualCopy(key,old,"Mixed",state,history,{},{}));
        QVERIFY(db.recordOriginalIdentity(old,identity));Identity alias;QVERIFY(db.originalIdentity("./old.raw",&alias));QVERIFY(alias==identity);
        QVERIFY(db.recordOriginalIdentity("old.raw",identity));
        QVERIFY(QFile::rename(old,candidate));QVERIFY(db.relinkSource({old,candidate,identity,false}));
        QVector<ProjectDatabase::SavedPhoto> photos;QVERIFY(db.open(folder,&photos));QCOMPARE(photos.size(),2);
        QCOMPARE(photos[0].path,candidate);QCOMPARE(photos[1].path,candidate);QCOMPARE(photos[1].copyKey,key);
        QVERIFY(sqlRow(folder,"SELECT path FROM photos WHERE path='old.raw'").isEmpty());
        state.exposure=1;QVERIFY(db.updateAdjustment(candidate,state));QVERIFY(db.flush());QVERIFY(db.readSnapshot(folder,&photos));QCOMPARE(photos.size(),2);QCOMPARE(photos[0].adjustments.exposure,1.0);
        // Two original PKs resolving to one source are ambiguous, never merged.
        QVERIFY(db.updateAdjustment("./new.raw",{}));QVERIFY(db.flush());const auto before=databaseRows(folder);
        QVERIFY(!db.relinkSource({candidate,QDir(folder).filePath("another.raw"),identity,false}));QCOMPARE(databaseRows(folder),before);
    }

    void conflictsAndInvalidIdentitiesNeverAdoptAnotherFamilyOrOrphanAnnotations() {
        QTemporaryDir dir;ProjectDatabase db;QVERIFY(db.create(dir.path(),"Conflicts"));const auto folder=db.projectPath();
        const auto old=QDir(folder).filePath("old.raw"),other=QDir(folder).filePath("other.raw"),onlyCopy=QDir(folder).filePath("copy-only.raw"),orphan=QDir(folder).filePath("orphan.raw");
        QVERIFY(fileFixture(old,"original"));const auto identity=identityOf(old);AdjustmentState state;EditHistory history;history.initialize(state);
        QVERIFY(db.updateBatch({{old,state},{other,state}}));QVERIFY(db.flush());QVERIFY(db.addVirtualCopy(copyKey(),onlyCopy,"Other family",state,history,{},{}));
        QVERIFY(db.updateTagsBatch({{orphan,{}}}));QVERIFY(db.flush());QVERIFY(db.recordOriginalIdentity(old,identity));
        const auto before=databaseRows(folder);
        for(const auto &target:QStringList{other,onlyCopy,orphan}) {
            QVERIFY(!db.relinkSource({old,target,identity,false}));QVERIFY(!db.lastError().isEmpty());QCOMPARE(databaseRows(folder),before);QVERIFY(db.flush());
        }
        for(const auto &invalid:QList<Identity>{{-1,identity.sha256},{identity.sizeBytes,QByteArray(64,'A')},{identity.sizeBytes,"short"}}) {
            QVERIFY(!invalid.isKnown());QVERIFY(!db.recordOriginalIdentity(old,invalid));
            QVERIFY(!db.relinkSource({old,QDir(folder).filePath("fresh.raw"),invalid,true}));QCOMPARE(databaseRows(folder),before);
        }
        auto wrong=identity;++wrong.sizeBytes;QVERIFY(!db.recordOriginalIdentity(old,wrong));QCOMPARE(databaseRows(folder),before);
        QVERIFY(!db.relinkSource({old,"relative.raw",identity,true}));QCOMPARE(databaseRows(folder),before);
        QVERIFY(!db.relinkSource({"not-in-project.raw",QDir(folder).filePath("fresh.raw"),identity,true}));QCOMPARE(databaseRows(folder),before);
    }

    void sqlFailureAtEveryMutationRollsBackAllTablesAndWriterCanRetry() {
        QTemporaryDir dir;ProjectDatabase db;QVERIFY(db.create(dir.path(),"Rollback"));const auto folder=db.projectPath();
        const auto old=QDir(folder).filePath("old.raw"),candidate=QDir(folder).filePath("moved.raw");
        QVERIFY(fileFixture(old,"rollback source"));const auto identity=identityOf(old);AdjustmentState state;state.exposure=.5;
        EditHistory history;history.initialize(state);CatalogTags tags;tags.keywords={"retained"};PhotoTimeline dates;dates.importedAt=1234;
        QVERIFY(db.updateBatch({{old,state}},{{old,history}},{{old,dates}}));QVERIFY(db.updateTagsBatch({{old,tags}}));QVERIFY(db.updateCurationBatch({{old,{5,"pick"}}}));QVERIFY(db.flush());
        QVERIFY(db.addVirtualCopy(copyKey(),old,"Retained",state,history,tags,{4,"reject"},dates));
        const auto legacy=databaseRows(folder);QVERIFY(!legacy.contains("original_identity"));
        QVERIFY(sqlExec(folder,"CREATE TRIGGER block_relink BEFORE UPDATE ON photos BEGIN SELECT RAISE(ABORT,'blocked first relink'); END"));
        QVERIFY(!db.relinkSource({old,candidate,identity,true}));QCOMPARE(databaseRows(folder),legacy);QVERIFY(db.flush());
        QVERIFY(sqlExec(folder,"DROP TRIGGER block_relink"));QVERIFY(db.recordOriginalIdentity(old,identity));
        for(const auto &table:QStringList{"photos","curation","catalog_tags","catalog_dates","virtual_sources","original_identity"}) {
            const auto action=table=="original_identity"?QString("INSERT"):QString("UPDATE");
            QVERIFY(sqlExec(folder,"CREATE TRIGGER block_relink BEFORE "+action+" ON "+table+" BEGIN SELECT RAISE(ABORT,'blocked relink'); END"));
            const auto before=databaseRows(folder);QVERIFY(!before.contains("error"));
            QVERIFY(!db.relinkSource({old,candidate,identity,false}));QVERIFY(db.lastError().contains("blocked relink"));
            QCOMPARE(databaseRows(folder),before);QVERIFY(db.flush());
            QVERIFY(sqlExec(folder,"DROP TRIGGER block_relink"));
        }
        QVERIFY(db.relinkSource({old,candidate,identity,false}));
        state.exposure=1;QVERIFY(db.updateAdjustment(candidate,state));QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> after;QVERIFY(db.readSnapshot(folder,&after));QCOMPARE(after.size(),2);QCOMPARE(after[0].adjustments.exposure,1.0);
    }

    void legacyReadOnlySnapshotRetainsMissingRecordsAndFirstIdentityWriteBacksUpWal() {
        QTemporaryDir dir;ProjectDatabase db;QVERIFY(db.create(dir.path(),"Legacy"));const auto folder=db.projectPath();
        AdjustmentState state;state.exposure=.25;EditHistory history;history.initialize(state);const auto key=copyKey();
        QVERIFY(db.updateAdjustment("missing.raw",state));QVERIFY(db.flush());QVERIFY(db.addVirtualCopy(key,"missing.raw","Missing copy",state,history,{},{}));
        const auto before=databaseRows(folder);QVERIFY(!before.contains("original_identity"));
        const auto backups=QDir(QDir(folder).filePath("backups")).entryList(QDir::Files);
        ProjectDatabase reader;QVERIFY(reader.create(dir.path(),"Unrelated"));const auto active=reader.projectPath();
        QVector<ProjectDatabase::SavedPhoto> photos;QVERIFY(reader.readSnapshot(folder,&photos));QCOMPARE(reader.projectPath(),active);QCOMPARE(photos.size(),2);
        QCOMPARE(photos[0].sourceKey,QString("missing.raw"));QCOMPARE(photos[1].sourceKey,QString("missing.raw"));QVERIFY(!photos[0].identity.isKnown());
        Identity unknown;QVERIFY(db.originalIdentity("missing.raw",&unknown));QVERIFY(!unknown.isKnown());
        QCOMPARE(databaseRows(folder),before);QCOMPARE(QDir(QDir(folder).filePath("backups")).entryList(QDir::Files),backups);
        const auto present=QDir(folder).filePath("missing.raw");QVERIFY(fileFixture(present,"baseline observed by caller"));
        state.exposure=.75;QVERIFY(db.updateAdjustment("missing.raw",state)); // still in committed WAL before migration
        QVERIFY(db.recordOriginalIdentity("missing.raw",identityOf(present)));
        const auto migrated=QDir(QDir(folder).filePath("backups")).entryList({"Project-before-original_identity-*.db"},QDir::Files);QCOMPARE(migrated.size(),1);
        const auto backupDir=dir.filePath("Backup.jlp");QVERIFY(QDir().mkpath(backupDir));
        QVERIFY(QFile::copy(QDir(folder).filePath("backups/"+migrated[0]),QDir(backupDir).filePath("Project.db")));
        QVector<ProjectDatabase::SavedPhoto> backedUp;QVERIFY(reader.readSnapshot(backupDir,&backedUp));QCOMPARE(backedUp.size(),2);QCOMPARE(backedUp[0].adjustments.exposure,.75);QVERIFY(!backedUp[0].identity.isKnown());
        // A schema-1 NULL sort key reads as the original key, without writes.
        QVERIFY(sqlExec(folder,"UPDATE original_identity SET sort_key=NULL"));QVERIFY(reader.readSnapshot(folder,&photos));QVERIFY(photos[0].identity.isKnown());
        const auto withIdentity=databaseRows(folder);QVERIFY(reader.readSnapshot(folder,&photos));QCOMPARE(databaseRows(folder),withIdentity);
    }
    void familyComparisonPreservesPersistedSpellingAndWindowsCaseAliases() {
        QTemporaryDir dir;ProjectDatabase db;QVERIFY(db.create(dir.path(),"CaseAliases"));const auto folder=db.projectPath();
        const auto old=QDir(folder).filePath("Original.RAW"),candidate=QDir(folder).filePath("Recovered.RAW");
#ifdef Q_OS_WIN
        QVERIFY(fileFixture(old,"case-preserving original"));const auto identity=identityOf(old);
        AdjustmentState state;EditHistory history;history.initialize(state);const auto one=copyKey(),two=copyKey();
        QVERIFY(db.updateAdjustment(old,state));QVERIFY(db.flush());
        QVERIFY(db.addVirtualCopy(one,"original.raw","Relative case",state,history,{},{}));
        QVERIFY(db.addVirtualCopy(two,old.toUpper(),"Absolute case",state,history,{},{}));
        QCOMPARE(ProjectDatabase::normalizedSourcePath(folder,old),ProjectDatabase::normalizedSourcePath(folder,"original.raw"));
        QVERIFY(db.recordOriginalIdentity(old.toLower(),identity));
        const auto occupied=QDir(folder).filePath("Occupied.RAW"),orphan=QDir(folder).filePath("Orphan.RAW");
        QVERIFY(db.updateAdjustment(occupied,{}));QVERIFY(db.updateTagsBatch({{orphan,{}}}));QVERIFY(db.flush());
        const auto before=databaseRows(folder);
        QVERIFY(!db.relinkSource({old.toUpper(),occupied.toUpper(),identity,false}));QCOMPARE(databaseRows(folder),before);
        QVERIFY(!db.relinkSource({old.toLower(),orphan.toLower(),identity,false}));QCOMPARE(databaseRows(folder),before);
        QVERIFY(QFile::rename(old,candidate));QVERIFY(db.relinkSource({old.toLower(),candidate,identity,false}));
        QVector<ProjectDatabase::SavedPhoto> photos;QVERIFY(db.readSnapshot(folder,&photos));int familyMembers=0;
        for(const auto &photo:photos)if(photo.identity==identity){++familyMembers;QCOMPARE(photo.path,candidate);QCOMPARE(photo.sourceKey,candidate);}
        QCOMPARE(familyMembers,3);
        QCOMPARE(sqlRow(folder,"SELECT source_key FROM original_identity").value(0).toString(),candidate);
        QVERIFY(!sqlRow(folder,"SELECT path FROM photos WHERE path=?",{candidate}).isEmpty());
        // Two original PKs with only case differences must be rejected as an
        // ambiguous family, rather than merged into one version silently.
        QVERIFY(db.updateAdjustment(candidate.toLower(),{}));QVERIFY(db.flush());const auto ambiguous=databaseRows(folder);
        QVERIFY(!db.relinkSource({candidate,QDir(folder).filePath("Another.RAW"),identity,false}));QCOMPARE(databaseRows(folder),ambiguous);
#else
        QCOMPARE(ProjectDatabase::normalizedSourcePath(folder,old),old);
        QVERIFY(ProjectDatabase::normalizedSourcePath(folder,old)!=ProjectDatabase::normalizedSourcePath(folder,old.toLower()));
        QVERIFY(db.updateAdjustment(old,{}));QVERIFY(db.flush());QVERIFY(fileFixture(old,"case-preserving original"));
        const auto identity=identityOf(old);QVERIFY(QFile::rename(old,candidate));QVERIFY(db.relinkSource({old,candidate,identity,true}));
        QVector<ProjectDatabase::SavedPhoto> photos;QVERIFY(db.readSnapshot(folder,&photos));QCOMPARE(photos[0].path,candidate);
#endif
    }
};
QTEST_GUILESS_MAIN(ProjectRelinkTests)
#include "ProjectRelinkTests.moc"
