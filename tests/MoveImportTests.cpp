#include <QtTest>
#include <QApplication>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSettings>
#include <QSignalSpy>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <thread>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/import/MoveImport.h"
#include "core/import/MoveImportQueue.h"
#include "core/project/ProjectDatabase.h"
#include "diagnostics/ActionTrace.h"

namespace {
QByteArray bytes(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
bool image(const QString &path,int shift=0){QImage image(37,29,QImage::Format_RGBA64);image.setColorSpace(QColorSpace(QColorSpace::SRgb));for(int y=0;y<29;++y)for(int x=0;x<37;++x)reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64((x*503+shift)%65536,(y*719+shift)%65536,15000,(x+y)%5?47000:0);return image.save(path);}
struct Fixture {
    QTemporaryDir root;ProjectDatabase catalog;QString destination,trash;QStringList sources;bool valid=false;int calls=0;
    Fixture(){if(!root.isValid())return;destination=root.filePath("destination");trash=root.filePath("owned-trash");QDir().mkpath(destination);QDir().mkpath(trash);
        if(!catalog.create(root.path(),"Move"))return;for(int i=0;i<2;++i){const auto source=root.filePath(QString("source-%1.png").arg(i));if(!image(source,i*300))return;sources.push_back(source);}valid=true;}
    MoveImportBatch prepare(const CancelToken &cancel={},const CopyImportProgress &progress={}){return prepareMoveImport(sources,destination,catalog.projectPath(),cancel,progress);}
    bool save(const MoveImportBatch &batch){for(const auto &file:batch.files){if(!catalog.addOrUpdatePhoto(file.copy.destination,{})||!catalog.flush()
        ||!catalog.recordOriginalIdentity(file.copy.destination,{file.bytes,file.copy.sha256.toLatin1()}))return false;}return catalog.flush();}
    MoveImportTrash retirement(){return [this](const QString &source,QString *target){++calls;*target=QDir(trash).filePath(QString::number(calls)+"-"+QFileInfo(source).fileName());return QFile::rename(source,*target);};}
};
bool rewrite(const QString &path,const std::function<void(QJsonObject &)> &change){auto data=QJsonDocument::fromJson(bytes(path)).object();change(data);QFile file(path);return file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(QJsonDocument(data).toJson())>0;}
}
class MoveImportTests:public QObject {
    Q_OBJECT
private slots:
    void verifiedCopyNeverRetiresBeforeSavedCatalogIdentity(){
        Fixture f;QVERIFY(f.valid);const auto originals=QList<QByteArray>{bytes(f.sources[0]),bytes(f.sources[1])};const auto batch=f.prepare();QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));QCOMPARE(batch.files.size(),2);QCOMPARE(batch.phase,QString("copied"));
        for(int i=0;i<2;++i){QCOMPARE(bytes(f.sources[i]),originals[i]);QCOMPARE(bytes(batch.files[i].copy.destination),originals[i]);}
        const auto blocked=retireMoveImport(batch.journal,{}, {},f.retirement(),batch.recordDigest);QVERIFY(!blocked.error.isEmpty());QCOMPARE(f.calls,0);for(int i=0;i<2;++i)QCOMPARE(bytes(f.sources[i]),originals[i]);
    }
    void savedCopiesRetireOnceAndRepeatedResumePreservesNewSourceAtOldPath(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();QVERIFY(f.save(batch));const auto done=retireMoveImport(batch.journal,{}, {},f.retirement(),batch.recordDigest);
        QVERIFY2(done.error.isEmpty(),qPrintable(done.error));QCOMPARE(done.phase,QString("done"));QCOMPARE(done.retired(),2);QCOMPARE(f.calls,2);
        for(const auto &file:done.files){QVERIFY(!QFileInfo::exists(file.copy.source));QCOMPARE(bytes(file.trashPath),bytes(file.copy.destination));}
        QVERIFY(image(f.sources[0],9876));const auto replacement=bytes(f.sources[0]);QString error;const auto loaded=readMoveImport(batch.journal,&error);QVERIFY(error.isEmpty());
        const auto again=retireMoveImport(batch.journal,{}, {},f.retirement(),loaded.recordDigest);QVERIFY(again.error.isEmpty());QCOMPARE(f.calls,2);QCOMPARE(bytes(f.sources[0]),replacement);
    }
    void changedSourceOrDestinationPreventsWholeSelectedSetRetirement(){
        for(bool source:{false,true}){Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();QVERIFY(f.save(batch));const auto path=source?f.sources[1]:batch.files[1].copy.destination;
            auto data=bytes(path);data[data.size()-1]=char(data.back()^1);QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(data),qint64(data.size()));file.close();
            const auto result=retireMoveImport(batch.journal,{}, {},f.retirement(),batch.recordDigest);QVERIFY(!result.error.isEmpty());QCOMPARE(f.calls,0);for(const auto &original:f.sources)QVERIFY(QFileInfo::exists(original));}
    }
    void existingCatalogSourcesAndOccupiedNamesRejectBeforeCopy(){
        Fixture f;QVERIFY(f.valid);QVERIFY(f.catalog.addOrUpdatePhoto(f.sources[0],{}));QVERIFY(f.catalog.flush());const auto rejected=f.prepare();QVERIFY(!rejected.error.isEmpty());QVERIFY(rejected.files.isEmpty());QVERIFY(rejected.journal.isEmpty());QCOMPARE(f.calls,0);
        Fixture occupied;QVERIFY(occupied.valid);const auto path=QDir(occupied.destination).filePath(QFileInfo(occupied.sources[1]).fileName());QVERIFY(image(path,987));const auto prior=bytes(path);const auto collision=occupied.prepare();QVERIFY(!collision.error.isEmpty());QVERIFY(collision.files.isEmpty());QCOMPARE(bytes(path),prior);for(const auto &source:occupied.sources)QVERIFY(QFileInfo::exists(source));
    }
    void absentOrWrongSavedIdentityNeverAdmitsRetirement(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();for(const auto &file:batch.files)QVERIFY(f.catalog.addOrUpdatePhoto(file.copy.destination,{}));QVERIFY(f.catalog.flush());
        auto result=retireMoveImport(batch.journal,{}, {},f.retirement(),batch.recordDigest);QVERIFY(!result.error.isEmpty());QCOMPARE(f.calls,0);
        for(const auto &file:batch.files)QVERIFY(f.catalog.recordOriginalIdentity(file.copy.destination,{file.bytes,QByteArray(64,'a')}));
        const auto fresh=readMoveImport(batch.journal);result=retireMoveImport(batch.journal,{}, {},f.retirement(),fresh.recordDigest);QVERIFY(!result.error.isEmpty());QCOMPARE(f.calls,0);
    }
    void cancelAfterFirstRetirementKeepsCopiesAndResumesRemainingOnly(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();QVERIFY(f.save(batch));auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto partial=retireMoveImport(batch.journal,cancel,[&](qint64,qint64,int completed,int,const QString &){if(completed==1)cancel->store(true);},f.retirement(),batch.recordDigest);
        QVERIFY(partial.wasCancelled);QCOMPARE(partial.retired(),1);QCOMPARE(f.calls,1);QVERIFY(QFileInfo::exists(f.sources[1]));QVERIFY(image(f.sources[0],7878));const auto replacement=bytes(f.sources[0]);
        const auto fresh=readMoveImport(batch.journal);const auto done=retireMoveImport(batch.journal,{}, {},f.retirement(),fresh.recordDigest);QVERIFY2(done.error.isEmpty(),qPrintable(done.error));QCOMPARE(done.retired(),2);QCOMPARE(f.calls,2);QCOMPARE(bytes(f.sources[0]),replacement);
    }
    void cancelledCopyLeavesOriginalsAndCompletedCopiesRecoverable(){
        Fixture f;QVERIFY(f.valid);auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto partial=f.prepare(cancel,[&](qint64,qint64,int done,int,const QString &stage){if(stage=="published"&&done==1)cancel->store(true);});
        QVERIFY(partial.wasCancelled);QCOMPARE(partial.files.size(),1);QCOMPARE(partial.phase,QString("copy-incomplete"));for(const auto &source:f.sources)QVERIFY(QFileInfo::exists(source));QCOMPARE(bytes(partial.files[0].copy.destination),bytes(f.sources[0]));
        QString error;const auto recovery=readMoveImport(partial.journal,&error);QVERIFY(error.isEmpty());QCOMPARE(recovery.files.size(),1);QVERIFY(recovery.wasCancelled);QCOMPARE(f.calls,0);
    }
    void failedTrashRetainsSourceAndExplicitRecoveryCanRetry(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();QVERIFY(f.save(batch));int attempted=0;
        const auto failed=retireMoveImport(batch.journal,{}, {},[&](const QString &,QString *){++attempted;return false;},batch.recordDigest);QVERIFY(!failed.error.isEmpty());QCOMPARE(attempted,1);QCOMPARE(failed.retired(),0);for(const auto &source:f.sources)QVERIFY(QFileInfo::exists(source));
        const auto fresh=readMoveImport(batch.journal);const auto done=retireMoveImport(batch.journal,{}, {},f.retirement(),fresh.recordDigest);QVERIFY(done.error.isEmpty());QCOMPARE(done.retired(),2);
    }
    void changedRecoveryRecordIsRejectedWithoutReplacingOriginalEvidence(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();QVERIFY(f.save(batch));QVERIFY(rewrite(batch.journal,[](QJsonObject &j){j["error"]="owned intervening recovery";}));const auto original=bytes(batch.journal);
        const auto failed=retireMoveImport(batch.journal,{}, {},f.retirement(),batch.recordDigest);QVERIFY(!failed.error.isEmpty());QCOMPARE(bytes(batch.journal),original);QCOMPARE(f.calls,0);
    }
    void malformedOrDuplicatedJournalAndSymlinkSourcesAreRefused(){
        for(int variant=0;variant<3;++variant){Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();QVERIFY(rewrite(batch.journal,[=](QJsonObject &j){if(variant==0)j["schema"]=2;else if(variant==1){auto rows=j["files"].toArray();rows[1]=rows[0];j["files"]=rows;}else j["directory"]="relative";}));
            QString error;readMoveImport(batch.journal,&error);QVERIFY(!error.isEmpty());const auto result=retireMoveImport(batch.journal,{}, {},f.retirement());QVERIFY(!result.error.isEmpty());QCOMPARE(f.calls,0);}
#ifndef Q_OS_WIN
        Fixture f;QVERIFY(f.valid);const auto link=f.root.filePath("alias.png");QVERIFY(QFile::link(f.sources[0],link));const auto result=prepareMoveImport({link},f.destination,f.catalog.projectPath());QVERIFY(!result.error.isEmpty());QVERIFY(result.files.isEmpty());QVERIFY(QFileInfo::exists(f.sources[0]));
#endif
    }
    void journalLocksSerializeRetirementAndInvalidFilesAreRejectedBeforeLocking(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();QVERIFY(f.save(batch));const auto before=bytes(batch.journal);QLockFile lock(batch.journal+".lock");QVERIFY(lock.tryLock(0));
        const auto blocked=retireMoveImport(batch.journal,{}, {},f.retirement(),batch.recordDigest);QVERIFY(blocked.error.contains("busy"));QCOMPARE(f.calls,0);QCOMPARE(bytes(batch.journal),before);
        const auto unrelated=f.root.filePath("unrelated.json");QFile file(unrelated);QVERIFY(file.open(QIODevice::WriteOnly));file.write("owned unrelated original");file.close();QLockFile other(unrelated+".lock");QVERIFY(other.tryLock(0));
        const auto invalid=retireMoveImport(unrelated,{}, {},f.retirement());QVERIFY(invalid.error.contains("Invalid move journal"));QVERIFY(other.isLocked());QCOMPARE(bytes(unrelated),QByteArray("owned unrelated original"));QCOMPARE(f.calls,0);
    }
    void queueRequiresGuiCatalogHandoffAndCloseKeepsSources(){
        Fixture f;QVERIFY(f.valid);MoveImportQueue queue(nullptr,f.retirement());QSignalSpy ready(&queue,&MoveImportQueue::copiesReady),finished(&queue,&MoveImportQueue::finished);
        QVERIFY(queue.start(f.sources,f.destination,f.catalog.projectPath()));QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);QVERIFY(queue.busy());QCOMPARE(finished.size(),0);QCOMPARE(f.calls,0);queue.stopAndCollect();QCOMPARE(finished.size(),1);QVERIFY(!queue.busy());QCOMPARE(f.calls,0);for(const auto &source:f.sources)QVERIFY(QFileInfo::exists(source));
    }
    void queueCommitsBeforeBackgroundRetirement(){
        Fixture f;QVERIFY(f.valid);bool worker=false,handoff=false;auto retire=f.retirement();MoveImportQueue queue(nullptr,[&](const QString &source,QString *target){worker=QThread::currentThread()!=qApp->thread();return retire(source,target);});QSignalSpy finished(&queue,&MoveImportQueue::finished);
        connect(&queue,&MoveImportQueue::copiesReady,&queue,[&](const MoveImportBatch &batch){handoff=QThread::currentThread()==qApp->thread()&&f.save(batch);queue.catalogReady(handoff);});
        QVERIFY(queue.start(f.sources,f.destination,f.catalog.projectPath()));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);QVERIFY(handoff);QVERIFY(worker);QVERIFY(!queue.busy());QCOMPARE(f.calls,2);
    }
    void controllerMovesOnlyAfterDurableCatalogAndRecoveryReview(){
        Fixture f;QVERIFY(f.valid);ProcessedImageProvider provider;PhotoController c(&provider,nullptr,{},f.retirement());c.setGpuEnabled(false);QVariantList urls;for(const auto &source:f.sources)urls.push_back(QUrl::fromLocalFile(source));
        const QJsonObject command{{"command","photo.move"},{"sources",QJsonArray::fromStringList(f.sources)},{"folder",f.destination},{"pattern",""},{"sequenceStart",1}};
        QVERIFY(!c.moveImport(urls,QUrl::fromLocalFile(f.destination)));QVERIFY(!c.executeCommand(command.toVariantMap()));
        QVERIFY(c.openProject(QUrl::fromLocalFile(f.catalog.projectPath())));QVERIFY(c.executeCommand(command.toVariantMap()));const auto first=c.copyImportOperationId();QVERIFY(!first.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!c.copyImportBusy(),10000);QCOMPARE(f.calls,2);QCOMPARE(c.library().size(),2);QVERIFY(!c.moveImportJournal().isEmpty());
        ProjectDatabase read;QVector<ProjectDatabase::SavedPhoto> photos;QVERIFY(read.readSnapshot(c.projectPath(),&photos));QCOMPARE(photos.size(),2);for(const auto &photo:photos)QVERIFY(photo.identity.isKnown());
        QVERIFY(!c.resumeMoveImport(c.moveImportJournal()));c.inspectMoveImportRecovery(c.moveImportJournal());QTRY_VERIFY_WITH_TIMEOUT(c.moveImportRecoveryPreview()["valid"].toBool(),10000);
        QVERIFY(c.resumeMoveImport(c.moveImportJournal()));const auto recovery=c.copyImportOperationId();QVERIFY(!recovery.isEmpty());QVERIFY(recovery!=first);
        QVERIFY(!c.executeCommand({{"command","photo.transfer.cancel"},{"operation_id",first}}));QTRY_VERIFY_WITH_TIMEOUT(!c.copyImportBusy(),10000);QCOMPARE(f.calls,2);QVERIFY(c.copyImportOperationId().isEmpty());QCOMPARE(c.moveImportDiagnostics()["phase"].toString(),QString("done"));
        bool canonical=false;for(const auto &entry:ActionTrace::instance().snapshot()) {
            const auto row=entry.toObject();if(row["action"].toString()=="move_import_started"&&row["command"].toObject()==command)canonical=true;
        }
        QVERIFY(canonical);
    }
    void cancellingThenClosingControllerNeverRetiresAnOriginal(){
        Fixture f;QVERIFY(f.valid);ProcessedImageProvider provider;PhotoController c(&provider,nullptr,{},f.retirement());c.setGpuEnabled(false);QVERIFY(c.openProject(QUrl::fromLocalFile(f.catalog.projectPath())));
        QVariantList urls;for(const auto &source:f.sources)urls.push_back(QUrl::fromLocalFile(source));QVERIFY(c.moveImport(urls,QUrl::fromLocalFile(f.destination)));c.cancelCopyImport();QVERIFY(c.prepareToClose());QVERIFY(!c.copyImportBusy());QCOMPARE(f.calls,0);for(const auto &source:f.sources)QVERIFY(QFileInfo::exists(source));
    }
    void recoveryCancellationRequiresItsOwnIdAndCannotRetireSources(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();QVERIFY(batch.error.isEmpty());QVERIFY(f.save(batch));
        ProcessedImageProvider provider;PhotoController c(&provider,nullptr,{},f.retirement());c.setGpuEnabled(false);QVERIFY(c.openProject(QUrl::fromLocalFile(f.catalog.projectPath())));
        c.inspectMoveImportRecovery(batch.journal);QTRY_VERIFY_WITH_TIMEOUT(c.moveImportRecoveryPreview()["valid"].toBool(),10000);QVERIFY(c.resumeMoveImport(batch.journal));const auto first=c.copyImportOperationId();QVERIFY(!first.isEmpty());
        bool foreign=false,accepted=true;std::thread worker([&]{foreign=QThread::currentThread()!=c.thread();accepted=c.executeCommand({{"command","photo.transfer.cancel"},{"operation_id",first}});c.cancelCopyImport();});worker.join();QVERIFY(foreign);QVERIFY(!accepted);
        const QVariantMap cancel{{"command","photo.transfer.cancel"},{"operation_id",first}};QVERIFY(c.executeCommand(cancel));QVERIFY(c.prepareToClose());QCOMPARE(f.calls,0);QVERIFY(!c.copyImportBusy());QVERIFY(c.copyImportOperationId().isEmpty());
        QVERIFY(!c.executeCommand(cancel));c.inspectMoveImportRecovery(batch.journal);QTRY_VERIFY_WITH_TIMEOUT(c.moveImportRecoveryPreview()["valid"].toBool(),10000);QVERIFY(c.resumeMoveImport(batch.journal));const auto second=c.copyImportOperationId();QVERIFY(!second.isEmpty());QVERIFY(second!=first);QVERIFY(!c.executeCommand(cancel));
        c.cancelCopyImport();QVERIFY(c.prepareToClose());QCOMPARE(f.calls,0);QVERIFY(c.copyImportOperationId().isEmpty());
        bool traced=false;for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject();if(row["action"]=="operation_cancel_requested"&&row["command"].toObject()==QJsonObject::fromVariantMap(cancel)){QCOMPARE(row["kind"].toString(),QString("move"));traced=true;}}
        QVERIFY(traced);for(const auto &file:batch.files){QVERIFY(QFileInfo::exists(file.copy.source));QCOMPARE(bytes(file.copy.source),bytes(file.copy.destination));}
    }
    void recoveryReviewIsRetiredWhenTheCurrentProjectChanges(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();ProcessedImageProvider provider;PhotoController c(&provider,nullptr,{},f.retirement());c.setGpuEnabled(false);QVERIFY(c.openProject(QUrl::fromLocalFile(f.catalog.projectPath())));
        c.inspectMoveImportRecovery(batch.journal);QTRY_VERIFY_WITH_TIMEOUT(c.moveImportRecoveryPreview()["valid"].toBool(),10000);QVERIFY(c.createProject(QUrl::fromLocalFile(f.root.path()),"Other"));QVERIFY(!c.moveImportRecoveryPreview()["valid"].toBool());QVERIFY(!c.resumeMoveImport(batch.journal));QCOMPARE(f.calls,0);for(const auto &source:f.sources)QVERIFY(QFileInfo::exists(source));
    }
    void copyMoveAndRecoveryDialogsStayBilingualAndWithinCompactWindows(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();ProcessedImageProvider provider;PhotoController c(&provider,nullptr,{},f.retirement());c.setGpuEnabled(false);QVERIFY(c.openProject(QUrl::fromLocalFile(f.catalog.projectPath())));
        for(const auto &language:{QString("en_US"),QString("zh_CN")})for(int width:{580,1180})for(bool recovery:{false,true}){
            c.setLanguage(language);QQmlEngine engine;const auto path=recovery?QFINDTESTDATA("../ui/MoveImportRecoveryDialog.qml"):QFINDTESTDATA("../ui/CopyImportDialog.qml");QQmlComponent component(&engine,QUrl::fromLocalFile(path));QVERIFY2(component.isReady(),qPrintable(component.errorString()));
            QQuickWindow window;window.resize(width,780);window.show();const QVariantMap properties{{"controller",QVariant::fromValue<QObject *>(&c)},{"hostWidth",width},{"hostHeight",780}};
            std::unique_ptr<QObject> dialog(component.createWithInitialProperties(properties));QVERIFY2(dialog,qPrintable(component.errorString()));dialog->setProperty("parent",QVariant::fromValue(window.contentItem()));
            if(recovery)dialog->setProperty("journal",batch.journal);else dialog->setProperty("moveMode",true);
            QVERIFY(QMetaObject::invokeMethod(dialog.get(),"open"));QTest::qWait(80);QVERIFY(dialog->property("visible").toBool());QVERIFY(dialog->property("width").toDouble()<=width-31);
            const auto title=dialog->property("title").toString();QVERIFY(title.contains(language=="en_US"?"Move":"移动",Qt::CaseInsensitive));
            if(recovery){QTRY_VERIFY_WITH_TIMEOUT(c.moveImportRecoveryPreview()["valid"].toBool(),10000);auto *field=dialog->findChild<QQuickItem *>("moveImportJournalPath");QVERIFY(field);QVERIFY(field->width()>0);QVERIFY(field->width()<width);}
            else{auto *explanation=dialog->findChild<QQuickItem *>("moveImportExplanation");QVERIFY(explanation);QVERIFY(explanation->isVisible());QVERIFY(explanation->width()<width);QVERIFY(explanation->property("text").toString().contains(language=="en_US"?"trash":"废纸篓"));}
            QVERIFY(QMetaObject::invokeMethod(dialog.get(),"close"));QTest::qWait(10);QVERIFY(!dialog->property("visible").toBool());
        }
        QCOMPARE(f.calls,0);for(const auto &source:f.sources)QVERIFY(QFileInfo::exists(source));
    }
    void reviewedJournalChangesCannotStartControllerImportOrTrash(){
        Fixture f;QVERIFY(f.valid);const auto batch=f.prepare();ProcessedImageProvider provider;PhotoController c(&provider,nullptr,{},f.retirement());c.setGpuEnabled(false);QVERIFY(c.openProject(QUrl::fromLocalFile(f.catalog.projectPath())));
        c.inspectMoveImportRecovery(batch.journal);QTRY_VERIFY_WITH_TIMEOUT(c.moveImportRecoveryPreview()["valid"].toBool(),10000);QVERIFY(rewrite(batch.journal,[](QJsonObject &j){j["error"]="changed after visible review";}));
        QVERIFY(c.resumeMoveImport(batch.journal));QTRY_VERIFY_WITH_TIMEOUT(!c.copyImportBusy(),10000);QCOMPARE(c.library().size(),0);QCOMPARE(f.calls,0);for(const auto &source:f.sources)QVERIFY(QFileInfo::exists(source));
    }
};
int main(int argc,char **argv){QQuickStyle::setStyle("Fusion");QApplication app(argc,argv);QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());QCoreApplication::setOrganizationName("JixelLightOwnedTests");QCoreApplication::setApplicationName("MoveImport");QStandardPaths::setTestModeEnabled(true);MoveImportTests tests;return QTest::qExec(&tests,argc,argv);}
#include "MoveImportTests.moc"
