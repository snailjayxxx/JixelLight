#include <QtTest>
#include <QColorSpace>
#include <QFile>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonDocument>
#include "core/project/ProjectDatabase.h"
#include <QImageReader>
#include <QRgba64>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QUuid>
#include <QUrl>
#include <exiv2/exiv2.hpp>
#include <QtEndian>
#include <algorithm>
#include <cmath>

#include "app/PhotoController.h"
#include "core/color/ColorManagement.h"
#include "core/metadata/MetadataReader.h"
#include "core/metadata/XmpSidecar.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/pipeline/StageGraph.h"
#include "core/export/PngExporter.h"
#include "core/export/RasterExporter.h"
#include "core/commands/CommandRegistry.h"
#include "core/commands/AdjustmentTransfer.h"
#include "core/commands/NamedPresets.h"
#include "core/raw/RawDecoder.h"
#include "core/scopes/ScopesEngine.h"
#include "core/scopes/ScopePlot.h"
#include "core/image/ProcessedImageProvider.h"
#include <numeric>
#include "diagnostics/ZipStoreWriter.h"
#include "diagnostics/DiagnosticBundle.h"

namespace {
int channelSpread(const QColor &c) {
    const int hi = std::max({c.red(), c.green(), c.blue()});
    const int lo = std::min({c.red(), c.green(), c.blue()});
    return hi - lo;
}
QImage sceneGrayImage(double value) {
    QImage image(1, 1, QImage::Format_RGBA64);
    auto *px = reinterpret_cast<QRgba64 *>(image.scanLine(0));
    const quint16 code = static_cast<quint16>(std::lround(value * 65535.0));
    px[0] = QRgba64::fromRgba64(code, code, code, 65535);
    return image;
}
QByteArray storedZipEntry(const QString &path, const QByteArray &entry) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes=file.readAll(); qsizetype offset=0;
    while (offset+30<=bytes.size() && qFromLittleEndian<quint32>(bytes.constData()+offset)==0x04034b50) {
        const auto size=qFromLittleEndian<quint32>(bytes.constData()+offset+18);
        const auto nameSize=qFromLittleEndian<quint16>(bytes.constData()+offset+26);
        const auto extraSize=qFromLittleEndian<quint16>(bytes.constData()+offset+28);
        const auto start=offset+30+nameSize+extraSize;
        if (start>bytes.size() || size>bytes.size()-start) return {};
        if (bytes.mid(offset+30,nameSize)==entry) return bytes.mid(start,size);
        offset=start+size;
    }
    return {};
}
}

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void adjustmentTransferGroupsAreCompleteIndependentAndRejectPartialInput() {
        AdjustmentState source,target;
        auto json=source.toJson(); int n=0;
        for (auto it=json.begin();it!=json.end();++it) {
            if (it.value().isDouble()) it.value()=++n*.1;
            else if (it.value().isArray()) { auto a=it.value().toArray(); for (int i=0;i<a.size();++i) a[i]=.1+i*.15; it.value()=a; }
        }
        source=AdjustmentState::fromJson(json); source.geometry.crop={.1,.2,.7,.6}; source.geometry.straighten=4;
        source.look.mode="calibrated"; source.look.lut=LookLut::identity(); source.look.strength=.7;
        const QMap<QString,QStringList> groups{
            {"exposure",{"exposure"}},{"white_balance",{"temperature","tint"}},
            {"tone",{"contrast","highlights","shadows","whites","blacks","highlightRecovery"}},
            {"color",{"hue","saturation","vibrance"}},{"hsl",{"hslHue","hslSaturation","hslLuminance"}},
            {"curves",{"masterCurve","redCurve","greenCurve","blueCurve"}},{"sony_look",{"look"}},{"geometry",{"geometry"}}
        };
        const auto before=target.toJson(),after=source.toJson(); QSet<QString> covered;
        for (auto group=groups.begin();group!=groups.end();++group) {
            auto actual=target; QVERIFY(AdjustmentTransfer::apply(actual,source,{group.key()}));
            auto expected=before; for (const auto &key : group.value()) { QVERIFY(!covered.contains(key)); covered.insert(key); expected[key]=after[key]; }
            QCOMPARE(actual.toJson(),expected);
            if (group.key()=="sony_look") QCOMPARE(actual.look.lut,source.look.lut);
        }
        const auto keys=after.keys(); QCOMPARE(covered,QSet<QString>(keys.begin(),keys.end()));
        QVERIFY(AdjustmentTransfer::apply(target,source,AdjustmentTransfer::allGroups())); QCOMPARE(target.toJson(),after);
        QCOMPARE(target.look.lut,source.look.lut);
        for (const QStringList &invalid : QList<QStringList>{{},{"exposure","unknown"},{"exposure","exposure"}}) {
            const auto saved=target.toJson(); QVERIFY(!AdjustmentTransfer::apply(target,AdjustmentState{},invalid)); QCOMPARE(target.toJson(),saved);
        }
        source.look={}; source.look.mode="as-shot";
        QVERIFY(AdjustmentTransfer::apply(target,source,{"sony_look"})); QCOMPARE(target.look.mode,QString("as-shot"));
        QVERIFY(target.look.code.isEmpty()); QVERIFY(target.look.parameters.isEmpty()); QVERIFY(!target.look.lut);
    }
    void selectivePasteUsesFrozenClipboardAndKeepsOtherGroupsAndUndo() {
        QTemporaryDir dir; QImage image(24,16,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto a=dir.filePath("a.png"),b=dir.filePath("b.png"); QVERIFY(image.save(a)); QVERIFY(image.save(b));
        PhotoController c(nullptr); c.setGpuEnabled(false); QVERIFY(c.importFile(QUrl::fromLocalFile(a))); QVERIFY(c.importFile(QUrl::fromLocalFile(b)));
        QVERIFY(!c.hasAdjustmentClipboard()); QVERIFY(!c.pasteAdjustmentGroups({"exposure"}));
        c.setExposure(1.5); c.setTemperature(12); c.setLookCode("FL"); c.setStraighten(3); c.copyAdjustments();
        QVERIFY(c.hasAdjustmentClipboard()); QCOMPARE(c.adjustmentClipboardName(),QString("a.png"));
        c.setExposure(2); c.setLookCode("VV"); c.selectPhoto(1); c.setExposure(-1); c.setTemperature(-8); c.setSaturation(25); c.setLookCode("ST"); c.setStraighten(-4);
        c.setRating(3); c.setSelectionKeywords("target"); c.finishInteraction(); const auto history=c.editHistory();
        QVERIFY(!c.pasteAdjustmentGroups({"exposure","bad"})); QCOMPARE(c.exposure(),-1.0); QCOMPARE(c.editHistory(),history);
        QVERIFY(c.pasteAdjustmentGroups({"exposure","sony_look"})); QCOMPARE(c.exposure(),1.5); QCOMPARE(c.lookState()["code"].toString(),QString("FL"));
        QCOMPARE(c.temperature(),-8.0); QCOMPARE(c.saturation(),25.0); QCOMPARE(c.geometry()["straighten"].toDouble(),-4.0);
        QCOMPARE(c.currentRating(),3); QCOMPARE(c.currentKeywords(),QStringList{"target"}); QCOMPARE(c.editHistory().size(),history.size()+1);
        c.undo(); QCOMPARE(c.exposure(),-1.0); QCOMPARE(c.lookState()["code"].toString(),QString("ST")); c.redo(); QCOMPARE(c.exposure(),1.5);
        const auto pastedHistory=c.editHistory(); QVERIFY(c.pasteAdjustmentGroups({"exposure","sony_look"})); QCOMPARE(c.editHistory(),pastedHistory);
        QVERIFY(c.pasteAdjustmentGroups({"geometry"})); QCOMPARE(c.geometry()["straighten"].toDouble(),3.0); c.undo(); QCOMPARE(c.geometry()["straighten"].toDouble(),-4.0);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Paste")); QVERIFY(c.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath()))); reopened.selectPhoto(1);
        QCOMPARE(reopened.exposure(),1.5); QCOMPARE(reopened.geometry()["straighten"].toDouble(),-4.0); QVERIFY(reopened.canRedo());
        reopened.redo(); QCOMPARE(reopened.geometry()["straighten"].toDouble(),3.0);
        QFile original(a); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes=original.readAll(); original.close();
        QFile second(b); QVERIFY(second.open(QIODevice::ReadOnly)); QCOMPARE(second.readAll(),bytes);
    }
    void selectiveSyncExcludesSourceAndUnselectedVersionsAndPersistsPerTargetHistory() {
        QTemporaryDir dir; QImage image(24,16,QImage::Format_RGB32); image.fill(Qt::gray); PhotoController c(nullptr); c.setGpuEnabled(false);
        for (const auto &name : {"a.png","b.png","c.png"}) { const auto path=dir.filePath(name); QVERIFY(image.save(path)); QVERIFY(c.importFile(QUrl::fromLocalFile(path))); }
        c.selectPhoto(1); c.setExposure(-1); c.setStraighten(-4); c.setRating(2); c.setSelectionKeywords("keep"); c.finishInteraction();
        const auto targetHistory=c.editHistory();
        c.selectPhoto(0); c.setExposure(1.25); c.setTemperature(17); c.setLookCode("FL"); c.setStraighten(5); c.finishInteraction();
        QVERIFY(c.createVirtualCopy("independent")); c.setExposure(-2); c.setStraighten(9); c.finishInteraction();
        c.selectPhoto(0); c.setSaturation(30); c.undo(); QVERIFY(c.canRedo()); const auto sourceHistory=c.editHistory();
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Sync")); QVERIFY(c.flushEdits());
        QVERIFY(c.setPhotoSelection({0})); QCOMPARE(c.syncAdjustmentGroups({"exposure"},true),0);
        QVERIFY(c.setPhotoSelection({0,1,3})); const auto before=c.library();
        QCOMPARE(c.syncAdjustmentGroups({"exposure","white_balance","sony_look"},true),2);
        QCOMPARE(c.editHistory(),sourceHistory); QVERIFY(c.canRedo()); QCOMPARE(c.selectedIndices(),QVariantList({0,1,3}));
        const auto once=c.library(); QCOMPARE(c.syncAdjustmentGroups({"exposure","white_balance","sony_look"},true),0); QCOMPARE(c.library(),once);
        QCOMPARE(c.syncAdjustmentGroups({"exposure","invalid"},false),-1); QCOMPARE(c.library(),once);
        QCOMPARE(c.library()[2],before[2]);
        c.selectPhoto(1); QCOMPARE(c.exposure(),1.25); QCOMPARE(c.temperature(),17.0); QCOMPARE(c.geometry()["straighten"].toDouble(),-4.0);
        QCOMPARE(c.currentRating(),2); QCOMPARE(c.currentKeywords(),QStringList{"keep"}); QCOMPARE(c.editHistory().size(),targetHistory.size()+1);
        c.undo(); QCOMPARE(c.exposure(),-1.0); QCOMPARE(c.temperature(),0.0); QVERIFY(c.canRedo()); QVERIFY(c.flushEdits());
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> saved; QVERIFY(db.readSnapshot(c.projectPath(),&saved));
        int looks=0; for (const auto &photo : saved) if (photo.adjustments.look.code=="FL") ++looks;
        QCOMPARE(looks,2); // Source + virtual copy; target B is at its pre-sync undo cursor.
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath()))); reopened.selectPhoto(1);
        QCOMPARE(reopened.exposure(),-1.0); QVERIFY(reopened.canRedo()); reopened.redo(); QCOMPARE(reopened.exposure(),1.25);
        reopened.selectPhoto(3); QCOMPARE(reopened.exposure(),1.25); QCOMPARE(reopened.geometry()["straighten"].toDouble(),9.0);
        reopened.undo(); QCOMPARE(reopened.exposure(),-2.0); QCOMPARE(reopened.geometry()["straighten"].toDouble(),9.0);
        reopened.selectPhoto(0); QCOMPARE(reopened.syncAdjustmentGroups({"exposure"},false),2); // B already matches.
        reopened.selectPhoto(2); QCOMPARE(reopened.exposure(),1.25); QVERIFY(reopened.canUndo());
    }
    void selectiveSyncDatabaseFailureRollsBackBatchAndCanRetry() {
        QTemporaryDir dir; QImage image(12,8,QImage::Format_RGB32); image.fill(Qt::gray); PhotoController c(nullptr); c.setGpuEnabled(false);
        for (const auto &name : {"a.png","b.png","c.png"}) { const auto path=dir.filePath(name); QVERIFY(image.save(path)); QVERIFY(c.importFile(QUrl::fromLocalFile(path))); }
        c.setExposure(1); c.finishInteraction(); QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"RetrySync")); QVERIFY(c.flushEdits());
        const QString connection="sync-failure-fixture";
        auto trigger=[&](bool enable) {
            { auto db=QSqlDatabase::addDatabase("QSQLITE",connection); db.setDatabaseName(QDir(c.projectPath()).filePath("Project.db")); QVERIFY(db.open());
              QSqlQuery q(db); QVERIFY(q.exec(enable ? "CREATE TRIGGER block_sync BEFORE UPDATE ON photos WHEN NEW.path LIKE '%b.png' BEGIN SELECT RAISE(ABORT,'blocked sync'); END" : "DROP TRIGGER block_sync")); db.close(); }
            QSqlDatabase::removeDatabase(connection);
        };
        trigger(true); QCOMPARE(c.syncAdjustmentGroups({"exposure"},false),2); QVERIFY(!c.flushEdits());
        const auto saveError=c.statusMessage(); QCOMPARE(c.syncAdjustmentGroups({"exposure"},false),0); QCOMPARE(c.statusMessage(),saveError);
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> saved; QVERIFY(db.readSnapshot(c.projectPath(),&saved));
        for (const auto &photo : saved) if (!photo.path.endsWith("a.png")) QCOMPARE(photo.adjustments.exposure,0.0);
        QCoreApplication::processEvents(); trigger(false); QCOMPARE(c.syncAdjustmentGroups({"exposure"},false),0); QVERIFY(c.flushEdits()); QVERIFY(db.readSnapshot(c.projectPath(),&saved));
        for (auto &photo : saved) { QCOMPARE(photo.adjustments.exposure,1.0); if (!photo.path.endsWith("a.png")) { QVERIFY(photo.history.canUndo()); QCOMPARE(photo.history.undo().exposure,0.0); } }
    }
    void copyImportVerifiesContentAndRejectsWholePlanConflicts() {
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkdir("out")); QVERIFY(QDir(dir.path()).mkdir("other"));
        QImage image(20,10,QImage::Format_RGB32); image.fill(Qt::red);
        const auto first=dir.filePath("photo.png"),second=dir.filePath("other/photo.png"),out=dir.filePath("out");
        QVERIFY(image.save(first)); QVERIFY(image.save(second));
        QFile original(first); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes=original.readAll(); original.close();
        auto result=copyImportFiles({first,dir.filePath("missing.png")},out); QVERIFY(!result.error.isEmpty()); QVERIFY(result.completed.isEmpty()); QVERIFY(QDir(out).isEmpty());
        result=copyImportFiles({first,second},out); QVERIFY(!result.error.isEmpty()); QVERIFY(QDir(out).isEmpty());
        result=copyImportFiles({first},out); QVERIFY(result.error.isEmpty()); QCOMPARE(result.completed.size(),1);
        QFile copied(QDir(out).filePath("photo.png")); QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(),bytes); copied.close();
        QCOMPARE(result.completed[0].sha256,QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));
        QVERIFY(!copyImportFiles({first},out).error.isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),bytes); original.close();
        QVERIFY(QFile::remove(copied.fileName()));
        { QFile upper(QDir(out).filePath("PHOTO.PNG")); QVERIFY(upper.open(QIODevice::WriteOnly|QIODevice::NewOnly)); QCOMPARE(upper.write(bytes),qint64(bytes.size())); }
        QVERIFY(!copyImportFiles({first},out).error.isEmpty());
        QCOMPARE(QDir(out).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{"PHOTO.PNG"});
    }
    void copyImportCancellationAndPublicationRaceCleanStaging() {
        QTemporaryDir dir; const auto out=dir.filePath("out"); QVERIFY(QDir(dir.path()).mkdir("out"));
        QImage image(10,10,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto first=dir.filePath("first.png"), second=dir.filePath("second.png"); QVERIFY(image.save(first)); QVERIFY(image.save(second));
        { QFile large(second); QVERIFY(large.open(QIODevice::Append)); QCOMPARE(large.write(QByteArray(3*1024*1024,'x')),qint64(3*1024*1024)); }
        auto token=std::make_shared<std::atomic_bool>(false);
        const auto result=copyImportFiles({first,second},out,token,[&](qint64,qint64,int done,int,const QString &stage) { if (done==1 && stage=="copy") token->store(true); });
        QVERIFY(result.wasCancelled); QVERIFY(result.error.isEmpty()); QCOMPARE(result.completed.size(),1);
        QCOMPARE(QDir(out).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{"first.png"});
        QVERIFY(QFileInfo::exists(first)); QVERIFY(QFileInfo::exists(second));
        QVERIFY(QFile::remove(QDir(out).filePath("first.png")));
        const auto collision=QDir(out).filePath("first.png");
        const auto raced=copyImportFiles({first},out,{},[&](qint64,qint64,int,int,const QString &stage) {
            if (stage=="verify") { QFile file(collision); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly)); QCOMPARE(file.write("existing"),qint64(8)); }
        });
        QVERIFY(!raced.error.isEmpty()); QVERIFY(raced.completed.isEmpty());
        QFile existing(collision); QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(),QByteArray("existing")); existing.close();
        QCOMPARE(QDir(out).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{"first.png"});
        QVERIFY(copyImportFiles({second},out,token).wasCancelled);
    }
    void copyImportDetectsSourceChangesAndPreservesEarlierResults() {
        QTemporaryDir dir; const auto out=dir.filePath("out"); QVERIFY(QDir(dir.path()).mkdir("out"));
        QImage image(10,10,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto first=dir.filePath("first.png"),second=dir.filePath("second.png"); QVERIFY(image.save(first)); QVERIFY(image.save(second));
        bool changed=false;
        const auto result=copyImportFiles({first,second},out,{},[&](qint64,qint64,int done,int,const QString &stage) {
            if (done==1 && stage=="published" && !changed) { QFile file(second); QVERIFY(file.open(QIODevice::Append)); QCOMPARE(file.write("changed"),qint64(7)); changed=true; }
        });
        QVERIFY(changed); QVERIFY(!result.error.isEmpty()); QCOMPARE(result.completed.size(),1);
        QCOMPARE(QDir(out).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{"first.png"});
        QVERIFY(QFile::remove(QDir(out).filePath("first.png")));
        const auto timestamp=QFileInfo(first).lastModified();
        const auto sameSize=copyImportFiles({first},out,{},[&](qint64,qint64,int,int,const QString &stage) {
            if (stage=="verify") {
                QFile file(first); QVERIFY(file.open(QIODevice::ReadWrite)); QVERIFY(file.seek(file.size()-1));
                QCOMPARE(file.write("X"),qint64(1)); QVERIFY(file.flush()); QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime));
            }
        });
        QVERIFY(sameSize.error.contains("Source content changed")); QVERIFY(sameSize.completed.isEmpty()); QVERIFY(QDir(out).isEmpty());
    }
    void copyImportControllerKeepsProjectAndPersistsCompletedCopiesOnClose() {
        QTemporaryDir dir; const auto out=dir.filePath("out"); QVERIFY(QDir(dir.path()).mkdir("out"));
        QImage image(20,10,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto first=dir.filePath("first.png"), second=dir.filePath("second.png"); QVERIFY(image.save(first)); QVERIFY(image.save(second));
        { QFile large(second); QVERIFY(large.open(QIODevice::Append)); QCOMPARE(large.write(QByteArray(16*1024*1024,'x')),qint64(16*1024*1024)); }
        PhotoController controller(nullptr); controller.setGpuEnabled(false); QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Copied"));
        const auto project=controller.projectPath(); bool closeCollected=false,closeOk=false;
        connect(&controller,&PhotoController::copyImportChanged,&controller,[&] {
            if (!closeCollected && controller.copyImportBusy() && controller.copyImportStatus().contains("1/2")) {
                closeCollected=true; closeOk=controller.prepareToClose();
            }
        });
        QVERIFY(controller.copyImport({QUrl::fromLocalFile(first),QUrl::fromLocalFile(second)},QUrl::fromLocalFile(out)));
        QVERIFY(controller.copyImportBusy()); QVERIFY(!controller.createProject(QUrl::fromLocalFile(dir.path()),"Wrong"));
        QVERIFY(!controller.openProject(QUrl::fromLocalFile(project))); QCOMPARE(controller.projectPath(),project);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.copyImportBusy(),10000); QVERIFY(closeCollected); QVERIFY(closeOk);
        QVERIFY(!controller.library().isEmpty());
        for (const auto &row : controller.library()) QVERIFY(row.toMap()["path"].toString().startsWith(out+"/"));
        ProjectDatabase read; QVector<ProjectDatabase::SavedPhoto> saved; QVERIFY(read.readSnapshot(project,&saved)); QCOMPARE(saved.size(),controller.library().size());
        QVERIFY(QFileInfo::exists(first)); QVERIFY(QFileInfo::exists(second));
        QVERIFY(QDir(out).entryList({".jixellight-import-*"},QDir::Dirs|QDir::Hidden).isEmpty());
        // A new job immediately after synchronous collection cannot consume an
        // older watcher's queued finished/progress signal as its own result.
        const auto third=dir.filePath("third.png"); QVERIFY(image.save(third));
        QVERIFY(controller.copyImport({QUrl::fromLocalFile(third)},QUrl::fromLocalFile(out)));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.copyImportBusy(),10000); QVERIFY(QFileInfo::exists(QDir(out).filePath("third.png")));
        QVERIFY(controller.prepareToClose());
    }
    void straightenResamplesLinear16BitWithoutBlackCornersOrUpscaling() {
        QImage source(101,61,QImage::Format_RGBA64); source.setColorSpace(QColorSpace::SRgbLinear);
        source.setText("JixelLightSource","RAW"); source.setDotsPerMeterX(3000); source.setDevicePixelRatio(2);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(1001+101*x+37*y,2003+53*x+211*y,3007+13*x+17*y,65535);
        const auto unchanged=source.copy(); GeometryState g;
        QCOMPARE(g.apply(source).cacheKey(),source.cacheKey()); QCOMPARE(g.toJson()["schema"].toInt(),1);
        for (double degrees : {-45.0,-13.25,.1,23.0,45.0}) {
            g.straighten=degrees; const auto output=g.apply(source); QVERIFY(!output.isNull());
            QCOMPARE(output.format(),QImage::Format_RGBA64); QCOMPARE(output.colorSpace(),source.colorSpace());
            QCOMPARE(output.text("JixelLightSource"),QStringLiteral("RAW")); QCOMPARE(output.devicePixelRatio(),2.0); QCOMPARE(output.dotsPerMeterX(),3000);
            QVERIFY(output.width()<source.width()); QVERIFY(output.height()<source.height());
            const double angle=degrees*std::acos(-1)/180, c=std::cos(angle), s=std::sin(angle);
            for (int y=0;y<output.height();++y) for (int x=0;x<output.width();++x) {
                const double u=x-(output.width()-1)/2.0, v=y-(output.height()-1)/2.0;
                const double sx=50+c*u+s*v, sy=30-s*u+c*v;
                QVERIFY(sx>=-1e-9 && sx<=100+1e-9 && sy>=-1e-9 && sy<=60+1e-9);
                const auto p=reinterpret_cast<const QRgba64 *>(output.constScanLine(y))[x];
                QVERIFY(std::abs(int(p.red())-std::lround(1001+101*sx+37*sy))<=1);
                QVERIFY(std::abs(int(p.green())-std::lround(2003+53*sx+211*sy))<=1);
                QCOMPARE(p.alpha(),quint16(65535));
            }
        }
        QCOMPARE(source,unchanged);
        QImage transparent(2,2,QImage::Format_RGBA64); transparent.fill(QColor(0,0,255,0));
        reinterpret_cast<QRgba64 *>(transparent.scanLine(0))[0]=QRgba64::fromRgba64(65535,0,0,65535);
        g.straighten=45; const auto center=g.apply(transparent); QCOMPARE(center.size(),QSize(1,1));
        const auto pixel=reinterpret_cast<const QRgba64 *>(center.constScanLine(0))[0];
        QCOMPARE(pixel.red(),quint16(65535)); QCOMPARE(pixel.blue(),quint16(0)); QCOMPARE(pixel.alpha(),quint16(16384));
        QImage thin(1,7,QImage::Format_RGBA64); thin.fill(Qt::green); QCOMPARE(g.apply(thin).size(),QSize(1,1));
    }
    void straightenGeometryOrderCancellationAndPipelineAgreement() {
        QImage source(31,23,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(x*1901,y*2501,(x+y)*1007,65535);
        GeometryState angle; angle.straighten=12.5; const auto straight=angle.apply(source);
        GeometryState rest; rest.crop={.2,.1,.6,.8}; rest.quarterTurns=1; rest.flipHorizontal=true;
        auto combined=rest; combined.straighten=angle.straighten;
        const auto expected=rest.apply(straight); QCOMPARE(combined.apply(source),expected);
        const auto cancelledToken=std::make_shared<std::atomic_bool>(true);
        QVERIFY(combined.apply(source,cancelledToken).isNull());
        PrepareRequest request; request.image=source; request.geometry=combined; request.viewport={4096,4096};
        QCOMPARE(preparePreview(request,{}).normal,expected);
        auto key=StageGraph::prepareKey(request); request.geometry.straighten=13; QVERIFY(StageGraph::prepareKey(request)!=key);
        AdjustmentState state; state.geometry=combined; state.exposure=.3;
        QTemporaryDir dir;
        for (bool raw : {false,true}) {
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,raw,0);
            const auto rendered=ImagePipeline::processWithPlan(expected,plan);
            const auto plot=renderScopePlot({source,plan,combined,"waveform",1,true},{});
            ScopePlotCounts reference("waveform"); QVERIFY(reference.add(rendered)); QCOMPARE(plot.image,reference.image());
            QCOMPARE(plot.pixels,quint64(expected.width())*expected.height());
            QString error; const auto path=dir.filePath(raw ? "raw.png" : "photo.png");
            QVERIFY2(exportRaster(source,state,path,ColorManagement::OutputSpace::SRgb,RasterFormat::Png16,100,{},&error,raw),qPrintable(error));
            QCOMPARE(QImageReader(path).read().convertToFormat(QImage::Format_RGBA64),rendered);
        }
    }
    void straightenHistoryProjectAndInteractiveCropRoundTrip() {
        QTemporaryDir dir; QImage image(80,60,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path=dir.filePath("source.png"); QVERIFY(image.save(path));
        PhotoController controller(nullptr); controller.setGpuEnabled(false); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY(controller.previewReady() && !controller.rendering()); const auto initial=controller.editHistory().size();
        controller.setStraighten(2); controller.setStraighten(5); controller.setStraighten(8); controller.finishInteraction();
        QCOMPARE(controller.editHistory().size(),initial+1); QCOMPARE(controller.geometry()["straighten"].toDouble(),8.0);
        const auto saved=controller.geometry(); controller.setStraighten(45.1); QCOMPARE(controller.geometry(),saved);
        controller.setExactScopes(true); GeometryState g; g.straighten=8; const auto size=g.straightenedSize(image.size());
        QTRY_COMPARE_WITH_TIMEOUT(controller.scopesPixelCount(),quint64(size.width())*size.height(),10000);
        controller.undo(); QVERIFY(!controller.geometry().contains("straighten")); controller.redo(); QCOMPARE(controller.geometry(),saved);
        QTRY_VERIFY(!controller.rendering()); QVERIFY(controller.beginCrop()); QTRY_VERIFY(!controller.rendering());
        QCOMPARE(controller.gpuSource().size(),size); QVERIFY(controller.applyCrop(.25,0,.5,1));
        QCOMPARE(controller.geometry()["straighten"].toDouble(),8.0); controller.undo(); QCOMPARE(controller.geometry(),saved);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Straightened")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); reopened.setGpuEnabled(false); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.geometry(),saved); QVERIFY(reopened.canRedo()); reopened.redo(); QCOMPARE(reopened.geometry()["width"].toDouble(),.5);
        reopened.undo(); reopened.undo(); QVERIFY(!reopened.geometry().contains("straighten")); reopened.redo(); QCOMPARE(reopened.geometry(),saved);
        AdjustmentState commands; QVERIFY(CommandRegistry::execute(commands,{{"command","geometry.straighten"},{"degrees",8.0}}));
        QCOMPARE(commands.geometry.toJson(),QJsonObject::fromVariantMap(saved));
        const auto before=commands.toJson();
        QVERIFY(!CommandRegistry::execute(commands,{{"command","geometry.straighten"},{"degrees",46.0}})); QCOMPARE(commands.toJson(),before);
        QVERIFY(!CommandRegistry::execute(commands,{{"command","geometry.straighten"},{"degrees","8"}})); QCOMPARE(commands.toJson(),before);
    }
    void invalidGeometryVersionWithoutHistoryPreservesActiveProject() {
        QTemporaryDir dir; ProjectDatabase active, candidate; QVERIFY(active.create(dir.path(),"Active")); QVERIFY(candidate.create(dir.path(),"Candidate"));
        AdjustmentState state; QVERIFY(candidate.addOrUpdatePhoto("source.png",state)); QVERIFY(candidate.flush());
        const auto activePath=active.projectPath();
        for (const auto invalid : {QJsonObject{{"schema",3},{"straighten",5}},QJsonObject{{"schema",2},{"straighten",46}}}) {
            const auto connection=QStringLiteral("invalid-geometry-test");
            { auto db=QSqlDatabase::addDatabase("QSQLITE",connection); db.setDatabaseName(QDir(candidate.projectPath()).filePath("Project.db")); QVERIFY(db.open());
              auto json=state.toJson(); json.insert("geometry",invalid); QSqlQuery q(db); q.prepare("UPDATE photos SET adjustment_json=?");
              q.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))); QVERIFY(q.exec()); }
            QSqlDatabase::removeDatabase(connection); QVector<ProjectDatabase::SavedPhoto> photos;
            QVERIFY(!active.open(candidate.projectPath(),&photos)); QCOMPARE(active.projectPath(),activePath); QVERIFY(active.isOpen());
        }
        auto legacy=GeometryState{}.toJson(); QVERIFY(GeometryState::validJson(legacy)); QVERIFY(!legacy.contains("straighten"));
        auto spoof=legacy; spoof.insert("straighten",10); QVERIFY(!GeometryState::validJson(spoof));
    }
    void scopePlotsCountEndpointsChannelsAndNeutralChroma() {
        QImage image(2,1,QImage::Format_RGBA64); auto row=reinterpret_cast<QRgba64 *>(image.scanLine(0));
        row[0]=QRgba64::fromRgba64(0,0,0,65535); row[1]=QRgba64::fromRgba64(65535,65535,65535,65535);
        ScopePlotCounts wave("waveform"); QVERIFY(wave.add(image)); QCOMPARE(wave.height,1024); QCOMPARE(wave.pixels,quint64(2));
        QCOMPARE(wave.bins[1023*wave.width],quint64(1)); QCOMPARE(wave.bins[255],quint64(1));
        QCOMPARE(std::accumulate(wave.bins.cbegin(),wave.bins.cend(),quint64(0)),quint64(2));
        ScopePlotCounts parade("parade"); QVERIFY(parade.add(image)); QCOMPARE(parade.width,768);
        for (int channel=0;channel<3;++channel) {
            QCOMPARE(parade.bins[1023*768+256*channel],quint64(1)); QCOMPARE(parade.bins[256*channel+255],quint64(1));
        }
        QCOMPARE(std::accumulate(parade.bins.cbegin(),parade.bins.cend(),quint64(0)),quint64(6));
        ScopePlotCounts vector("vectorscope"); QVERIFY(vector.add(image)); QCOMPARE(vector.pixels,quint64(2));
        quint64 center=0; for (int y=255;y<=256;++y) for (int x=255;x<=256;++x) center+=vector.bins[y*512+x];
        QCOMPARE(center,quint64(2)); QCOMPARE(vector.image().size(),QSize(512,512));
        row[0]=row[1]=QRgba64::fromRgba64(65535,0,0,65535);
        ScopePlotCounts red("vectorscope"); QVERIFY(red.add(image)); QCOMPARE(red.bins[197],quint64(2));
        QVERIFY(ScopePlotCounts("invalid").bins.isEmpty());
        const auto cancel=std::make_shared<std::atomic_bool>(true); ScopePlotCounts cancelledPlot("waveform");
        QVERIFY(!cancelledPlot.add(image,cancel)); QCOMPARE(cancelledPlot.pixels,quint64(0));
    }
    void scopePlotTiledReferenceMatchesFullImageAcrossRegionsAndGeometry() {
        QImage source(20,260,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64((x*3123+y*153)%65536,(x*543+y*731)%65536,(x*831+y*87)%65536,65535);
        AdjustmentState state; state.exposure=.5; state.geometry.crop={0,0,.75,1}; state.geometry.flipVertical=true;
        state.look.mode="manual"; state.look.code="ST"; state.look.parameters={{"sharpness",4},{"clarity",3}};
        for (bool raw : {false,true}) {
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,raw,0);
            const auto encoded=ImagePipeline::processWithPlan(state.geometry.apply(source),plan);
            for (const QString mode : {"waveform","parade","vectorscope"}) {
                ScopePlotCounts reference(mode); QVERIFY(reference.add(encoded));
                ScopePlotRequest request{source,plan,state.geometry,mode,7,true};
                const auto result=renderScopePlot(request,{}); QVERIFY(result.error.isEmpty());
                QCOMPARE(result.pixels,quint64(encoded.width())*encoded.height()); QCOMPARE(result.image,reference.image());
                QVERIFY(renderScopePlot(request,std::make_shared<std::atomic_bool>(true)).image.isNull());
            }
        }
    }
    void controllerScopePlotRejectsOldEditsAndPhotosAndIgnoresMonitorLut() {
        QTemporaryDir dir; QImage image(80,40,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto first=dir.filePath("first.png"), second=dir.filePath("second.png"); QVERIFY(image.save(first));
        QVERIFY(image.scaled(20,10).save(second)); ProcessedImageProvider provider;
        PhotoController controller(&provider); controller.setGpuEnabled(false); QVERIFY(controller.importFile(QUrl::fromLocalFile(first)));
        QTRY_VERIFY(controller.previewReady() && !controller.rendering()); controller.setScopeMode("waveform");
        controller.setExactScopes(true); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        QCOMPARE(controller.scopePlotPixels(),quint64(3200)); QVERIFY(!controller.scopePlotUrl().isEmpty());
        controller.setExposure(.5); QVERIFY(!controller.scopePlotCurrent()); controller.setCrop(0,0,.5,1);
        controller.setScopeMode("parade"); controller.finishInteraction(); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        QCOMPARE(controller.scopePlotPixels(),quint64(1600));
        QSize size; const auto before=provider.requestImage("scopes/plot",&size,{});
        QCOMPARE(provider.requestImage("scopes/plot",nullptr,QSize(300,150)).size(),QSize(300,150));
        controller.setDisplayColorLut({},"identity-for-scope-test"); QCOMPARE(provider.requestImage("scopes/plot",&size,{}),before);
        controller.setScopeMode("vectorscope"); QVERIFY(controller.importFile(QUrl::fromLocalFile(second))); controller.selectPhoto(1);
        QVERIFY(!controller.scopePlotCurrent()); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        QCOMPARE(controller.scopePlotPixels(),quint64(200)); QCOMPARE(provider.requestImage("scopes/plot",&size,{}).size(),QSize(512,512));
        controller.setScopeMode("histogram"); QVERIFY(controller.scopePlotUrl().isEmpty()); QCOMPARE(controller.scopePlotPixels(),quint64(0));
        controller.setScopeMode("waveform"); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        ProjectDatabase empty; QVERIFY(empty.create(dir.path(),"Empty"));
        QVERIFY(controller.openProject(QUrl::fromLocalFile(empty.projectPath())));
        QVERIFY(!controller.hasImage()); QVERIFY(controller.scopePlotUrl().isEmpty()); QVERIFY(!controller.scopePlotCurrent());
        QVERIFY(provider.requestImage("scopes/plot",nullptr,{}).isNull());
    }

    void tiff16AndWebpKeepTargetIccGeometryPixelsAndCancellation() {
        QTemporaryDir dir; QImage source(9,7,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(1000+x*6101,503+y*9011,123+x*997+y*997,65535);
        AdjustmentState state; state.exposure=.35; state.geometry.crop={.1,.2,.7,.7}; state.geometry.quarterTurns=1; state.geometry.straighten=3.5;
        for (const auto format : {RasterFormat::Tiff16,RasterFormat::WebP8}) for (const auto space : {ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::AdobeRgb,ColorManagement::OutputSpace::ProPhotoRgb}) for (bool raw : {false,true}) {
            QString error; const auto path=dir.filePath(format==RasterFormat::Tiff16 ? "out.tif" : "out.webp");
            QVERIFY2(exportRaster(source,state,path,space,format,100,{},&error,raw),qPrintable(error));
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,raw,0.0f);
            auto expected=ImagePipeline::processWithPlan(state.geometry.apply(source),plan);
            const auto actual=QImageReader(path).read(); QVERIFY(!actual.isNull()); QCOMPARE(actual.size(),expected.size());
            QCOMPARE(actual.colorSpace().iccProfile(),expected.colorSpace().iccProfile());
            if (format==RasterFormat::Tiff16) {
                QCOMPARE(actual.depth(),64); QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);
            } else {
                QCOMPARE(actual.convertToFormat(QImage::Format_RGBA8888),expected.convertToFormat(QImage::Format_RGBA8888));
            }
            QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll(); file.close();
            const auto token=std::make_shared<std::atomic_bool>(true);
            QVERIFY(!exportRaster(source,{},path,space,format,92,token,&error,raw));
            QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes);
        }
    }

    void orientedCropCoordinatesMatchAllPixelOrientations() {
        QImage image(16,12,QImage::Format_RGBA64);
        for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
            reinterpret_cast<QRgba64 *>(image.scanLine(y))[x] = QRgba64::fromRgba64(x*3000,y*5000,(x+y)*1700,65535);
        for (int turns=0;turns<4;++turns) for (bool horizontal : {false,true}) for (bool vertical : {false,true}) {
            GeometryState g; g.crop = {.25,.25,.5,.5}; g.quarterTurns = turns; g.flipHorizontal = horizontal; g.flipVertical = vertical;
            const auto oriented = g.orientedRect(g.crop);
            QCOMPARE(g.orientedRect(oriented,true),g.crop);
            auto full = g; full.crop = {0,0,1,1}; const auto rotated = full.apply(image);
            const auto reference = rotated.copy(qRound(oriented.x()*rotated.width()),qRound(oriented.y()*rotated.height()),
                qRound(oriented.width()*rotated.width()),qRound(oriented.height()*rotated.height()));
            QCOMPARE(g.apply(image),reference);
            QCOMPARE(g.orientedRect(QRectF(0,0,1,1)),QRectF(0,0,1,1));
        }
        GeometryState g; g.quarterTurns=1; QCOMPARE(g.orientedRect({0,0,.25,.5}),QRectF(.5,0,.5,.25));
        g.flipHorizontal=true; QCOMPARE(g.orientedRect({0,0,.25,.5}),QRectF(0,0,.5,.25));
    }
    void interactiveCropDraftCancelCommitUndoAndPhotoSwitchStayIsolated() {
        QTemporaryDir dir; QImage image(80,40,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto source=dir.filePath("source.png"), other=dir.filePath("other.png"); QVERIFY(image.save(source)); QVERIFY(image.save(other));
        PhotoController controller(nullptr); controller.setGpuEnabled(false); QVERIFY(controller.importFile(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(controller.previewReady() && !controller.rendering());
        controller.setCrop(.25,0,.5,1); controller.rotatePhoto(1); controller.flipPhoto(true); controller.finishInteraction();
        QTRY_VERIFY(!controller.rendering()); const auto saved=controller.geometry(); const auto history=controller.editHistory();
        QVERIFY(controller.beginCrop()); QVERIFY(controller.cropEditing()); QTRY_VERIFY(!controller.rendering());
        QCOMPARE(controller.geometry(),saved); QCOMPARE(controller.editHistory(),history);
        QCOMPARE(controller.gpuSource().size(),QSize(40,80)); // full oriented photo; stored crop still intact
        QVERIFY(!controller.applyCrop(-1,0,1,1)); QVERIFY(controller.cropEditing());
        controller.cancelCrop(); QVERIFY(!controller.cropEditing()); QCOMPARE(controller.editHistory(),history);
        QTRY_VERIFY(!controller.rendering()); QCOMPARE(controller.gpuSource().size(),QSize(40,40));
        QVERIFY(controller.beginCrop()); QVERIFY(controller.applyCrop(.25,0,.5,.75));
        const auto expected=GeometryState::fromJson(QJsonObject::fromVariantMap(saved)).orientedRect({.25,0,.5,.75},true);
        QCOMPARE(controller.geometry().value("x").toDouble(),expected.x()); QCOMPARE(controller.geometry().value("height").toDouble(),expected.height());
        QCOMPARE(controller.editHistory().size(),history.size()+1); controller.undo(); QCOMPARE(controller.geometry(),saved);
        QTRY_VERIFY(!controller.rendering()); QVERIFY(controller.beginCrop()); controller.setExposure(.5); QVERIFY(!controller.cropEditing());
        QTRY_VERIFY(!controller.rendering()); QVERIFY(controller.beginCrop());
        QVERIFY(controller.importFile(QUrl::fromLocalFile(other))); controller.selectPhoto(1); QVERIFY(!controller.cropEditing());
        QCOMPARE(controller.geometry().value("width").toDouble(),1.0); controller.selectPhoto(0); QCOMPARE(controller.geometry(),saved);
    }

    void xmpRoundTripPreservesSonyGeometryAndProtectsExistingFiles() {
        QTemporaryDir dir; QString error; const auto path = dir.filePath("photo.ARW.xmp");
        AdjustmentState state; state.exposure = .75; state.look.mode = "as-shot";
        state.geometry.crop = {.1,.2,.7,.6}; state.geometry.quarterTurns = 3; state.geometry.flipHorizontal = true; state.geometry.straighten=7.5;
        CatalogTags tags; tags.keywords = {"A & B","旅行 <日本>"}; tags.albums = {"旅行"}; tags.label = "red";
        QVERIFY(CatalogTags::normalize(&tags.keywords,64));
        QVERIFY2(XmpSidecar::writeNew(path,state,tags,4,"reject",&error),qPrintable(error));
        XmpSidecar::Document restored;
        QVERIFY2(XmpSidecar::read(path,&restored,&error),qPrintable(error));
        QCOMPARE(restored.adjustments.toJson(),state.toJson()); QCOMPARE(restored.tags.toJson(),tags.toJson());
        QCOMPARE(restored.rating,4); QCOMPARE(restored.flag,QStringLiteral("reject")); QVERIFY(restored.hasAdjustments);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
        QVERIFY(bytes.contains("<xmp:Rating>-1</xmp:Rating>")); QVERIFY(bytes.contains("A &amp; B"));
        QVERIFY(!XmpSidecar::writeNew(path,{},tags,0,"none",&error));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes); file.close();
        QVERIFY(!XmpSidecar::writeNew(dir.filePath("source.ARW"),state,tags,4,"none",&error));
        state.look.error = "broken";
        QVERIFY(!XmpSidecar::writeNew(dir.filePath("invalid.xmp"),state,tags,4,"none",&error));
        QVERIFY(!QFileInfo::exists(dir.filePath("invalid.xmp")));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
    }
    void xmpStandardNamespacesAndPublicOverridesAreExplicit() {
        QTemporaryDir dir; const auto path = dir.filePath("external.xmp"); QString error;
        QFile file(path); auto put = [&](const QByteArray &bytes) {
            if (!file.open(QIODevice::WriteOnly)) return false;
            const bool ok = file.write(bytes) == bytes.size(); file.close(); return ok;
        };
        const QByteArray xml = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><r:RDF xmlns:r="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><r:Description r:about="" xmlns:a="http://ns.adobe.com/xap/1.0/" xmlns:d="http://purl.org/dc/elements/1.1/" xmlns:c="http://ns.adobe.com/camera-raw-settings/1.0/" a:Rating="3.0" a:Label="Blue" c:Exposure2012="2"><d:subject><r:Bag><r:li> Japan </r:li><r:li>Japan</r:li><r:li>A &amp; B</r:li></r:Bag></d:subject></r:Description></r:RDF></x:xmpmeta>)";
        QVERIFY(put(xml)); XmpSidecar::Document result;
        QVERIFY2(XmpSidecar::read(path,&result,&error),qPrintable(error));
        QVERIFY(!result.hasAdjustments); QCOMPARE(result.rating,3); QCOMPARE(result.tags.label,QStringLiteral("blue"));
        QCOMPARE(result.tags.keywords,QStringList({"A & B","Japan"})); QVERIFY(!result.hasAlbums); QCOMPARE(result.warnings.size(),1);
        auto custom = xml; custom.replace("a:Label=\"Blue\"","a:Label=\"Client choice\""); QVERIFY(put(custom));
        QVERIFY(XmpSidecar::read(path,&result,&error)); QVERIFY(!result.hasLabel); QCOMPARE(result.warnings.size(),2);
        QVERIFY(file.remove()); AdjustmentState state; state.look.mode = "as-shot";
        QVERIFY(XmpSidecar::writeNew(path,state,{},5,"reject",&error));
        QVERIFY(file.open(QIODevice::ReadOnly)); auto changed = file.readAll(); file.close();
        changed.replace("<xmp:Rating>-1</xmp:Rating>","<xmp:Rating>2</xmp:Rating>"); QVERIFY(put(changed));
        QVERIFY(XmpSidecar::read(path,&result,&error)); QCOMPARE(result.rating,2); QCOMPARE(result.flag,QStringLiteral("none"));
        QVERIFY(result.hasAdjustments); QCOMPARE(result.adjustments.look.mode,QStringLiteral("as-shot"));
    }
    void xmpRejectsMalformedDuplicateEntityAndFutureSnapshotsWithoutMutation() {
        QTemporaryDir dir; QString error; const auto path = dir.filePath("invalid.xmp"); QFile file(path);
        const QByteArray start = R"(<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description rdf:about="" xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:dc="http://purl.org/dc/elements/1.1/">)";
        const QByteArray end = "</rdf:Description></rdf:RDF>";
        const QList<QByteArray> invalid = {
            start+"<xmp:Rating>2.5</xmp:Rating>"+end,
            start+"<xmp:Rating>2</xmp:Rating><xmp:Rating>3</xmp:Rating>"+end,
            start+"<xmp:Rating><rdf:value>3</rdf:value></xmp:Rating>"+end,
            start+"<dc:subject><rdf:Seq><rdf:li>A</rdf:li></rdf:Seq></dc:subject>"+end,
            "<!DOCTYPE x [<!ENTITY test '3'>]>"+start+"<xmp:Rating>&test;</xmp:Rating>"+end,
            start+"<xmp:Rating>3</xmp:Rating>",
            start+"<dc:subject><rdf:Bag><rdf:li><rdf:Bag/></rdf:li></rdf:Bag></dc:subject>"+end
        };
        XmpSidecar::Document result; result.rating = 5; result.adjustments.exposure = 1;
        for (const auto &bytes : invalid) {
            QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes),bytes.size()); file.close();
            QVERIFY(!XmpSidecar::read(path,&result,&error)); QVERIFY(!error.isEmpty());
            QCOMPARE(result.rating,5); QCOMPARE(result.adjustments.exposure,1.0);
        }
        QVERIFY(file.remove()); QVERIFY(XmpSidecar::writeNew(path,{}, {},2,"pick",&error));
        QVERIFY(file.open(QIODevice::ReadOnly)); auto bytes = file.readAll(); file.close();
        bytes.replace(ProcessingPlan::EngineVersion,"future-engine");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bytes); file.close();
        QVERIFY(!XmpSidecar::read(path,&result,&error)); QCOMPARE(result.rating,5);
    }
    void controllerXmpImportIsPerVersionUndoableAndPersistsCatalog() {
        QTemporaryDir dir; QImage image(12,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto source = dir.filePath("source.png"), sidecar = dir.filePath("saved.xmp"); QVERIFY(image.save(source));
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(source)));
        controller.setExposure(.5); controller.finishInteraction(); controller.setCrop(.1,.2,.8,.7);
        controller.setRating(4); controller.setFlag("pick"); QVERIFY(controller.setSelectionKeywords("Japan, Sony"));
        QVERIFY(controller.setSelectionLabel("green")); QVERIFY(controller.addSelectionToAlbum("Trip"));
        QVERIFY(controller.exportXmp(QUrl::fromLocalFile(sidecar))); QVERIFY(controller.createVirtualCopy("Alternate"));
        controller.resetAdjustments(); controller.setRating(1); QVERIFY(controller.setSelectionKeywords("Other"));
        QVERIFY(controller.importXmp(QUrl::fromLocalFile(sidecar)));
        QCOMPARE(controller.exposure(),.5); QCOMPARE(controller.geometry().value("x").toDouble(),.1);
        QCOMPARE(controller.currentRating(),4); QCOMPARE(controller.currentKeywords(),QStringList({"Japan","Sony"}));
        controller.undo(); QCOMPARE(controller.exposure(),0.0); controller.redo(); QCOMPARE(controller.exposure(),.5);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"XMP")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.library().size(),2); reopened.selectPhoto(1); QCOMPARE(reopened.exposure(),.5);
        QCOMPARE(reopened.currentColorLabel(),QStringLiteral("green")); QCOMPARE(reopened.currentAlbums(),QStringList({"Trip"}));
        reopened.undo(); QCOMPARE(reopened.exposure(),0.0); reopened.selectPhoto(0); QCOMPARE(reopened.exposure(),.5);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),original);
    }

    void editHistoryBranchesAndPreservesSonyState() {
        EditHistory history;
        AdjustmentState original;
        original.look.mode = "as-shot";
        history.initialize(original);
        auto edited = original;
        edited.exposure = 1;
        QVERIFY(history.record(edited, "exposure", "exposure"));
        edited.exposure = 2;
        QVERIFY(history.record(edited, "exposure", "exposure"));
        QCOMPARE(history.entries().size(), 2);
        QCOMPARE(history.undo().toJson(), original.toJson());
        QCOMPARE(history.redo().exposure, 2.0);
        history.finish();
        edited.look.mode = "manual";
        edited.look.code = "FL";
        edited.look.parameters["contrast"] = -2;
        history.record(edited, "look");
        QCOMPARE(history.undo().look.mode, QStringLiteral("as-shot"));
        edited.saturation = 12;
        history.record(edited, "saturation");
        QVERIFY(!history.canRedo());
        for (int i = 0; i < 160; ++i) {
            edited.tint = i;
            history.record(edited, "tint");
        }
        QCOMPARE(history.entries().size(), 129);
        int undos = 0;
        while (history.canUndo()) { history.undo(); ++undos; }
        QCOMPARE(undos, 128);
    }

    void controllerUndoIsPerPhotoAndPersistsRestoredState() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const QString a = dir.filePath("a.png"), b = dir.filePath("b.png");
        QVERIFY(image.save(a)); QVERIFY(image.save(b));
        PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(a)));
        QVERIFY(controller.importFile(QUrl::fromLocalFile(b)));
        controller.selectPhoto(0);
        controller.setExposure(1); controller.setExposure(2);
        controller.finishInteraction();
        controller.setSaturation(30);
        controller.undo(); QCOMPARE(controller.saturation(), 0.0);
        controller.undo(); QCOMPARE(controller.exposure(), 0.0);
        controller.redo(); QCOMPARE(controller.exposure(), 2.0);
        controller.selectPhoto(1); QVERIFY(!controller.canUndo());
        controller.setExposure(-1); controller.finishInteraction();
        controller.selectPhoto(0); QCOMPARE(controller.exposure(), 2.0);
        controller.resetAdjustments(); QCOMPARE(controller.exposure(), 0.0);
        controller.undo(); QCOMPARE(controller.exposure(), 2.0);
        controller.copyAdjustments(); controller.selectPhoto(1);
        controller.pasteAdjustments(); QCOMPARE(controller.exposure(), 2.0);
        controller.undo(); QCOMPARE(controller.exposure(), -1.0);
        controller.rotatePhoto(1);
        QCOMPARE(controller.geometry()["quarterTurns"].toInt(),1);
        controller.undo(); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),0);
        controller.redo(); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),1);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()), "history"));
        QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr);
        QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        reopened.selectPhoto(1); QCOMPARE(reopened.exposure(), -1.0);
        QCOMPARE(reopened.geometry()["quarterTurns"].toInt(),1);
        QVERIFY(reopened.canUndo());
        reopened.undo(); QCOMPARE(reopened.geometry()["quarterTurns"].toInt(),0);
        QVERIFY(reopened.canRedo());
        QVERIFY(reopened.flushEdits());
        PhotoController resumed(nullptr);
        QVERIFY(resumed.openProject(QUrl::fromLocalFile(controller.projectPath())));
        resumed.selectPhoto(1);
        QVERIFY(resumed.canRedo());
        resumed.redo(); QCOMPARE(resumed.geometry()["quarterTurns"].toInt(),1);
        QSignalSpy exported(&controller, &PhotoController::exportFinished);
        QVERIFY(controller.exportAll(QUrl::fromLocalFile(dir.path()), "display-p3",92,"png"));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(),1,10000);
        QCOMPARE(exported.first()[0].toInt(),2);
        QCOMPARE(QImage(dir.filePath("a_JixelLight.png")).depth(),64);
        QCOMPARE(QImage(dir.filePath("b_JixelLight.png")).depth(),64);
    }

    void historyPersistenceRejectsUnsupportedOrInconsistentSnapshots() {
        AdjustmentState original, edited; edited.exposure = 1;
        EditHistory history; history.initialize(original); history.record(edited, "exposure");
        history.undo();
        EditHistory restored;
        QVERIFY(restored.restore(history.toJson(), original));
        QVERIFY(restored.canRedo()); QCOMPARE(restored.redo().exposure, 1.0);
        auto invalid = history.toJson(); invalid.insert("schema", 2);
        QVERIFY(!restored.restore(invalid, original));
        QVERIFY(!restored.restore(history.toJson(), edited));
        invalid = history.toJson(); invalid.insert("cursor", 0.5);
        QVERIFY(!restored.restore(invalid, original));
        QCOMPARE(restored.cursor(), 1);
    }

    void persistentHistorySharesAndValidatesLuts() {
        AdjustmentState state; state.look.mode = "calibrated"; state.look.lut = LookLut::identity();
        EditHistory history; history.initialize(state);
        state.exposure = 1; history.record(state, "exposure");
        const auto json = history.toJson(); QCOMPARE(json.value("luts").toArray().size(), 1);
        EditHistory restored; QVERIFY(restored.restore(json, state));
        QCOMPARE(restored.entries()[0].state.look.lut, restored.entries()[1].state.look.lut);
        QCOMPARE(restored.undo().look.lut->digest, state.look.lut->digest);
        auto invalid = json; auto luts = json.value("luts").toArray();
        auto lut = luts[0].toObject(); lut.insert("sha256", "invalid"); luts[0] = lut; invalid.insert("luts", luts);
        QVERIFY(!restored.restore(invalid, state));
    }

    void persistentHistoryAndPresetsRejectDifferentSonyFitEngine() {
        auto lut = std::make_shared<LookLut>(*LookLut::identity());
        lut->evidence = {{"kind", "multi-scene-empirical-fit"}, {"engineVersion", "older-engine"}};
        AdjustmentState state; state.look.mode = "calibrated"; state.look.lut = lut;
        EditHistory history; history.initialize(state); EditHistory restored;
        QVERIFY(!restored.restore(history.toJson(), state));
        QTemporaryDir dir; QVERIFY(dir.isValid()); QString error;
        NamedPresets presets(dir.filePath("presets.json")); QVERIFY(presets.load(&error));
        QVERIFY(!presets.save("Incompatible", state, &error));
        QVERIFY(!QFileInfo::exists(dir.filePath("presets.json")));
        const auto json = QJsonDocument(QJsonObject{{"schema", 1}, {"presets", QJsonObject{{"Incompatible", state.toJson()}}}}).toJson();
        QFile file(dir.filePath("presets.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(json), json.size()); file.close();
        QVERIFY(!presets.load(&error));
        QVERIFY(!presets.remove("Incompatible", &error));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), json);
    }

    void namedPresetsRoundTripWithoutGeometryAndProtectUnreadableFiles() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = dir.filePath("presets.json"); QString error;
        NamedPresets presets(file); QVERIFY(presets.load(&error));
        AdjustmentState state; state.exposure = 1.25; state.look.mode = "as-shot";
        state.geometry.quarterTurns = 1;
        QVERIFY(presets.save("Portrait", state, &error));
        QVERIFY(!presets.save("Portrait", state, &error)); // replacement uses a separate explicit operation
        NamedPresets reopened(file); QVERIFY(reopened.load(&error));
        AdjustmentState loaded; QVERIFY(reopened.get("Portrait", &loaded));
        QCOMPARE(loaded.exposure, 1.25); QCOMPARE(loaded.look.mode, QStringLiteral("as-shot"));
        QCOMPARE(loaded.geometry.quarterTurns, 0);
        QVERIFY(reopened.remove("Portrait", &error));
        QVERIFY(!reopened.get("Portrait", &loaded));
        QFile corrupt(file); QVERIFY(corrupt.open(QIODevice::WriteOnly));
        const QByteArray unknown = R"({"schema":99,"presets":{}})";
        QCOMPARE(corrupt.write(unknown), unknown.size()); corrupt.close();
        QVERIFY(!reopened.load(&error)); QVERIFY(!reopened.save("New", state, &error));
        QVERIFY(corrupt.open(QIODevice::ReadOnly)); QCOMPARE(corrupt.readAll(), unknown);
    }

    void namedPresetManagementAndPortableFilesPreserveIntent() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QString error; NamedPresets presets(dir.filePath("presets.json")); QVERIFY(presets.load(&error));
        AdjustmentState state; state.exposure=.5; state.look.mode="as-shot"; state.geometry.quarterTurns=1;
        QVERIFY(presets.save("First",state,&error)); QVERIFY(presets.save("Other",state,&error));
        QVERIFY(!presets.rename("First","Other",&error)); QVERIFY(presets.rename("First","Renamed",&error));
        state.exposure=1.5; QVERIFY(presets.replace("Renamed",state,&error)); QVERIFY(!presets.replace("Missing",state,&error));
        AdjustmentState loaded; QVERIFY(presets.get("Renamed",&loaded)); QCOMPARE(loaded.exposure,1.5); QCOMPARE(loaded.geometry.quarterTurns,0);
        const auto path=dir.filePath("portable.jixelpreset.json"); QVERIFY(presets.exportFile("Renamed",path,&error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll(); file.close();
        QVERIFY(!presets.exportFile("Renamed",path,&error)); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes); file.close();
        NamedPresets imported(dir.filePath("imported.json")); QVERIFY(imported.load(&error)); QVERIFY(imported.importFile(path,{},&error));
        QVERIFY(imported.get("Renamed",&loaded)); QCOMPARE(loaded.exposure,1.5); QCOMPARE(loaded.look.mode,QStringLiteral("as-shot"));
        QVERIFY(loaded.look.code.isEmpty()); QVERIFY(!imported.importFile(path,{},&error)); QVERIFY(imported.importFile(path,"Imported copy",&error));
        NamedPresets reopened(dir.filePath("imported.json")); QVERIFY(reopened.load(&error)); QCOMPARE(reopened.names().size(),2);
        auto invalid=QJsonDocument::fromJson(bytes).object(); invalid.insert("engine","future-engine");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(invalid).toJson()); file.close();
        QVERIFY(!reopened.importFile(path,"Invalid",&error)); QCOMPARE(reopened.names().size(),2);
        QFile protectedFile(dir.filePath("imported.json")); QVERIFY(protectedFile.open(QIODevice::ReadOnly)); const auto before=protectedFile.readAll(); protectedFile.close();
        state.look.error="invalid"; QVERIFY(!reopened.replace("Renamed",state,&error));
        QVERIFY(protectedFile.open(QIODevice::ReadOnly)); QCOMPARE(protectedFile.readAll(),before);
    }

    void controllerPresetManageImportExportLeavesDevelopAndOriginalUntouched() {
        const auto oldName=QCoreApplication::applicationName(); QCoreApplication::setApplicationName("JixelLightPresetManage-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto data=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto restore=qScopeGuard([&] { QDir(data).removeRecursively(); QCoreApplication::setApplicationName(oldName); });
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path=dir.filePath("source.png"); QVERIFY(image.save(path)); QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly)); const auto bytes=source.readAll(); source.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(path))); controller.setExposure(.5); controller.finishInteraction();
        QVERIFY(controller.saveNamedPreset("First")); QVERIFY(controller.renameNamedPreset("First","Renamed"));
        controller.setExposure(1.5); controller.finishInteraction(); controller.rotatePhoto(1); controller.setRating(4);
        const auto steps=controller.editHistory().size(); QVERIFY(controller.replaceNamedPreset("Renamed"));
        QCOMPARE(controller.editHistory().size(),steps); QCOMPARE(controller.exposure(),1.5); QCOMPARE(controller.currentRating(),4);
        const auto exported=QUrl::fromLocalFile(dir.filePath("preset.jixelpreset.json")); QVERIFY(controller.exportNamedPreset("Renamed",exported));
        QVERIFY(!controller.exportNamedPreset("Renamed",QUrl::fromLocalFile(path))); QVERIFY(!controller.exportNamedPreset("Renamed",exported));
        QVERIFY(controller.importNamedPreset(exported,"Imported")); QCOMPARE(controller.editHistory().size(),steps);
        controller.setExposure(-.5); controller.finishInteraction(); QVERIFY(controller.applyNamedPreset("Imported"));
        QCOMPARE(controller.exposure(),1.5); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),1); QCOMPARE(controller.currentRating(),4);
        controller.undo(); QCOMPARE(controller.exposure(),-.5); QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),bytes);
    }

    void controllerNamedPresetPreservesGeometryCurationAndUndo() {
        const auto oldName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("JixelLightPresetTest-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto restoreName = qScopeGuard([&] { QDir(data).removeRecursively(); QCoreApplication::setApplicationName(oldName); });
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("photo.png"); QVERIFY(image.save(path));
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        controller.setExposure(1.5); controller.finishInteraction();
        QVERIFY(controller.saveNamedPreset("Portrait"));
        controller.setExposure(-1); controller.finishInteraction(); controller.rotatePhoto(1);
        controller.setRating(4); controller.setFlag("pick");
        QVERIFY(controller.applyNamedPreset("Portrait")); QCOMPARE(controller.exposure(), 1.5);
        QCOMPARE(controller.currentRating(), 4); QCOMPARE(controller.currentFlag(), QStringLiteral("pick"));
        QCOMPARE(controller.geometry()["quarterTurns"].toInt(), 1);
        controller.undo(); QCOMPARE(controller.exposure(), -1.0);
        QCOMPARE(controller.geometry()["quarterTurns"].toInt(), 1);
        PhotoController reopened(nullptr); QCOMPARE(reopened.presetNames(), QStringList{"Portrait"});
        QVERIFY(reopened.removeNamedPreset("Portrait")); QVERIFY(reopened.presetNames().isEmpty());
    }

    void projectHistoryRejectsUnknownSchemaWithoutSwitchingWriter() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ProjectDatabase candidate; QVERIFY(candidate.create(dir.path(), "Future"));
        AdjustmentState state; EditHistory history; history.initialize(state);
        QVERIFY(candidate.updateBatch({{"test.ARW", state}}, {{"test.ARW", history}}));
        QVERIFY(candidate.flush());
        const QString connection = "future-history-fixture";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(QDir(candidate.projectPath()).filePath("Project.db")); QVERIFY(db.open());
            QSqlQuery query(db); auto json = state.toJson(); auto invalid = history.toJson();
            invalid.insert("schema", 99); json.insert("_history", invalid);
            query.prepare("UPDATE photos SET adjustment_json=?");
            query.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
            QVERIFY(query.exec()); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        ProjectDatabase active; QVERIFY(active.create(dir.path(), "Active"));
        QVector<ProjectDatabase::SavedPhoto> photos;
        QVERIFY(!active.open(candidate.projectPath(), &photos));
        QCOMPARE(active.projectName(), QStringLiteral("Active"));
        QVERIFY(active.updateAdjustment("active.ARW", state)); QVERIFY(active.flush());
    }

    void stageDependenciesIsolateColorFromGeometry() {
        PrepareRequest request;
        request.image = QImage(256,128,QImage::Format_RGBA64);
        request.image.fill(Qt::gray);
        request.fullResolution = true;
        AdjustmentState state;
        const auto before = StageGraph::describe("source", request, state, true, 0);
        state.exposure = 1;
        const auto edited = StageGraph::describe("source", request, state, true, 0);
        QCOMPARE(before["prepare"], edited["prepare"]);
        QVERIFY(before["render"] != edited["render"]);
        QVERIFY(before["scopes"] != edited["scopes"]);
        request.zoom = 1;
        QVERIFY(before["prepare"] != StageGraph::describe("source",request,state,true,0)["prepare"]);
        const auto first = preparePreview(request, {});
        const auto second = preparePreview(request, {});
        QCOMPARE(first.normal, second.normal);
        QCOMPARE(first.gpu.cacheKey(), second.gpu.cacheKey());
        auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(preparePreview(request, cancel).normal.isNull());
        request.image.fill(Qt::red);
        const auto changed = preparePreview(request, {});
        QVERIFY(changed.normal != first.normal);
    }

    void stageOutputFingerprintsIgnoreRowPaddingAndSeparateIcc() {
        QByteArray a(24,'\0'), b(24,'\0');
        for (int row=0;row<2;++row) for (int byte=0;byte<9;++byte) a[row*12+byte]=b[row*12+byte]=char(20+byte);
        for (int row=0;row<2;++row) for (int byte=9;byte<12;++byte) b[row*12+byte]=char(240);
        QImage first(reinterpret_cast<uchar *>(a.data()),3,2,12,QImage::Format_RGB888);
        QImage second(reinterpret_cast<uchar *>(b.data()),3,2,12,QImage::Format_RGB888);
        const auto before=StageGraph::outputFingerprint(first);
        QCOMPARE(before.value("pixel_sha256"),StageGraph::outputFingerprint(second).value("pixel_sha256"));
        second.setColorSpace(QColorSpace::AdobeRgb); const auto tagged=StageGraph::outputFingerprint(second);
        QCOMPARE(before.value("pixel_sha256"),tagged.value("pixel_sha256")); QVERIFY(before.value("icc_sha256")!=tagged.value("icc_sha256"));
        first.setPixelColor(0,0,Qt::red); QVERIFY(before.value("pixel_sha256")!=StageGraph::outputFingerprint(first).value("pixel_sha256"));
        QVERIFY(before.value("pixel_sha256")!=StageGraph::outputFingerprint(second.copy(0,0,2,2)).value("pixel_sha256"));
        QVERIFY(!StageGraph::outputFingerprint({}).value("available").toBool());
    }

    void diagnosticZipContainsStructuredStageOutputs() {
        QImage image(2,2,QImage::Format_RGBA64); image.fill(Qt::gray);
        const QJsonObject outputs{{"schema",1},{"source",StageGraph::outputFingerprint(image)}};
        const auto path=DiagnosticBundle::create(image,"synthetic.png",{}, {},0,0,"test",{},outputs); QVERIFY(!path.isEmpty());
        const auto cleanup=qScopeGuard([&] { QFile::remove(path); });
        QCOMPARE(QJsonDocument::fromJson(storedZipEntry(path,"stage_outputs.json")).object(),outputs);
        QCOMPARE(QJsonDocument::fromJson(storedZipEntry(path,"manifest.json")).object().value("stage_outputs").toObject(),outputs);
    }

    void controllerDiagnosticHashesUseCurrentParametersAndGeometry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(4,2,QImage::Format_RGB32); image.fill(QColor(64,96,128));
        const auto source=dir.filePath("source.png"); QVERIFY(image.save(source));
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(source)));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && controller.previewReady(),10000);
        QStringList paths; const auto cleanup=qScopeGuard([&] { for(const auto &path:paths) QFile::remove(path); });
        const auto baseline=controller.reportBug(); paths << baseline; QVERIFY(!baseline.isEmpty());
        const auto before=QJsonDocument::fromJson(storedZipEntry(baseline,"stage_outputs.json")).object();
        QVERIFY(before.value("source").toObject().value("available").toBool());
        QCOMPARE(before.value("prepared_preview").toObject().value("width").toInt(),4);
        controller.setExposure(.5); // Deliberately capture before asynchronous display refinement.
        const auto edited=controller.reportBug(); paths << edited; QVERIFY(!edited.isEmpty()); QVERIFY(edited!=baseline);
        const auto after=QJsonDocument::fromJson(storedZipEntry(edited,"stage_outputs.json")).object();
        QCOMPARE(after.value("source"),before.value("source")); QCOMPARE(after.value("prepared_preview"),before.value("prepared_preview"));
        QVERIFY(after.value("cpu_srgb_output").toObject().value("pixel_sha256")!=before.value("cpu_srgb_output").toObject().value("pixel_sha256"));
        QCOMPARE(QJsonDocument::fromJson(storedZipEntry(edited,"manifest.json")).object().value("adjustments").toObject().value("exposure").toDouble(),.5);
        controller.setCrop(0,0,.5,1);
        const auto cropped=controller.reportBug(); paths << cropped; QVERIFY(!cropped.isEmpty());
        const auto geometry=QJsonDocument::fromJson(storedZipEntry(cropped,"stage_outputs.json")).object();
        QCOMPARE(geometry.value("source"),before.value("source"));
        QCOMPARE(geometry.value("prepared_preview").toObject().value("width").toInt(),2);
        QCOMPARE(geometry.value("cpu_srgb_output").toObject().value("width").toInt(),2);
        QImage capture; QVERIFY(capture.loadFromData(storedZipEntry(cropped,"current_preview.png"),"PNG"));
        QCOMPARE(StageGraph::outputFingerprint(capture).value("pixel_sha256"),geometry.value("cpu_srgb_output").toObject().value("pixel_sha256"));
    }

    void pngExportPreserves16BitsAndIccAndCancellation() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage source(17,9,QImage::Format_RGBA64);
        source.fill(QColor::fromRgbF(.17,.23,.29));
        AdjustmentState state; state.exposure = .5;
        const auto space = ColorManagement::OutputSpace::ProPhotoRgb;
        QString error;
        const QString path = dir.filePath("export.png");
        QVERIFY2(exportPng16(source,state,path,space,{},&error),qPrintable(error));
        QImageReader reader(path);
        const auto reopened = reader.read();
        QCOMPARE(reopened.depth(), 64);
        QCOMPARE(reopened.colorSpace(), ColorManagement::colorSpace(space));
        const auto expected = ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,false,0));
        const auto actualPixel = reopened.pixelColor(4,4).rgba64(), expectedPixel = expected.pixelColor(4,4).rgba64();
        QCOMPARE(actualPixel, expectedPixel);
        const QString rawPath = dir.filePath("raw.png");
        QVERIFY2(exportPng16(source,state,rawPath,space,{},&error,true,.3f),qPrintable(error));
        const auto rawExpected = ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,true,.3f));
        QCOMPARE(QImage(rawPath).pixelColor(4,4).rgba64(), rawExpected.pixelColor(4,4).rgba64());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray before = file.readAll(); file.close();
        auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!exportPng16(source,state,path,space,cancel,&error));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),before);
    }

    void commandRegistryRejectsInvalidAndMatchesUiRanges() {
        AdjustmentState state;
        QString error;
        QVERIFY(CommandRegistry::execute(state,{{"command","develop.set"},{"parameter","exposure"},{"value",8}},&error));
        QCOMPARE(state.exposure,5.0);
        const auto before=state.toJson();
        QVERIFY(!CommandRegistry::execute(state,{{"command","develop.set"},{"parameter","unknown"},{"value",1}},&error));
        QVERIFY(!error.isEmpty()); QCOMPARE(state.toJson(),before);
        QVERIFY(!CommandRegistry::set(state,"exposure",std::numeric_limits<double>::infinity(),&error));
        QCOMPARE(state.toJson(),before);
        QVERIFY(!CommandRegistry::execute(state,{{"command","develop.set"},{"parameter","exposure"},{"value","2"}},&error));
        QCOMPARE(CommandRegistry::schema()["parameters"].toArray().size(),12);
    }

    void geometryPreservesPixelsAndRestoresOldProjectDefaults() {
        QImage source(4,2,QImage::Format_RGBA64);
        source.setColorSpace(ColorManagement::colorSpace(ColorManagement::OutputSpace::ProPhotoRgb));
        for(int y=0;y<2;++y)for(int x=0;x<4;++x) source.setPixelColor(x,y,QColor(20+x*30,40+y*60,80));
        const auto original=source.copy();
        GeometryState geometry;
        QCOMPARE(geometry.apply(source).cacheKey(),source.cacheKey());
        geometry.quarterTurns=1;
        const auto rotated=geometry.apply(source);
        QCOMPARE(rotated.size(),QSize(2,4));
        QCOMPARE(rotated.pixelColor(1,0),source.pixelColor(0,0));
        QCOMPARE(rotated.colorSpace(),source.colorSpace());
        geometry.quarterTurns=0; geometry.crop=QRectF(.25,0,.5,1);
        QCOMPARE(geometry.apply(source),source.copy(1,0,2,2));
        geometry.flipHorizontal=true;
        QCOMPARE(geometry.apply(source),source.copy(1,0,2,2).mirrored(true,false));
        QCOMPARE(source,original);
        AdjustmentState state; state.geometry=geometry;
        QCOMPARE(AdjustmentState::fromJson(state.toJson()).geometry.toJson(),geometry.toJson());
        QCOMPARE(AdjustmentState::fromJson({}).geometry.crop,QRectF(0,0,1,1));
        QVERIFY(!GeometryState::validCrop(-.1,0,.5,1));
        QVERIFY(!GeometryState::validCrop(0,0,0,1));
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString error;
        QVERIFY2(exportPng16(source,state,dir.filePath("cropped.png"),ColorManagement::OutputSpace::SRgb,{},&error),qPrintable(error));
        QCOMPARE(QImage(dir.filePath("cropped.png")).size(),QSize(2,2));
        PrepareRequest request; request.image=source; request.geometry=geometry;
        QCOMPARE(preparePreview(request,{}).normal,geometry.apply(source));
    }

    void identityPipelinePreservesDisplayPixel() {
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.fill(QColor(64, 128, 192, 255));
        const QImage out = ImagePipeline::process(image, {});
        const QColor pixel = out.pixelColor(0,0);
        QVERIFY(std::abs(pixel.red() - 64) <= 2);
        QVERIFY(std::abs(pixel.green() - 128) <= 2);
        QVERIFY(std::abs(pixel.blue() - 192) <= 2);
        QCOMPARE(out.format(), QImage::Format_RGBA64);
        QCOMPARE(out.colorSpace(), QColorSpace(QColorSpace::SRgb));
    }

    void srgbExposureHappensInLinearLight() {
        QImage image(1, 1, QImage::Format_RGBA64);
        image.fill(QColor(50,50,50));
        AdjustmentState state;
        state.exposure = 1.0;
        const auto out = ImagePipeline::process(image, state);
        const int value = out.pixelColor(0,0).red();
        QVERIFY(value > 65);
        QVERIFY(value < 80);
    }

    void rawExposureUsesLinearProPhotoStops() {
        const QImage image = sceneGrayImage(ProcessingPlan::RawNeutralSceneGray);
        const QImage baseline = ImagePipeline::process(image, {}, ImagePipeline::InputEncoding::LinearProPhoto);
        AdjustmentState plusOne;
        plusOne.exposure = 1.0;
        const QImage brighter = ImagePipeline::process(image, plusOne, ImagePipeline::InputEncoding::LinearProPhoto);

        const int baseline8 = baseline.pixelColor(0,0).red();
        const int plusOne8 = brighter.pixelColor(0,0).red();
        QVERIFY2(baseline8 >= 114 && baseline8 <= 121, qPrintable(QString::number(baseline8)));
        QVERIFY2(plusOne8 >= 148 && plusOne8 <= 155, qPrintable(QString::number(plusOne8)));
        QVERIFY(plusOne8 > baseline8 + 28);
        QVERIFY(plusOne8 < baseline8 * 2);
    }

    void rawBaseRenderingIsSeparateFromLinearRepresentation() {
        const auto rawPlan = ProcessingPlan::compile({}, ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb, true, 0.0f);
        const auto nonRawLinearPlan = ProcessingPlan::compile({}, ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb, false, 0.0f);
        QVERIFY(rawPlan.rawSource);
        QVERIFY(!nonRawLinearPlan.rawSource);
        QCOMPARE(rawPlan.data[ProcessingPlan::Flags].w, 1.0f);
        QCOMPARE(nonRawLinearPlan.data[ProcessingPlan::Flags].w, 0.0f);

        const QImage linear = sceneGrayImage(0.18);
        const auto rawRendered=ImagePipeline::processWithPlan(linear,rawPlan);
        const auto nonRawRendered=ImagePipeline::processWithPlan(linear,nonRawLinearPlan);
        const int rawV=rawRendered.pixelColor(0,0).red();
        const int nonRawV=nonRawRendered.pixelColor(0,0).red();
        QVERIFY2(rawV >= 200 && rawV <= 210,qPrintable(QString::number(rawV)));
        QVERIFY2(nonRawV >= 115 && nonRawV <= 122,qPrintable(QString::number(nonRawV)));
        QVERIFY(rawV > nonRawV + 70);
        QCOMPARE(rawRendered.text("JixelLightPipeline"),QString::fromLatin1(ProcessingPlan::EngineVersion));
        QVERIFY(rawRendered.text("JixelLightBaseRendering").contains("Jixel Neutral v2"));
        QVERIFY(!rawRendered.text("JixelLightBaseRendering").contains("+2.5 EV"));
        QCOMPARE(nonRawRendered.text("JixelLightBaseRendering"),QStringLiteral("none"));
    }

    void rawCameraBaselineIsSeparateFromUserExposure() {
        const QImage image=sceneGrayImage(ProcessingPlan::RawNeutralSceneGray);
        const auto basePlusOne=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb,true,1.0f);
        const auto cameraRendered=ImagePipeline::processWithPlan(image,basePlusOne);
        AdjustmentState userPlusOne; userPlusOne.exposure=1.0;
        const auto userPlan=ProcessingPlan::compile(userPlusOne,ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb,true,0.0f);
        const auto userRendered=ImagePipeline::processWithPlan(image,userPlan);
        QCOMPARE(basePlusOne.state.exposure,0.0);
        QVERIFY(std::abs(cameraRendered.pixelColor(0,0).red()-userRendered.pixelColor(0,0).red())<=1);
        QVERIFY(cameraRendered.text("JixelLightBaseRendering").contains("1.000 EV"));
    }

    void rawBaseRenderingKeepsOutOfSrgbHighlightHueOrdering() {
        // This ProPhoto fixture is already outside sRGB before any creative
        // adjustment (its linear-sRGB red channel is negative). The correct
        // display result may therefore land on the sRGB boundary; protect hue
        // ordering and continuity rather than alpha.10's arbitrary red code.
        QImage image(1,1,QImage::Format_RGBA64);
        auto *px=reinterpret_cast<QRgba64 *>(image.scanLine(0));
        px[0]=QRgba64::fromRgba64(23559,59073,40796,65535);
        AdjustmentState state;state.hue=-23;state.saturation=18;state.vibrance=22;
        state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8;
        const QColor out=ImagePipeline::process(image,state,ImagePipeline::InputEncoding::LinearProPhoto).pixelColor(0,0);
        QVERIFY2(out.green()>240,qPrintable(QString("red=%1 green=%2 blue=%3").arg(out.red()).arg(out.green()).arg(out.blue())));
        QVERIFY(out.blue()>100);
        QVERIFY(out.green()>out.blue());
        QVERIFY(out.blue()>out.red());
    }

    void saturationRunsInsideWorkingPipeline() {
        QImage image(1, 1, QImage::Format_RGBA64);
        image.fill(QColor(180, 105, 80));
        const QColor baseline = ImagePipeline::process(image, {}).pixelColor(0,0);
        AdjustmentState state;
        state.saturation = 70.0;
        const QColor saturated = ImagePipeline::process(image, state).pixelColor(0,0);
        QVERIFY(channelSpread(saturated) > channelSpread(baseline));
    }

    void masterCurveChangesRenderedMidtones() {
        QImage image(1, 1, QImage::Format_RGBA64);
        image.fill(QColor(128,128,128));
        const int baseline = ImagePipeline::process(image, {}).pixelColor(0,0).red();
        AdjustmentState state;
        state.masterCurve[1] = 0.10;
        const int darker = ImagePipeline::process(image, state).pixelColor(0,0).red();
        QVERIFY(darker < baseline - 10);
    }

    void colorAndCurveStateRoundTripsThroughJson() {
        AdjustmentState state;
        state.hue = 15.0;
        state.saturation = 22.0;
        state.vibrance = 31.0;
        state.highlightRecovery = 45.0;
        state.hslHue[0] = -12.0;
        state.hslSaturation[5] = 38.0;
        state.hslLuminance[1] = 19.0;
        state.masterCurve[2] = 0.61;
        state.redCurve[3] = 0.82;
        const AdjustmentState restored = AdjustmentState::fromJson(state.toJson());
        QCOMPARE(restored.hue, state.hue);
        QCOMPARE(restored.saturation, state.saturation);
        QCOMPARE(restored.vibrance, state.vibrance);
        QCOMPARE(restored.highlightRecovery, state.highlightRecovery);
        QCOMPARE(restored.hslHue[0], state.hslHue[0]);
        QCOMPARE(restored.hslSaturation[5], state.hslSaturation[5]);
        QCOMPARE(restored.hslLuminance[1], state.hslLuminance[1]);
        QCOMPARE(restored.masterCurve[2], state.masterCurve[2]);
        QCOMPARE(restored.redCurve[3], state.redCurve[3]);
    }

    void histogramUses1024BinsAndCountsPixels() {
        QImage image(10, 10, QImage::Format_RGBA64);
        image.fill(QColor(128,64,32));
        const auto scopes = ScopesEngine::analyze(image, 1024);
        QCOMPARE(scopes.red.size(), 1024);
        qulonglong total = 0;
        for (const auto &v : scopes.red) total += v.toULongLong();
        QCOMPARE(total, qulonglong(100));
    }

    void namedOutputProfilesAreValidRgbIcc() {
        for (const QString &key : ColorManagement::keys()) {
            const auto space = ColorManagement::fromKey(key);
            const QByteArray profile = ColorManagement::iccProfile(space);
            QVERIFY2(profile.size() > 100, qPrintable(QStringLiteral("ICC profile missing for %1").arg(key)));
            QString description;
            QVERIFY2(ColorManagement::validateIcc(profile, &description), qPrintable(QStringLiteral("LittleCMS rejected %1").arg(key)));
        }
    }

    void colorManagedConversionAssignsDestinationProfile() {
        QImage image(4, 4, QImage::Format_RGBA64);
        image.fill(QColor(220, 80, 55));
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));

        const auto target = ColorManagement::OutputSpace::DisplayP3;
        const QImage converted = ColorManagement::convertFromSrgb(image, target);
        QVERIFY(!converted.isNull());
        QCOMPARE(converted.format(), QImage::Format_RGBA64);
        QCOMPARE(converted.colorSpace(), ColorManagement::colorSpace(target));
        QCOMPARE(converted.text(QStringLiteral("JixelLightICCManaged")), QStringLiteral("true"));
        QVERIFY(ColorManagement::validateIcc(converted.colorSpace().iccProfile()));
    }

    void rawExtensionsAreRecognized() {
        QVERIFY(RawDecoder::isRawFile("DSC00001.ARW"));
        QVERIFY(RawDecoder::isRawFile("IMG_0001.CR3"));
        QVERIFY(RawDecoder::isRawFile("DSC_0001.NEF"));
        QVERIFY(RawDecoder::isRawFile("FUJI0001.RAF"));
        QVERIFY(RawDecoder::isRawFile("photo.DNG"));
        QVERIFY(!RawDecoder::isRawFile("photo.jpg"));
    }

    void realRawMetadataSmoke() {
        const QString rawPath = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (rawPath.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");

        QString error;
        const QVariantMap metadata = MetadataReader::read(rawPath, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!metadata.isEmpty());
        QVERIFY(metadata.contains(QStringLiteral("make")));
        QVERIFY(metadata.contains(QStringLiteral("model")));
        QVERIFY(!metadata.value(QStringLiteral("make")).toString().isEmpty());
        QVERIFY(!metadata.value(QStringLiteral("model")).toString().isEmpty());
        QVERIFY(metadata.value(QStringLiteral("pixelWidth")).toULongLong() > 1000);
        QVERIFY(metadata.value(QStringLiteral("pixelHeight")).toULongLong() > 1000);
    }

    void realRawDecodeSmoke() {
        const QString rawPath = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (rawPath.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");

        QString error;
        RawMetadata metadata;
        const QImage image = RawDecoder::decode(rawPath, &error, &metadata);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(image.format(), QImage::Format_RGBA64);
        QCOMPARE(image.text(QStringLiteral("JixelLightWorkingSpace")), QStringLiteral("Linear ProPhoto RGB"));
        QVERIFY(image.width() > 1000);
        QVERIFY(image.height() > 1000);
        QCOMPARE(metadata.bitsPerChannel, 16);
        QCOMPARE(metadata.workingSpace, QStringLiteral("Linear ProPhoto RGB"));
        QVERIFY(metadata.cameraMatrixEnabled);
        QVERIFY(metadata.cameraWhiteBalanceEnabled);
        QVERIFY(!metadata.highlightBlendEnabled);
        QCOMPARE(metadata.highlightMode,1);
        QVERIFY(std::abs(metadata.adjustMaximumThreshold-0.75f)<1.0e-6f);
        QCOMPARE(image.text(QStringLiteral("JixelLightLibRawHighlightMode")),QStringLiteral("1 / unclip"));
        QCOMPARE(image.text(QStringLiteral("JixelLightAdjustMaximumThreshold")),QStringLiteral("0.75"));
        QVERIFY(!metadata.make.isEmpty());
        QVERIFY(!metadata.model.isEmpty());
    }

    void controllerImportsRealRawIntoWideGamutPipeline() {
        const QString rawPath = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (rawPath.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");

        PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(rawPath)));
        QCOMPARE(controller.library().size(), 1);
        QVERIFY(controller.hasImage());
        QVERIFY(controller.currentIsRaw());
        QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady() && !controller.previewUrl().isEmpty(), 30000);
        QVERIFY(controller.pipelineDescription().contains(QStringLiteral("Linear ProPhoto RGB")));
        QVERIFY(controller.pipelineDescription().contains(QStringLiteral("ICC sRGB Preview")));
        QVERIFY(controller.currentMetadata().contains(QStringLiteral("make")));
        QVERIFY(controller.currentMetadata().contains(QStringLiteral("model")));
        QCOMPARE(controller.currentMetadata().value(QStringLiteral("workingSpace")).toString(), QStringLiteral("Linear ProPhoto RGB"));
        QCOMPARE(controller.currentMetadata().value(QStringLiteral("bitDepth")).toInt(), 16);
        QVERIFY(controller.currentMetadata().contains(QStringLiteral("rawBaseExposureStops")));
        QCOMPARE(controller.currentMetadata().value(QStringLiteral("libRawHighlightMode")).toInt(),1);

        controller.setSaturation(25.0);
        controller.setColorMix(5, 1, 30.0);
        controller.setCurvePoint(0, 2, 0.58);
        QCOMPARE(controller.saturation(), 25.0);
        QCOMPARE(controller.hslSaturation().at(5).toDouble(), 30.0);
        QCOMPARE(controller.masterCurve().at(2).toDouble(), 0.58);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString exportPath = dir.filePath(QStringLiteral("p3-export.jpg"));
        QSignalSpy exportedSignal(&controller, &PhotoController::exportFinished);
        QVERIFY(controller.exportCurrent(QUrl::fromLocalFile(exportPath), QStringLiteral("display-p3"), 91));
        QTRY_COMPARE_WITH_TIMEOUT(exportedSignal.size(), 1, 60000);
        QCOMPARE(exportedSignal.first().at(0).toInt(), 1);
        QCOMPARE(exportedSignal.first().at(1).toInt(), 0);
        QVERIFY(QFile::exists(exportPath));

        QImageReader reader(exportPath);
        const QImage exported = reader.read();
        QVERIFY2(!exported.isNull(), qPrintable(reader.errorString()));
        QVERIFY(exported.colorSpace().isValid());
        QCOMPARE(exported.colorSpace(), ColorManagement::colorSpace(ColorManagement::OutputSpace::DisplayP3));
        QVERIFY(ColorManagement::validateIcc(exported.colorSpace().iccProfile()));
    }

    void projectCanOpenAnExistingCatalogWithEdits() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectDatabase created;
        QVERIFY(created.create(dir.path(), QStringLiteral("Resume")));
        AdjustmentState state;
        state.exposure = 1.25;
        state.highlights = -40;
        state.hslHue[2] = 8;
        const QString original = QDir(dir.path()).filePath(QStringLiteral("sample.ARW"));
        QVERIFY(created.addOrUpdatePhoto(original, state));
        QVERIFY(created.flush());
        ProjectDatabase::PhotoCuration picked {4, QStringLiteral("pick")};
        QVERIFY(created.updateCurationBatch({{original, picked}}));
        QVERIFY(created.flush());

        ProjectDatabase reopened;
        QVector<ProjectDatabase::SavedPhoto> loaded;
        QVERIFY2(reopened.open(QDir(dir.path()).filePath(QStringLiteral("Resume.jlp")), &loaded),
                 qPrintable(reopened.lastError()));
        QCOMPARE(reopened.projectName(), QStringLiteral("Resume"));
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().path, original);
        QCOMPARE(loaded.first().adjustments.exposure, 1.25);
        QCOMPARE(loaded.first().adjustments.highlights, -40.0);
        QCOMPARE(loaded.first().adjustments.hslHue[2], 8.0);
        QCOMPARE(loaded.first().rating, 4);
        QCOMPARE(loaded.first().flag, QStringLiteral("pick"));
        AdjustmentState changed = loaded.first().adjustments;
        changed.exposure = -0.75;
        QVERIFY(reopened.updateAdjustment(original, changed));
        QVERIFY(reopened.flush());
        QVERIFY2(reopened.open(QDir(dir.path()).filePath(QStringLiteral("Resume.jlp")), &loaded),
                 qPrintable(reopened.lastError()));
        QCOMPARE(loaded.first().adjustments.exposure, -0.75);
    }

    void projectSnapshotPreservesVersionsRedoAndAnActiveWriter() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ProjectDatabase writer, source;
        QVERIFY(writer.create(dir.path(),"Writer")); QVERIFY(source.create(dir.path(),"Snapshot"));
        const auto active=dir.filePath("active.png"), original=dir.filePath("original.png");
        AdjustmentState activeState; activeState.exposure=.7; QVERIFY(writer.updateAdjustment(active,activeState));
        AdjustmentState state; state.exposure=.25; EditHistory history; history.initialize({}); history.record(state,"exposure");
        auto redo=state; redo.exposure=1.25; history.record(redo,"exposure"); history.undo();
        QVERIFY(source.updateBatch({{original,state}},{{original,history}}));
        auto copy=state; copy.exposure=-.5; copy.geometry.crop={0,0,.5,1}; copy.look.mode="manual"; copy.look.code="FL";
        EditHistory copyHistory; copyHistory.initialize(copy); CatalogTags tags; tags.keywords={"portrait"};
        const auto key="jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(source.addVirtualCopy(key,original,"Alternative",copy,copyHistory,tags,{4,"pick"})); QVERIFY(source.flush());
        const auto writerPath=writer.projectPath(), sourcePath=source.projectPath();
        QVector<ProjectDatabase::SavedPhoto> loaded;
        QVERIFY2(writer.readSnapshot(sourcePath,&loaded),qPrintable(writer.lastError()));
        QCOMPARE(writer.projectPath(),writerPath); QCOMPARE(writer.projectName(),QStringLiteral("Writer")); QVERIFY(writer.isOpen());
        QCOMPARE(loaded.size(),2); QCOMPARE(loaded[0].adjustments.exposure,.25); QVERIFY(loaded[0].history.canRedo());
        QCOMPARE(loaded[0].history.redo().exposure,1.25); QCOMPARE(loaded[1].copyKey,key); QCOMPARE(loaded[1].versionName,QStringLiteral("Alternative"));
        QCOMPARE(loaded[1].adjustments.geometry.crop,QRectF(0,0,.5,1)); QCOMPARE(loaded[1].adjustments.look.code,QStringLiteral("FL"));
        QCOMPARE(loaded[1].tags.keywords,QStringList{"portrait"}); QCOMPARE(loaded[1].rating,4);
        ProjectDatabase readonly; QVERIFY(readonly.readSnapshot(sourcePath,&loaded)); QVERIFY(!readonly.isOpen());
        QVERIFY(!readonly.updateAdjustment(original,{}));
        QVERIFY(!writer.readSnapshot(dir.filePath("missing.jlp"),&loaded)); QCOMPARE(loaded.size(),2);
        activeState.exposure=.8; QVERIFY(writer.updateAdjustment(active,activeState)); QVERIFY(writer.flush());
        QVERIFY(writer.open(writerPath,&loaded)); QCOMPARE(loaded.size(),1); QCOMPARE(loaded[0].adjustments.exposure,.8);
    }

    void legacyProjectCanReadWithoutCurationAndUpgradeOnFirstRating() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString folder = QDir(dir.path()).filePath(QStringLiteral("Legacy.jlp"));
        QVERIFY(QDir().mkpath(folder));
        const QString dbPath = QDir(folder).filePath(QStringLiteral("Project.db"));
        const QString conn = QStringLiteral("jixellight-legacy-fixture");
        const QString photoPath = QDir(folder).filePath(QStringLiteral("older.ARW"));
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            database.setDatabaseName(dbPath);
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT)"));
            QVERIFY(query.exec("INSERT INTO meta(key,value) VALUES('project_name','Legacy')"));
            QVERIFY(query.exec("CREATE TABLE photos(path TEXT PRIMARY KEY, imported_at TEXT DEFAULT CURRENT_TIMESTAMP, adjustment_json TEXT NOT NULL DEFAULT '{}')"));
            query.prepare("INSERT INTO photos(path, adjustment_json) VALUES(?, ?)");
            query.addBindValue(photoPath);
            AdjustmentState fromOld;
            fromOld.exposure = 0.75;
            query.addBindValue(QString::fromUtf8(QJsonDocument(fromOld.toJson()).toJson(QJsonDocument::Compact)));
            QVERIFY(query.exec());
            database.close();
        }
        QSqlDatabase::removeDatabase(conn);
        ProjectDatabase restored;
        QVector<ProjectDatabase::SavedPhoto> loaded;
        QFile before(dbPath); QVERIFY(before.open(QIODevice::ReadOnly)); const auto original=before.readAll(); before.close();
        ProjectDatabase snapshot; QVector<ProjectDatabase::SavedPhoto> readonly;
        QVERIFY2(snapshot.readSnapshot(folder,&readonly),qPrintable(snapshot.lastError()));
        QVERIFY(!snapshot.isOpen()); QCOMPARE(readonly.size(),1); QCOMPARE(readonly[0].adjustments.exposure,.75);
        QVERIFY(before.open(QIODevice::ReadOnly)); QCOMPARE(before.readAll(),original); before.close();
        QVERIFY(!QDir(folder).exists("backups"));
        QVERIFY2(restored.open(folder, &loaded), qPrintable(restored.lastError()));
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].adjustments.exposure, 0.75);
        QCOMPARE(loaded[0].rating, 0);
        QCOMPARE(loaded[0].flag, QStringLiteral("none"));
        QVERIFY(restored.updateCurationBatch({{photoPath, {5, QStringLiteral("reject")}}}));
        QVERIFY(restored.flush());
        QVERIFY2(restored.open(folder, &loaded), qPrintable(restored.lastError()));
        QCOMPARE(loaded[0].rating, 5);
        QCOMPARE(loaded[0].flag, QStringLiteral("reject"));
        QCOMPARE(loaded[0].adjustments.exposure, 0.75);
    }

    void projectRejectsNonexistentDatabaseWithoutCreatingOne() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectDatabase db;
        QVector<ProjectDatabase::SavedPhoto> records;
        const QString invalid = QDir(dir.path()).filePath(QStringLiteral("Absent.jlp"));
        QVERIFY(!db.open(invalid, &records));
        QVERIFY(!QFileInfo::exists(QDir(invalid).filePath(QStringLiteral("Project.db"))));
        QVERIFY(records.isEmpty());
    }

    void failedProjectOpenMustNotPoisonCurrentProject() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectDatabase db;
        QVERIFY(db.create(dir.path(), QStringLiteral("Working")));
        AdjustmentState before;
        before.exposure = 0.3;
        const QString photo = QDir(dir.path()).filePath(QStringLiteral("existing.ARW"));
        QVERIFY(db.updateAdjustment(photo, before));
        QVERIFY(db.flush());

        QVector<ProjectDatabase::SavedPhoto> photos;
        const QString bad = QDir(dir.path()).filePath(QStringLiteral("DoesNotExist.jlp"));
        QVERIFY(!db.open(bad, &photos));
        QVERIFY(!db.lastError().isEmpty());
        // A bad destination must not switch projects or block shutdown saves.
        QCOMPARE(db.projectName(), QStringLiteral("Working"));
        QVERIFY(db.flush());
        AdjustmentState after = before;
        after.exposure = 1.3;
        QVERIFY(db.updateAdjustment(photo, after));
        QVERIFY(db.flush());
        QVERIFY2(db.open(QDir(dir.path()).filePath(QStringLiteral("Working.jlp")), &photos),
                 qPrintable(db.lastError()));
        QCOMPARE(photos.size(), 1);
        QCOMPARE(photos[0].adjustments.exposure, 1.3);
    }

    void projectCreationNeverOverwritesOrDropsCurrentWriter() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ProjectDatabase db; QVERIFY(db.create(dir.path(), "Existing"));
        AdjustmentState state; state.exposure = 0.75;
        QVERIFY(db.updateAdjustment("original.ARW", state)); QVERIFY(db.flush());
        const auto folder = db.projectPath();
        QVERIFY(!db.create(dir.path(), "Existing")); QCOMPARE(db.projectPath(), folder);
        QVERIFY(db.flush());
        QFile blocker(dir.filePath("file")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QVERIFY(!db.create(blocker.fileName(), "Unavailable")); QCOMPARE(db.projectPath(), folder);
        state.exposure = 1.75; QVERIFY(db.updateAdjustment("original.ARW", state)); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(folder, &photos));
        QCOMPARE(photos.size(), 1); QCOMPARE(photos[0].adjustments.exposure, 1.75);
    }

    void catalogBatchAnnotationsAndSelectionSurviveReopenWithoutDevelopChanges() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        PhotoController controller(nullptr);
        for (int i = 0; i < 3; ++i) { const auto path = dir.filePath(QString::number(i)+".png"); QVERIFY(image.save(path)); QVERIFY(controller.importFile(QUrl::fromLocalFile(path))); }
        controller.setExposure(1.25); controller.finishInteraction();
        QVERIFY(controller.setPhotoSelection({0,2,2})); QCOMPARE(controller.selectedIndices().size(),2);
        QVERIFY(!controller.setPhotoSelection({0,3})); QCOMPARE(controller.selectedIndices().size(),2);
        QVERIFY(!controller.setPhotoSelection({0,1.5}));
        QVERIFY(controller.setSelectionKeywords("travel, Sony, travel"));
        QVERIFY(controller.addSelectionToAlbum(" Japan "));
        QVERIFY(controller.setSelectionLabel("blue"));
        QVERIFY(controller.setSelectionRating(4)); QVERIFY(controller.setSelectionFlag("pick"));
        QVERIFY(!controller.setSelectionLabel("unknown"));
        QVERIFY(!controller.setSelectionKeywords(QString(81,'a')));
        QCOMPARE(controller.exposure(),1.25); QVERIFY(controller.canUndo());
        const auto untouched = controller.library()[1].toMap();
        QCOMPARE(untouched["rating"].toInt(),0); QVERIFY(untouched["keywords"].toStringList().isEmpty());
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Tags")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.albumNames(),QStringList{"Japan"});
        reopened.selectPhoto(2); QCOMPARE(reopened.currentKeywords(),(QStringList{"Sony","travel"}));
        QCOMPARE(reopened.currentColorLabel(),QStringLiteral("blue")); QCOMPARE(reopened.currentRating(),4);
        QCOMPARE(reopened.exposure(),0.0); // original edit belongs to photo 0
        QVERIFY(reopened.setPhotoSelection({0,2})); QVERIFY(reopened.removeSelectionFromAlbum("Japan"));
        QVERIFY(reopened.albumNames().isEmpty()); QVERIFY(reopened.flushEdits());
        QVERIFY(reopened.setPhotoSelection({})); QVERIFY(!reopened.setSelectionRating(3));
    }

    void legacyCatalogTagsMigrationHasWalConsistentBackupAndRecoversFromFailure() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ProjectDatabase db; QVERIFY(db.create(dir.path(),"Migration"));
        AdjustmentState state; state.exposure = 1.5;
        QVERIFY(db.updateAdjustment("source.ARW",state)); QVERIFY(db.flush());
        const auto folder = db.projectPath(); const QString connection = "tag-migration-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); QVERIFY(query.exec("DROP TABLE catalog_tags")); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(QDir(QDir(folder).filePath("backups")).removeRecursively());
        QFile blocked(QDir(folder).filePath("backups")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        CatalogTags tags; tags.keywords = {"Sony"}; tags.albums = {"Japan"};
        QVERIFY(db.updateTagsBatch({{"source.ARW",tags}})); QVERIFY(!db.flush());
        QVERIFY(blocked.remove());
        QVERIFY(db.updateTagsBatch({{"source.ARW",tags}})); QVERIFY2(db.flush(),qPrintable(db.lastError()));
        const auto backups = QDir(QDir(folder).filePath("backups")).entryList({"*.db"},QDir::Files);
        QCOMPARE(backups.size(),1);
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("backups/"+backups[0]));
            sql.setConnectOptions("QSQLITE_OPEN_READONLY"); QVERIFY(sql.open()); QSqlQuery query(sql);
            QVERIFY(query.exec("SELECT adjustment_json FROM photos")); QVERIFY(query.next());
            QCOMPARE(AdjustmentState::fromJson(QJsonDocument::fromJson(query.value(0).toByteArray()).object()).exposure,1.5);
            QVERIFY(query.exec("SELECT name FROM sqlite_master WHERE name='catalog_tags'")); QVERIFY(!query.next()); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(folder,&photos));
        QCOMPARE(photos[0].tags.keywords,QStringList{"Sony"}); QCOMPARE(photos[0].adjustments.exposure,1.5);
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); auto invalid = tags.toJson(); invalid.insert("schema",99);
            query.prepare("UPDATE catalog_tags SET json=?"); query.addBindValue(QString::fromUtf8(QJsonDocument(invalid).toJson())); QVERIFY(query.exec()); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        ProjectDatabase active; QVERIFY(active.create(dir.path(),"Active"));
        QVERIFY(!active.open(folder,&photos)); QCOMPARE(active.projectName(),QStringLiteral("Active")); QVERIFY(active.flush());
    }

    void virtualCopiesHaveIndependentStateHistoryCatalogAndExports() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto originalBytes = file.readAll(); file.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        controller.setExposure(1); controller.finishInteraction();
        QVERIFY(controller.createVirtualCopy("Warm")); QCOMPARE(controller.library().size(),2);
        QCOMPARE(controller.currentFile(),path); QCOMPARE(controller.exposure(),1.0); QVERIFY(!controller.canUndo());
        controller.setExposure(-1); controller.finishInteraction(); controller.rotatePhoto(1);
        controller.setRating(5); controller.setFlag("reject");
        QVERIFY(controller.setSelectionKeywords("warm")); QVERIFY(controller.addSelectionToAlbum("Versions"));
        controller.selectPhoto(0); QCOMPARE(controller.exposure(),1.0); QCOMPARE(controller.currentRating(),0);
        QCOMPARE(controller.geometry()["quarterTurns"].toInt(),0); QVERIFY(controller.currentKeywords().isEmpty());
        controller.selectPhoto(1); QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Copies")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY2(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())),qPrintable(controller.projectPath()));
        QCOMPARE(reopened.library().size(),2); reopened.selectPhoto(1);
        QVERIFY(reopened.library()[1].toMap()["virtual"].toBool()); QCOMPARE(reopened.currentFile(),path);
        QCOMPARE(reopened.exposure(),-1.0); QCOMPARE(reopened.currentRating(),5); QCOMPARE(reopened.currentFlag(),QStringLiteral("reject"));
        QCOMPARE(reopened.currentKeywords(),QStringList{"warm"}); QVERIFY(reopened.canUndo());
        reopened.undo(); QCOMPARE(reopened.geometry()["quarterTurns"].toInt(),0);
        QVERIFY(reopened.flushEdits());
        QVERIFY(reopened.createVirtualCopy("Cool")); QCOMPARE(reopened.library().size(),3);
        reopened.setExposure(-2); reopened.finishInteraction(); QVERIFY(reopened.flushEdits());
        PhotoController final(nullptr); QVERIFY(final.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(final.library().size(),3);
        int warm = -1, cool = -1;
        for (int i = 0; i < final.library().size(); ++i) {
            const auto row = final.library()[i].toMap();
            if (row["versionName"].toString() == "Warm") warm = i;
            if (row["versionName"].toString() == "Cool") cool = i;
        }
        QVERIFY(warm >= 0 && cool >= 0); final.selectPhoto(warm); QCOMPARE(final.exposure(),-1.0); QVERIFY(final.canRedo());
        final.selectPhoto(cool); QCOMPARE(final.exposure(),-2.0); final.selectPhoto(0); QCOMPARE(final.exposure(),1.0);
        QSignalSpy exported(&final,&PhotoController::exportFinished);
        QVERIFY(final.exportAll(QUrl::fromLocalFile(dir.path()),"srgb",92,"png")); QTRY_COMPARE_WITH_TIMEOUT(exported.size(),1,10000);
        QCOMPARE(exported.first()[0].toInt(),3); QCOMPARE(exported.first()[1].toInt(),0);
        const auto outputs = QDir(dir.path()).entryList({"source_JixelLight*.png"},QDir::Files); QCOMPARE(outputs.size(),3);
        QVERIFY(QImage(dir.filePath(outputs[0])) != QImage(dir.filePath(outputs[1])));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),originalBytes);
    }

    void virtualCopyCreationRollsBackAndUnknownMappingPreservesActiveWriter() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"Copies"));
        AdjustmentState state; EditHistory history; history.initialize(state); CatalogTags tags;
        const auto key = "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(db.addVirtualCopy(key,"source.ARW","Version",state,history,tags,{}));
        QVERIFY(!db.addVirtualCopy(key,"different.ARW","Changed",state,history,tags,{})); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(db.projectPath(),&photos));
        QCOMPARE(photos.size(),1); QCOMPARE(photos[0].path,QStringLiteral("source.ARW")); QCOMPARE(photos[0].copyKey,key);
        const QString connection = "future-copy-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(db.projectPath()).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); QVERIFY(query.exec("UPDATE virtual_sources SET json='{\"schema\":99,\"source\":\"source.ARW\",\"name\":\"Version\"}'")); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        ProjectDatabase active; QVERIFY(active.create(dir.path(),"Active")); QVERIFY(!active.open(db.projectPath(),&photos));
        QCOMPARE(active.projectName(),QStringLiteral("Active")); QVERIFY(active.updateAdjustment("original.ARW",state)); QVERIFY(active.flush());
    }

    void relativeCatalogPathsKeepTheirDatabaseKeyWhenEditedOrCopied() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"Relative"));
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto source = QDir(db.projectPath()).filePath("source.png"); QVERIFY(image.save(source));
        AdjustmentState state; QVERIFY(db.updateAdjustment("source.png",state)); QVERIFY(db.flush());
        PhotoController controller(nullptr); QVERIFY(controller.openProject(QUrl::fromLocalFile(db.projectPath())));
        controller.setExposure(1); controller.finishInteraction(); QVERIFY(controller.flushEdits());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(db.projectPath(),&photos));
        QCOMPARE(photos.size(),1); QCOMPARE(photos[0].path,QStringLiteral("source.png")); QCOMPARE(photos[0].adjustments.exposure,1.0);
        QVERIFY(controller.createVirtualCopy("Relative copy"));
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Copied")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.library().size(),2); QCOMPARE(reopened.currentFile(),source);
        QCOMPARE(reopened.exposure(),1.0);
    }

    void legacyVirtualCopyMigrationBacksUpBeforeAtomicInsert() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"LegacyCopy"));
        AdjustmentState state; state.exposure = 0.75; EditHistory history; history.initialize(state);
        QVERIFY(db.updateAdjustment("source.ARW",state)); QVERIFY(db.flush());
        const auto folder = db.projectPath(); const QString connection = "virtual-migration-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); QVERIFY(query.exec("DROP TABLE virtual_sources")); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(QDir(QDir(folder).filePath("backups")).removeRecursively());
        QFile blocked(QDir(folder).filePath("backups")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        const auto key = "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(!db.addVirtualCopy(key,"source.ARW","Version",state,history,{},{})); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(folder,&photos)); QCOMPARE(photos.size(),1);
        QVERIFY(blocked.remove()); QVERIFY(db.addVirtualCopy(key,"source.ARW","Version",state,history,{},{}));
        QVERIFY(db.open(folder,&photos)); QCOMPARE(photos.size(),2);
        const auto backups = QDir(QDir(folder).filePath("backups")).entryList({"*.db"},QDir::Files); QCOMPARE(backups.size(),1);
        ProjectDatabase backup; QVector<ProjectDatabase::SavedPhoto> original;
        const auto backupFolder = dir.filePath("Backup.jlp"); QVERIFY(QDir().mkpath(backupFolder));
        QVERIFY(QFile::copy(QDir(folder).filePath("backups/"+backups[0]),QDir(backupFolder).filePath("Project.db")));
        QVERIFY(backup.open(backupFolder,&original)); QCOMPARE(original.size(),1); QCOMPARE(original[0].adjustments.exposure,0.75);
    }

    void virtualCopyRenameRemovalPreservesOriginalSelectionAndHistory() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path));
        QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly)); const auto bytes = source.readAll(); source.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        controller.setExposure(.5); controller.finishInteraction();
        QVERIFY(!controller.renameCurrentVirtualCopy("Original")); QVERIFY(!controller.removeCurrentVirtualCopy());
        QVERIFY(controller.createVirtualCopy("First")); controller.setExposure(-1); controller.finishInteraction(); controller.undo();
        QVERIFY(controller.createVirtualCopy("Second")); controller.setExposure(2); controller.finishInteraction();
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"ManageCopies")); QVERIFY(controller.flushEdits());
        controller.selectPhoto(1); const auto key = controller.library()[1].toMap().value("id").toString();
        QVERIFY(controller.canRedo()); QVERIFY(controller.renameCurrentVirtualCopy(" Renamed "));
        QVERIFY(controller.canRedo()); QCOMPARE(controller.library()[1].toMap().value("versionName").toString(),QStringLiteral("Renamed"));
        QVERIFY(!controller.renameCurrentVirtualCopy("")); QVERIFY(!controller.renameCurrentVirtualCopy(QString(81,'x')));
        QVERIFY(controller.setPhotoSelection({0,1,2})); QVERIFY(controller.removeCurrentVirtualCopy());
        QCOMPARE(controller.library().size(),2); QCOMPARE(controller.selectedIndices(),(QVariantList{0,1}));
        QCOMPARE(controller.exposure(),2.0); QVERIFY(controller.canUndo());
        controller.selectPhoto(0); QCOMPARE(controller.exposure(),.5); QVERIFY(controller.canUndo());
        QVERIFY(controller.flushEdits()); QCoreApplication::processEvents(); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.library().size(),2); QCOMPARE(reopened.exposure(),.5);
        reopened.selectPhoto(1); QCOMPARE(reopened.library()[1].toMap().value("versionName").toString(),QStringLiteral("Second"));
        QCOMPARE(reopened.exposure(),2.0); QVERIFY(reopened.canUndo());
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),bytes);
        for (const auto &row : reopened.library()) QVERIFY(row.toMap().value("id").toString() != key);
    }

    void virtualCopyDeleteRollsBackAllTablesOnFailureAndCanRetry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"CopyRollback"));
        AdjustmentState state; state.exposure = .75; EditHistory history; history.initialize(state);
        CatalogTags tags; tags.keywords = {"keep"}; const auto key = "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(db.updateAdjustment("source.ARW",state)); QVERIFY(db.flush());
        QVERIFY(db.addVirtualCopy(key,"source.ARW","Version",state,history,tags,{4,"pick"}));
        QVERIFY(!db.removeVirtualCopy("source.ARW")); QVERIFY(!db.renameVirtualCopy("source.ARW","Changed"));
        const QString connection = "copy-delete-fixture";
        auto trigger = [&](bool enable) {
            { auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(db.projectPath()).filePath("Project.db")); QVERIFY(sql.open());
              QSqlQuery query(sql); QVERIFY(query.exec(enable ? "CREATE TRIGGER block_copy_delete BEFORE DELETE ON curation BEGIN SELECT RAISE(ABORT,'blocked'); END" : "DROP TRIGGER block_copy_delete")); sql.close(); }
            QSqlDatabase::removeDatabase(connection);
        };
        trigger(true); QVERIFY(!db.removeVirtualCopy(key)); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(db.projectPath(),&photos)); QCOMPARE(photos.size(),2);
        QCOMPARE(photos[1].copyKey,key); QCOMPARE(photos[1].tags.keywords,QStringList{"keep"}); QCOMPARE(photos[1].rating,4);
        QCOMPARE(photos[1].adjustments.exposure,.75); trigger(false);
        QVERIFY(db.renameVirtualCopy(key,"Retained")); QVERIFY(db.open(db.projectPath(),&photos)); QCOMPARE(photos[1].versionName,QStringLiteral("Retained"));
        QVERIFY(db.removeVirtualCopy(key)); QVERIFY(!db.removeVirtualCopy(key)); QVERIFY(db.flush());
        QVERIFY(db.open(db.projectPath(),&photos)); QCOMPARE(photos.size(),1); QCOMPARE(photos[0].path,QStringLiteral("source.ARW"));
    }

    void deletingLastCatalogCopyClearsCanvasWithoutTouchingSource() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path));
        ProjectDatabase db; QVERIFY(db.create(dir.path(),"OnlyCopy")); AdjustmentState state; EditHistory history; history.initialize(state);
        const auto key = "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(db.addVirtualCopy(key,path,"Only",state,history,{},{}));
        PhotoController controller(nullptr); QVERIFY(controller.openProject(QUrl::fromLocalFile(db.projectPath())));
        QVERIFY(controller.hasImage()); controller.setViewport(64,64,1,0,.5,.5);
        QVERIFY(controller.removeCurrentVirtualCopy()); QCOMPARE(controller.library().size(),0);
        QCOMPARE(controller.currentIndex(),-1); QVERIFY(!controller.hasImage()); QVERIFY(!controller.loading()); QVERIFY(!controller.rendering());
        QCOMPARE(controller.scopesPixelCount(),0); QVERIFY(!controller.canUndo()); QVERIFY(QFileInfo::exists(path));
        QTest::qWait(50); QVERIFY(controller.previewUrl().isEmpty()); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
    }

    void catalogTimelineRejectsInvalidDatesWithoutGuessingCaptureTime() {
        PhotoTimeline timeline; timeline.captureTime=PhotoTimeline::cameraTime("2025:02:28 12:34:56"); timeline.captureChecked=true;
        timeline.importedAt=1740000000000LL; timeline.editedAt=1740000000123LL;
        PhotoTimeline restored; QVERIFY(PhotoTimeline::fromJson(timeline.toJson(),&restored)); QCOMPARE(restored.toJson(),timeline.toJson());
        QCOMPARE(restored.captureTime,QStringLiteral("2025-02-28 12:34:56")); QVERIFY(restored.captureOrder()>0);
        QVERIFY(PhotoTimeline::cameraTime("2025:02:29 12:34:56").isEmpty()); QVERIFY(PhotoTimeline::cameraTime("2025:02:28 25:34:56").isEmpty());
        QVERIFY(PhotoTimeline::cameraTime("invalid").isEmpty()); QCOMPARE(PhotoTimeline{}.captureOrder(),0);
        for (const auto &change : QVector<QJsonObject>{{{"schema",99}},{{"capture","x"}},{{"imported",-.5}},{{"edited",1.5}},{{"captureChecked",false}},{{"unknown",true}}}) {
            auto invalid=timeline.toJson(); for (auto it=change.begin();it!=change.end();++it) invalid.insert(it.key(),it.value());
            QVERIFY(!PhotoTimeline::fromJson(invalid,&restored)); QCOMPARE(restored.toJson(),timeline.toJson());
        }
    }

    void captureIndexAndPerVersionDevelopDatesPersistAcrossProjects() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto jpeg=dir.filePath("dated.jpg"), png=dir.filePath("undated.png"); QVERIFY(image.save(jpeg)); QVERIFY(image.save(png));
        { auto photo=Exiv2::ImageFactory::open(jpeg.toStdString()); QVERIFY(photo.get()); photo->readMetadata();
          auto exif=photo->exifData(); exif["Exif.Photo.DateTimeOriginal"]="2025:01:02 03:04:05"; photo->setExifData(exif); photo->writeMetadata(); }
        QFile source(jpeg); QVERIFY(source.open(QIODevice::ReadOnly)); const auto bytes=source.readAll(); source.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(jpeg))); QVERIFY(controller.importFile(QUrl::fromLocalFile(png)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.library()[0].toMap().value("captureChecked").toBool() && controller.library()[1].toMap().value("captureChecked").toBool(),10000);
        QCOMPARE(controller.library()[0].toMap().value("captureTime").toString(),QStringLiteral("2025-01-02 03:04:05"));
        QCOMPARE(controller.library()[1].toMap().value("captureOrder").toLongLong(),0);
        const auto imported=controller.library()[0].toMap().value("importedAt").toLongLong(); QVERIFY(imported>0);
        controller.setExposure(.5); controller.finishInteraction(); const auto edited=controller.library()[0].toMap().value("editedAt").toLongLong(); QVERIFY(edited>=imported);
        QVERIFY(controller.createVirtualCopy("Dated copy")); QCOMPARE(controller.library()[2].toMap().value("editedAt").toLongLong(),0);
        controller.setExposure(-.5); controller.finishInteraction(); QVERIFY(controller.library()[2].toMap().value("editedAt").toLongLong()>0);
        QCOMPARE(controller.library()[0].toMap().value("editedAt").toLongLong(),edited);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Dates")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.library()[0].toMap().value("captureTime").toString(),QStringLiteral("2025-01-02 03:04:05"));
        QCOMPARE(reopened.library()[0].toMap().value("importedAt").toLongLong(),imported); QCOMPARE(reopened.library()[0].toMap().value("editedAt").toLongLong(),edited);
        QVERIFY(reopened.createProject(QUrl::fromLocalFile(dir.path()),"CopiedDates")); QVERIFY(reopened.flushEdits());
        PhotoController final(nullptr); QVERIFY(final.openProject(QUrl::fromLocalFile(reopened.projectPath())));
        QCOMPARE(final.library()[0].toMap().value("importedAt").toLongLong(),imported);
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),bytes);
    }

    void legacyDateMigrationBacksUpAndRollsBackEditsBeforeRetry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"DateMigration"));
        AdjustmentState original; original.exposure=.5; QVERIFY(db.updateAdjustment("source.ARW",original)); QVERIFY(db.flush());
        const auto folder=db.projectPath(); const QString connection="date-migration-fixture";
        { auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
          QSqlQuery query(sql); QVERIFY(query.exec("DROP TABLE catalog_dates")); QVERIFY(query.exec("UPDATE photos SET imported_at='2020-01-02 03:04:05'")); sql.close(); }
        QSqlDatabase::removeDatabase(connection);
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(folder,&photos)); QCOMPARE(photos[0].timeline.importedAt,PhotoTimeline::sqlImportTime("2020-01-02 03:04:05"));
        QVERIFY(QDir(QDir(folder).filePath("backups")).removeRecursively()); QFile blocked(QDir(folder).filePath("backups")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        auto edited=original; edited.exposure=1; auto dates=photos[0].timeline; dates.editedAt=1740000000123LL;
        QVERIFY(db.updateBatch({{"source.ARW",edited}},{},{{"source.ARW",dates}})); QVERIFY(!db.flush()); QVERIFY(blocked.remove());
        QVERIFY(db.updateBatch({{"source.ARW",edited}},{},{{"source.ARW",dates}})); QVERIFY(db.flush());
        const auto backups=QDir(QDir(folder).filePath("backups")).entryList({"*.db"},QDir::Files); QCOMPARE(backups.size(),1);
        { auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("backups/"+backups[0])); QVERIFY(sql.open());
          QSqlQuery query(sql); QVERIFY(query.exec("SELECT adjustment_json FROM photos")); QVERIFY(query.next()); QCOMPARE(AdjustmentState::fromJson(QJsonDocument::fromJson(query.value(0).toByteArray()).object()).exposure,.5); sql.close(); }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(db.open(folder,&photos)); QCOMPARE(photos[0].adjustments.exposure,1.0); QCOMPARE(photos[0].timeline.editedAt,dates.editedAt);
        { auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
          QSqlQuery query(sql); QVERIFY(query.exec("UPDATE catalog_dates SET json='{\"schema\":99}'")); sql.close(); }
        QSqlDatabase::removeDatabase(connection);
        ProjectDatabase active; QVERIFY(active.create(dir.path(),"WorkingDates")); QVERIFY(!active.open(folder,&photos)); QCOMPARE(active.projectName(),QStringLiteral("WorkingDates")); QVERIFY(active.flush());
    }

    void realSonyAsShotHistoryPersistsIntentAfterMetadataResolution() {
        const auto folder = qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if (folder.isEmpty()) QSKIP("Sony real fixtures are not configured");
        QString source;
        for (const auto &name : QDir(folder).entryList({"*.arw","*.ARW"},QDir::Files)) {
            const auto path = QDir(folder).filePath(name);
            if (MetadataReader::read(path).value("sonyLook").toMap().value("autoEligible").toBool()) { source = path; break; }
        }
        QVERIFY2(!source.isEmpty(),"No verified Sony As Shot fixture found");
        QTemporaryDir dir; QVERIFY(dir.isValid()); PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(source)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentMetadata().value("sonyLook").toMap().value("autoEligible").toBool(),60000);
        controller.setViewport(320,240,1,0,.5,.5);
        controller.setExposure(.5); controller.finishInteraction();
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"AsShot")); QVERIFY(controller.flushEdits());
        controller.setExposure(1); controller.finishInteraction(); QVERIFY(controller.flushEdits());
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY2(db.open(controller.projectPath(),&photos),qPrintable(db.lastError()));
        QCOMPARE(photos.size(),1); QCOMPARE(photos[0].adjustments.look.mode,QStringLiteral("as-shot"));
        QVERIFY(photos[0].adjustments.look.code.isEmpty()); QVERIFY(photos[0].adjustments.look.parameters.isEmpty());
        QVERIFY(photos[0].history.canUndo()); QCOMPARE(photos[0].history.undo().exposure,.5);
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        reopened.undo(); QCOMPARE(reopened.exposure(),.5); QVERIFY(reopened.canRedo()); QVERIFY(reopened.flushEdits());
        QVERIFY(reopened.createVirtualCopy("Transfer target")); reopened.setLookCode("FL"); reopened.setExposure(-.5); reopened.finishInteraction();
        reopened.selectPhoto(0);
        QTRY_VERIFY_WITH_TIMEOUT(reopened.currentMetadata().value("sonyLook").toMap().value("autoEligible").toBool(),60000);
        QVERIFY(!reopened.lookState()["code"].toString().isEmpty()); reopened.copyAdjustments();
        reopened.selectPhoto(1); QVERIFY(reopened.pasteAdjustmentGroups({"sony_look"})); QCOMPARE(reopened.exposure(),-.5);
        reopened.undo(); QCOMPARE(reopened.lookState()["mode"].toString(),QString("manual"));
        reopened.selectPhoto(0); QVERIFY(reopened.setPhotoSelection({0,1})); QCOMPARE(reopened.syncAdjustmentGroups({"sony_look"},true),1);
        QVERIFY(reopened.flushEdits()); QVERIFY(db.readSnapshot(reopened.projectPath(),&photos)); QCOMPARE(photos.size(),2);
        for (const auto &photo : photos) {
            QCOMPARE(photo.adjustments.look.mode,QString("as-shot")); QVERIFY(photo.adjustments.look.code.isEmpty()); QVERIFY(photo.adjustments.look.parameters.isEmpty());
        }
    }

    void failedAnnotationWritesRetainHistoryAndTagsForRetry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path)); PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path))); controller.setExposure(.5); controller.finishInteraction();
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Retry")); QVERIFY(controller.flushEdits());
        const auto folder = controller.projectPath(); const QString connection = "controller-retry-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); QVERIFY(query.exec("DROP TABLE catalog_tags")); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(QDir(QDir(folder).filePath("backups")).removeRecursively());
        QFile blocked(QDir(folder).filePath("backups")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        QVERIFY(controller.setSelectionKeywords("retained"));
        // A later successful curation batch must not mask a failed tags batch.
        QVERIFY(controller.setSelectionRating(4)); QVERIFY(!controller.flushEdits());
        QCoreApplication::processEvents(); // Deliver writer failure and refill all dirty snapshots.
        QVERIFY(blocked.remove()); QVERIFY(controller.flushEdits());
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY2(db.open(folder,&photos),qPrintable(db.lastError()));
        QCOMPARE(photos[0].tags.keywords,QStringList{"retained"}); QVERIFY(photos[0].history.canUndo());
        QCOMPARE(photos[0].history.undo().exposure,0.0);
    }

    void olderResolvedAsShotCatalogHistoryRecoversOnlyMetadataDifferences() {
        AdjustmentState intent; intent.look.mode = "as-shot"; intent.exposure = .5;
        EditHistory history; history.initialize(intent);
        auto rendered = intent; rendered.look.code = "ST"; rendered.look.parameters.insert("clarity",1);
        EditHistory restored; QVERIFY(restored.restore(history.toJson(),rendered));
        rendered.exposure = 1; QVERIFY(!restored.restore(history.toJson(),rendered));
        rendered.exposure = .5; rendered.look.strength = .5; QVERIFY(!restored.restore(history.toJson(),rendered));
        rendered.look.strength = 1;
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"AsShotDraft"));
        QVERIFY(db.updateBatch({{"source.ARW",rendered}},{{"source.ARW",history}})); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(db.projectPath(),&photos));
        QVERIFY(photos[0].adjustments.look.code.isEmpty()); QVERIFY(photos[0].adjustments.look.parameters.isEmpty());
        QCOMPARE(photos[0].adjustments.exposure,.5);
    }

    void sharedGeometryHslCurveCommandsMatchGuiAndRejectPartialMutations() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path=dir.filePath("source.png"); QVERIFY(image.save(path)); PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path))); AdjustmentState state;
        const QVector<QJsonObject> commands{
            {{"command","develop.set"},{"parameter","exposure"},{"value",.5}},
            {{"command","geometry.crop"},{"x",0.0},{"y",.25},{"width",.5},{"height",.5}},
            {{"command","geometry.rotate"},{"quarterTurns",-1}},
            {{"command","geometry.flip"},{"axis","vertical"}},
            {{"command","hsl.set"},{"band",2},{"component","saturation"},{"value",25}},
            {{"command","curve.set"},{"channel","red"},{"point",2},{"value",.6}}
        };
        for (const auto &command : commands) QVERIFY(CommandRegistry::execute(state,command));
        controller.setExposure(.5); controller.finishInteraction(); controller.setCrop(0,.25,.5,.5);
        controller.rotatePhoto(-1); controller.flipPhoto(false); controller.setColorMix(2,1,25); controller.setCurvePoint(1,2,.6);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Commands")); QVERIFY(controller.flushEdits());
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(controller.projectPath(),&photos));
        QCOMPARE(photos[0].adjustments.toJson(),state.toJson());
        const auto before=state.toJson();
        const QVector<QJsonObject> invalid{
            {{"command","geometry.crop"},{"x",.8},{"y",0.0},{"width",.5},{"height",1.0}},
            {{"command","geometry.rotate"},{"quarterTurns",.5}},
            {{"command","geometry.flip"},{"axis","diagonal"}},
            {{"command","hsl.set"},{"band",8},{"component","hue"},{"value",1}},
            {{"command","curve.set"},{"channel","red"},{"point",2},{"value","invalid"}},
            {{"command","develop.reset"},{"unexpected",true}}
        };
        for (const auto &command : invalid) { QVERIFY(!CommandRegistry::execute(state,command)); QCOMPARE(state.toJson(),before); }
        QVERIFY(controller.executeEditCommand({{"command","geometry.reset"}})); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),0);
        controller.undo(); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),3);
        QVERIFY(!controller.executeEditCommand({{"command","curve.set"},{"channel","red"},{"point",5},{"value",.5}}));
        QVERIFY(CommandRegistry::execute(state,{{"command","curve.reset"},{"channel","red"}})); QCOMPARE(state.redCurve[2],.5);
        QVERIFY(CommandRegistry::execute(state,{{"command","develop.reset"}})); QCOMPARE(state.toJson(),AdjustmentState{}.toJson());
    }

    void zipWriterCreatesZipSignature() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("test.zip");
        ZipStoreWriter zip(path);
        QVERIFY(zip.open());
        QVERIFY(zip.addFile("hello.txt", "hello"));
        QVERIFY(zip.close());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.read(4), QByteArray("PK\x03\x04",4));
        const auto rest=f.readAll(); f.close();
        ZipStoreWriter collision(path); QVERIFY(!collision.open());
        QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("PK\x03\x04",4)+rest);
    }
};

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
