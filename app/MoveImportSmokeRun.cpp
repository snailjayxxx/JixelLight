#include "app/PhotoController.h"
#include "app/SmokeRun.h"
#include "core/import/MoveImport.h"
#include "core/commands/CommandRegistry.h"
#include "core/scopes/ScopesEngine.h"
#include "diagnostics/ActionTrace.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QColorSpace>
#include <QTemporaryDir>
#include <QUuid>
#include <QRgba64>
#include <QCryptographicHash>
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
#include <algorithm>
#include <cmath>
#include <memory>
#ifndef JIXELLIGHT_GIT_COMMIT
#define JIXELLIGHT_GIT_COMMIT "unknown"
#endif
namespace {
QQuickItem *findVisual(QQuickItem *p,const QString &name){if(!p)return nullptr;if(p->objectName()==name)return p;for(auto *c:p->childItems())if(auto *f=findVisual(c,name))return f;return nullptr;}
void revealItem(QQuickItem *p){for(auto *a=p?p->parentItem():nullptr;a;a=a->parentItem()){if(!a->inherits("QQuickFlickable"))continue;auto *content=a->property("contentItem").value<QQuickItem*>();if(!content)continue;const double y=p->mapToItem(content,QPointF{}).y(),maximum=std::max(0.,a->property("contentHeight").toDouble()-a->height());a->setProperty("contentY",std::clamp(y-80.,0.,maximum));}}
QQuickItem *paintChildAt(QQuickItem *parent,QPointF scene) {
    auto children=parent->childItems();
    std::stable_sort(children.begin(),children.end(),[](QQuickItem *a,QQuickItem *b){return a->z()<b->z();});
    for(auto i=children.crbegin();i!=children.crend();++i)if((*i)->isVisible()&&(*i)->contains((*i)->mapFromScene(scene)))return *i;
    return nullptr;
}
bool inClips(QQuickWindow *w,QQuickItem *p,QPointF q){if(!p||!p->isVisible()||!p->isEnabled()||p->width()<=0||p->height()<=0||q.x()<0||q.y()<0||q.x()>=w->width()||q.y()>=w->height())return false;for(auto *a=p->parentItem();a;a=a->parentItem()){if(a->clip()&&!a->contains(a->mapFromScene(q)))return false;if(a->property("moving").toBool()||a->property("flicking").toBool()||a->property("dragging").toBool())return false;}QQuickItem *hit=w->contentItem();for(int depth=0;hit&&depth<64;++depth){if(hit==p)return true;hit=paintChildAt(hit,q);}return false;}
QByteArray fileHash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
QByteArray visibleBytes(const QImage &image){QByteArray b;for(int y=0;y<image.height();++y)b.append(reinterpret_cast<const char*>(image.constScanLine(y)),image.width()*image.depth()/8);return b;}
QString pixelsHash(const QImage &image){return QString::fromLatin1(QCryptographicHash::hash(visibleBytes(image),QCryptographicHash::Sha256).toHex());}
bool writeObject(const QString &path,const QJsonObject &j,bool replace=false){if(!replace&&(QFileInfo::exists(path)||QFileInfo(path).isSymLink()))return false;QSaveFile f(path);const auto b=QJsonDocument(j).toJson(QJsonDocument::Indented);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
}
void startMoveImportSmokeRun(PhotoController *c,QQuickWindow *w,const QString &report,const QString &language){
    const QFileInfo output(report);const QString evidence=output.absoluteFilePath();const QDir parent(output.absolutePath());
    if(output.exists()||output.isSymLink()||!parent.exists()||!parent.entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty()){
        QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;
    }
    struct State {
        QElapsedTimer clock;QTimer *timer=nullptr;int phase=0,lastPhase=-1,frames=0,stableFrame=0,resizes=0;ulong stamp=5000;
        bool done=false,initialized=false,admitted=false,exportStarted=false,exportPassed=false,diagnosticStarted=false,reopened=false,reopenInteractionFinished=false;
        std::unique_ptr<QTemporaryDir> owned;QString baseline,destination,projectRoot,project,recoveryJournal;
        QStringList sources,before;QList<QByteArray> originalHashes;QList<MoveImportBatch> batches;
        QImage fixture,reference;ScopesResult scopes;QByteArray baselineHash;qint64 sourceKey=0,reopenedSourceKey=0;quint64 revision=0,reopenedRevision=0;
        QElapsedTimer reopenStableClock;int reopenSettledMs=0,scopeWaits=0;QJsonArray reopenSignature,events,scopeWaitStates;QString initialScopeStatus;
        QSize logical,physical;double dpr=0;QJsonObject initial,admission,diagnostic,exportResult,review,sharedCommand;
        QJsonArray history,signature,pointers,captures,records,phases,operationIds;bool staleCancelRefused=false;
    };
    auto s=std::make_shared<State>();s->clock.start();s->owned=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/JixelLight-owned-move-XXXXXX");
    s->baseline=evidence+".baseline-source.png";s->destination=evidence+".destination";s->projectRoot=evidence+".project-root";
    if(!s->owned->isValid()||!QDir().mkdir(s->destination)||!QDir().mkdir(s->projectRoot)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    const auto fixture=[](int shift){QImage image(307,211,QImage::Format_RGBA64);image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
        for(int y=0;y<211;++y)for(int x=0;x<307;++x)reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64((x*149+y*31+shift)%60000,(y*197+x*19+shift)%60000,(x*59+y*47+shift)%60000,(x+y)%7?47000:0);return image;};
    s->fixture=fixture(0);if(!s->fixture.save(s->baseline)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}s->baselineHash=fileHash(s->baseline);
    const auto unique=QUuid::createUuid().toString(QUuid::WithoutBraces);
    for(int i=0;i<3;++i){const auto source=s->owned->filePath(QString("JixelLight-owned-%1-%2.png").arg(unique).arg(i));const auto before=evidence+QString(".source-%1-before.png").arg(i);
        if(!fixture((i+1)*731).save(source)||!QFile::copy(source,before)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
        s->sources.push_back(QFileInfo(source).canonicalFilePath());s->before.push_back(before);s->originalHashes.push_back(fileHash(source));}
    s->timer=new QTimer(c);s->timer->setInterval(20);QObject::connect(w,&QQuickWindow::frameSwapped,c,[s]{++s->frames;},Qt::QueuedConnection);
    const auto item=[w](const QString &name){return findVisual(w->contentItem(),name);};
    const auto object=[w](const QString &name){return w->findChild<QObject *>(name);};
    const auto intent=[c]{return c->gpuPlan(true).state.toJson();};const auto history=[c]{return QJsonArray::fromVariantList(c->editHistory());};
    const auto original=[=](bool sameBuffer){return c->currentFile()==s->baseline&&intent()==s->initial&&history()==s->history&&fileHash(s->baseline)==s->baselineHash
        &&(!sameBuffer||(c->gpuSource().cacheKey()==s->sourceKey&&c->renderRevision()==s->revision))&&c->scopesStatus()==(language=="zh_CN"?QString::fromUtf8("全分辨率统计"):QStringLiteral("Full-resolution statistics"))&&c->scopesPixelCount()==s->scopes.pixelCount
        &&c->shadowClipPercent()==s->scopes.shadowClipPercent&&c->highlightClipPercent()==s->scopes.highlightClipPercent
        &&c->redHistogram()==s->scopes.red&&c->greenHistogram()==s->scopes.green&&c->blueHistogram()==s->scopes.blue&&c->lumaHistogram()==s->scopes.luma;};
    const auto reopenedOwnerCurrent=[=]{return c->gpuSource().cacheKey()==s->reopenedSourceKey&&c->renderRevision()==s->reopenedRevision;};
    const auto trace=[=](const QString &event){s->events.append(QJsonObject{{"event",event},{"at_ms",s->clock.elapsed()},{"phase",s->phase},
        {"source_key",QString::number(c->gpuSource().cacheKey())},{"revision",QString::number(c->renderRevision())},{"loading",c->loading()},{"rendering",c->rendering()},{"preview_ready",c->previewReady()}});while(s->events.size()>64)s->events.removeFirst();};
    QObject::connect(c,&PhotoController::gpuFrameChanged,c,[=]{trace("frame_changed");});
    QObject::connect(c,&PhotoController::previewGeometryChanged,c,[=]{trace("geometry_changed");});
    QObject::connect(c,&PhotoController::activityChanged,c,[=]{trace("activity_changed");});
    const auto checkpoint=[=]{writeObject(evidence+".progress.json",{{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"move",QJsonObject::fromVariantMap(c->moveImportDiagnostics())},{"pointers",s->pointers},{"records",s->records}},true);};
    const auto finish=[=](const QString &error){
        if(s->done)return;s->done=true;s->timer->stop();
        const bool passed=error.isEmpty()&&s->phase==12&&s->exportPassed&&s->reopened&&s->records.size()==3&&s->pointers.size()==2&&s->captures.size()==4
            &&original(false)&&!s->sharedCommand.isEmpty()&&s->operationIds.size()==2&&s->staleCancelRefused&&c->copyImportOperationId().isEmpty()
            &&c->library().size()==4&&s->clock.elapsed()<60000&&qApp->property("jixellightSmokeSettingsIsolated").toBool()
            &&c->moveImportDiagnostics()["retirement_backend"].toString()=="system-trash"&&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        QJsonObject r{{"passed",passed},{"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"deadline_ms",60000},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"actual_Main_App_PhotoCanvas",true},{"controller_on_actual_GUI_thread",c->thread()==QThread::currentThread()},{"language",language},{"actual_qpa",QGuiApplication::platformName()},
            {"actual_backend",c->processingBackend()},{"GPU_active",c->gpuActive()},{"settings_isolated",qApp->property("jixellightSmokeSettingsIsolated").toBool()},
            {"actual_system_trash",true},{"controlled_trash_service",false},{"owned_fixture_file_picker_setup",true},{"native_file_picker_verified",false},{"forced_process_crash_verified",false},{"owned_interrupted_copy_fixture",true},
            {"baseline_path",s->baseline},{"baseline_sha256",QString::fromLatin1(s->baselineHash)},{"unselected_original_unchanged",fileHash(s->baseline)==s->baselineHash},
            {"initial_whole_Develop",s->initial},{"final_whole_Develop",intent()},{"initial_history",s->history},{"final_history",history()},
            {"initial_source_key",QString::number(s->sourceKey)},{"initial_render_revision",QString::number(s->revision)},
            {"final_source_key",QString::number(c->gpuSource().cacheKey())},{"final_render_revision",QString::number(c->renderRevision())},{"catalog_count",c->library().size()},
            {"reopened_source_key",QString::number(s->reopenedSourceKey)},{"reopened_render_revision",QString::number(s->reopenedRevision)},{"reopen_settled_ms",s->reopenSettledMs},{"reopened_owner_current",reopenedOwnerCurrent()},{"events",s->events},
            {"records",s->records},{"review",s->review},{"pointers",s->pointers},{"window_capture_admission",s->admission},{"window_captures",s->captures},{"phase_checkpoints",s->phases},
            {"scope_wait_observations",s->scopeWaits},{"scope_wait_states",s->scopeWaitStates},
            {"scope",QJsonObject{{"status",s->initialScopeStatus},{"pixels",qint64(s->scopes.pixelCount)},{"bins",1024},{"four_histograms_exact",true},{"shadow_clip_percent",s->scopes.shadowClipPercent},{"highlight_clip_percent",s->scopes.highlightClipPercent},{"red",QJsonArray::fromVariantList(s->scopes.red)},
                {"green",QJsonArray::fromVariantList(s->scopes.green)},{"blue",QJsonArray::fromVariantList(s->scopes.blue)},{"luma",QJsonArray::fromVariantList(s->scopes.luma)}}},
            {"project_path",s->project},{"durable_identities_verified",s->records.size()==3},{"project_reopened",s->reopened},{"export",s->exportResult},{"diagnostic",s->diagnostic},
            {"move",QJsonObject::fromVariantMap(c->moveImportDiagnostics())},{"shared_import_command",s->sharedCommand},
            {"transfer_operation_ids",s->operationIds},{"stale_transfer_cancel_refused",s->staleCancelRefused},{"idle_transfer_operation_id",c->copyImportOperationId()},
            {"physical_device_verified",false},{"native_float_RAW_decoder",false},{"product_development_complete",false}};
        const bool saved=writeObject(evidence,r);
        // Restore only these verified, generated fixture files before their
        // process-owned temporary directory is cleaned. Never replace a path.
        for(const auto &batch:s->batches)for(const auto &file:batch.files)if(file.state=="trashed"&&!file.trashPath.isEmpty()
            &&file.copy.source.startsWith(QFileInfo(s->owned->path()).canonicalFilePath()+"/")&&!QFileInfo::exists(file.copy.source)&&!QFileInfo(file.copy.source).isSymLink()
            &&fileHash(file.trashPath)==file.copy.sha256.toLatin1())QFile::rename(file.trashPath,file.copy.source);
        QTimer::singleShot(0,c,[=]{QCoreApplication::exit(passed&&saved?0:1);});
    };
    const auto stable=[=]{const QJsonArray signature{s->phase,QString::number(c->gpuSource().cacheKey()),QString::number(c->renderRevision()),c->copyImportBusy(),c->moveImportRecoveryPreview()["valid"].toBool(),w->width(),w->height()};
        if(signature!=s->signature){s->signature=signature;s->stableFrame=s->frames;return false;}if(s->frames-s->stableFrame<3){w->update();return false;}return true;};
    const auto capture=[=](const QString &role){const auto image=w->grabWindow();if(!s->admitted||image.size()!=s->physical||QSize(w->width(),w->height())!=s->logical||w->devicePixelRatio()!=s->dpr||image.depth()!=32)return false;
        const auto path=evidence+"."+role+".window.png";if(!image.save(path))return false;s->captures.append(QJsonObject{{"role",role},{"path",path},{"phase",s->phase},{"logical_width",w->width()},{"logical_height",w->height()},
            {"physical_width",image.width()},{"physical_height",image.height()},{"DPR",s->dpr},{"presented_frames",s->frames-s->stableFrame},{"source_key",QString::number(c->gpuSource().cacheKey())},{"render_revision",QString::number(c->renderRevision())},
            {"preview_ready",c->previewReady()},{"loading",c->loading()},{"rendering",c->rendering()},{"source_available",!c->gpuSource().isNull()},{"at_ms",s->clock.elapsed()}});return true;};
    const auto click=[=](const QString &name){auto *target=item(name);if(!target)return false;const auto point=target->mapToScene({target->width()/2,target->height()/2});if(!inClips(w,target,point))return false;
        const auto source=c->gpuSource().cacheKey();const auto revision=c->renderRevision();const auto text=target->property("text").toString();
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease})QWindowSystemInterface::handleMouseEvent(w,s->stamp+=3,QHighDpi::toNativePixels(point,w),QHighDpi::toNativePixels(w->mapToGlobal(point),w),type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::LeftButton,type);
        QWindowSystemInterface::flushWindowSystemEvents();s->pointers.append(QJsonObject{{"target",name},{"text",text},{"QPA_mouse_route",true},{"x",point.x()},{"y",point.y()},
            {"source_key",QString::number(source)},{"render_revision",QString::number(revision)},{"post_event_source_key",QString::number(c->gpuSource().cacheKey())},{"post_event_render_revision",QString::number(c->renderRevision())},{"at_ms",s->clock.elapsed()}});return true;};
    const auto record=[=](const QString &journal,const QList<int> &indices){
        QString error;const auto batch=readMoveImport(journal,&error);if(!error.isEmpty()||batch.phase!="done"||!batch.error.isEmpty()||batch.retired()!=indices.size()||batch.files.size()!=indices.size())return false;
        ProjectDatabase reader;QVector<ProjectDatabase::SavedPhoto> photos;if(!reader.readSnapshot(s->project,&photos))return false;
        for(int j=0;j<indices.size();++j){const int index=indices[j];const auto &file=batch.files[j];bool saved=false;
            for(const auto &photo:photos)if(QFileInfo(photo.path).canonicalFilePath()==QFileInfo(file.copy.destination).canonicalFilePath()&&photo.copyKey.isEmpty()&&photo.identity.isKnown()
                &&photo.identity.sizeBytes==file.bytes&&photo.identity.sha256==s->originalHashes[index])saved=true;
            if(file.copy.source!=s->sources[index]||QFileInfo(file.copy.source).isSymLink()||QFileInfo::exists(file.copy.source)||file.copy.sha256.toLatin1()!=s->originalHashes[index]||fileHash(file.copy.destination)!=s->originalHashes[index]||!saved)return false;
            s->records.append(QJsonObject{{"index",index},{"source",file.copy.source},{"before",s->before[index]},{"destination",file.copy.destination},{"sha256",file.copy.sha256},
                {"bytes",file.bytes},{"state",file.state},{"trash_path_reported",!file.trashPath.isEmpty()},{"source_absent_after_system_trash",true},{"durable_identity",true},
                {"journal",journal},{"journal_sha256",QString::fromLatin1(fileHash(journal))}});
        }s->batches.push_back(batch);return true;
    };
    QObject::connect(c,&PhotoController::copyImportChanged,c,[=]{
        if(s->done||!c->moveImportBusy())return;const auto id=c->copyImportOperationId();if(s->operationIds.contains(id))return;
        CommandRegistry::OperationCancel parsed;if(!CommandRegistry::decodeOperationCancel({{"command","photo.transfer.cancel"},{"operation_id",id}},&parsed)) {finish("Actual move operation identity unavailable");return;}
        if(!s->operationIds.isEmpty()) {
            if(c->executeCommand({{"command","photo.transfer.cancel"},{"operation_id",s->operationIds.last().toString()}})){finish("Stale cancellation reached reviewed recovery operation");return;}
            s->staleCancelRefused=true;
        }
        s->operationIds.append(id);
    });
    QObject::connect(c,&PhotoController::exportFinished,c,[=](int succeeded,int failed,bool cancelled){if(s->done||s->phase!=9)return;
        const QImage image(evidence+".export.png");const auto actual=image.convertToFormat(QImage::Format_RGBA64);
        if(succeeded!=1||failed||cancelled||actual.size()!=s->reference.size()||visibleBytes(actual)!=visibleBytes(s->reference)||image.colorSpace()!=QColorSpace(QColorSpace::SRgb)){finish("Move changed unselected original export or export failed");return;}
        s->exportPassed=true;s->exportResult={{"path",evidence+".export.png"},{"RGBA16_exact",true},{"ICC_srgb",true},{"sha256",QString::fromLatin1(fileHash(evidence+".export.png"))}};s->phase=10;
    });
    QObject::connect(c,&PhotoController::diagnosticFinished,c,[=](const QString &path){if(s->done||s->phase!=10)return;const auto target=evidence+".diagnostic.zip";
        if(path.isEmpty()||!QFile::copy(path,target)){finish("Cannot retain frozen move diagnostic");return;}s->diagnostic={{"path",target},{"sha256",QString::fromLatin1(fileHash(target))}};QFile::remove(path);s->phase=11;
    });
    QObject::connect(s->timer,&QTimer::timeout,c,[=]{
        if(s->done)return;if(s->clock.elapsed()>=60000){finish("Move MainApp validation exceeded60000ms");return;}
        if(s->lastPhase!=s->phase){s->lastPhase=s->phase;s->phases.append(QJsonObject{{"phase",s->phase},{"at_ms",s->clock.elapsed()}});checkpoint();}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive){QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        if(!w->isActive()){QWindowSystemInterface::handleFocusWindowChanged(w);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        if(!c->previewReady()||c->loading()||c->rendering()||c->gpuSource().isNull()||(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!c->gpuActive()))return;
        if(s->reopened&&!reopenedOwnerCurrent()){finish("Frozen reopened source/revision changed before export/diagnostic completion");return;}
        if(s->phase==0&&!s->initialized){s->initialized=true;c->setExposure(.2);c->finishInteraction();c->setExactScopes(true);
            if(!c->createProject(QUrl::fromLocalFile(s->projectRoot),"OwnedMove")){finish("Cannot create actual move QA project");return;}s->project=c->projectPath();return;}
        if(!stable())return;
        if(s->phase==0){const auto expectedStatus=language=="zh_CN"?QString::fromUtf8("全分辨率统计"):QStringLiteral("Full-resolution statistics");
            if(c->scopesStatus()!=expectedStatus){++s->scopeWaits;s->scopeWaitStates.append(QJsonObject{{"status",c->scopesStatus()},{"revision",QString::number(c->renderRevision())},{"pixels",qint64(c->scopesPixelCount())}});while(s->scopeWaitStates.size()>16)s->scopeWaitStates.removeFirst();return;}
            s->initialScopeStatus=c->scopesStatus();s->reference=ImagePipeline::processWithPlan(s->fixture,c->gpuPlan(true));s->scopes=ScopesEngine::analyze(s->reference);if(c->scopesPixelCount()!=s->scopes.pixelCount||c->redHistogram().size()!=1024)return;
            s->initial=intent();s->history=history();s->sourceKey=c->gpuSource().cacheKey();s->revision=c->renderRevision();if(!original(true)||!s->reference.save(evidence+".reference.png")){finish("Original baseline state/scopes differ before move");return;}
            const auto observed=w->grabWindow();s->dpr=w->devicePixelRatio();const QSize logical(w->width(),w->height()),physical(qRound(logical.width()*s->dpr),qRound(logical.height()*s->dpr));
            if(observed.size()!=physical){const QSize fit(qRound(observed.width()/s->dpr),qRound(observed.height()/s->dpr));if(s->resizes||fit.width()<w->minimumWidth()||fit.height()<w->minimumHeight()||QSize(qRound(fit.width()*s->dpr),qRound(fit.height()*s->dpr))!=observed.size()){finish("Actual drawable cannot hold complete move window");return;}++s->resizes;w->resize(fit);return;}
            s->admitted=true;s->logical=logical;s->physical=physical;s->admission={{"accepted",true},{"before_any_input",true},{"resize_count",s->resizes},{"logical_width",logical.width()},{"logical_height",logical.height()},
                {"physical_width",physical.width()},{"physical_height",physical.height()},{"DPR",s->dpr},{"presented_frames",s->frames-s->stableFrame}};s->phase=1;return;}
        if(s->phase==1){c->moveImportRequested({QUrl::fromLocalFile(s->sources[0]),QUrl::fromLocalFile(s->sources[1])});auto *dialog=object("copyImportDialog");if(!dialog){finish("Actual move import dialog unavailable");return;}
            dialog->setProperty("customNames",false);dialog->setProperty("destination",QUrl::fromLocalFile(s->destination));if(!dialog->property("moveMode").toBool()){finish("Actual dialog is not move mode");return;}s->phase=2;return;}
        if(s->phase==2){if(click("copyImportApply"))s->phase=3;return;}
        if(s->phase==3){if(c->copyImportBusy())return;const auto facts=c->moveImportDiagnostics();if(facts["phase"].toString()!="done"){finish("Actual system-trash move failed: "+facts["error"].toString());return;}
            const QJsonObject expected{{"command","photo.move"},{"sources",QJsonArray{s->sources[0],s->sources[1]}},
                {"folder",s->destination},{"pattern",""},{"sequenceStart",1}};
            for(const auto &value:ActionTrace::instance().snapshot()) {
                const auto row=value.toObject();if(row["action"].toString()=="move_import_started"&&row["command"].toObject()==expected)s->sharedCommand=expected;
            }
            if(s->sharedCommand.isEmpty()){finish("Reviewed move did not record the shared import command");return;}
            if(!original(true)||!record(c->moveImportJournal(),{0,1})||!capture("moved")){finish("Move copies, durable identities or unselected original differ");return;}
            const auto recovery=prepareMoveImport({s->sources[2]},s->destination,s->project);if(!recovery.error.isEmpty()||recovery.files.size()!=1||!QFileInfo::exists(s->sources[2])){finish("Cannot prepare owned interrupted-copy recovery fixture");return;}
            s->recoveryJournal=recovery.journal;if(!QFile::copy(recovery.journal,evidence+".reviewed-move.json")){finish("Cannot retain original reviewed move record");return;}s->phase=4;return;}
        if(s->phase==4){auto *dialog=object("moveImportRecoveryDialog");if(!dialog){finish("Actual recovery dialog unavailable");return;}dialog->setProperty("journal",s->recoveryJournal);if(!QMetaObject::invokeMethod(dialog,"open")){finish("Cannot open actual recovery dialog");return;}s->phase=5;return;}
        if(s->phase==5){const auto preview=c->moveImportRecoveryPreview();if(!preview["valid"].toBool()||preview["pending"].toBool())return;
            if(s->review.isEmpty()){if(preview["rows"].toList().size()!=1||c->library().size()!=3||!QFileInfo::exists(s->sources[2])||!capture("recovery")){finish("Read-only recovery preview changed files/catalog");return;}
                s->review={{"read_only_before_click",true},{"visible_rows",1},{"record_digest",preview["recordDigest"].toString()},{"source_still_present",true},{"catalog_count_before",c->library().size()}};}
            if(click("moveImportResume"))s->phase=6;return;}
        if(s->phase==6){if(c->copyImportBusy())return;const auto facts=c->moveImportDiagnostics();if(facts["phase"].toString()!="done"){finish("Actual system-trash recovery failed: "+facts["error"].toString());return;}
            if(!original(true)||!record(s->recoveryJournal,{2})||c->library().size()!=4||!capture("recovered")||!c->flushEdits()){finish("Reviewed recovery or saved original state differs");return;}
            s->phase=7;if(!c->openProject(QUrl::fromLocalFile(s->project))){finish("Cannot reopen saved move project");return;}return;}
        if(s->phase==7){if(c->currentFile()!=s->baseline){const auto library=c->library();for(int i=0;i<library.size();++i)if(library[i].toMap()["path"].toString()==s->baseline){c->selectPhoto(i);return;}finish("Original baseline missing from reopened catalog");return;}
            if(!s->reopenInteractionFinished){s->reopenInteractionFinished=true;c->finishInteraction();return;}
            const QJsonArray owner{QString::number(c->gpuSource().cacheKey()),QString::number(c->renderRevision()),w->width(),w->height()};
            if(owner!=s->reopenSignature){s->reopenSignature=owner;s->reopenStableClock.restart();return;}
            if(s->reopenStableClock.elapsed()<200)return;
            if(c->scopesPixelCount()!=s->scopes.pixelCount||c->redHistogram().size()!=1024)return;
            s->reopenedSourceKey=c->gpuSource().cacheKey();s->reopenedRevision=c->renderRevision();s->reopenSettledMs=int(s->reopenStableClock.elapsed());
            if(!original(false)||c->library().size()!=4||!capture("reopened")){finish("Reopened original history/scopes/catalog differ");return;}s->reopened=true;s->phase=8;return;}
        if(s->phase==8){s->phase=9;return;}
        if(s->phase==9&&!s->exportStarted){s->exportStarted=true;if(!c->exportCurrent(QUrl::fromLocalFile(evidence+".export.png"),"srgb",100))finish("Unselected original export refused");return;}
        if(s->phase==10&&!s->diagnosticStarted){s->diagnosticStarted=true;if(!c->requestBugReport())finish("Frozen move diagnostic refused");return;}
        if(s->phase==11){if(!original(false)){finish("Export/diagnostic changed unselected original");return;}s->phase=12;finish({});}
    });
    c->setLanguage(language);w->resize(1180,720);w->setProperty("workspaceIndex",1);
    if(!c->importFile(QUrl::fromLocalFile(s->baseline))){finish("Cannot import owned baseline image");return;}s->timer->start();
}
