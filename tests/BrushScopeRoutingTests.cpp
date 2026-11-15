#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <QRgba64>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>
#include "app/PhotoController.h"
#include "core/cache/AnalysisFrameCache.h"
#include "core/cache/FullScopesCache.h"
#include "core/cache/ScopePlotCache.h"
#include "core/commands/CommandRegistry.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/metadata/XmpSidecar.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/StageGraph.h"
#include "diagnostics/PerformanceRecorder.h"
#include "diagnostics/ActionTrace.h"

namespace {
QImage sourceImage(bool floating=false) {
    QImage image(2203,1207,floating?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
        if(floating) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            p[0]=float(x%47)/23.f-.17f;p[1]=float(y%43)/29.f;p[2]=float((x+y)%37)/31.f-.08f;p[3]=(x+y)%7?.61f:0.f;
        }else reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(4000+x*17,6000+y*23,8000+(x+y)*9,65535);
    }
    return image;
}
AdjustmentState painted() {
    AdjustmentState state;state.exposure=-.4;state.saturation=11;
    state.look.mode="manual";state.look.code="FL";state.look.parameters={{"clarity",3},{"sharpness",4}};
    BrushMaskGeometry::Stroke stroke;stroke.style={.025,.43,.71};
    for(int i=0;i<13;++i)stroke.dabs.push_back({{.21+i*.041,.32+i*.019},.6+(i%3)*.1});
    state.brushMasks.layers={{"scope",true,.8,{stroke}}};return state;
}
QImage reference(const QImage &source,const ProcessingPlan &plan,const GeometryState &geometry) {
    const auto input=geometry.apply(source);QImage result(input.size(),QImage::Format_RGBA64);
    result.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<input.height();y+=128) {
        const auto strip=ImagePipeline::processRegion(input,plan,{0,y,input.width(),std::min(128,input.height()-y)});
        if(strip.isNull())return {};
        for(int row=0;row<strip.height();++row)std::memcpy(result.scanLine(y+row),strip.constScanLine(row),std::size_t(input.width())*8);
    }
    return result;
}
bool sameHistogram(const ScopesResult &a,const ScopesResult &b) {
    return a.pixelCount==b.pixelCount&&a.red==b.red&&a.green==b.green&&a.blue==b.blue&&a.luma==b.luma
        &&a.shadowClipPercent==b.shadowClipPercent&&a.highlightClipPercent==b.highlightClipPercent;
}
QByteArray fileHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file)?hash.result().toHex():QByteArray{};
}
qint64 counter(const QString &name){return PerformanceRecorder::snapshot()["counters"].toObject()[name].toInteger();}
QByteArray storedZipEntry(const QString &path,const QByteArray &entry) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};const auto bytes=file.readAll();qsizetype offset=0;
    while(offset+30<=bytes.size()&&qFromLittleEndian<quint32>(bytes.constData()+offset)==0x04034b50) {
        const auto size=qFromLittleEndian<quint32>(bytes.constData()+offset+18);
        const auto nameSize=qFromLittleEndian<quint16>(bytes.constData()+offset+26),extra=qFromLittleEndian<quint16>(bytes.constData()+offset+28);
        const auto start=offset+30+nameSize+extra;if(start>bytes.size()||size>bytes.size()-start)return {};
        if(bytes.mid(offset+30,nameSize)==entry)return bytes.mid(start,size);offset=start+size;
    }
    return {};
}
}
class BrushScopeRoutingTests final:public QObject {
    Q_OBJECT
private:
    void prepareCloseRecovery(PhotoController &c,const QString &path,const QString &directory) {
        c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        QVERIFY(c.createProject(QUrl::fromLocalFile(directory),"QueuedRecovery"));c.setExposure(.75);c.finishInteraction();
        const BrushMaskGeometry::Stroke one{{.002,.6,1},{{{.5,.5},1}}};auto large=one;large.dabs.resize(BrushMaskGeometry::MaxDabsPerStroke,one.dabs[0]);
        BrushMaskDocument::Document document{{{"full",true,0,{}}}};
        for(std::size_t n=0;n<BrushMaskDocument::MaxTotalDabs/BrushMaskGeometry::MaxDabsPerStroke;++n)document.layers[0].strokes.push_back(large);
        document.layers[0].strokes.back().dabs.pop_back();QJsonObject json;QVERIFY(BrushMaskDocument::toJson(document,&json));
        QVERIFY(c.executeEditCommand({{"command","mask.brush.replace"},{"document",json}}));c.finishInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        QVERIFY(c.beginBrushStrokeModePressure("full",.5,.5,.002,.6,1,1,"paint",.5));
        QVERIFY(c.appendBrushStroke(.5,.5,.5));QVERIFY(c.appendBrushStroke(.7,.5,.5));QVERIFY(c.endBrushStroke().isEmpty());
        QVERIFY(c.m_brushRecovery);QVERIFY(c.brushRecoveryAvailable());QVERIFY(!c.m_brushGesture);QVERIFY(c.flushEdits());
    }
private slots:
    void queuedRecoveryExpiryPreservesClosedOwner_data() {
        QTest::addColumn<QString>("signal");QTest::addColumn<bool>("queueBeforeClose");
        for(const auto &signal:QStringList{"backendChanged","activityChanged","previewGeometryChanged"})
            for(bool before:{true,false})QTest::newRow(qPrintable(signal+(before?"-before-close":"-after-close")))<<signal<<before;
    }
    void queuedRecoveryExpiryPreservesClosedOwner() {
        QFETCH(QString,signal);QFETCH(bool,queueBeforeClose);QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("queued-recovery.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);prepareCloseRecovery(c,path,dir.path());if(QTest::currentTestFailed())return;
        const auto state=c.m_photos[0].state.toJson(),history=c.m_photos[0].history.toJson();const auto project=c.projectPath();
        QSignalSpy recovery(&c,&PhotoController::brushRecoveryChanged);QVERIFY(recovery.isValid());
        if(queueBeforeClose)QVERIFY(QMetaObject::invokeMethod(&c,signal.toLatin1().constData(),Qt::DirectConnection));
        QVERIFY(c.beginClose());QVERIFY(c.m_closing);QVERIFY(c.m_brushRecovery);const auto revision=c.m_requestedRevision,serial=c.m_brushGestureSerial;const auto status=c.statusMessage();
        if(!queueBeforeClose)QVERIFY(QMetaObject::invokeMethod(&c,signal.toLatin1().constData(),Qt::DirectConnection));
        QCoreApplication::sendPostedEvents(&c,QEvent::MetaCall);QCoreApplication::sendPostedEvents(&c,QEvent::MetaCall);
        QVERIFY(c.m_brushRecovery);QCOMPARE(recovery.size(),0);QVERIFY(!c.m_brushGesture);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.m_brushGestureSerial,serial);QCOMPARE(c.statusMessage(),status);
        QCOMPARE(c.m_photos[0].state.toJson(),state);QCOMPARE(c.m_photos[0].history.toJson(),history);
        ProjectDatabase saved;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(saved.readSnapshot(project,&rows));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].adjustments.toJson(),state);QCOMPARE(rows[0].history.toJson(),history);QCOMPARE(fileHash(path),original);
    }
    void recoveryDiscardRefusesForeignThreadAndAcceptedClose() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("discard-recovery.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);prepareCloseRecovery(c,path,dir.path());if(QTest::currentTestFailed())return;
        QSignalSpy recovery(&c,&PhotoController::brushRecoveryChanged);const auto revision=c.m_requestedRevision;
        std::thread foreign([&]{c.discardBrushRecovery();});foreign.join();QVERIFY(c.m_brushRecovery);QCOMPARE(recovery.size(),0);QCOMPARE(c.m_requestedRevision,revision);
        QVERIFY(c.beginClose());QVERIFY(c.m_brushRecovery);c.discardBrushRecovery();QVERIFY(c.m_brushRecovery);QCOMPARE(recovery.size(),0);QCOMPARE(c.m_requestedRevision,revision);
    }
    void gestureTransitionSignalsMayCloseOrDestroyOwner_data() {
        QTest::addColumn<QString>("action");QTest::addColumn<bool>("closeOnly");
        for(const auto &action:QStringList{"crop-begin-refine","crop-begin-flag","crop-cancel-flag","crop-apply-flag","crop-apply-commit","brush-begin","brush-end","brush-end-preview","brush-end-recovery","brush-begin-warning","brush-append-warning","brush-capacity-drawing","brush-capacity-warning","brush-capacity-recovery","brush-apply-recovery","brush-invalid-cancel","brush-discard-recovery","brush-cancel"})
            for(bool closeOnly:{true,false})QTest::newRow(qPrintable(action+(closeOnly?"-close":"-destroy")))<<action<<closeOnly;
    }
    void gestureTransitionSignalsMayCloseOrDestroyOwner() {
        QFETCH(QString,action);QFETCH(bool,closeOnly);QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("gesture-owner.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;auto owner=std::make_unique<PhotoController>(&provider);owner->setGpuEnabled(false);QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering()&&!owner->m_prepare->busy()&&!owner->m_render->busy(),15000);
        QVERIFY(owner->createProject(QUrl::fromLocalFile(dir.path()),"GestureOwner"));owner->setExposure(.75);owner->finishInteraction();
        const bool capacity=action.contains("warning")||action.contains("capacity")||action.contains("recovery");
        if(capacity){
            const BrushMaskGeometry::Stroke one{{.002,.6,1},{{{.5,.5},1}}};auto large=one;large.dabs.resize(BrushMaskGeometry::MaxDabsPerStroke,one.dabs[0]);
            BrushMaskDocument::Document document{{{"full",true,0,{}}}};for(std::size_t n=0;n<BrushMaskDocument::MaxTotalDabs/BrushMaskGeometry::MaxDabsPerStroke;++n)document.layers[0].strokes.push_back(large);
            document.layers[0].strokes.back().dabs.pop_back();QJsonObject json;QVERIFY(BrushMaskDocument::toJson(document,&json));QVERIFY(owner->executeEditCommand({{"command","mask.brush.replace"},{"document",json}}));owner->finishInteraction();
        }
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&!owner->rendering()&&!owner->m_prepare->busy()&&!owner->m_render->busy(),15000);
        if(action.startsWith("crop-")&&!action.startsWith("crop-begin")){QVERIFY(owner->beginCrop());QTRY_VERIFY_WITH_TIMEOUT(owner->previewReady()&&!owner->m_preparing&&!owner->rendering()&&!owner->m_prepare->busy()&&!owner->m_render->busy(),15000);}
        if(action.startsWith("brush-")&&action!="brush-begin"&&action!="brush-begin-warning"){
            QVERIFY(owner->beginBrushStrokeModePressure(capacity?"full":"",.5,.5,.002,.6,1,1,"paint",action.contains("recovery")?.5:(capacity?0:1)));
            if(capacity&&action!="brush-append-warning")QVERIFY(owner->appendBrushStroke(.5,.5,.5));
            if(action=="brush-end-recovery")QVERIFY(owner->appendBrushStroke(.7,.5,.5));
            if(action=="brush-apply-recovery"||action=="brush-discard-recovery"){
                bool refused=false;for(int i=0;i<40&&!refused;++i)refused=!owner->appendBrushStroke(i%2?.1:.9,.5,1);
                QVERIFY(refused);QVERIFY(owner->brushRecoveryAvailable());
            }
        }
        if(action=="brush-end-preview")QTRY_VERIFY_WITH_TIMEOUT(!owner->m_brushPreviewId.isEmpty()&&!owner->m_brushPreviewJob->busy(),5000);
        QVERIFY(owner->flushEdits());const auto project=owner->projectPath();const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("gesture_owner_marker",{{"id",marker}});
        QPointer<PhotoController> alive(owner.get());bool notified=false,closed=false;AdjustmentState expected;EditHistory history;QVariantList stopped;
        const auto workState=[&]{
            QVariantList result{qulonglong(owner->m_requestedRevision),qulonglong(owner->m_prepareGeneration),qulonglong(owner->m_brushGestureSerial),owner->m_cropEditing,owner->m_zoom,owner->m_centerX,owner->m_centerY,owner->m_interacting,bool(owner->m_brushGesture),bool(owner->m_brushRecovery),owner->m_brushPreviewId,qulonglong(owner->m_brushPreviewSample),owner->statusMessage(),int(owner->m_dirtyEdits.size()),int(owner->m_dirtyHistories.size())};
            for(auto value:QList<quint64>{owner->m_loader->sequence(),owner->m_prepare->sequence(),owner->m_render->sequence(),owner->m_scopeJob->sequence(),owner->m_inputScopeJob->sequence(),owner->m_fullScopeJob->sequence(),owner->m_plotJob->sequence()})result.append(qulonglong(value));
            for(auto *timer:{&owner->m_saveTimer,&owner->m_saveMaxTimer,&owner->m_refineTimer,&owner->m_exactTimer,&owner->m_prefetchTimer,&owner->m_catalogDateTimer,&owner->m_plotTimer,&owner->m_budgetRetryTimer,&owner->m_brushPreviewRetryTimer,&owner->m_brushPreviewScheduleTimer,&owner->m_softProofDelay,&owner->m_softProofDeadline,&owner->m_diagnosticTimer})result.append(timer->isActive());
            return result;
        };
        const auto terminal=[&]{if(notified||!owner)return;notified=true;expected=owner->m_photos[0].state;history=owner->m_photos[0].history;
            if(closeOnly){closed=owner->beginClose();if(owner)stopped=workState();}else owner.reset();};
        QMetaObject::Connection connection;
        if(action=="crop-begin-refine")connection=QObject::connect(owner.get(),&PhotoController::libraryChanged,owner.get(),terminal);
        else if(action=="crop-apply-commit")connection=QObject::connect(owner.get(),&PhotoController::historyChanged,owner.get(),terminal);
        else if(action.startsWith("crop-"))connection=QObject::connect(owner.get(),&PhotoController::cropEditingChanged,owner.get(),terminal);
        else if(action=="brush-end-preview")connection=QObject::connect(owner.get(),&PhotoController::brushPreviewChanged,owner.get(),terminal);
        else if(action.contains("warning"))connection=QObject::connect(owner.get(),&PhotoController::brushCapacityWarningChanged,owner.get(),terminal);
        else if(action.contains("recovery"))connection=QObject::connect(owner.get(),&PhotoController::brushRecoveryChanged,owner.get(),terminal);
        else connection=QObject::connect(owner.get(),&PhotoController::brushDrawingChanged,owner.get(),terminal);
        const auto disconnect=qScopeGuard([&]{QObject::disconnect(connection);});bool accepted=false;QString layer;
        if(action.startsWith("crop-begin"))accepted=owner->beginCrop();
        else if(action=="crop-cancel-flag")owner->cancelCrop();
        else if(action.startsWith("crop-apply"))accepted=owner->applyCrop(.2,.2,.6,.6);
        else if(action=="brush-begin"||action=="brush-begin-warning")accepted=owner->beginBrushStrokeModePressure(capacity?"full":"",.5,.5,.002,.6,1,1,"paint",1);
        else if(action.startsWith("brush-end"))layer=owner->endBrushStroke();
        else if(action=="brush-apply-recovery")layer=owner->applyBrushRecovery();
        else if(action=="brush-discard-recovery")owner->discardBrushRecovery();
        else if(action=="brush-cancel")owner->cancelBrushStroke();
        else if(action=="brush-invalid-cancel")accepted=owner->appendBrushStroke(std::numeric_limits<double>::quiet_NaN(),.5,1);
        else if(action.startsWith("brush-capacity-")){
            accepted=true;for(int i=0;i<40&&accepted;++i)accepted=owner->appendBrushStroke(i%2?.1:.9,.5,1);
        }else accepted=owner->appendBrushStroke(.5,.5,1);
        QVERIFY(notified);if(action=="crop-apply-commit")QVERIFY(accepted);else QVERIFY(!accepted);QVERIFY(layer.isEmpty());
        if(closeOnly){
            QVERIFY(closed);QVERIFY(owner&&alive&&owner->m_closing);
            QTRY_VERIFY_WITH_TIMEOUT(!owner->m_prepare->busy()&&!owner->m_render->busy()&&!owner->m_scopeJob->busy()&&!owner->m_inputScopeJob->busy()&&!owner->m_fullScopeJob->busy()&&!owner->m_plotJob->busy()&&!owner->m_brushPreviewJob->busy(),5000);
            QCOMPARE(workState(),stopped);
        }else{QVERIFY(!owner);QVERIFY(!alive);}
        ProjectDatabase saved;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(saved.readSnapshot(project,&rows));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].adjustments.toJson(),expected.toJson());QCOMPARE(rows[0].history.toJson(),history.toJson());
        bool after=false;int commits=0;for(const auto &value:ActionTrace::instance().snapshot()){const auto event=value.toObject();if(event["action"]=="gesture_owner_marker"&&event["id"]==marker)after=true;
            else if(after&&(event["action"]=="geometry_crop"||event["action"]=="brush_stroke"||event["action"]=="brush_erase")){++commits;QCOMPARE(event["file"].toString(),path);}}
        QVERIFY(after);QCOMPARE(commits,action=="crop-apply-commit"?1:0);QCOMPARE(fileHash(path),original);
    }
    void gestureEntryPointsRefuseForeignThreadAndClose() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("gesture-entry.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        const auto state=c.m_photos[0].state.toJson(),history=c.m_photos[0].history.toJson();const auto revision=c.m_requestedRevision,serial=c.m_brushGestureSerial;
        bool crop=true,apply=true,brush=true;QString end="unexpected",recovery="unexpected";
        const auto attempted=[&]{crop=c.beginCrop();c.cancelCrop();apply=c.applyCrop(.2,.2,.6,.6);brush=c.beginBrushStroke("",.5,.5,.02,.6,1,1);c.appendBrushStroke(.7,.5,1);end=c.endBrushStroke();recovery=c.applyBrushRecovery();};
        std::thread foreign(attempted);foreign.join();QVERIFY(!crop);QVERIFY(!apply);QVERIFY(!brush);QVERIFY(end.isEmpty());QVERIFY(recovery.isEmpty());
        QCOMPARE(c.m_photos[0].state.toJson(),state);QCOMPARE(c.m_photos[0].history.toJson(),history);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.m_brushGestureSerial,serial);QVERIFY(!c.m_cropEditing);QVERIFY(!c.brushDrawing());
        QVERIFY(c.beginClose());attempted();QVERIFY(!crop);QVERIFY(!apply);QVERIFY(!brush);QVERIFY(end.isEmpty());QVERIFY(recovery.isEmpty());QVERIFY(!c.brushReady());QCOMPARE(c.m_photos[0].state.toJson(),state);QCOMPARE(c.m_photos[0].history.toJson(),history);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.m_brushGestureSerial,serial);QVERIFY(!c.m_cropEditing);QVERIFY(!c.brushDrawing());
    }
    void endingGestureKeepsReentrantNewGestureUncommitted() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("new-gesture.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        const auto state=c.m_photos[0].state.toJson(),history=c.m_photos[0].history.toJson();QVERIFY(c.beginBrushStroke("",.2,.5,.02,.6,1,1));
        bool started=false,nested=false;const auto connection=QObject::connect(&c,&PhotoController::brushDrawingChanged,&c,[&]{if(started||c.brushDrawing())return;started=true;nested=c.beginBrushStroke("",.7,.5,.02,.6,1,1);});
        const auto oldLayer=c.endBrushStroke();QObject::disconnect(connection);QVERIFY(started);QVERIFY(nested);QVERIFY(oldLayer.isEmpty());QVERIFY(c.brushDrawing());QCOMPARE(c.m_photos[0].state.toJson(),state);QCOMPARE(c.m_photos[0].history.toJson(),history);
        const auto newLayer=c.endBrushStroke();QVERIFY(!newLayer.isEmpty());QCOMPARE(c.m_photos[0].state.brushMasks.layers.size(),std::size_t(1));QCOMPARE(c.m_photos[0].state.brushMasks.layers[0].id,newLayer);QVERIFY(c.m_photos[0].state.brushMasks.layers[0].strokes[0].dabs[0].center.x>.6);QCOMPARE(fileHash(path),original);
    }
    void cropRefinementRejectsRecursiveEntry() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("recursive-crop.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        int notifications=0;bool nested=true;const auto connection=QObject::connect(&c,&PhotoController::libraryChanged,&c,[&]{if(++notifications==1)nested=c.beginCrop();});
        const bool entered=c.beginCrop();QObject::disconnect(connection);QVERIFY(entered);QVERIFY(!nested);QCOMPARE(notifications,1);QVERIFY(c.m_cropEditing);QVERIFY(!c.m_finishingInteraction);
    }
    void cropEntryAcceptsSynchronousViewportPreparation_data() {
        QTest::addColumn<bool>("resize");QTest::newRow("resize")<<true;QTest::newRow("pan-center")<<false;
    }
    void cropEntryAcceptsSynchronousViewportPreparation() {
        QFETCH(bool,resize);QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("crop-viewport.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        c.setViewport(640,480,1,2,.45,.55);QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        const auto state=c.gpuPlan(true).state.toJson(),history=c.m_photos[0].history.toJson();bool notified=false;quint64 prepared=0,submitted=0;
        const auto connection=QObject::connect(&c,&PhotoController::cropEditingChanged,&c,[&]{if(!c.cropEditing()||notified)return;notified=true;c.setViewport(resize?641:640,resize?481:480,1,0,.49,.51);prepared=c.m_prepareGeneration;submitted=c.m_prepare->sequence();});
        const bool entered=c.beginCrop();QObject::disconnect(connection);QVERIFY(notified);QVERIFY(entered);QVERIFY(c.cropEditing());
        QCOMPARE(c.m_prepareGeneration,prepared);QCOMPARE(c.m_prepare->sequence(),submitted);QCOMPARE(c.gpuPlan(true).state.toJson(),state);QCOMPARE(c.m_photos[0].history.toJson(),history);
        QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.m_preparing&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);QCOMPARE(fileHash(path),original);
    }
    void cropEntryRefusesCancelAndReentrantRestart() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("crop-restart.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        const auto state=c.gpuPlan(true).state.toJson(),history=c.m_photos[0].history.toJson();bool notified=false,nested=true;quint64 prepared=0;
        const auto connection=QObject::connect(&c,&PhotoController::cropEditingChanged,&c,[&]{if(!c.cropEditing()||notified)return;notified=true;c.cancelCrop();nested=c.beginCrop();prepared=c.m_prepareGeneration;});
        const bool entered=c.beginCrop();QObject::disconnect(connection);QVERIFY(notified);QVERIFY(!entered);QVERIFY(!nested);QVERIFY(!c.cropEditing());QCOMPARE(c.m_prepareGeneration,prepared);QCOMPARE(c.gpuPlan(true).state.toJson(),state);QCOMPARE(c.m_photos[0].history.toJson(),history);
    }
    void cropEntryCannotResumeAfterNotificationAcceptedClose() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("crop-entry-close.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"CropEntryClose"));c.setExposure(.75);c.finishInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);QVERIFY(c.flushEdits());
        bool notified=false,closed=false;quint64 revision=0;
        QObject::connect(&c,&PhotoController::libraryChanged,&c,[&]{if(notified)return;notified=true;closed=c.beginClose();revision=c.m_requestedRevision;});
        const bool accepted=c.beginCrop();QVERIFY(notified);QVERIFY(closed);QVERIFY(c.m_closing);
        QCOMPARE(c.m_cropEditing,false);QVERIFY(!accepted);QCOMPARE(c.m_requestedRevision,revision);
        ProjectDatabase saved;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(saved.readSnapshot(c.projectPath(),&rows));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].adjustments.exposure,.75);QCOMPARE(fileHash(path),original);
    }
    void editPublicationSignalsMayCloseOrDestroyOwner_data() {
        QTest::addColumn<QString>("action");QTest::addColumn<QString>("context");QTest::addColumn<bool>("closeOnly");
        const auto rows=[](const QString &action,const QStringList &contexts){for(const auto &context:contexts)for(bool closeOnly:{true,false})
            QTest::newRow(qPrintable(action+"-"+context+(closeOnly?"-close":"-destroy")))<<action<<context<<closeOnly;};
        rows("scalar",{"history","adjustments"});rows("reset",{"library","history","adjustments"});
        rows("undo",{"library","adjustments","look","history"});rows("redo",{"library","adjustments","look","history"});
        rows("paste",{"history"});rows("xmp",{"history"});rows("brush-remove",{"history"});
        rows("apply",{"crop","calibration","plot","scopes"});rows("finish",{"library","activity"});
    }
    void editPublicationSignalsMayCloseOrDestroyOwner() {
        QFETCH(QString,action);QFETCH(QString,context);QFETCH(bool,closeOnly);
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("edit-owner.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;auto owner=std::make_unique<PhotoController>(&provider);owner->setGpuEnabled(false);QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering()&&!owner->m_prepare->busy()&&!owner->m_render->busy(),15000);
        QVERIFY(owner->createProject(QUrl::fromLocalFile(dir.path()),"EditOwner"));owner->setExposure(.75);owner->finishInteraction();
        if(action=="redo"){owner->undo();owner->finishInteraction();}
        if(action=="paste"){owner->copyAdjustments();owner->setExposure(.25);owner->finishInteraction();}
        if(action=="brush-remove"){owner->mutableCurrentState()->brushMasks=painted().brushMasks;QVERIFY(owner->persistAndApply("fixture_brush"));owner->finishInteraction();}
        const auto xmp=dir.filePath("edit-owner.xmp");
        if(action=="xmp"){
            auto state=owner->m_photos[0].state;state.exposure=.5;QString error;
            QVERIFY2(XmpSidecar::writeNew(xmp,state,{},4,"none",&error),qPrintable(error));
        }
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&!owner->rendering()&&!owner->m_prepare->busy()&&!owner->m_render->busy(),15000);
        QVERIFY(owner->flushEdits());owner->m_refineTimer.stop();owner->m_exactTimer.stop();owner->m_plotTimer.stop();
        if(action=="apply"&&context=="crop")owner->m_cropEditing=true;
        if(action=="apply"&&context=="calibration")owner->m_calibrationBusy=true;
        if(action=="finish")owner->m_interacting=true;
        const auto project=owner->projectPath(),key=owner->m_photos[0].storageKey();
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("edit_owner_marker",{{"id",marker}});
        QPointer<PhotoController> alive(owner.get());bool notified=false,closed=false;AdjustmentState expected;EditHistory history;int rating=0;QString flag;CatalogTags tags;QList<quint64> stopped;
        const auto workState=[&]{
            QList<quint64> result{owner->m_requestedRevision,owner->m_prepareGeneration,owner->m_loader->sequence(),owner->m_prefetch->sequence(),owner->m_prepare->sequence(),owner->m_render->sequence(),owner->m_scopeJob->sequence(),owner->m_inputScopeJob->sequence(),owner->m_fullScopeJob->sequence(),owner->m_plotJob->sequence(),owner->m_referenceJob->sequence(),owner->m_calibrationJob->sequence(),quint64(owner->m_interacting)};
            for(auto *timer:{&owner->m_saveTimer,&owner->m_saveMaxTimer,&owner->m_refineTimer,&owner->m_exactTimer,&owner->m_prefetchTimer,&owner->m_catalogDateTimer,&owner->m_plotTimer,&owner->m_budgetRetryTimer,&owner->m_brushPreviewRetryTimer,&owner->m_brushPreviewScheduleTimer,&owner->m_softProofDelay,&owner->m_softProofDeadline,&owner->m_diagnosticTimer})result.append(timer->isActive());
            return result;
        };
        const auto terminal=[&]{
            if(notified||!owner)return;notified=true;const auto &photo=owner->m_photos[0];expected=photo.state;history=photo.history;rating=photo.rating;flag=photo.flag;tags=photo.tags;
            if(closeOnly){closed=owner->beginClose();if(owner)stopped=workState();}else owner.reset();
        };
        QMetaObject::Connection connection;
        if(context=="history")connection=QObject::connect(owner.get(),&PhotoController::historyChanged,owner.get(),terminal);
        else if(context=="library")connection=QObject::connect(owner.get(),&PhotoController::libraryChanged,owner.get(),terminal);
        else if(context=="adjustments")connection=QObject::connect(owner.get(),&PhotoController::adjustmentsChanged,owner.get(),terminal);
        else if(context=="look")connection=QObject::connect(owner.get(),&PhotoController::lookChanged,owner.get(),terminal);
        else if(context=="crop")connection=QObject::connect(owner.get(),&PhotoController::cropEditingChanged,owner.get(),terminal);
        else if(context=="calibration")connection=QObject::connect(owner.get(),&PhotoController::calibrationChanged,owner.get(),terminal);
        else if(context=="plot")connection=QObject::connect(owner.get(),&PhotoController::scopePlotChanged,owner.get(),terminal);
        else if(context=="scopes")connection=QObject::connect(owner.get(),&PhotoController::scopesChanged,owner.get(),terminal);
        else connection=QObject::connect(owner.get(),&PhotoController::activityChanged,owner.get(),terminal);
        const auto disconnect=qScopeGuard([&]{QObject::disconnect(connection);});
        if(action=="scalar")owner->setExposure(.5);
        else if(action=="reset")owner->resetAdjustments();
        else if(action=="undo")owner->undo();
        else if(action=="redo")owner->redo();
        else if(action=="paste")owner->pasteAdjustments();
        else if(action=="xmp")QVERIFY(owner->importXmp(QUrl::fromLocalFile(xmp)));
        else if(action=="brush-remove")QVERIFY(owner->removeBrushLayer("scope"));
        else if(action=="apply")owner->applyCurrent();
        else owner->finishInteraction();
        QVERIFY(notified);
        if(closeOnly){
            QVERIFY(closed);QVERIFY(owner&&alive&&owner->m_closing);
            QTRY_VERIFY_WITH_TIMEOUT(!owner->m_prepare->busy()&&!owner->m_render->busy()&&!owner->m_scopeJob->busy()&&!owner->m_inputScopeJob->busy()&&!owner->m_fullScopeJob->busy()&&!owner->m_plotJob->busy(),5000);
            QCOMPARE(workState(),stopped);
        }else{QVERIFY(!owner);QVERIFY(!alive);}
        ProjectDatabase saved;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(saved.readSnapshot(project,&rows));QCOMPARE(rows.size(),1);
        QCOMPARE(rows[0].adjustments.toJson(),expected.toJson());QCOMPARE(rows[0].history.toJson(),history.toJson());QCOMPARE(rows[0].rating,rating);QCOMPARE(rows[0].flag,flag);QCOMPARE(rows[0].tags.toJson(),tags.toJson());
        const QString traceAction=action=="scalar"?"adjustment":action=="reset"?"reset_adjustments":action=="undo"?"edit_undo":action=="redo"?"edit_redo":action=="paste"?"paste_adjustments":action=="xmp"?"xmp_import":action=="brush-remove"?"brush_layer_remove":QString{};
        if(!traceAction.isEmpty()){
            const auto trace=ActionTrace::instance().snapshot();bool after=false;int count=0;
            for(const auto &value:trace){const auto event=value.toObject();if(event["action"]=="edit_owner_marker"&&event["id"]==marker)after=true;
                else if(after&&event["action"]==traceAction){++count;QCOMPARE(event["file"].toString(),path);if(event.contains("photo_key"))QCOMPARE(event["photo_key"].toString(),key);}}
            QVERIFY(after);QCOMPARE(count,1);
        }
        QCOMPARE(fileHash(path),original);
    }
    void editEntryPointsRefuseForeignThreadAndAcceptedClose() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("edit-entry.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        c.setExposure(.75);c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        const auto state=c.m_photos[0].state.toJson(),history=c.m_photos[0].history.toJson();const auto revision=c.m_requestedRevision;
        const auto attempted=[&]{c.setExposure(2);c.resetAdjustments();c.undo();c.redo();c.applyCurrent();c.finishInteraction();};
        std::thread foreign(attempted);foreign.join();QCOMPARE(c.m_photos[0].state.toJson(),state);QCOMPARE(c.m_photos[0].history.toJson(),history);QCOMPARE(c.m_requestedRevision,revision);
        QVERIFY(c.beginClose());const auto closedRevision=c.m_requestedRevision;attempted();QCOMPARE(c.m_photos[0].state.toJson(),state);QCOMPARE(c.m_photos[0].history.toJson(),history);QCOMPARE(c.m_requestedRevision,closedRevision);
        QVERIFY(!c.canUndo());QVERIFY(!c.canRedo());QVERIFY(!c.m_saveTimer.isActive());QVERIFY(!c.m_saveMaxTimer.isActive());QVERIFY(!c.m_refineTimer.isActive());
    }
    void editNotificationPhotoSwitchKeepsOriginalTargetAndPersistence() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto first=dir.filePath("first.png"),second=dir.filePath("second.png");QVERIFY(sourceImage().scaled(321,197).save(first));QVERIFY(sourceImage().scaled(197,321).save(second));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.importFiles({QUrl::fromLocalFile(first),QUrl::fromLocalFile(second)});
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Switch"));QVERIFY(c.flushEdits());
        bool switched=false;quint64 revision=0;QObject::connect(&c,&PhotoController::historyChanged,&c,[&]{if(switched)return;switched=true;c.selectPhoto(1);revision=c.m_requestedRevision;});
        c.setExposure(.75);QVERIFY(switched);QCOMPARE(c.currentIndex(),1);QCOMPARE(c.m_requestedRevision,revision);
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);QVERIFY(c.flushEdits());
        ProjectDatabase saved;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(saved.readSnapshot(c.projectPath(),&rows));QCOMPARE(rows.size(),2);
        for(const auto &photo:rows)QCOMPARE(photo.adjustments.exposure,photo.path==first?.75:0.);
        const auto trace=ActionTrace::instance().snapshot();bool found=false;for(auto i=trace.size();i-->0;){const auto event=trace[i].toObject();if(event["action"]=="adjustment"&&event["file"]==first){QCOMPARE(event["photo_key"].toString(),first);found=true;break;}}QVERIFY(found);
    }
    void reentrantEditRecordsMutationOrderAndSchedulesLatestOnce() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("reentrant.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Reentrant"));QVERIFY(c.flushEdits());
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("edit_order_marker",{{"id",marker}});const auto revision=c.m_requestedRevision;
        bool nested=false;QObject::connect(&c,&PhotoController::historyChanged,&c,[&]{if(nested)return;nested=true;c.setExposure(.5);});
        c.setExposure(.75);QVERIFY(nested);QCOMPARE(c.exposure(),.5);QCOMPARE(c.m_requestedRevision,revision+1);QVERIFY(c.flushEdits());
        ProjectDatabase saved;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(saved.readSnapshot(c.projectPath(),&rows));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].adjustments.exposure,.5);QCOMPARE(rows[0].history.toJson(),c.m_photos[0].history.toJson());
        QList<double> values;bool after=false;for(const auto &value:ActionTrace::instance().snapshot()){const auto event=value.toObject();if(event["action"]=="edit_order_marker"&&event["id"]==marker)after=true;else if(after&&event["action"]=="adjustment"&&event["file"]==path)values.append(event["value"].toDouble());}
        QVERIFY(after);QCOMPARE(values,(QList<double>{.75,.5}));
    }
    void closeFromEditHistoryNotificationSavesLastEdit() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("history-close.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"HistoryClose"));QVERIFY(c.flushEdits());
        bool notified=false,closed=false;EditHistory expected;
        QObject::connect(&c,&PhotoController::historyChanged,&c,[&]{if(notified)return;notified=true;expected=c.m_photos[c.m_currentIndex].history;closed=c.beginClose();});
        c.setExposure(.75);QVERIFY(notified);QVERIFY(closed);QVERIFY(c.m_closing);
        ProjectDatabase saved;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(saved.readSnapshot(c.projectPath(),&rows));QCOMPARE(rows.size(),1);
        QCOMPARE(rows[0].adjustments.exposure,.75);QCOMPARE(rows[0].history.toJson(),expected.toJson());QCOMPARE(fileHash(path),original);
    }
    void previewCompletionSignalsMayCloseOrDestroyOwner_data() {
        QTest::addColumn<QString>("context");QTest::addColumn<bool>("closeOnly");
        for(const auto &context:QStringList{"source-metadata","source-activity","prepare-geometry","prepare-activity","render-preview","render-activity","render-plot","exact-request","gpu-backend"})
            for(bool closeOnly:{false,true})QTest::newRow(qPrintable(context+(closeOnly?"-close":"-destroy")))<<context<<closeOnly;
    }
    void previewCompletionSignalsMayCloseOrDestroyOwner() {
        QFETCH(QString,context);QFETCH(bool,closeOnly);
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("preview-owner.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;auto owner=std::make_unique<PhotoController>(&provider);owner->setGpuEnabled(false);
        QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering()&&owner->scopesPixelCount()>0&&!owner->m_prepare->busy()&&!owner->m_render->busy()&&!owner->m_scopeJob->busy(),15000);
        QVERIFY(owner->flushEdits());owner->m_saveTimer.stop();owner->m_saveMaxTimer.stop();owner->m_refineTimer.stop();owner->m_exactTimer.stop();owner->m_plotTimer.stop();owner->setExactScopes(false);
        const auto generation=owner->m_prepareGeneration,preview=owner->m_previewRevision,load=owner->m_loader->sequence();
        QPointer<PhotoController> alive(owner.get());bool terminal=false,closed=false;QList<quint64> stopped;
        const auto workState=[&]{return QList<quint64>{owner->m_requestedRevision,owner->m_loader->sequence(),owner->m_prefetch->sequence(),owner->m_prepare->sequence(),owner->m_render->sequence(),owner->m_scopeJob->sequence(),owner->m_inputScopeJob->sequence(),owner->m_fullScopeJob->sequence(),owner->m_plotJob->sequence(),owner->m_referenceJob->sequence(),quint64(owner->m_exactTimer.isActive())+2*quint64(owner->m_plotTimer.isActive())};};
        const auto completed=[&]{
            if(terminal||!owner)return;
            const bool ready=context.startsWith("source-")?owner->m_loader->sequence()>load
                :context.startsWith("prepare-")?!owner->m_preparing&&owner->m_prepareGeneration>generation
                :context=="gpu-backend"?owner->m_gpuActive:context=="exact-request"?owner->m_scopesUpdating:owner->m_previewRevision>preview&&(context=="render-preview"||!owner->rendering());
            if(!ready)return;
            terminal=true;
            if(closeOnly){closed=owner->beginClose();if(owner)stopped=workState();}
            else owner.reset();
        };
        QMetaObject::Connection connection;
        if(context=="source-metadata")connection=QObject::connect(owner.get(),&PhotoController::currentMetadataChanged,owner.get(),completed);
        else if(context=="prepare-geometry")connection=QObject::connect(owner.get(),&PhotoController::previewGeometryChanged,owner.get(),completed);
        else if(context=="source-activity"||context=="prepare-activity"||context=="render-activity")connection=QObject::connect(owner.get(),&PhotoController::activityChanged,owner.get(),completed);
        else if(context=="gpu-backend")connection=QObject::connect(owner.get(),&PhotoController::backendChanged,owner.get(),completed);
        else if(context=="render-preview")connection=QObject::connect(owner.get(),&PhotoController::previewUrlChanged,owner.get(),completed);
        else if(context=="render-plot")connection=QObject::connect(owner.get(),&PhotoController::scopePlotChanged,owner.get(),completed);
        else connection=QObject::connect(owner.get(),&PhotoController::scopesChanged,owner.get(),completed);
        const auto disconnect=qScopeGuard([&]{QObject::disconnect(connection);});
        if(context.startsWith("source-")){owner->m_loadedKey.clear();owner->loadCurrent();}
        else if(context.startsWith("prepare-")){owner->m_viewport=QSize(177,123);owner->prepareCurrent();}
        else if(context=="gpu-backend"){owner->m_gpuEnabled=true;owner->m_gpuActive=false;owner->m_gpuSource=owner->m_previewSource;owner->gpuPresented(owner->m_requestedRevision,"controlled-lifetime-backend");}
        else if(context=="exact-request"){owner->m_exactScopes=true;owner->requestFullScopes();}
        else owner->scheduleRender(false);
        QTRY_VERIFY_WITH_TIMEOUT(terminal,15000);
        if(closeOnly){
            QVERIFY(closed);QVERIFY(owner&&alive);QVERIFY(owner->m_closing);
            QTRY_VERIFY_WITH_TIMEOUT(!owner->m_prepare->busy()&&!owner->m_render->busy()&&!owner->m_scopeJob->busy()&&!owner->m_inputScopeJob->busy()&&!owner->m_fullScopeJob->busy()&&!owner->m_plotJob->busy(),5000);
            QCOMPARE(workState(),stopped);
        }else{QVERIFY(!owner);QVERIFY(!alive);}
        QCOMPARE(fileHash(path),original);
    }
    void windowCloseIsTerminalWhilePreflightRemainsReusable() {
        QStandardPaths::setTestModeEnabled(true);ProcessedImageProvider provider;PhotoController c(&provider);
        bool foreign=true;std::thread worker([&]{foreign=c.beginClose();});worker.join();QVERIFY(!foreign);QVERIFY(!c.m_closing);
        QVERIFY(c.prepareToClose());QVERIFY(!c.m_closing);QVERIFY(c.executeCommand({{"command","scopes.domain"},{"domain","source"}}));
        QVERIFY(c.beginClose());QVERIFY(c.m_closing);QVERIFY(!c.m_closePreparing);
        const auto sequence=c.m_fullScopeJob->sequence();QVERIFY(c.beginClose());QCOMPARE(c.m_fullScopeJob->sequence(),sequence);
        QVERIFY(!c.executeCommand({{"command","scopes.domain"},{"domain","working"}}));QCOMPARE(c.scopeDomain(),QString("source"));
    }
    void failedWindowCloseRetainsWriterAndCanRetry() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("close-retry.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"WindowClose"));QVERIFY(c.flushEdits());
        const auto connection=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto trigger=[&](bool enabled){
            {auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(QDir(c.projectPath()).filePath("Project.db"));QVERIFY(db.open());QSqlQuery query(db);QVERIFY(query.exec(enabled?"CREATE TRIGGER block_window_close BEFORE UPDATE ON photos BEGIN SELECT RAISE(ABORT,'blocked close'); END":"DROP TRIGGER block_window_close"));db.close();}
            QSqlDatabase::removeDatabase(connection);
        };
        trigger(true);auto restore=qScopeGuard([&]{trigger(false);});
        c.setExposure(.75);c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        QVERIFY(!c.beginClose());QVERIFY(!c.m_closing);QVERIFY(!c.m_closePreparing);QCOMPARE(c.exposure(),.75);
        QVERIFY(c.executeCommand({{"command","scopes.domain"},{"domain","working"}}));QCoreApplication::processEvents();trigger(false);restore.dismiss();
        QVERIFY(c.beginClose());QVERIFY(c.m_closing);ProjectDatabase saved;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(saved.readSnapshot(c.projectPath(),&rows));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].adjustments.exposure,.75);QCOMPARE(fileHash(path),original);
    }
    void asynchronousLinearScopeDeliveryMayDestroyOwner_data() {
        QTest::addColumn<QString>("domain");QTest::addColumn<bool>("plot");
        for(const auto &domain:QStringList{"source","working"}) {
            QTest::newRow(qPrintable(domain+"-histogram"))<<domain<<false;
            QTest::newRow(qPrintable(domain+"-plot"))<<domain<<true;
        }
    }
    void asynchronousLinearScopeDeliveryMayDestroyOwner() {
        QFETCH(QString,domain);QFETCH(bool,plot);
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("asynchronous-owner.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;auto owner=std::make_unique<PhotoController>(&provider);owner->setGpuEnabled(false);
        QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering()&&owner->scopesPixelCount()>0&&!owner->m_scopeJob->busy(),15000);
        QVERIFY(owner->flushEdits());owner->m_saveTimer.stop();owner->m_saveMaxTimer.stop();owner->m_refineTimer.stop();owner->setExactScopes(false);
        if(plot){owner->setScopeDomain(domain);QTRY_VERIFY_WITH_TIMEOUT(owner->scopesPixelCount()>0&&!owner->m_inputScopeJob->busy(),15000);}
        QPointer<PhotoController> alive(owner.get());bool deleted=false;
        const auto completed=[&]{
            if(!owner)return;
            const bool current=plot?owner->scopePlotCurrent():owner->scopesPixelCount()>0&&owner->scopeDomain()==domain;
            if(current){owner.reset();deleted=true;}
        };
        if(plot)QObject::connect(owner.get(),&PhotoController::scopePlotChanged,owner.get(),completed);
        else QObject::connect(owner.get(),&PhotoController::scopesChanged,owner.get(),completed);
        if(plot)owner->setScopeMode("waveform");else owner->setScopeDomain(domain);
        QTRY_VERIFY_WITH_TIMEOUT(deleted,15000);QVERIFY(!alive);QVERIFY(!owner);QCOMPARE(fileHash(path),original);
    }
    void controllerFullLinearPlotsReuseCompletedFramesWhileBrushBudgetIsBusy() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("linear-frame-reuse.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        auto budget=std::make_shared<BrushRenderBudget>(512LL*1024*1024);ProcessedImageProvider provider;PhotoController c(&provider,nullptr,budget);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto saved=c.m_photos[c.m_currentIndex].state;const auto restore=qScopeGuard([&]{c.m_photos[c.m_currentIndex].state=saved;c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_exactTimer.stop();c.m_plotTimer.stop();});c.m_photos[c.m_currentIndex].state=painted();++c.m_requestedRevision;c.m_scopesRank=-1;c.scheduleRender(false);c.setExactScopes(true);c.setScopeDomain("working");
        QTRY_VERIFY_WITH_TIMEOUT(c.m_scopesRank==1&&c.scopesPixelCount()>0&&!c.rendering()&&!c.m_fullScopeJob->busy()&&!c.m_scopeJob->busy()&&!c.m_inputScopeJob->busy(),15000);
        const auto key=StageGraph::linearAnalysisFrameKey(c.m_fullSource,c.gpuPlan(true),c.previewGeometry(),AnalysisDomain::Working);const auto completed=c.m_analysisFrameCache->findLinear(key,AnalysisDomain::Working);QVERIFY(completed);QCOMPARE(completed.image().format(),QImage::Format_RGBA32FPx4);QCOMPARE(c.m_analysisFrameCache->snapshot()["cached_domain"].toString(),QString("working"));
        const auto scopes=c.m_scopes;const auto intent=c.gpuPlan(true).state.toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision;const auto preview=StageGraph::outputFingerprint(c.m_processedPreview);QVERIFY(sameHistogram(scopes,ScopesEngine::analyzeLinear(completed.image())));
        std::vector<std::unique_ptr<BrushAnalysisRequest>> held;QString error;BrushAdmissionFailure reason=BrushAdmissionFailure::None;
        for(int i=0;i<64;++i){auto request=BrushAnalysisRequest::createPrepared(c.m_previewSource,c.gpuPlan(),BrushRenderRequest::Layout::Stripes128,{},&error,budget,&reason);if(!request)break;held.push_back(std::move(request));}
        QVERIFY(!held.empty());QCOMPARE(reason,BrushAdmissionFailure::BudgetUnavailable);const auto charge=budget->snapshot().charged;const auto hits=c.m_analysisFrameCache->snapshot()["hits"].toInteger();
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);PerformanceRecorder::sample("linear-frame-controller-boundary",0,{{"marker",marker}});
        for(const auto &mode:QStringList{"waveform","parade","vectorscope"}){c.setScopeMode(mode);QTRY_VERIFY_WITH_TIMEOUT(c.scopePlotCurrent(),15000);QCOMPARE(c.scopePlotPixels(),c.scopesPixelCount());ScopePlotCounts expected(mode);QVERIFY(expected.addLinear(completed.image()));QCOMPARE(c.m_scopePlot.image,expected.image());QVERIFY(!c.m_deferredBudgetJobs[2].pending);}
        bool seen=false;int renders=0,geometry=0;for(const auto &value:PerformanceRecorder::snapshot()["timings"].toArray()){const auto row=value.toObject();if(row["stage"]=="linear-frame-controller-boundary"&&row["marker"]==marker){seen=true;renders=geometry=0;}else if(seen){renders+=row["stage"]=="cpu_pipeline";geometry+=row["stage"]=="geometry_straighten"||row["stage"]=="geometry_corrections";}}
        QVERIFY(seen);QCOMPARE(renders,0);QCOMPARE(geometry,0);QVERIFY(c.m_analysisFrameCache->snapshot()["hits"].toInteger()>hits);QCOMPARE(budget->snapshot().charged,charge);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.gpuPlan(true).state.toJson(),intent);QCOMPARE(c.editHistory(),history);QCOMPARE(StageGraph::outputFingerprint(c.m_processedPreview),preview);QVERIFY(sameHistogram(c.m_scopes,scopes));QCOMPARE(fileHash(path),original);held.clear();
    }
    void domainSelectionKeepsPixelsHistoryAndRevisionsAndRefusesStaleDomains() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("domains.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&c.scopesPixelCount()>0,15000);
        c.setExposure(1);c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(!c.rendering()&&c.m_scopesRevision==c.m_requestedRevision&&c.scopesPixelCount()>0,15000);
        QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto original=fileHash(path);const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision,photo=c.m_photoEpoch,prepare=c.m_prepareGeneration;const auto frame=StageGraph::outputFingerprint(c.m_processedPreview);
        QSignalSpy changed(&c,&PhotoController::scopeDomainChanged);QCOMPARE(c.scopeDomain(),QString("display"));c.setScopeDomain("Source");QCOMPARE(changed.count(),0);
        std::thread foreign([&]{c.setScopeDomain("source");});foreign.join();QCOMPARE(changed.count(),0);QCOMPARE(c.scopeDomain(),QString("display"));
        for(const auto domain:{AnalysisDomain::Working,AnalysisDomain::Source,AnalysisDomain::Display}) {
            c.setScopeDomain(analysisDomainKey(domain));QCOMPARE(c.scopesPixelCount(),qulonglong(0));QVERIFY(!c.scopePlotCurrent());
            QTRY_VERIFY_WITH_TIMEOUT(c.m_scopesDomain==domain&&c.scopesPixelCount()>0&&!c.m_inputScopeJob->busy(),15000);
            const auto expected=domain==AnalysisDomain::Display?ScopesEngine::analyze(c.m_processedPreview):ScopesEngine::analyzeDomain(c.m_previewSource,c.gpuPlan(),domain);
            QVERIFY(sameHistogram(c.m_scopes,expected));const auto current=c.m_scopes;
            c.acceptScopes(revision,expected,1,"stale",domain==AnalysisDomain::Display?AnalysisDomain::Working:AnalysisDomain::Display);QVERIFY(sameHistogram(c.m_scopes,current));QCOMPARE(c.m_scopesRank,0);
            if(domain!=AnalysisDomain::Display) {
                std::array<quint32,HistogramCounts::GpuWords> words{};for(int i=0;i<4;++i)words[i*1024]=1;words[4098]=1;
                const QByteArray packet(reinterpret_cast<const char *>(words.data()),sizeof(words));const bool enabled=c.m_gpuEnabled;c.m_gpuEnabled=true;c.gpuScopes(revision,packet,1);c.m_gpuEnabled=enabled;QVERIFY(sameHistogram(c.m_scopes,current));
            }
            for(const auto &mode:QStringList{"waveform","vectorscope"}) {c.setScopeMode(mode);QTRY_VERIFY_WITH_TIMEOUT(c.scopePlotCurrent(),15000);QCOMPARE(c.m_plotDomain,domain);QCOMPARE(c.scopePlotPixels(),c.scopesPixelCount());}
            c.setScopeMode("histogram");QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.m_photoEpoch,photo);QCOMPARE(c.m_prepareGeneration,prepare);QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(StageGraph::outputFingerprint(c.m_processedPreview),frame);QCOMPARE(fileHash(path),original);
        }
        QCOMPARE(changed.count(),3);c.m_closing=true;c.setScopeDomain("source");QCOMPARE(changed.count(),3);QCOMPARE(c.scopeDomain(),QString("display"));c.m_closing=false;
    }
    void domainChangesCancelBlockedExactWorkWithoutPublishingOldCounts() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("blocked-domains.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        QSemaphore entered,release;ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto revision=c.m_requestedRevision;const auto history=c.editHistory();auto cache=std::make_shared<FullScopesCache>();std::atomic_int calls{0};
        const auto cleanup=qScopeGuard([&]{release.release();c.m_exactTimer.stop();c.m_plotTimer.stop();});
        c.m_fullScopeJob=std::make_unique<LatestJob<ScopeRequest,ScopesResult>>([&](const ScopeRequest &r,const CancelToken &){if(calls.fetch_add(1)==0){entered.release();release.acquire();}return cache->analyze(r,{});},[&](const ScopeRequest &r,ScopesResult result){c.acceptScopes(r.revision,result,1,"Full-resolution statistics",r.domain);});
        c.m_exactScopes=true;c.setScopeDomain("working");QTRY_VERIFY_WITH_TIMEOUT(entered.available()>0,5000);entered.acquire();QVERIFY(c.m_fullScopeJob->busy());
        c.setScopeDomain("source");QCOMPARE(c.scopesPixelCount(),qulonglong(0));ScopesResult stale;stale.pixelCount=1;c.acceptScopes(revision,stale,1,"old working",AnalysisDomain::Working);QCOMPARE(c.scopesPixelCount(),qulonglong(0));
        bool responsive=false;QTimer::singleShot(0,&c,[&]{responsive=true;});QTRY_VERIFY_WITH_TIMEOUT(responsive,1000);release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!c.m_fullScopeJob->busy()&&c.scopesPixelCount()>0,15000);QCOMPARE(c.m_scopesDomain,AnalysisDomain::Source);QCOMPARE(c.m_scopesRank,1);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.editHistory(),history);
        const auto corrected=c.previewGeometry().apply(c.m_fullSource);QVERIFY(sameHistogram(c.m_scopes,ScopesEngine::analyzeDomain(corrected,c.gpuPlan(true),AnalysisDomain::Source)));
        c.setExactScopes(false);QTRY_VERIFY_WITH_TIMEOUT(c.m_scopesRank==0&&c.scopesPixelCount()>0,15000);QCOMPARE(c.m_scopesDomain,AnalysisDomain::Source);
        c.m_scopePlot={QImage(3,3,QImage::Format_ARGB32),1,{}};c.m_plotRevision=revision;c.m_plotFull=false;c.m_plotDomain=AnalysisDomain::Working;QVERIFY(!c.scopePlotCurrent());
    }
    void domainSignalCanDestroyItsOwnerBeforeAnyNewWorkStarts() {
        for(const auto &command:QList<QJsonObject>{{{"command","scopes.domain"},{"domain","source"}},{{"command","scopes.mode"},{"mode","waveform"}},{{"command","scopes.full-resolution"},{"enabled",true}}}) {
            ProcessedImageProvider provider;QPointer<PhotoController> owner=new PhotoController(&provider);const auto name=command["command"].toString();
            if(name=="scopes.domain")QObject::connect(owner,&PhotoController::scopeDomainChanged,owner,[&]{delete owner.data();});
            else if(name=="scopes.mode")QObject::connect(owner,&PhotoController::scopePlotChanged,owner,[&]{delete owner.data();});
            else QObject::connect(owner,&PhotoController::scopesChanged,owner,[&]{delete owner.data();});
            QVERIFY(owner->executeCommand(command.toVariantMap()));QVERIFY(owner.isNull());const auto row=ActionTrace::instance().snapshot().last().toObject();QCOMPARE(row["action"].toString(),QString("scopes_command"));QCOMPARE(row["command"].toObject(),command);QVERIFY(row["presentation_only"].toBool());
        }
    }
    void referenceCalibrationCommandsShareAdaptersAndTraceBeforeNestedCancel() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("fit-command.png");const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(path));const auto original=fileHash(path);
        const QJsonObject fit{{"command","camera-reference.fit"}},cancel{{"command","camera-reference.fit.cancel"}};
        for(bool generic:{false,true}) {
            std::atomic_int starts{0};ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(!c.executeCommand(fit.toVariantMap()));QVERIFY(c.executeCommand(cancel.toVariantMap()));
            QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_refineTimer.stop();c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_photos[0].raw=true;c.m_referenceImage=image;
            c.m_calibrationJob=std::make_unique<LatestJob<LookCalibrationRequest,LookCalibrationResult>>([&](const LookCalibrationRequest &,const CancelToken &){++starts;return LookCalibrationResult{};},[](const LookCalibrationRequest &,LookCalibrationResult){});
            const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision,photo=c.m_photoEpoch;const auto pixels=StageGraph::outputFingerprint(c.m_fullSource);
            const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("reference_fit_test_boundary",{{"marker",marker}});
            const auto commands=[&]{QJsonArray rows;bool seen=false;for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject();if(row["action"]=="reference_fit_test_boundary"&&row["marker"]==marker){seen=true;rows={};}else if(seen&&row["action"]=="camera_reference_calibration_command")rows.append(row["command"]);}return rows;};
            for(const auto &bad:QList<QVariantMap>{{{"command","camera-reference.fit"},{"value",true}},{{"command","camera-reference.fit.cancel"},{"source",path}},{{"command","camera-reference.fit.unknown"}}})QVERIFY(!c.executeCommand(bad));
            bool notified=false,cancelled=false,traceBeforeCallback=false;QObject::connect(&c,&PhotoController::calibrationChanged,&c,[&]{if(notified||!c.calibrationBusy())return;notified=true;const auto event=ActionTrace::instance().snapshot().last().toObject();traceBeforeCallback=event["action"]=="camera_reference_calibration_command"&&event["command"].toObject()==fit&&event["revision"].toString()==QString::number(revision)&&event["photo_epoch"].toString()==QString::number(photo);cancelled=c.executeCommand(cancel.toVariantMap());});
            QVERIFY(generic?c.executeCommand(fit.toVariantMap()):c.calibrateFromReference());QVERIFY(notified&&cancelled&&traceBeforeCallback);QVERIFY(!c.calibrationBusy());QVERIFY(!c.m_calibrationJob->busy());QCOMPARE(starts.load(),0);QCOMPARE(commands(),QJsonArray({fit,cancel}));
            c.cancelCalibration();QCOMPARE(commands(),QJsonArray({fit,cancel,cancel}));const auto sequence=c.m_calibrationJob->sequence();bool foreignFit=true,foreignCancel=true;
            std::thread worker([&]{foreignFit=c.executeCommand(fit.toVariantMap());foreignCancel=c.executeCommand(cancel.toVariantMap());c.calibrateFromReference();c.cancelCalibration();});worker.join();QVERIFY(!foreignFit&&!foreignCancel);QCOMPARE(c.m_calibrationJob->sequence(),sequence);QCOMPARE(commands(),QJsonArray({fit,cancel,cancel}));
            QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.m_photoEpoch,photo);QCOMPARE(StageGraph::outputFingerprint(c.m_fullSource),pixels);QCOMPARE(fileHash(path),original);
            QVERIFY(c.beginClose());const auto terminal=c.m_calibrationJob->sequence();QVERIFY(!c.executeCommand(fit.toVariantMap()));QVERIFY(!c.executeCommand(cancel.toVariantMap()));c.cancelCalibration();QCOMPARE(c.m_calibrationJob->sequence(),terminal);QCOMPARE(commands(),QJsonArray({fit,cancel,cancel}));
        }
    }
    void referenceCalibrationCommandAcceptanceSurvivesOwnerCloseOrDeletion() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("fit-command-owner.png");const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(path));const auto original=fileHash(path);
        for(bool closeOnly:{false,true}) {
            std::atomic_int starts{0};ProcessedImageProvider provider;QPointer<PhotoController> owner=new PhotoController(&provider);owner->setGpuEnabled(false);QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering(),15000);QVERIFY(owner->flushEdits());owner->m_refineTimer.stop();owner->m_photos[0].raw=true;owner->m_referenceImage=image;
            owner->m_calibrationJob=std::make_unique<LatestJob<LookCalibrationRequest,LookCalibrationResult>>([&](const LookCalibrationRequest &,const CancelToken &){++starts;return LookCalibrationResult{};},[](const LookCalibrationRequest &,LookCalibrationResult){});
            const auto revision=owner->m_requestedRevision,photo=owner->m_photoEpoch;bool notified=false,closed=false;quint64 stopped=0;
            QObject::connect(owner,&PhotoController::calibrationChanged,owner,[&]{if(notified)return;notified=true;if(closeOnly){closed=owner->beginClose();stopped=owner->m_calibrationJob->sequence();}else delete owner.data();});
            const QJsonObject fit{{"command","camera-reference.fit"}};QVERIFY(owner->executeCommand(fit.toVariantMap()));QVERIFY(notified);QCOMPARE(starts.load(),0);QJsonObject accepted;
            for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject();if(row["action"]=="camera_reference_calibration_command")accepted=row;}
            QCOMPARE(accepted["command"].toObject(),fit);QCOMPARE(accepted["revision"].toString(),QString::number(revision));QCOMPARE(accepted["photo_epoch"].toString(),QString::number(photo));QCOMPARE(accepted["completion"].toString(),QString("calibration-request-accepted"));
            if(closeOnly){QVERIFY(closed&&owner&&owner->m_closing);QCOMPARE(owner->m_calibrationJob->sequence(),stopped);delete owner.data();}else QVERIFY(owner.isNull());QCOMPARE(fileHash(path),original);
        }
    }
    void calibrationStartNotificationsMayCloseOrDestroyOwner() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("calibration-terminal.png");const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(path));const auto bytes=fileHash(path);
        for(bool closeOnly:{false,true}) {
            std::atomic_int starts{0};ProcessedImageProvider provider;QPointer<PhotoController> owner=new PhotoController(&provider);owner->setGpuEnabled(false);
            QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering(),15000);
            QVERIFY(owner->flushEdits());owner->m_refineTimer.stop();owner->m_photos[0].raw=true;owner->m_referenceImage=image;
            owner->m_calibrationJob=std::make_unique<LatestJob<LookCalibrationRequest,LookCalibrationResult>>([&](const LookCalibrationRequest &,const CancelToken &){++starts;return LookCalibrationResult{};},[](const LookCalibrationRequest &,LookCalibrationResult){});
            bool notified=false,closed=false;quint64 stopped=0;
            QObject::connect(owner,&PhotoController::calibrationChanged,owner,[&]{if(notified)return;notified=true;if(closeOnly){closed=owner->beginClose();stopped=owner->m_calibrationJob->sequence();}else delete owner.data();});
            QVERIFY(owner->calibrateFromReference());QVERIFY(notified);QCOMPARE(starts.load(),0);
            if(closeOnly){QVERIFY(closed&&owner&&owner->m_closing);QCOMPARE(owner->m_calibrationJob->sequence(),stopped);QVERIFY(!owner->calibrateFromReference());owner->cancelCalibration();owner->setLookMode("as-shot");QCOMPARE(owner->m_calibrationJob->sequence(),stopped);delete owner.data();}
            else QVERIFY(owner.isNull());
        }
        QCOMPARE(fileHash(path),bytes);
    }
    void calibrationStartCancellationAndNestedRestartKeepLatestRequest() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("calibration-reentry.png");
        const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(path));const auto bytes=fileHash(path);
        for(const auto &action:QStringList{"cancel","restart","snapshot"}) {
            std::atomic_int starts{0};QList<LookCalibrationRequest> delivered;ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);
            QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
            c.m_photos[0].raw=true;c.m_referenceImage=image;c.m_referenceInfo={{"fixture","original"}};c.m_currentMetadata["model"]="original-camera";
            c.m_calibrationJob=std::make_unique<LatestJob<LookCalibrationRequest,LookCalibrationResult>>([&](const LookCalibrationRequest &,const CancelToken &){++starts;return LookCalibrationResult{};},[&](const LookCalibrationRequest &request,LookCalibrationResult){delivered.append(request);c.m_calibrationBusy=false;});
            const auto sequence=c.m_calibrationJob->sequence(),revision=c.m_requestedRevision;const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto status=c.statusMessage();
            bool foreign=true;std::thread worker([&]{foreign=c.calibrateFromReference();c.cancelCalibration();c.setLookMode("as-shot");});worker.join();QVERIFY(!foreign);QCOMPARE(c.m_calibrationJob->sequence(),sequence);QCOMPARE(c.statusMessage(),status);QVERIFY(!c.calibrationBusy());
            bool notified=false,nested=false;QObject::connect(&c,&PhotoController::calibrationChanged,&c,[&]{if(notified||!c.calibrationBusy())return;notified=true;c.m_referenceInfo["fixture"]="nested";c.m_currentMetadata["model"]="nested-camera";if(action!="snapshot")c.cancelCalibration();if(action=="restart")nested=c.calibrateFromReference();});
            QVERIFY(c.calibrateFromReference());QVERIFY(notified);
            if(action=="cancel"){QCOMPARE(starts.load(),0);QVERIFY(!c.calibrationBusy());QVERIFY(delivered.isEmpty());}
            else {QCOMPARE(nested,action=="restart");QTRY_COMPARE_WITH_TIMEOUT(delivered.size(),1,5000);QCOMPARE(starts.load(),1);const auto &request=delivered.first();QCOMPARE(request.provenance.value("fixture").toString(),action=="restart"?QString("nested"):QString("original"));QCOMPARE(request.provenance.value("camera").toString(),action=="restart"?QString("nested-camera"):QString("original-camera"));QCOMPARE(request.provenance.value("sourceIdentity").toString(),c.m_loadedKey);QCOMPARE(request.photo,c.m_photoEpoch);QCOMPARE(request.revision,revision);}
            QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(c.m_requestedRevision,revision);
        }
        QCOMPARE(fileHash(path),bytes);
    }
    void referenceResetClearsCalibrationInputsBeforeNotifying() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto first=dir.filePath("calibration-first.png"),second=dir.filePath("calibration-second.png");
        const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(first));QVERIFY(image.mirrored().save(second));const auto firstBytes=fileHash(first),secondBytes=fileHash(second);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(first)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);
        QVERIFY(c.importFile(QUrl::fromLocalFile(second)));c.selectPhoto(0);QTRY_VERIFY_WITH_TIMEOUT(c.currentIndex()==0&&c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_refineTimer.stop();
        c.m_photos[1].raw=true;c.m_referenceImage=image;c.m_referenceInfo={{"fixture","old-reference"}};c.m_referenceScopes.pixelCount=7680;c.m_manualReferencePath="old-reference.jpg";c.m_calibrationReport={{"fixture","old-report"}};c.m_calibrationBusy=true;provider.setReference(image);
        bool notified=false,accepted=true,empty=false;QObject::connect(&c,&PhotoController::calibrationChanged,&c,[&]{if(notified||c.currentIndex()!=1)return;notified=true;empty=c.m_referenceImage.isNull()&&c.m_referenceInfo.isEmpty()&&c.m_referenceScopes.pixelCount==0&&c.m_manualReferencePath.isEmpty()&&c.m_calibrationReport.isEmpty()&&provider.requestImage("reference/1",nullptr,{}).isNull();accepted=c.calibrateFromReference();});
        c.selectPhoto(1);QVERIFY(notified&&empty);QVERIFY(!accepted);QVERIFY(!c.calibrationBusy());QVERIFY(!c.m_calibrationJob->busy());QCOMPARE(fileHash(first),firstBytes);QCOMPARE(fileHash(second),secondBytes);
    }
    void calibrationCompletionNotificationCanRetireResult() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("calibration-completion.png");QVERIFY(sourceImage().scaled(160,128).save(path));const auto bytes=fileHash(path);
        for(bool restart:{false,true}) {
            ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();c.m_exactTimer.stop();c.m_plotTimer.stop();
            c.m_photos[0].raw=true;const auto small=c.m_fullSource.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);c.m_referenceImage=ImagePipeline::process(small,{},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb);QVERIFY(!c.m_referenceImage.isNull());
            for(int y=0;y<c.m_referenceImage.height();++y){auto *p=reinterpret_cast<QRgba64 *>(c.m_referenceImage.scanLine(y));for(int x=0;x<c.m_referenceImage.width();++x)p[x]=QRgba64::fromRgba64(p[x].red()*.88+2200,p[x].green()*.95+1000,p[x].blue()*.86+2800,65535);}
            const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision;bool notified=false,nested=false;QVariantMap completed;
            QObject::connect(&c,&PhotoController::calibrationChanged,&c,[&]{if(notified||c.calibrationBusy()||c.calibrationReport().isEmpty())return;notified=true;completed=c.calibrationReport();c.cancelCalibration();if(restart){c.m_referenceImage=c.m_referenceImage.scaled(10,8);nested=c.calibrateFromReference();}});
            QVERIFY(c.calibrateFromReference());QTRY_VERIFY_WITH_TIMEOUT(notified&&!c.calibrationBusy()&&!c.m_calibrationJob->busy(),10000);QVERIFY(completed.value("error").toString().isEmpty());QCOMPARE(nested,restart);
            if(restart)QVERIFY(c.calibrationReport().value("error").toString().contains("too small"));
            QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(fileHash(path),bytes);
        }
    }
    void cameraReferenceSelectionLoadsReadOnlyJPEGAndSharesLegacyAdapter() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("selection-source.png"),jpeg=dir.filePath("selection-reference.jpg");
        const auto image=sourceImage().scaled(192,128);QVERIFY(image.save(path));QVERIFY(image.convertToFormat(QImage::Format_RGB888).save(jpeg));
        const auto sourceBytes=fileHash(path),jpegBytes=fileHash(jpeg);ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);
        const QJsonObject load{{"command","camera-reference.load"},{"source",jpeg}},automatic{{"command","camera-reference.auto"}};
        QVERIFY(!c.executeCommand(load.toVariantMap()));QVERIFY(!c.useAutomaticCameraReference());
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&c.scopesPixelCount()>0,15000);
        QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision,photo=c.m_photoEpoch;
        const auto preview=StageGraph::outputFingerprint(c.m_processedPreview),source=StageGraph::outputFingerprint(c.m_fullSource);const auto scopes=c.m_scopes;
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("reference_selection_test_boundary",{{"marker",marker}});
        const auto commands=[&]{QJsonArray rows;bool seen=false;for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject();if(row["action"]=="reference_selection_test_boundary"&&row["marker"]==marker){seen=true;rows={};}else if(seen&&row["action"]=="camera_reference_selection_command")rows.append(row["command"]);}return rows;};
        QVERIFY(c.executeCommand(load.toVariantMap()));QVERIFY(c.showCameraReference());QVERIFY(c.referenceBusy());QVERIFY(c.cameraReferenceUrl().isEmpty());QVERIFY(c.cameraReferenceInfo().isEmpty());QCOMPARE(c.referenceHistogram().value("pixels").toULongLong(),qulonglong(0));
        QTRY_VERIFY_WITH_TIMEOUT(!c.referenceBusy()&&!c.cameraReferenceUrl().isEmpty(),5000);
        QCOMPARE(c.cameraReferenceInfo().value("kind").toString(),QString("manual-reference"));QCOMPARE(c.cameraReferenceInfo().value("pairStatus").toString(),QString("user-selected-not-authenticated"));
        const auto reference=StageGraph::outputFingerprint(c.m_referenceImage);QVERIFY(c.loadReference(QUrl::fromLocalFile(jpeg)));
        QTRY_VERIFY_WITH_TIMEOUT(!c.referenceBusy()&&!c.cameraReferenceUrl().isEmpty(),5000);QCOMPARE(StageGraph::outputFingerprint(c.m_referenceImage),reference);
        const auto referenceRevision=c.m_referenceRevision,selectionRevision=c.m_referenceSelectionRevision;
        for(const auto &bad:QList<QVariantMap>{{{"command","camera-reference.load"},{"source",true}},{{"command","camera-reference.load"},{"source",dir.filePath("missing.jpg")}},{{"command","camera-reference.load"},{"source",dir.path()}},{{"command","camera-reference.auto"},{"extra",true}},automatic.toVariantMap()})QVERIFY(!c.executeCommand(bad));
        QVERIFY(!c.loadReference(QUrl("https://example.invalid/reference.jpg")));QVERIFY(!c.useAutomaticCameraReference());
        bool generic=true,adapter=true,autoAdapter=true;std::thread worker([&]{generic=c.executeCommand(load.toVariantMap());adapter=c.loadReference(QUrl::fromLocalFile(jpeg));autoAdapter=c.useAutomaticCameraReference();c.openReferenceDialog();});worker.join();QVERIFY(!generic&&!adapter&&!autoAdapter);
        QCOMPARE(c.m_referenceRevision,referenceRevision);QCOMPARE(c.m_referenceSelectionRevision,selectionRevision);QCOMPARE(commands(),QJsonArray({load,load}));
        const auto corrupt=dir.filePath("unreadable-reference.jpg");QFile bad(corrupt);QVERIFY(bad.open(QIODevice::WriteOnly));QCOMPARE(bad.write("not a JPEG"),qint64(10));bad.close();
        QVERIFY(c.loadReference(QUrl::fromLocalFile(corrupt)));QVERIFY(c.cameraReferenceUrl().isEmpty());QVERIFY(c.cameraReferenceInfo().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!c.referenceBusy(),5000);QVERIFY(c.cameraReferenceUrl().isEmpty());QVERIFY(!c.cameraReferenceInfo().value("error").toString().isEmpty());QCOMPARE(c.referenceHistogram().value("pixels").toULongLong(),qulonglong(0));
        QVERIFY(c.loadReference(QUrl::fromLocalFile(jpeg)));QTRY_VERIFY_WITH_TIMEOUT(!c.referenceBusy()&&!c.cameraReferenceUrl().isEmpty(),5000);QCOMPARE(StageGraph::outputFingerprint(c.m_referenceImage),reference);
        QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.m_photoEpoch,photo);
        QCOMPARE(StageGraph::outputFingerprint(c.m_processedPreview),preview);QCOMPARE(StageGraph::outputFingerprint(c.m_fullSource),source);QVERIFY(sameHistogram(c.m_scopes,scopes));
        QCOMPARE(fileHash(path),sourceBytes);QCOMPARE(fileHash(jpeg),jpegBytes);QVERIFY(c.isProtectedPhoto(jpeg));
    }
    void cameraReferenceResetNotificationUsesOnlyItsActualSourceMetadata() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto first=dir.filePath("metadata-first.png"),second=dir.filePath("metadata-second.png");
        const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(first));QVERIFY(image.mirrored().save(second));
        const auto firstBytes=fileHash(first),secondBytes=fileHash(second);QMutex requestMutex;QList<CameraReferenceRequest> requests;std::atomic_int captured{0};
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(first)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);
        QVERIFY(c.importFile(QUrl::fromLocalFile(second)));c.selectPhoto(0);QTRY_VERIFY_WITH_TIMEOUT(c.currentIndex()==0&&c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        QVERIFY(!c.m_loadedKey.isEmpty());QCOMPARE(c.m_loadedKey,SourceCache::fileKey(first));QVERIFY(c.m_loadedKey!=SourceCache::fileKey(second));
        const QVariantMap previousMetadata{{"model","previous-photo-sentinel"},{"probe_owner","first-photo"},{"captureTime","2025-01-01T00:00:01"}};c.m_currentMetadata=previousMetadata;
        c.m_referenceJob=std::make_unique<LatestJob<CameraReferenceRequest,CameraReferenceResult>>([&](const CameraReferenceRequest &request,const CancelToken &){{QMutexLocker lock(&requestMutex);requests.append(request);}++captured;return CameraReferenceResult{};},[&](const CameraReferenceRequest &,CameraReferenceResult){c.m_referenceBusy=false;});
        // Synthetic RAW flags only admit isolated automatic commands for the
        // actual tiny PNG sources. This does not qualify RAW decoding.
        c.m_photos[0].raw=true;QVERIFY(c.useAutomaticCameraReference());c.m_photos[0].raw=false;
        QTRY_COMPARE_WITH_TIMEOUT(captured.load(),1,5000);QTRY_VERIFY_WITH_TIMEOUT(!c.m_referenceJob->busy(),5000);
        CameraReferenceRequest firstRequest;{QMutexLocker lock(&requestMutex);firstRequest=requests.first();}QCOMPARE(firstRequest.rawPath,first);QCOMPARE(firstRequest.metadata,previousMetadata);
        bool invoked=false,accepted=false;QVariantMap notificationMetadata;
        QObject::connect(&c,&PhotoController::referenceChanged,&c,[&]{if(!invoked&&c.currentIndex()==1){invoked=true;notificationMetadata=c.m_currentMetadata;accepted=c.useAutomaticCameraReference();}});
        const auto priorPhoto=c.m_photoEpoch;const auto before=captured.load();c.m_photos[1].raw=true;c.selectPhoto(1);c.m_photos[1].raw=false;QVERIFY(invoked&&accepted);QCOMPARE(notificationMetadata,previousMetadata);
        QTRY_VERIFY_WITH_TIMEOUT(captured.load()>before,5000);CameraReferenceRequest currentRequest;{QMutexLocker lock(&requestMutex);currentRequest=requests[before];}
        QCOMPARE(currentRequest.rawPath,second);QCOMPARE(currentRequest.photo,c.m_photoEpoch);QVERIFY(currentRequest.photo!=priorPhoto);QVERIFY(currentRequest.manualPath.isEmpty());QVERIFY(currentRequest.metadata.isEmpty());
        QCOMPARE(fileHash(first),firstBytes);QCOMPARE(fileHash(second),secondBytes);
    }
    void cameraReferenceSelectionPhotoSwitchCannotSubmitTheOldManualRequest() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto first=dir.filePath("reference-switch-first.png"),second=dir.filePath("reference-switch-second.png"),jpeg=dir.filePath("reference-switch.jpg");
        const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(first));QVERIFY(image.mirrored().save(second));QVERIFY(image.convertToFormat(QImage::Format_RGB888).save(jpeg));
        const auto firstBytes=fileHash(first),secondBytes=fileHash(second),jpegBytes=fileHash(jpeg);QList<CameraReferenceRequest> delivered;
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(first)));QVERIFY(c.importFile(QUrl::fromLocalFile(second)));c.selectPhoto(0);
        QTRY_VERIFY_WITH_TIMEOUT(c.currentIndex()==0&&c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto firstIntent=c.m_photos[0].state.toJson(),secondIntent=c.m_photos[1].state.toJson();const auto photo=c.m_photoEpoch;
        c.m_referenceJob=std::make_unique<LatestJob<CameraReferenceRequest,CameraReferenceResult>>([](const CameraReferenceRequest &,const CancelToken &){return CameraReferenceResult{};},[&](const CameraReferenceRequest &request,CameraReferenceResult){delivered.append(request);c.m_referenceBusy=false;});
        bool switched=false;QObject::connect(&c,&PhotoController::referenceChanged,&c,[&]{if(!switched&&c.currentIndex()==0){switched=true;c.selectPhoto(1);}});
        const QJsonObject command{{"command","camera-reference.load"},{"source",jpeg}};QVERIFY(c.executeCommand(command.toVariantMap()));QVERIFY(switched);QCOMPARE(c.currentIndex(),1);
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.referenceBusy()&&!delivered.isEmpty(),15000);
        for(const auto &request:delivered){QCOMPARE(request.rawPath,second);QVERIFY(request.manualPath.isEmpty());QCOMPARE(request.photo,c.m_photoEpoch);QVERIFY(request.photo!=photo);}
        QVERIFY(c.m_manualReferencePath.isEmpty());QCOMPARE(c.m_photos[0].state.toJson(),firstIntent);QCOMPARE(c.m_photos[1].state.toJson(),secondIntent);
        QJsonObject accepted;for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject();if(row["action"]=="camera_reference_selection_command")accepted=row;}
        QCOMPARE(accepted["command"].toObject(),command);QCOMPARE(accepted["photo_epoch"].toString(),QString::number(photo));
        QCOMPARE(fileHash(first),firstBytes);QCOMPARE(fileHash(second),secondBytes);QCOMPARE(fileHash(jpeg),jpegBytes);
    }
    void cameraReferenceSelectionNotificationsCanCloseOrDestroyOwner() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("selection-owner.png"),jpeg=dir.filePath("selection-owner.jpg");
        const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(path));QVERIFY(image.convertToFormat(QImage::Format_RGB888).save(jpeg));
        for(bool calibrationSignal:{false,true})for(bool closeOnly:{false,true}) {
            ProcessedImageProvider provider;QPointer<PhotoController> owner=new PhotoController(&provider);owner->setGpuEnabled(false);
            QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&!owner->rendering(),15000);QVERIFY(owner->flushEdits());owner->m_saveTimer.stop();owner->m_saveMaxTimer.stop();owner->m_refineTimer.stop();
            bool notified=false,closed=false;const auto revision=owner->m_requestedRevision,photo=owner->m_photoEpoch;
            const auto notify=[&]{notified=true;if(closeOnly)closed=owner->beginClose();else delete owner.data();};
            if(calibrationSignal){owner->m_calibrationBusy=true;QObject::connect(owner,&PhotoController::calibrationChanged,owner,notify);}
            else QObject::connect(owner,&PhotoController::referenceChanged,owner,notify);
            const QJsonObject command{{"command","camera-reference.load"},{"source",jpeg}};QVERIFY(owner->executeCommand(command.toVariantMap()));QVERIFY(notified);
            QJsonObject accepted;for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject();if(row["action"]=="camera_reference_selection_command")accepted=row;}
            QCOMPARE(accepted["command"].toObject(),command);QCOMPARE(accepted["revision"].toString(),QString::number(revision));QCOMPARE(accepted["photo_epoch"].toString(),QString::number(photo));
            if(closeOnly){QVERIFY(closed);QVERIFY(owner&&owner->m_closing);QVERIFY(!owner->referenceBusy());QVERIFY(!owner->m_referenceJob->busy());QVERIFY(!owner->loadReference(QUrl::fromLocalFile(jpeg)));QVERIFY(!owner->useAutomaticCameraReference());delete owner.data();}else QVERIFY(owner.isNull());
        }
    }
    void cameraReferenceSelectionReentrySubmitsOnlyLatestIntent() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("selection-reentry.png"),jpeg=dir.filePath("selection-reentry.jpg");
        const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(path));QVERIFY(image.convertToFormat(QImage::Format_RGB888).save(jpeg));
        for(bool onBusy:{false,true}) {
            std::atomic_int starts{0};QList<CameraReferenceRequest> delivered;
            ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();c.m_photos[c.m_currentIndex].raw=true;
            c.m_referenceJob=std::make_unique<LatestJob<CameraReferenceRequest,CameraReferenceResult>>([&](const CameraReferenceRequest &,const CancelToken &){++starts;return CameraReferenceResult{};},[&](const CameraReferenceRequest &request,CameraReferenceResult){delivered.append(request);c.m_referenceBusy=false;});
            bool nested=false,nestedAccepted=false;QObject::connect(&c,&PhotoController::referenceChanged,&c,[&]{if(!nested&&c.referenceBusy()==onBusy){nested=true;nestedAccepted=c.useAutomaticCameraReference();}});
            const auto revision=c.m_requestedRevision,photo=c.m_photoEpoch;
            QVERIFY(c.loadReference(QUrl::fromLocalFile(jpeg)));QVERIFY(nested&&nestedAccepted);QTRY_COMPARE_WITH_TIMEOUT(delivered.size(),1,5000);
            QCOMPARE(starts.load(),1);QVERIFY(delivered.first().manualPath.isEmpty());QCOMPARE(delivered.first().rawPath,path);QCOMPARE(delivered.first().photo,photo);QVERIFY(c.m_manualReferencePath.isEmpty());QCOMPARE(c.m_requestedRevision,revision);
            QJsonArray commands;for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject();if(row["action"]=="camera_reference_selection_command")commands.append(row["command"]);}
            QVERIFY(commands.size()>=2);QCOMPARE(commands[commands.size()-2].toObject()["command"].toString(),QString("camera-reference.load"));QCOMPARE(commands.last().toObject(),QJsonObject({{"command","camera-reference.auto"}}));
        }
    }
    void cameraReferenceSelectionSupersedesBlockedWorkWithoutPublishingOldPixels() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("selection-pending.png"),jpeg=dir.filePath("selection-pending.jpg");
        const auto image=sourceImage().scaled(96,80);QVERIFY(image.save(path));QVERIFY(image.convertToFormat(QImage::Format_RGB888).save(jpeg));
        QSemaphore entered,release;QStringList published;std::atomic_int starts{0};ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();c.m_photos[c.m_currentIndex].raw=true;
        c.m_referenceJob=std::make_unique<LatestJob<CameraReferenceRequest,CameraReferenceResult>>([&](const CameraReferenceRequest &request,const CancelToken &){++starts;if(!request.manualPath.isEmpty()){entered.release();release.tryAcquire(1,5000);}CameraReferenceResult result;result.info["kind"]=request.manualPath.isEmpty()?"automatic":"stale-manual";return result;},[&](const CameraReferenceRequest &,CameraReferenceResult result){published.append(result.info["kind"].toString());c.m_referenceBusy=false;});
        const auto cleanup=qScopeGuard([&]{release.release();});QVERIFY(c.loadReference(QUrl::fromLocalFile(jpeg)));QVERIFY(entered.tryAcquire(1,1000));QVERIFY(c.useAutomaticCameraReference());QVERIFY(c.referenceBusy());release.release();
        QTRY_COMPARE_WITH_TIMEOUT(published,QStringList{"automatic"},5000);QCOMPARE(starts.load(),2);QTRY_VERIFY_WITH_TIMEOUT(!c.m_referenceJob->busy(),5000);QVERIFY(!c.referenceBusy());QVERIFY(c.m_manualReferencePath.isEmpty());QVERIFY(c.isProtectedPhoto(jpeg));
    }
    void cameraReferenceCommandSharesAdaptersWithoutChangingPhotoIntentOrLoadedPixels() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("reference-commands.png"),jpeg=dir.filePath("manual-reference.jpg");
        const auto image=sourceImage().scaled(321,197);QVERIFY(image.save(path));QVERIFY(image.convertToFormat(QImage::Format_RGB888).save(jpeg));
        const auto original=fileHash(path),referenceBytes=fileHash(jpeg);ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&c.scopesPixelCount()>0,15000);
        QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();c.m_manualReferencePath=jpeg;
        const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision,photo=c.m_photoEpoch;
        const auto pixels=StageGraph::outputFingerprint(c.m_processedPreview),source=StageGraph::outputFingerprint(c.m_fullSource);const auto scopes=c.m_scopes;
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("reference_test_boundary",{{"marker",marker}});
        const auto captured=[&]{QJsonArray result;bool seen=false;for(const auto &value:ActionTrace::instance().snapshot()){const auto row=value.toObject();if(row["action"]=="reference_test_boundary"&&row["marker"]==marker){seen=true;result={};}else if(seen&&row["action"]=="camera_reference_command")result.append(row["command"]);}return result;};
        const QJsonObject show{{"command","camera-reference.show"},{"enabled",true}},hide{{"command","camera-reference.show"},{"enabled",false}};
        QSignalSpy changed(&c,&PhotoController::referenceChanged);QVERIFY(c.executeCommand(hide.toVariantMap()));QCOMPARE(changed.count(),0);
        QVERIFY(c.executeCommand(show.toVariantMap()));QVERIFY(c.showCameraReference());QVERIFY(c.referenceBusy());
        const auto pendingSignals=changed.count();QVERIFY(c.executeCommand(show.toVariantMap()));QCOMPARE(changed.count(),pendingSignals);
        QTRY_VERIFY_WITH_TIMEOUT(!c.referenceBusy()&&!c.cameraReferenceUrl().isEmpty(),5000);
        QCOMPARE(c.cameraReferenceInfo().value("kind").toString(),QString("manual-reference"));
        const auto loaded=c.m_referenceRevision;const auto url=c.cameraReferenceUrl();const auto referencePixels=StageGraph::outputFingerprint(c.m_referenceImage);
        c.setShowCameraReference(false);QVERIFY(!c.showCameraReference());QCOMPARE(c.cameraReferenceUrl(),url);
        c.setShowCameraReference(true);QVERIFY(c.showCameraReference());QVERIFY(!c.referenceBusy());QCOMPARE(c.m_referenceRevision,loaded);QCOMPARE(StageGraph::outputFingerprint(c.m_referenceImage),referencePixels);
        const QJsonArray expected{hide,show,show,hide,show};QCOMPARE(captured(),expected);
        for(const auto &bad:QList<QVariantMap>{{{"command","camera-reference.show"},{"enabled",1}},{{"command","camera-reference.show"},{"enabled",false},{"extra",true}},{{"command","camera-reference.unknown"},{"enabled",false}}})QVERIFY(!c.executeCommand(bad));
        bool foreign=true;std::thread worker([&]{foreign=c.executeCommand(hide.toVariantMap());c.setShowCameraReference(false);});worker.join();QVERIFY(!foreign);QVERIFY(c.showCameraReference());
        c.m_closing=true;QVERIFY(!c.executeCommand(hide.toVariantMap()));c.setShowCameraReference(false);c.m_closing=false;QVERIFY(c.showCameraReference());QCOMPARE(captured(),expected);
        QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.m_photoEpoch,photo);
        QCOMPARE(StageGraph::outputFingerprint(c.m_processedPreview),pixels);QCOMPARE(StageGraph::outputFingerprint(c.m_fullSource),source);QVERIFY(sameHistogram(c.m_scopes,scopes));
        QCOMPARE(fileHash(path),original);QCOMPARE(fileHash(jpeg),referenceBytes);
    }
    void cameraReferenceVisibilityNotificationCanDeleteOrCloseItsOwner() {
        for(bool closeOnly:{false,true}) {
            ProcessedImageProvider provider;QPointer<PhotoController> owner=new PhotoController(&provider);bool notified=false,closed=false;
            const auto revision=owner->m_requestedRevision,photo=owner->m_photoEpoch;
            QObject::connect(owner,&PhotoController::referenceChanged,owner,[&]{notified=true;if(closeOnly)closed=owner->beginClose();else delete owner.data();});
            const QJsonObject command{{"command","camera-reference.show"},{"enabled",true}};QVERIFY(owner->executeCommand(command.toVariantMap()));QVERIFY(notified);
            const auto row=ActionTrace::instance().snapshot().last().toObject();QCOMPARE(row["action"].toString(),QString("camera_reference_command"));QCOMPARE(row["command"].toObject(),command);
            QCOMPARE(row["revision"].toString(),QString::number(revision));QCOMPARE(row["photo_epoch"].toString(),QString::number(photo));QCOMPARE(row["completion"].toString(),QString("visibility-request-accepted"));
            if(closeOnly){QVERIFY(closed);QVERIFY(owner&&owner->m_closing);QVERIFY(!owner->referenceBusy());QVERIFY(!owner->m_referenceJob->busy());delete owner.data();}else QVERIFY(owner.isNull());
        }
    }
    void cameraReferenceReentrantHideKeepsAcceptanceOrderAndStartsNoSupersededLoad() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("reference-reentry.png");QVERIFY(sourceImage().scaled(96,80).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.rendering(),15000);
        std::atomic_int loads{0};c.m_referenceJob=std::make_unique<LatestJob<CameraReferenceRequest,CameraReferenceResult>>([&](const CameraReferenceRequest &,const CancelToken &){++loads;return CameraReferenceResult{};},[](const CameraReferenceRequest &,CameraReferenceResult){});
        const auto revision=c.m_requestedRevision;bool nested=false;
        QObject::connect(&c,&PhotoController::referenceChanged,&c,[&]{if(c.showCameraReference()){nested=true;c.setShowCameraReference(false);}});
        QVERIFY(c.executeCommand({{"command","camera-reference.show"},{"enabled",true}}));QVERIFY(nested);QVERIFY(!c.showCameraReference());QVERIFY(!c.referenceBusy());QVERIFY(!c.m_referenceJob->busy());QCOMPARE(loads.load(),0);QCOMPARE(c.m_requestedRevision,revision);
        const auto trace=ActionTrace::instance().snapshot();QVERIFY(trace.size()>=2);
        const auto outer=trace[trace.size()-2].toObject(),inner=trace.last().toObject();
        QCOMPARE(outer["action"].toString(),QString("camera_reference_command"));QVERIFY(outer["command"].toObject()["enabled"].toBool());
        QCOMPARE(inner["action"].toString(),QString("camera_reference_command"));QVERIFY(!inner["command"].toObject()["enabled"].toBool());
    }
    void scopePresentationCommandsAndLegacyAdaptersShareStrictAcceptedTraces() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("scope-commands.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&c.scopesPixelCount()>0,15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision;const auto pixels=StageGraph::outputFingerprint(c.m_processedPreview);const auto expected=c.m_scopes;
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("scopes_test_boundary",{{"marker",marker}});
        const auto captured=[&]{QJsonArray result;bool seen=false;for(const auto &value:ActionTrace::instance().snapshot()){const auto row=value.toObject();if(row["action"]=="scopes_test_boundary"&&row["marker"]==marker){seen=true;result={};}else if(seen&&row["action"]=="scopes_command")result.append(row["command"]);}return result;};
        const QJsonArray commands{QJsonObject{{"command","scopes.domain"},{"domain","source"}},QJsonObject{{"command","scopes.domain"},{"domain","working"}},QJsonObject{{"command","scopes.domain"},{"domain","display"}},
            QJsonObject{{"command","scopes.mode"},{"mode","waveform"}},QJsonObject{{"command","scopes.mode"},{"mode","parade"}},QJsonObject{{"command","scopes.mode"},{"mode","vectorscope"}},QJsonObject{{"command","scopes.mode"},{"mode","histogram"}},
            QJsonObject{{"command","scopes.full-resolution"},{"enabled",false}},QJsonObject{{"command","scopes.full-resolution"},{"enabled",true}},QJsonObject{{"command","scopes.full-resolution"},{"enabled",false}}};
        for(const auto &value:commands)QVERIFY(c.executeCommand(value.toObject().toVariantMap()));
        // The final resolution toggle cancels both earlier producers. Even
        // before Display's restored preview arrives, a replacement is required.
        QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()>0&&!c.m_inputScopeJob->busy(),15000);QVERIFY(sameHistogram(c.m_scopes,expected));QCOMPARE(captured(),commands);
        const auto reject=[&](QVariantMap command){QVERIFY(!c.executeCommand(command));QCOMPARE(c.scopeDomain(),QString("display"));QCOMPARE(c.scopeMode(),QString("histogram"));QVERIFY(!c.exactScopes());QCOMPARE(captured(),commands);};
        reject({{"command","scopes.domain"},{"domain","Source"}});reject({{"command","scopes.mode"},{"mode",true}});reject({{"command","scopes.full-resolution"},{"enabled",1}});reject({{"command","scopes.mode"},{"mode","waveform"},{"extra",true}});reject({{"command","scopes.unknown"}});
        bool foreign=true;std::thread worker([&]{foreign=c.executeCommand({{"command","scopes.domain"},{"domain","source"}});});worker.join();QVERIFY(!foreign);QCOMPARE(captured(),commands);
        c.m_closing=true;QVERIFY(!c.executeCommand({{"command","scopes.domain"},{"domain","source"}}));c.m_closing=false;
        c.setScopeDomain("display");c.setScopeMode("histogram");c.setExactScopes(false);auto adapters=commands;adapters.append(QJsonObject{{"command","scopes.domain"},{"domain","display"}});adapters.append(QJsonObject{{"command","scopes.mode"},{"mode","histogram"}});adapters.append(QJsonObject{{"command","scopes.full-resolution"},{"enabled",false}});QCOMPARE(captured(),adapters);
        QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(StageGraph::outputFingerprint(c.m_processedPreview),pixels);QCOMPARE(fileHash(path),original);
    }
    void linearPreviewBudgetRefusalRetriesAndSwitchingClearsDeferredWork() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("budget-domains.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        auto budget=std::make_shared<BrushRenderBudget>(512LL*1024*1024);ProcessedImageProvider provider;PhotoController c(&provider,nullptr,budget);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto saved=c.m_photos[c.m_currentIndex].state;const auto restore=qScopeGuard([&]{c.m_photos[c.m_currentIndex].state=saved;c.m_saveTimer.stop();c.m_saveMaxTimer.stop();});c.m_photos[c.m_currentIndex].state=painted();++c.m_requestedRevision;c.m_scopesRank=-1;
        std::vector<std::unique_ptr<BrushAnalysisRequest>> held;QString error;BrushAdmissionFailure reason=BrushAdmissionFailure::None;
        const auto fill=[&]{for(int i=0;i<64;++i){auto request=BrushAnalysisRequest::createPrepared(c.m_previewSource,c.gpuPlan(),BrushRenderRequest::Layout::Stripes128,{},&error,budget,&reason);if(!request)break;held.push_back(std::move(request));}};
        fill();QVERIFY(!held.empty());QCOMPARE(reason,BrushAdmissionFailure::BudgetUnavailable);c.setScopeDomain("working");QTRY_VERIFY_WITH_TIMEOUT(c.m_deferredBudgetJobs[3].pending,5000);QCOMPARE(c.scopesPixelCount(),qulonglong(0));QVERIFY(c.scopesStatus().contains("memory")||c.scopesStatus().contains("内存"));
        held.clear();c.retryBudgetJobs(true);QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()>0&&!c.m_deferredBudgetJobs[3].pending,15000);QCOMPARE(c.m_scopesDomain,AnalysisDomain::Working);QCOMPARE(budget->snapshot().charged,qint64(0));
        c.setScopeDomain("source");QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()>0,15000);fill();c.setScopeDomain("working");
        // The completed result cache may satisfy the same Working intent. Use
        // a different exposure so this second request must be admitted again.
        c.m_photos[c.m_currentIndex].state.exposure=.7;++c.m_requestedRevision;c.m_inputScopeSubmittedRevision=0;c.requestPreviewScopes();QTRY_VERIFY_WITH_TIMEOUT(c.m_deferredBudgetJobs[3].pending,5000);
        c.setScopeDomain("source");QVERIFY(!c.m_deferredBudgetJobs[3].pending);QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()>0,15000);QCOMPARE(c.m_scopesDomain,AnalysisDomain::Source);QCOMPARE(fileHash(path),original);held.clear();
    }
    void controllerStartsPendingExactAfterFinalFrame_data() {
        QTest::addColumn<QString>("context");
        for (const auto *name : {"final-current", "stale-frame", "interactive-frame",
                                "preparing", "preview-statistics", "no-pending"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void controllerStartsPendingExactAfterFinalFrame() {
        QFETCH(QString, context);
        QStandardPaths::setTestModeEnabled(true); QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto path=dir.filePath("final-preview-scopes.png");
        const auto image=sourceImage().scaled(321,197); QVERIFY(image.save(path)); const auto hash=fileHash(path);
        QSemaphore entered,release;
        ProcessedImageProvider provider; PhotoController controller(&provider); controller.setGpuEnabled(false);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.brushReady() && controller.m_sourceIsFull
            && !controller.m_prepare->busy() && !controller.m_render->busy(),15000);
        QVERIFY(controller.flushEdits());
        controller.m_saveTimer.stop(); controller.m_saveMaxTimer.stop(); controller.m_refineTimer.stop();
        controller.m_exactTimer.stop(); controller.setScopeMode("histogram");
        const auto saved=controller.m_photos[controller.m_currentIndex].state;
        const auto restore=qScopeGuard([&] {
            release.release(); controller.m_fullScopeJob->cancel();
            controller.m_photos[controller.m_currentIndex].state=saved;
            controller.m_preparing=false; controller.m_interacting=false;
            controller.m_saveTimer.stop(); controller.m_saveMaxTimer.stop();
            controller.m_exactTimer.stop(); controller.m_plotTimer.stop();
        });
        controller.m_photos[controller.m_currentIndex].state=painted();
        ++controller.m_requestedRevision; controller.m_scopesRank=-1;
        const auto intent=controller.gpuPlan(true).state.toJson(); const auto history=QJsonArray::fromVariantList(controller.editHistory());
        const auto histograms=std::make_shared<FullScopesCache>(4*1024*1024,controller.m_analysisFrameCache,controller.m_brushBudget);
        controller.m_fullScopeJob=std::make_unique<LatestJob<ScopeRequest,ScopesResult>>(
            [&,histograms](const ScopeRequest &request,const CancelToken &cancel) {
                entered.release();
                while (!release.tryAcquire(1,20)) if (cancelled(cancel)) return ScopesResult{};
                return histograms->analyze(request,cancel);
            },[&](const ScopeRequest &request,ScopesResult result) {
                controller.acceptScopes(request.revision,result,1,"Full-resolution statistics");
            });
        controller.m_preparing=context=="preparing";
        controller.m_interacting=context=="interactive-frame";
        controller.m_exactScopes=context!="preview-statistics";
        if (context!="no-pending") controller.m_exactTimer.start();
        controller.m_rendering=true;
        const auto sequence=controller.m_fullScopeJob->sequence();
        controller.completeFrame(controller.m_requestedRevision-(context=="stale-frame" ? 1 : 0));
        const bool starts=context=="final-current";
        QCOMPARE(controller.m_fullScopeJob->sequence(),sequence+(starts ? 1 : 0));
        QCOMPARE(controller.m_exactTimer.isActive(),!starts && context!="no-pending");
        if (starts) {
            // Repeated presentation of this revision cannot resubmit the work.
            controller.completeFrame(controller.m_requestedRevision);
            QCOMPARE(controller.m_fullScopeJob->sequence(),sequence+1);
        }
        controller.m_exactTimer.stop();
        bool responsive=false; QTimer::singleShot(0,&controller,[&] {responsive=true;});
        QTRY_VERIFY_WITH_TIMEOUT(responsive,1000);
        if (starts) {
            QTRY_VERIFY_WITH_TIMEOUT(entered.available()>0,5000); entered.acquire();
            QVERIFY(controller.m_fullScopeJob->busy());
            release.release();
            QTRY_VERIFY_WITH_TIMEOUT(!controller.m_fullScopeJob->busy(),15000);
            const auto expected=reference(controller.m_fullSource,controller.gpuPlan(true),controller.previewGeometry());
            QVERIFY(!expected.isNull()); QVERIFY(sameHistogram(controller.m_scopes,ScopesEngine::analyze(expected)));
            QCOMPARE(controller.scopesPixelCount(),qulonglong(321*197));
        } else {
            QCOMPARE(entered.available(),0); QVERIFY(!controller.m_fullScopeJob->busy());
        }
        QCOMPARE(controller.gpuPlan(true).state.toJson(),intent);
        QCOMPARE(QJsonArray::fromVariantList(controller.editHistory()),history); QCOMPARE(fileHash(path),hash);
    }

    void controllerDefersExactPlotsWithoutBlockingFallback_data() {
        QTest::addColumn<int>("completion");
        QTest::newRow("completed-frame-reuse")<<0;
        QTest::newRow("uncached-streamed-fallback")<<1;
        QTest::newRow("failed-histogram-fallback")<<2;
        QTest::newRow("cancelled-exact-preview")<<3;
    }
    void controllerDefersExactPlotsWithoutBlockingFallback() {
        QFETCH(int,completion);
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("scheduled-scopes.png");
        const auto image=sourceImage().scaled(321,197);QVERIFY(image.save(path));const auto hash=fileHash(path);
        QSemaphore entered,release;
        ProcessedImageProvider provider;PhotoController controller(&provider);controller.setGpuEnabled(false);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.brushReady()&&controller.m_sourceIsFull,15000);
        QVERIFY(controller.flushEdits());controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();controller.m_refineTimer.stop();
        const auto saved=controller.m_photos[controller.m_currentIndex].state;
        const auto restore=qScopeGuard([&]{
            release.release();controller.m_photos[controller.m_currentIndex].state=saved;
            controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();controller.m_exactTimer.stop();controller.m_plotTimer.stop();
        });
        controller.m_photos[controller.m_currentIndex].state=painted();++controller.m_requestedRevision;controller.m_scopesRank=-1;
        const auto intent=controller.gpuPlan(true).state.toJson();const auto history=controller.editHistory();
        const auto frames=completion==0?controller.m_analysisFrameCache:std::shared_ptr<AnalysisFrameCache>{};
        const auto histograms=std::make_shared<FullScopesCache>(4*1024*1024,frames,controller.m_brushBudget);
        controller.m_fullScopeJob=std::make_unique<LatestJob<ScopeRequest,ScopesResult>>(
            [&,histograms](const ScopeRequest &request,const CancelToken &cancel) {
                entered.release();
                while(!release.tryAcquire(1,20))if(cancelled(cancel))return ScopesResult{};
                if(completion==2){ScopesResult failed;failed.error="Controlled histogram failure";return failed;}
                return histograms->analyze(request,cancel);
            },[&](const ScopeRequest &request,ScopesResult result){controller.acceptScopes(request.revision,result,1,"Full-resolution statistics");});
        const auto deferred=counter("scope_plot_deferred_for_full_histogram"),encoded=counter("scope_plot_encoded_passes");
        controller.m_exactScopes=true;controller.m_exactTimer.start();controller.setScopeMode("waveform");
        const auto pendingSequence=controller.m_plotJob->sequence();controller.requestScopePlot();
        QCOMPARE(controller.m_plotJob->sequence(),pendingSequence);QVERIFY(!controller.m_plotJob->busy());QVERIFY(controller.m_plotTimer.isActive());
        QCOMPARE(counter("scope_plot_deferred_for_full_histogram"),deferred+1);
        controller.m_exactTimer.stop();controller.m_exactScopes=false;controller.setExactScopes(true);
        QTRY_VERIFY_WITH_TIMEOUT(entered.available()>0,5000);entered.acquire();
        const auto activeSequence=controller.m_plotJob->sequence();controller.requestScopePlot();
        QCOMPARE(controller.m_plotJob->sequence(),activeSequence);QVERIFY(!controller.m_plotJob->busy());QVERIFY(!controller.scopePlotCurrent());
        QVERIFY(counter("scope_plot_deferred_for_full_histogram")>=deferred+2);
        // A GUI turn runs while the producer is held; this is submission
        // ordering, never a blocking wait inside the controller or plot worker.
        bool responsive=false;QTimer::singleShot(0,&controller,[&]{responsive=true;});QTRY_VERIFY_WITH_TIMEOUT(responsive,1000);
        if(completion==3)controller.setExactScopes(false);
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.m_fullScopeJob->busy()&&controller.scopePlotCurrent(),15000);
        const auto source=completion==3?controller.m_previewSource:controller.m_fullSource;
        const auto expected=reference(source,controller.gpuPlan(completion!=3),completion==3?GeometryState{}:controller.previewGeometry());
        QVERIFY(!expected.isNull());const auto plot=renderEncodedScopePlot(expected,"waveform");
        QCOMPARE(provider.requestImage("scopes/current",nullptr,{}),plot.image);QCOMPARE(controller.scopePlotPixels(),plot.pixels);
        QCOMPARE(controller.m_plotFull,completion!=3);
        if(completion==0) {
            QVERIFY(sameHistogram(controller.m_scopes,ScopesEngine::analyze(expected)));
            QCOMPARE(counter("scope_plot_encoded_passes"),encoded+1);
            QVERIFY(controller.m_analysisFrameCache->snapshot()["hits"].toInteger()>=1);
        }else if(completion==2)QVERIFY(controller.scopesStatus().contains("Controlled histogram failure"));
        for(const auto &mode:{"parade","vectorscope"}) {
            controller.setScopeMode(mode);QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
            QCOMPARE(provider.requestImage("scopes/current",nullptr,{}),renderEncodedScopePlot(expected,mode).image);
        }
        QCOMPARE(controller.gpuPlan(true).state.toJson(),intent);QCOMPARE(controller.editHistory(),history);QCOMPARE(fileHash(path),hash);
        qInfo().noquote()<<"BRUSH_SCOPE_SCHEDULING_EVIDENCE "<<QJsonDocument(QJsonObject{
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"case",QString::fromLatin1(QTest::currentDataTag())},
            {"deferred_while_scheduled",true},{"deferred_while_running",true},{"gui_responsive",responsive},
            {"exact_plot_modes",3},{"completed_frame_reused",completion==0},{"preview_after_cancel",completion==3},
            {"source_pixels",qint64(plot.pixels)},{"source_unchanged",true},{"intent_and_history_unchanged",true}
        }).toJson(QJsonDocument::Compact);
    }
    void originalGeometryAndCompletedFrameReuse() {
        const auto source=sourceImage(true);const auto before=StageGraph::outputFingerprint(source);
        auto state=painted();state.geometry.crop={.1,.08,.53,.61};state.geometry.straighten=.7;state.geometry.quarterTurns=1;
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,.3f);
        auto geometry=state.geometry;geometry.crop={0,0,1,1}; // The actual uncropped crop-editor view.
        const auto expected=reference(source,plan,geometry);QVERIFY(!expected.isNull());
        const auto counts=ScopesEngine::analyze(expected);QVERIFY(counts.pixelCount>2*1024*1024);
        auto budget=std::make_shared<BrushRenderBudget>();auto frames=std::make_shared<AnalysisFrameCache>(64*1024*1024);
        FullScopesCache histograms(4*1024*1024,frames,budget);ScopeRequest request{source,plan,7,true,geometry};
        const auto actual=histograms.analyze(request);QVERIFY2(actual.pixelCount,qPrintable(actual.error));QVERIFY(sameHistogram(actual,counts));
        QCOMPARE(budget->snapshot().charged,0);QCOMPARE(frames->snapshot()["entries"].toInt(),1);
        const auto key=StageGraph::analysisFrameKey(source,plan,geometry);auto lease=frames->find(key);QVERIFY(lease);QCOMPARE(lease.image(),expected);
        auto denied=std::make_shared<BrushRenderBudget>(1);ScopePlotCache plots(16*1024*1024,frames,denied);
        const auto beforeEncoded=counter("scope_plot_encoded_passes");
        for(const auto &mode:{"waveform","parade","vectorscope"}) {
            const auto plot=plots.render({source,plan,geometry,mode,7,true});const auto oracle=renderEncodedScopePlot(expected,mode);
            QVERIFY2(!plot.image.isNull(),qPrintable(plot.error));QCOMPARE(plot.image,oracle.image);QCOMPARE(plot.pixels,counts.pixelCount);
        }
        QCOMPARE(counter("scope_plot_encoded_passes"),beforeEncoded+1);QCOMPARE(plots.snapshot()["hits"].toInteger(),2);
        QCOMPARE(denied->snapshot().charged,0);
        auto changed=request;changed.plan.state.geometry.flipHorizontal=true;
        const auto invalid=histograms.analyze(changed);QVERIFY(!invalid.error.isEmpty());QVERIFY(!invalid.retryableBudget);QCOMPARE(invalid.pixelCount,0);
        const auto badPlot=plots.render({source,changed.plan,geometry,"waveform",7,true});QVERIFY(!badPlot.error.isEmpty());QCOMPARE(badPlot.pixels,0);
        auto different=geometry;different.flipHorizontal=true;
        const auto miss=plots.render({source,plan,different,"waveform",7,true});QVERIFY(miss.retryableBudget);QCOMPARE(miss.pixels,0);
        QCOMPARE(StageGraph::outputFingerprint(source),before);
        ScopePlotCache streamed(16*1024*1024,{},budget);ScopePlotCache single(0,{},budget);
        const auto oracle=renderEncodedScopePlot(expected,"parade");
        QCOMPARE(streamed.render({source,plan,geometry,"parade",7,true}).image,oracle.image);
        QCOMPARE(single.render({source,plan,geometry,"parade",7,true}).image,oracle.image);QCOMPARE(budget->snapshot().charged,0);
    }
    void admissionFailuresAndCollectorCancellation() {
        const auto source=sourceImage();const auto state=painted();const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        QVERIFY(!ScopesEngine::analyzeFull(source,plan).pixelCount); // Keep the old research boundary.
        auto denied=std::make_shared<BrushRenderBudget>(1);auto frames=std::make_shared<AnalysisFrameCache>();
        FullScopesCache cache(4*1024*1024,frames,denied);ScopeRequest request{source,plan,9,true,{}};
        const auto rejected=cache.analyze(request);QVERIFY(rejected.retryableBudget);QVERIFY(rejected.error.contains("memory budget"));
        QCOMPARE(cache.snapshot()["entries"].toInt(),0);QCOMPARE(frames->snapshot()["entries"].toInt(),0);
        ScopePlotCache plots(16*1024*1024,frames,denied);QVERIFY(plots.render({source,plan,{},"waveform",9,true}).retryableBudget);
        BrushAdmissionFailure failure=BrushAdmissionFailure::None;QString error;auto budget=std::make_shared<BrushRenderBudget>();
        auto malformed=plan;malformed.data[ProcessingPlan::Tonal].x+=.1f;
        QVERIFY(!BrushRenderRequest::createOriginal(source,malformed,{},BrushRenderRequest::Layout::Stripes128,{},&error,budget,&failure));
        QCOMPARE(failure,BrushAdmissionFailure::Invalid);QCOMPARE(budget->snapshot().charged,0);
        auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(!BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,token,&error,budget,&failure));
        QCOMPARE(failure,BrushAdmissionFailure::Cancelled);token->store(false);
        auto admitted=BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,{},&error,budget,&failure);
        QVERIFY2(admitted,qPrintable(error));QCOMPARE(failure,BrushAdmissionFailure::None);
        int observers=0;const auto before=counter("full_scopes_collector_failures");
        const auto survived=ScopesEngine::analyzeAdmitted(*admitted,{},&error,nullptr,[&](const QImage &,int){++observers;throw std::runtime_error("optional collector fixture");});
        QVERIFY2(survived.pixelCount,qPrintable(error));QCOMPARE(observers,1);QCOMPARE(counter("full_scopes_collector_failures"),before+1);
        admitted.reset();QCOMPARE(budget->snapshot().charged,0);
        admitted=BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,token,&error,budget);
        QVERIFY2(admitted,qPrintable(error));auto writer=frames->begin("cancelled-admitted",source.size());QVERIFY(writer);
        std::array<ScopePlotResult,3> unpublished;unpublished[0].error="sentinel";int completed=0;
        const auto cancelledResult=ScopesEngine::analyzeAdmitted(*admitted,token,&error,&unpublished,[&](const QImage &strip,int y){
            if(!writer.append(strip,y,token))throw std::runtime_error("append failed");if(++completed==2)token->store(true);
        });
        QCOMPARE(completed,2);QCOMPARE(cancelledResult.pixelCount,0);QCOMPARE(unpublished[0].error,QString("sentinel"));
        QVERIFY(!writer.publish(token));writer={};QCOMPARE(frames->snapshot()["entries"].toInt(),0);
        admitted.reset();QCOMPARE(budget->snapshot().charged,0);token->store(false);
        FullScopesCache healthy(4*1024*1024,{},budget);QVERIFY(sameHistogram(healthy.analyze(request),survived));QCOMPARE(budget->snapshot().charged,0);
    }
    void nativeSonyFullStatisticsReuse() {
        const auto fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()) {if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required public Sony RAW unavailable");QSKIP("No public Sony dataset configured");}
        const auto path=QDir(fixtures).filePath("sony_a7_iv_07.arw");const QByteArray hash="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";
        QCOMPARE(fileHash(path),hash);const auto source=loadSourceFresh(path,{});QVERIFY2(!source.image.isNull(),qPrintable(source.error));
        QCOMPARE(source.image.size(),QSize(7028,4688));QCOMPARE(source.image.format(),QImage::Format_RGBA64);
        const auto before=StageGraph::outputFingerprint(source.image);auto state=painted();state.look={};state.geometry.crop={.2,.1,.4,.6};
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,0);
        const GeometryState view;const auto expected=reference(source.image,plan,view);QVERIFY(!expected.isNull());
        const auto counts=ScopesEngine::analyze(expected);QCOMPARE(counts.pixelCount,32947264);
        auto budget=std::make_shared<BrushRenderBudget>();auto frames=std::make_shared<AnalysisFrameCache>();
        FullScopesCache histograms(4*1024*1024,frames,budget);const auto actual=histograms.analyze({source.image,plan,11,true,view});
        QVERIFY2(actual.pixelCount,qPrintable(actual.error));QVERIFY(sameHistogram(actual,counts));QCOMPARE(budget->snapshot().charged,0);
        auto denied=std::make_shared<BrushRenderBudget>(1);ScopePlotCache plots(16*1024*1024,frames,denied);
        const auto beforeEncoded=counter("scope_plot_encoded_passes");
        const auto beforeFrameHits=frames->snapshot()["hits"].toInteger();
        int requestedModes=0;
        for(const auto &mode:{"waveform","parade","vectorscope"}) {
            const auto actualPlot=plots.render({source.image,plan,view,mode,11,true});const auto expectedPlot=renderEncodedScopePlot(expected,mode);
            QVERIFY2(!actualPlot.image.isNull(),qPrintable(actualPlot.error));QCOMPARE(actualPlot.pixels,32947264);QCOMPARE(actualPlot.image,expectedPlot.image);
            ++requestedModes;
            QCOMPARE(plots.snapshot()["misses"].toInteger(),qint64(requestedModes));
            QCOMPARE(plots.snapshot()["hits"].toInteger(),qint64(requestedModes-1));
            QCOMPARE(plots.snapshot()["entries"].toInteger(),qint64(1));
            const auto hit=plots.render({source.image,plan,view,mode,12,true});
            QCOMPARE(hit.image.cacheKey(),actualPlot.image.cacheKey());QCOMPARE(hit.pixels,actualPlot.pixels);
            QCOMPARE(plots.snapshot()["hits"].toInteger(),qint64(requestedModes));
            QCOMPARE(plots.snapshot()["misses"].toInteger(),qint64(requestedModes));
        }
        // Large completed frames count each requested mode on demand. A second
        // visit hits that exact ink; no color-render admission or batched
        // speculative three-mode pass is needed.
        QCOMPARE(counter("scope_plot_encoded_passes"),beforeEncoded);
        QCOMPARE(plots.snapshot()["hits"].toInteger(),qint64(3));QCOMPARE(plots.snapshot()["misses"].toInteger(),qint64(3));
        QCOMPARE(frames->snapshot()["hits"].toInteger(),beforeFrameHits+3);
        QVERIFY(plots.snapshot()["charged_bytes"].toInteger()<=16*1024*1024);QCOMPARE(denied->snapshot().charged,0);
        QCOMPARE(StageGraph::outputFingerprint(source.image),before);QCOMPARE(fileHash(path),hash);
        qInfo().noquote()<<"BRUSH_SCOPE_ROUTING_EVIDENCE "<<QJsonDocument(QJsonObject{
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"case","native-sony-full-statistics"},{"raw_sha256",QString::fromLatin1(hash)},
            {"source_pixels",32947264},{"statistics_pixels",qint64(actual.pixelCount)},{"exact_histogram_bins",4096},{"exact_plot_modes",3},
            {"uncropped_view_geometry",true},{"completed_frame_plot_reuse",true},{"requested_full_frame_modes",3},{"same_mode_cache_hits",3},{"speculative_batch_passes",0},{"operation_peak_reserved_bytes",budget->snapshot().peak},
            {"operation_end_charged_bytes",budget->snapshot().charged},{"source_unchanged",true},{"native_float_raw_decoder",false},{"native_gpu_brush",false}}).toJson(QJsonDocument::Compact);
    }
    void controllerFailuresRetryAndDiagnostic() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto original=dir.filePath("source.png");
        const auto source=sourceImage();QVERIFY(source.save(original));const auto hash=fileHash(original);const auto state=painted();QString error;
        BrushRenderRequest::Spec spec;spec.layout=BrushRenderRequest::Layout::Stripes128;
        auto measured=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(measured,qPrintable(error));const auto needed=measured->reservedBytes();measured.reset();
        auto budget=std::make_shared<BrushRenderBudget>(needed+source.sizeInBytes());
        auto blocker=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(blocker,qPrintable(error));
        ProcessedImageProvider provider;
        {
            PhotoController controller(&provider,nullptr,budget);controller.setGpuEnabled(false);
            QVERIFY(controller.importFile(QUrl::fromLocalFile(original)));
            QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady()&&!controller.loading()&&!controller.rendering()&&controller.m_sourceIsFull,15000);
            QVERIFY(controller.flushEdits());controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();controller.m_refineTimer.stop();
            const auto saved=controller.m_photos[controller.m_currentIndex].state;
            const auto restore=qScopeGuard([&]{controller.m_photos[controller.m_currentIndex].state=saved;controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();});
            controller.m_photos[controller.m_currentIndex].state=state;++controller.m_requestedRevision;controller.m_scopesRank=-1;controller.m_scopesUpdating=true;
            controller.scheduleRender(false);controller.setExactScopes(true);controller.setScopeMode("waveform");
            QTRY_VERIFY_WITH_TIMEOUT(controller.m_deferredBudgetJobs[0].pending&&controller.m_deferredBudgetJobs[1].pending&&!controller.rendering(),10000);
            QVERIFY(!controller.m_scopesUpdating);QVERIFY(controller.scopesStatus().contains("内存")||controller.scopesStatus().contains("memory"));QVERIFY(!controller.scopePlotCurrent());
            QTRY_VERIFY_WITH_TIMEOUT(controller.m_deferredBudgetJobs[2].pending,10000);
            blocker.reset();
            // An actual completed independent export wakes deferred work immediately.
            QImage small(31,23,QImage::Format_RGBA64);small.fill(QColor(70,90,110));
            QSignalSpy finished(controller.m_exportQueue.get(),&ExportQueue::finished);
            QVERIFY(controller.m_exportQueue->start({{dir.filePath("small.png"),dir.filePath("small-output.png"),small,{},ColorManagement::OutputSpace::SRgb,100,true}}));
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,10000);QCOMPARE(finished[0][0].toInt(),1);
            QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering()&&controller.m_scopesRank==1&&controller.scopePlotCurrent(),30000);
            for(const auto &pending:controller.m_deferredBudgetJobs)QVERIFY(!pending.pending);
            QCOMPARE(controller.scopesPixelCount(),qulonglong(source.width())*source.height());QVERIFY(controller.m_scopesError.isEmpty());
            const auto expected=reference(controller.m_fullSource,controller.gpuPlan(true),controller.previewGeometry());
            QVERIFY(sameHistogram(controller.m_scopes,ScopesEngine::analyze(expected)));
            const auto report=controller.reportBug();QVERIFY2(!report.isEmpty(),qPrintable(controller.statusMessage()));
            const auto cleanup=qScopeGuard([&]{QFile::remove(report);});
            const auto trace=QJsonDocument::fromJson(storedZipEntry(report,"stage_outputs.json")).object();QVERIFY(!trace.isEmpty());
            QVERIFY(trace["cpu_srgb_output"].toObject()["available"].toBool());
            const auto stages=trace["color_stages"].toObject();QVERIFY(!stages["available"].toBool());QVERIFY2(stages["final_frame_admitted"].toBool(),qPrintable(stages["error"].toString()));
            QCOMPARE(fileHash(original),hash);
        }
        provider.setImage({});provider.setScopePlot({});QCOMPARE(budget->snapshot().charged,0);
    }
};
QTEST_MAIN(BrushScopeRoutingTests)
#include "BrushScopeRoutingTests.moc"
