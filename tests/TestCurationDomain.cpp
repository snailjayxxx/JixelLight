#include "core/project/ProjectDatabase.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <stdexcept>

namespace {
void require(bool ok, const QString &error) { if(!ok) throw std::runtime_error(error.toStdString()); }
QByteArray bytes(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly),file.errorString()); return file.readAll();
}
QMap<QString,QByteArray> inventory(const QString &folder) {
    QMap<QString,QByteArray> result;
    QDirIterator it(folder,QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(it.hasNext()) {
        const auto path=it.next();
        result.insert(QDir(folder).relativeFilePath(path),QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256));
    }
    return result;
}
QString catalog(const QString &base,const QString &rating="2",const QString &flag="'pick'",bool table=true,bool row=true) {
    const auto folder=base+"/Catalog-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".jlp";
    require(QDir().mkdir(folder),"Cannot create owned catalog");
    const auto name="curation-fixture-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(folder+"/Project.db");
        require(db.open(),db.lastError().text());
        QSqlQuery q(db);
        auto sql=[&](const QString &text){require(q.exec(text),q.lastError().text());};
        sql("CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT)");
        sql("INSERT INTO meta VALUES('project_name','Owned curation fixture')");
        sql("CREATE TABLE photos(path TEXT PRIMARY KEY,imported_at TEXT,adjustment_json TEXT NOT NULL)");
        sql("INSERT INTO photos VALUES('/owned/offline-original.raw','2026-11-08 00:00:00','{}')");
        if(table) {
            // Deliberately unconstrained legacy/corrupt storage, preserving
            // SQLite's actual storage classes rather than coercing fixtures.
            sql("CREATE TABLE curation(path TEXT PRIMARY KEY,rating,flag)");
            if(row)sql("INSERT INTO curation VALUES('/owned/offline-original.raw',"+rating+","+flag+")");
        }
        q.finish();db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return folder;
}
QJsonArray snapshot(const QVector<ProjectDatabase::SavedPhoto> &photos) {
    QJsonArray result;
    for(const auto &p:photos)result.append(QJsonObject{{"path",p.path},{"adjustments",p.adjustments.toJson()},
        {"history",p.history.toJson()},{"tags",p.tags.toJson()},{"timeline",p.timeline.toJson()},
        {"rating",p.rating},{"flag",p.flag},{"copy",p.copyKey},{"version",p.versionName},
        {"source",p.sourceKey},{"identity_size",QString::number(p.identity.sizeBytes)},
        {"identity_sha",QString::fromLatin1(p.identity.sha256)}});
    return result;
}
QVector<ProjectDatabase::SavedPhoto> sentinel() {
    ProjectDatabase::SavedPhoto p;p.path="/sentinel/unchanged";p.sourceKey=p.path;p.rating=5;p.flag="reject";
    p.versionName="Keep all staged destination fields";return {p};
}
}

