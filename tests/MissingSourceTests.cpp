#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QImageReader>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include "app/PhotoController.h"
#include "core/cache/SourceCache.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/look/LookLut.h"

namespace {
QByteArray bytes(const QString &path) {QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
ProjectDatabase::OriginalIdentity identity(const QString &path) {
    const auto data=bytes(path);return {data.size(),QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()};
}
bool fixture(const QString &path,int variant=0) {
    QImage image(384,256,QImage::Format_RGB32);image.setColorSpace(QColorSpace::SRgb);image.fill(QColor(83,124,169));
    for(int y=95;y<145;++y)for(int x=110;x<250;++x)image.setPixelColor(x,y,variant?QColor(179,31,62):QColor(17,201,92));
    return image.save(path,"BMP");
}
QImage reference(const QString &path,const AdjustmentState &state) {
    const auto source=loadSourceFresh(path,{});
    return ImagePipeline::processWithPlan(state.geometry.apply(source.image),ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::DisplayP3,false,0),{},false);
}
QString key() {return "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);}
}
class MissingSourceTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);QCoreApplication::setOrganizationName("JixelLightMissingSourceTests");
        QCoreApplication::setApplicationName("MissingSources");QSettings().clear();
    }
    void missingVersionsKeepHistoryCurationAndCopiesAcrossSaveAndReopen() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto original=dir.filePath("original.bmp");QVERIFY(fixture(original));
        QString project,copyKey=key();AdjustmentState state;state.exposure=.35;
        EditHistory history;history.initialize({});history.record(state,"exposure");auto future=state;future.exposure=.9;history.record(future,"exposure");history.undo();
        CatalogTags tags;tags.keywords={"retained"};tags.albums={"Offline"};tags.label="green";
        {
            ProjectDatabase db;QVERIFY(db.create(dir.path(),"Offline"));project=db.projectPath();
            QVERIFY(db.updateBatch({{original,state}},{{original,history}}));QVERIFY(db.flush());
            QVERIFY(db.updateTagsBatch({{original,tags}}));QVERIFY(db.updateCurationBatch({{original,{4,"pick"}}}));
            QVERIFY(db.addVirtualCopy(copyKey,original,"Alternative",state,history,tags,{3,"reject"}));QVERIFY(db.flush());
        }
        QVERIFY(QFile::remove(original));ProcessedImageProvider provider;PhotoController controller(&provider);controller.setGpuEnabled(false);
        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));QCOMPARE(controller.library().size(),2);QVERIFY(controller.hasImage());
        QVERIFY(!controller.currentSourceAvailable());QVERIFY(!controller.loading());QVERIFY(!controller.rendering());QVERIFY(controller.gpuSource().isNull());
        QVERIFY(controller.previewUrl().isEmpty());QCOMPARE(controller.scopesPixelCount(),qulonglong(0));QVERIFY(!controller.editHistory().isEmpty());
        QCOMPARE(controller.currentRating(),4);QCOMPARE(controller.currentKeywords(),QStringList{"retained"});
        const auto exposure=controller.exposure();controller.setExposure(2);QCOMPARE(controller.exposure(),exposure);QVERIFY(!controller.canUndo());
        QVERIFY(!controller.executeEditCommand({{"command","develop.set"},{"parameter","exposure"},{"value",2}}));
        const QByteArray cube="LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
        QString error;QVERIFY(LookLut::fromCube(cube,&error));const auto profile=dir.filePath("valid.cube");
        {QFile file(profile);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(cube),cube.size());}
        QVERIFY(!controller.loadLookProfile(QUrl::fromLocalFile(profile)));QCOMPARE(controller.exposure(),exposure);
        controller.setRating(5);QVERIFY(controller.setSelectionKeywords("offline, retained"));QVERIFY(controller.flushEdits());
        QVERIFY(controller.createVirtualCopy("Offline copy"));QVERIFY(!controller.currentSourceAvailable());
        QVERIFY(controller.renameCurrentVirtualCopy("Renamed offline"));QVERIFY(controller.removeCurrentVirtualCopy());
        QVERIFY(controller.flushEdits());QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QCOMPARE(controller.library().size(),2);QCOMPARE(controller.currentRating(),5);QVERIFY(controller.currentKeywords().contains("offline"));
        ProjectDatabase db;QVector<ProjectDatabase::SavedPhoto> saved;QVERIFY(db.readSnapshot(project,&saved));QCOMPARE(saved.size(),2);
        QVERIFY(saved[0].history.canRedo());QCOMPARE(saved[0].history.redo().exposure,.9);QCOMPARE(saved[1].copyKey,copyKey);
    }
    void selectedMissingVersionRefusesWholePlanWhileAvailableSelectionExports() {
        QTemporaryDir dir;const auto missing=dir.filePath("missing.bmp"),available=dir.filePath("available.bmp"),out=dir.filePath("out");
        QVERIFY(fixture(missing));QVERIFY(fixture(available,1));QVERIFY(QDir().mkpath(out));QString project;
        {ProjectDatabase db;QVERIFY(db.create(dir.path(),"Mixed"));project=db.projectPath();QVERIFY(db.updateBatch({{missing,{}},{available,{}}}));QVERIFY(db.flush());}
        QVERIFY(QFile::remove(missing));PhotoController controller(nullptr);controller.setGpuEnabled(false);QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QSignalSpy finished(&controller,&PhotoController::exportFinished);
        QVERIFY(!controller.exportVersions({missing,available},QUrl::fromLocalFile(out),"display-p3",92,"png","{name}"));
        QVERIFY(!controller.exportAll(QUrl::fromLocalFile(out),"srgb",92,"png"));QVERIFY(!controller.exportBusy());QVERIFY(QDir(out).isEmpty());QCOMPARE(finished.size(),0);
        QVERIFY(controller.exportVersions({available},QUrl::fromLocalFile(out),"display-p3",92,"png","{name}"));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);
        QCOMPARE(finished[0][0].toInt(),1);QCOMPARE(finished[0][1].toInt(),0);QVERIFY(!finished[0][2].toBool());
        QCOMPARE(QDir(out).entryList(QDir::Files),QStringList{"available.png"});
    }
    void knownIdentityRelinksWholeFamilyDespiteChangedSelectionAndPreservesExactPixels() {
        QTemporaryDir dir;const auto original=dir.filePath("original.bmp"),moved=dir.filePath("moved.bmp"),other=dir.filePath("other.bmp"),out=dir.filePath("out");
        QVERIFY(fixture(original));QVERIFY(fixture(other,1));QVERIFY(QDir().mkpath(out));QString project,copyKey=key();
        AdjustmentState state;state.exposure=.4;state.geometry.quarterTurns=1;state.geometry.crop={.05,.1,.9,.8};
        EditHistory history;history.initialize(state);CatalogTags tags;tags.keywords={"family"};
        {ProjectDatabase db;QVERIFY(db.create(dir.path(),"Known"));project=db.projectPath();QVERIFY(db.updateBatch({{original,state},{other,{}}}));QVERIFY(db.flush());
         QVERIFY(db.addVirtualCopy(copyKey,original,"Copy",state,history,tags,{4,"pick"}));QVERIFY(db.recordOriginalIdentity(original,identity(original)));}
        QVERIFY(QFile::rename(original,moved));PhotoController controller(nullptr);controller.setGpuEnabled(false);QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QVERIFY(controller.beginRelink(copyKey,QUrl::fromLocalFile(moved)));QTRY_VERIFY_WITH_TIMEOUT(!controller.relinkBusy(),10000);
        QCOMPARE(controller.relinkPreview().value("status").toString(),QString("ready"));QVERIFY(controller.relinkPreview().value("identityKnown").toBool());
        const auto requestId=controller.relinkPreview().value("requestId").toString();
        const auto rows=controller.library();int otherIndex=-1;for(const auto &row:rows)if(row.toMap().value("id")==other)otherIndex=row.toMap().value("index").toInt();QVERIFY(otherIndex>=0);
        controller.selectPhoto(otherIndex);QVERIFY(controller.confirmRelink(requestId));QTRY_VERIFY_WITH_TIMEOUT(!controller.relinkBusy(),10000);
        QCOMPARE(controller.relinkPreview().value("status").toString(),QString("complete"));QCOMPARE(controller.currentFile(),other);
        const auto after=controller.library();int members=0;for(const auto &row:after)if(row.toMap().value("path")==moved){++members;QVERIFY(row.toMap().value("available").toBool());}QCOMPARE(members,2);
        QSignalSpy finished(&controller,&PhotoController::exportFinished);QVERIFY(controller.exportVersions({copyKey},QUrl::fromLocalFile(out),"display-p3",92,"png","{version}"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);QCOMPARE(finished[0][1].toInt(),0);
        QImage actual(out+"/Copy.png");QVERIFY(!actual.isNull());const auto expected=reference(moved,state);
        QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);QCOMPARE(actual.colorSpace(),ColorManagement::colorSpace(ColorManagement::OutputSpace::DisplayP3));
        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));QCOMPARE(controller.library().size(),3);
        ProjectDatabase db;QVector<ProjectDatabase::SavedPhoto> saved;QVERIFY(db.readSnapshot(project,&saved));
        for(const auto &photo:saved)if(photo.copyKey==copyKey){QCOMPARE(photo.tags.keywords,QStringList{"family"});QCOMPARE(photo.rating,4);QCOMPARE(photo.adjustments.toJson(),state.toJson());}
    }
    void fullHashRejectsChangedMiddleBytesAtEqualSizeAndMtime() {
        QTemporaryDir dir;const auto original=dir.filePath("original.bmp"),candidate=dir.filePath("candidate.bmp");QVERIFY(fixture(original));
        const auto stamp=QFileInfo(original).lastModified();const auto known=identity(original);QString project;
        {ProjectDatabase db;QVERIFY(db.create(dir.path(),"Hash"));project=db.projectPath();QVERIFY(db.updateAdjustment(original,{}));QVERIFY(db.flush());QVERIFY(db.recordOriginalIdentity(original,known));}
        QVERIFY(QFile::rename(original,candidate));QVERIFY(fixture(candidate,1));{QFile f(candidate);QVERIFY(f.open(QIODevice::ReadWrite));QVERIFY(f.setFileTime(stamp,QFileDevice::FileModificationTime));}
        QVERIFY(identity(candidate).sha256!=known.sha256);QCOMPARE(identity(candidate).sizeBytes,known.sizeBytes);
        PhotoController controller(nullptr);QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));QVERIFY(controller.beginRelink(original,QUrl::fromLocalFile(candidate)));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.relinkBusy(),10000);QCOMPARE(controller.relinkPreview().value("status").toString(),QString("error"));
        QVERIFY(!controller.confirmRelink(controller.relinkPreview().value("requestId").toString(),true));QCOMPARE(controller.currentFile(),original);QVERIFY(!controller.currentSourceAvailable());
        ProjectDatabase db;QVector<ProjectDatabase::SavedPhoto> saved;QVERIFY(db.readSnapshot(project,&saved));QCOMPARE(saved[0].path,original);QCOMPARE(saved[0].identity.sha256,known.sha256);
    }
    void unknownIdentityRequiresAcknowledgementAndFreshDecodeForSamePathSizeAndMtime() {
        QTemporaryDir dir;const auto original=dir.filePath("same.bmp"),out=dir.filePath("out");QVERIFY(fixture(original));QVERIFY(QDir().mkpath(out));
        const auto stamp=QFileInfo(original).lastModified();const auto oldKey=SourceCache::fileKey(original);const auto oldBytes=bytes(original);QString project;
        {ProjectDatabase db;QVERIFY(db.create(dir.path(),"Legacy"));project=db.projectPath();QVERIFY(db.updateAdjustment(original,{}));QVERIFY(db.flush());}
        PhotoController controller(nullptr);controller.setGpuEnabled(false);QVERIFY(controller.importFile(QUrl::fromLocalFile(original)));QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady()&&!controller.loading(),10000);
        QVERIFY(QFile::remove(original));QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));QVERIFY(!controller.currentSourceAvailable());
        QVERIFY(fixture(original,1));{QFile f(original);QVERIFY(f.open(QIODevice::ReadWrite));QVERIFY(f.setFileTime(stamp,QFileDevice::FileModificationTime));}
        QCOMPARE(SourceCache::fileKey(original),oldKey);QVERIFY(bytes(original)!=oldBytes);
        QVERIFY(controller.beginRelink(original,QUrl::fromLocalFile(original)));QTRY_VERIFY_WITH_TIMEOUT(!controller.relinkBusy(),10000);
        const auto requestId=controller.relinkPreview().value("requestId").toString();QVERIFY(!controller.relinkPreview().value("identityKnown").toBool());
        QVERIFY(!controller.confirmRelink(requestId,false));QVERIFY(!controller.currentSourceAvailable());
        QVERIFY(controller.confirmRelink(requestId,true));QTRY_VERIFY_WITH_TIMEOUT(!controller.relinkBusy(),10000);QCOMPARE(controller.relinkPreview().value("status").toString(),QString("complete"));
        QVERIFY(SourceCache::fileKey(original)!=oldKey);QVERIFY(controller.currentSourceAvailable());QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady()&&!controller.loading(),10000);
        QSignalSpy finished(&controller,&PhotoController::exportFinished);QVERIFY(controller.exportVersions({original},QUrl::fromLocalFile(out),"display-p3",92,"png","fresh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);QCOMPARE(finished[0][1].toInt(),0);QImage actual(out+"/fresh.png");QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),reference(original,{}));
        ProjectDatabase db;QVector<ProjectDatabase::SavedPhoto> saved;QVERIFY(db.readSnapshot(project,&saved));QVERIFY(saved[0].identity.isKnown());QCOMPARE(saved[0].identity.sha256,identity(original).sha256);
    }
    void changedCandidateAndStaleRequestsNeverRetargetAProjectOrReplacementVersion() {
        QTemporaryDir dir;const auto original=dir.filePath("original.bmp"),candidate=dir.filePath("candidate.bmp");QVERIFY(fixture(original));QString first,second,copyKey=key();
        {ProjectDatabase db;QVERIFY(db.create(dir.path(),"First"));first=db.projectPath();QVERIFY(db.updateAdjustment(original,{}));QVERIFY(db.flush());EditHistory h;h.initialize({});QVERIFY(db.addVirtualCopy(copyKey,original,"Copy",{},h,{},{}));}
        {ProjectDatabase db;QVERIFY(db.create(dir.path(),"Second"));second=db.projectPath();QVERIFY(db.updateAdjustment(original,{}));QVERIFY(db.flush());}
        QVERIFY(QFile::rename(original,candidate));PhotoController controller(nullptr);QVERIFY(controller.openProject(QUrl::fromLocalFile(first)));
        QVERIFY(controller.beginRelink(copyKey,QUrl::fromLocalFile(candidate)));QTRY_VERIFY_WITH_TIMEOUT(!controller.relinkBusy(),10000);const auto oldId=controller.relinkPreview().value("requestId").toString();
        QVERIFY(fixture(candidate,1));QVERIFY(controller.confirmRelink(oldId,true));QTRY_VERIFY_WITH_TIMEOUT(!controller.relinkBusy(),10000);QCOMPARE(controller.relinkPreview().value("status").toString(),QString("error"));
        QVERIFY(controller.beginRelink(copyKey,QUrl::fromLocalFile(candidate)));controller.cancelRelink();QTest::qWait(50);QVERIFY(controller.relinkPreview().isEmpty());QVERIFY(!controller.confirmRelink(oldId,true));
        const auto pickerToken=controller.relinkCatalogToken();
        QVERIFY(controller.beginRelink(copyKey,QUrl::fromLocalFile(candidate)));QVERIFY(controller.openProject(QUrl::fromLocalFile(second)));QTest::qWait(50);
        QCOMPARE(controller.projectPath(),second);QCOMPARE(controller.library().size(),1);QVERIFY(controller.relinkPreview().isEmpty());
        QVERIFY(!controller.beginRelink(original,QUrl::fromLocalFile(candidate),pickerToken));QVERIFY(controller.relinkPreview().isEmpty());
        QVERIFY(controller.openProject(QUrl::fromLocalFile(first)));controller.selectPhoto(1);QVERIFY(controller.beginRelink(copyKey,QUrl::fromLocalFile(candidate)));
        QVERIFY(controller.removeCurrentVirtualCopy());QTRY_VERIFY_WITH_TIMEOUT(!controller.relinkBusy(),10000);QCOMPARE(controller.relinkPreview().value("status").toString(),QString("error"));
        QVERIFY(!controller.confirmRelink(controller.relinkPreview().value("requestId").toString(),true));QCOMPARE(controller.currentFile(),original);
    }
    void newImportRecordsBaselineAfterControllerDirtyRowsArePublished() {
        QTemporaryDir dir;const auto original=dir.filePath("new.bmp");QVERIFY(fixture(original));
        PhotoController controller(nullptr);controller.setGpuEnabled(false);QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"NewImport"));
        const auto project=controller.projectPath();QVERIFY(controller.importFile(QUrl::fromLocalFile(original)));
        ProjectDatabase reader;QVector<ProjectDatabase::SavedPhoto> saved;
        QTRY_VERIFY_WITH_TIMEOUT(([&]{return reader.readSnapshot(project,&saved)&&saved.size()==1&&saved[0].identity.isKnown();})(),10000);
        QCOMPARE(saved[0].identity,identity(original));QCOMPARE(saved[0].path,original);QVERIFY(controller.flushEdits());
    }
};
QTEST_MAIN(MissingSourceTests)
#include "MissingSourceTests.moc"