class CurationDomainTests final: public QObject {
    Q_OBJECT
private slots:
    void malformedRatingRejectedBeforeAdoption() {
        QTemporaryDir owned;QVERIFY(owned.isValid());
        const auto path=catalog(owned.path(),"4294967298");const auto before=inventory(path);
        ProjectDatabase project;auto destination=sentinel();const auto preserved=snapshot(destination);
        QVERIFY2(!project.readSnapshot(path,&destination),"A stored 64-bit rating must not wrap to a valid 32-bit rating");
        QCOMPARE(snapshot(destination),preserved);QVERIFY(!project.isOpen());QVERIFY(!project.lastError().isEmpty());
        QCOMPARE(inventory(path),before);
    }
    void malformedCuration_data() {
        QTest::addColumn<QString>("rating");QTest::addColumn<QString>("flag");
        const QList<QStringList> rows{
            {"negative","-1","'pick'"},{"above-five","6","'pick'"},
            {"negative-wrap","-4294967298","'pick'"},{"int64-max","9223372036854775807","'pick'"},
            {"int64-min","-9223372036854775808","'pick'"},{"fraction","1.5","'pick'"},
            {"numeric-text","'2'","'pick'"},{"bad-text","'bad'","'pick'"},
            {"rating-blob","X'32'","'pick'"},{"rating-null","NULL","'pick'"},
            {"unknown-flag","2","'unknown'"},{"upper-flag","2","'PICK'"},
            {"integer-flag","2","1"},{"flag-null","2","NULL"},
            {"flag-blob","2","X'7069636b'"},{"empty-flag","2","''"}};
        for(const auto &row:rows)QTest::newRow(qPrintable(row[0]))<<row[1]<<row[2];
    }
    void malformedCuration() {
        QFETCH(QString,rating);QFETCH(QString,flag);QTemporaryDir owned;QVERIFY(owned.isValid());
        const auto path=catalog(owned.path(),rating,flag);const auto before=inventory(path);
        ProjectDatabase project;auto destination=sentinel();const auto preserved=snapshot(destination);
        QVERIFY(!project.readSnapshot(path,&destination));QCOMPARE(snapshot(destination),preserved);
        QVERIFY(!project.open(path,&destination));QCOMPARE(snapshot(destination),preserved);
        QVERIFY(!project.isOpen());QVERIFY(project.lastError().contains("rating/flag"));
        QCOMPARE(inventory(path),before);
    }
    void absentCurationPreservesLegacyDefaults() {
        QTemporaryDir owned;QVERIFY(owned.isValid());
        for(bool table:{false,true}) {
            const auto path=catalog(owned.path(),"2","'pick'",table,false);const auto before=inventory(path);
            ProjectDatabase project;QVector<ProjectDatabase::SavedPhoto> photos;
            QVERIFY2(project.readSnapshot(path,&photos),qPrintable(project.lastError()));
            QCOMPARE(photos.size(),1);QCOMPARE(photos[0].rating,0);QCOMPARE(photos[0].flag,QString("none"));
            QCOMPARE(inventory(path),before);
        }
    }
    void allEighteenValidCurationValuesRemainExact() {
        QTemporaryDir owned;QVERIFY(owned.isValid());int checked=0;
        for(int rating=0;rating<=5;++rating)for(const auto &flag:QStringList{"none","pick","reject"}) {
            const auto path=catalog(owned.path(),QString::number(rating),"'"+flag+"'");const auto before=inventory(path);
            ProjectDatabase project;QVector<ProjectDatabase::SavedPhoto> photos;
            QVERIFY2(project.readSnapshot(path,&photos),qPrintable(project.lastError()));
            QCOMPARE(photos.size(),1);QCOMPARE(photos[0].rating,rating);QCOMPARE(photos[0].flag,flag);
            QCOMPARE(inventory(path),before);++checked;
        }
        QCOMPARE(checked,18);
    }
    void failedReplacementKeepsCurrentWriterAndFullDestination() {
        QTemporaryDir owned;QVERIFY(owned.isValid());ProjectDatabase project;
        QVERIFY(project.create(owned.path(),"Current"));const auto current=project.projectPath();
        AdjustmentState first;QVERIFY(project.addOrUpdatePhoto("/owned/current.raw",first));QVERIFY(project.flush());
        const auto invalid=catalog(owned.path(),"2","'unknown'");const auto before=inventory(invalid);
        auto destination=sentinel();const auto preserved=snapshot(destination);
        QVERIFY(!project.open(invalid,&destination));QCOMPARE(snapshot(destination),preserved);
        QCOMPARE(project.projectPath(),current);QVERIFY(project.isOpen());QVERIFY(project.flush());
        QVERIFY(project.updateCurationBatch({{"/owned/current.raw",{4,"reject"}}}));QVERIFY(project.flush());
        QVector<ProjectDatabase::SavedPhoto> photos;QVERIFY(project.readSnapshot(current,&photos));
        QCOMPARE(photos.size(),1);QCOMPARE(photos[0].rating,4);QCOMPARE(photos[0].flag,QString("reject"));
        QCOMPARE(inventory(invalid),before);
    }
    void readonlyFileSnapshotKeepsPermissionsBytesAndWriter() {
        QTemporaryDir owned;QVERIFY(owned.isValid());ProjectDatabase project;
        QVERIFY(project.create(owned.path(),"Current"));const auto current=project.projectPath();
        const auto path=catalog(owned.path(),"5","'reject'");const auto dbPath=path+"/Project.db";
        const auto originalPermissions=QFile::permissions(dbPath);
        struct Restore {QString path;QFile::Permissions permissions;~Restore(){QFile::setPermissions(path,permissions);}}
            restore{dbPath,originalPermissions};
        QVERIFY(QFile::setPermissions(dbPath,QFile::ReadOwner|QFile::ReadUser|QFile::ReadGroup|QFile::ReadOther));
        const auto permissions=QFile::permissions(dbPath);const auto before=inventory(path);
        QVector<ProjectDatabase::SavedPhoto> photos;
        QVERIFY2(project.readSnapshot(path,&photos),qPrintable(project.lastError()));
        QCOMPARE(photos.size(),1);QCOMPARE(photos[0].rating,5);QCOMPARE(photos[0].flag,QString("reject"));
        QCOMPARE(QFile::permissions(dbPath),permissions);QCOMPARE(inventory(path),before);
        QCOMPARE(project.projectPath(),current);QVERIFY(project.isOpen());QVERIFY(project.flush());
    }
};
QTEST_GUILESS_MAIN(CurationDomainTests)
#include "TestCurationDomain.moc"
