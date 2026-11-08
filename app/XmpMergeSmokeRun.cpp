#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "core/metadata/XmpSidecar.h"
#include "diagnostics/ActionTrace.h"
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QPointer>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>

#ifndef JIXELLIGHT_GIT_COMMIT
#define JIXELLIGHT_GIT_COMMIT "unknown"
#endif
namespace {
QByteArray fullHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&file))return {};return h.result().toHex();
}
QString threadID(){return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()),16);}
void click(QWidget *widget) {
    const QPointF local=widget->rect().center(),global=widget->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(widget,&press);QCoreApplication::sendEvent(widget,&release);
}
void key(QWidget *widget,int key) {
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier),release(QEvent::KeyRelease,key,Qt::NoModifier);
    QCoreApplication::sendEvent(widget,&press);QCoreApplication::sendEvent(widget,&release);
}
QDialog *review() {
    for(auto *widget:QApplication::topLevelWidgets())if(widget->isVisible()&&widget->objectName()=="xmp-merge-review")return qobject_cast<QDialog*>(widget);
    return nullptr;
}
QString sceneBackend(QQuickWindow *window) {
    const auto api=window->rendererInterface()->graphicsApi();
    if(api==QSGRendererInterface::Metal)return "Metal";
    if(api==QSGRendererInterface::Direct3D11)return "Direct3D 11";
    if(api==QSGRendererInterface::OpenGL)return "OpenGL";
    return "unsupported:"+QString::number(int(api));
}
bool saveNew(const QImage &image,const QString &path) {
    QFile output(path);return !image.isNull()&&output.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&image.save(&output,"PNG")&&output.flush();
}
struct State {
    QElapsedTimer clock;QString project,original,sidecar,originalSHA,sidecarSHA,guiThread;
    AdjustmentState incoming;int phase=0;bool finished=false,filePicked=false,normalActionReturned=false,busyObserved=false,allChoices=false;
    bool durableFullRecord=false,UIFullRecord=false,developUndo=false,storedUndo=false,annotationsPreserved=false;
    QJsonObject storedAfter,UIAfter,storedUndoRow;QJsonArray captures,sourceEvents,sqlEvents,publicationEvents;
    QString reviewTitle,reviewNotice;int conflictChoices=0;quint64 appliedStartFrame=0,undoStartFrame=0;
    qint64 inputChecks=0,inputWaitChecks=0,pendingPrepareFlagSignals=0;
    QJsonObject beforePickerInput,beforeOpenInput;
    std::atomic<quint64> frames{0};std::atomic<quintptr> renderThread{0};
};
}
void startXmpMergeSmokeRun(PhotoController *c,QQuickWindow *window,const QString &reportPath,const QString &language) {
    auto fixture=std::make_shared<QTemporaryDir>(QDir::tempPath()+"/JixelLight-XMP-App-XXXXXX");
    auto s=std::make_shared<State>();s->clock.start();s->guiThread=threadID();s->incoming.look.mode="off";s->incoming.exposure=1.25;
    auto *timer=new QTimer(c);timer->setInterval(10);
    // Observe the actual protected input state on the GUI owner. A visible
    // preview may precede a newer prepare job; it is not an import-ready flag.
    QObject::connect(c,&PhotoController::scopePlotChanged,c,[=] {
        if(c->m_preparing)++s->pendingPrepareFlagSignals;
    },Qt::DirectConnection);
    const auto inputReady=[=](QJsonObject *capture) {
        ++s->inputChecks;
        const bool image=c->hasImage();
        const bool available=image&&c->m_photos[c->m_currentIndex].available;
        const bool identity=image&&c->m_photos[c->m_currentIndex].identity.isKnown();
        const QJsonObject state{{"closing",c->m_closing},{"loading",c->m_loading},{"prepare_pending",c->m_preparing},
            {"brush_gesture",bool(c->m_brushGesture)},{"brush_recovery",bool(c->m_brushRecovery)},
            {"crop_editing",c->m_cropEditing},{"calibration_busy",c->m_calibrationBusy},{"relink_busy",c->m_relinkBusy},
            {"copy_import_busy",c->copyImportBusy()},{"xmp_merge_busy",c->xmpMergeBusy()},
            {"has_image",image},{"project_open",c->m_project.isOpen()},{"original_available",available},{"original_identity_known",identity},
            {"prepare_generation",qint64(c->m_prepareGeneration)},{"request_revision",qint64(c->m_requestedRevision)},
            {"catalog_epoch",qint64(c->m_catalogEpoch)},{"photo_epoch",qint64(c->m_photoEpoch)}};
        const bool ready=!c->m_closing&&!c->m_loading&&!c->m_preparing&&!c->m_brushGesture&&!c->m_brushRecovery
            &&!c->m_cropEditing&&!c->m_calibrationBusy&&!c->m_relinkBusy&&!c->copyImportBusy()&&!c->xmpMergeBusy()
            &&image&&c->m_project.isOpen()&&available&&identity;
        if(!ready)++s->inputWaitChecks;
        if(capture)*capture=state;
        return ready;
    };
    const auto addCapture=[=](const QString &kind,const QImage &image) {
        const QString path=QFileInfo(reportPath).absoluteFilePath()+"."+kind+".png";
        if(!saveNew(image,path))return false;
        s->captures.append(QJsonObject{{"kind",kind},{"path",path},{"sha256",QString::fromLatin1(fullHash(path))},{"width",image.width()},{"height",image.height()}});return true;
    };
    const auto finish=[=](const QString &error) {
        if(s->finished)return;s->finished=true;timer->stop();
        const bool originalUnchanged=!s->originalSHA.isEmpty()&&QString::fromLatin1(fullHash(s->original))==s->originalSHA;
        const bool sidecarUnchanged=!s->sidecarSHA.isEmpty()&&QString::fromLatin1(fullHash(s->sidecar))==s->sidecarSHA;
        for(const auto &value:ActionTrace::instance().snapshot()) {
            const auto event=value.toObject();const auto action=event.value("action").toString();
            if(event.value("project").toString()==s->project) {
                if(action=="xmp_merge_source_verified")s->sourceEvents.append(event);
                else if(action=="xmp_merge_sql_committed")s->sqlEvents.append(event);
                else if(action=="xmp_merge_commit_queued")s->publicationEvents.append(event);
            }
            if(action=="xmp_merge_durable"&&event.value("photo_key").toString()==s->original)s->publicationEvents.append(event);
        }
        const auto budget=c->brushRenderBudget()->snapshot();
        const bool roles=s->sourceEvents.size()==2&&s->sqlEvents.size()==1
            &&s->sourceEvents[0].toObject().value("worker_thread").toString()!=s->guiThread
            &&s->sourceEvents[1].toObject().value("worker_thread").toString()!=s->guiThread
            &&s->sqlEvents[0].toObject().value("SQL_thread").toString()!=s->guiThread
            &&s->renderThread.load(std::memory_order_acquire)!=reinterpret_cast<quintptr>(QThread::currentThreadId());
        const bool passed=error.isEmpty()&&s->phase==7&&s->filePicked&&s->busyObserved&&s->allChoices
            &&s->durableFullRecord&&s->UIFullRecord&&s->developUndo&&s->storedUndo&&s->annotationsPreserved
            &&roles&&originalUnchanged&&sidecarUnchanged&&s->captures.size()==3&&s->clock.elapsed()<60000
            &&budget.limit==3LL*1024*1024*1024&&budget.peak<=budget.limit
            &&s->inputChecks>=2&&s->pendingPrepareFlagSignals>0&&!s->beforePickerInput.isEmpty()&&!s->beforeOpenInput.isEmpty();
        const QJsonObject result{{"schema",1},{"mode","xmp-normal-App-merge"},{"passed",passed},{"error",error},{"phase",s->phase},
            {"elapsed_ms",s->clock.elapsed()},{"GUI_deadline_ms",60000},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"language",language},{"actual_qpa",QGuiApplication::platformName()},{"actual_scene_backend",sceneBackend(window)},
            {"actual_DPR",window->effectiveDevicePixelRatio()},{"GUI_thread",s->guiThread},
            {"afterFrameEnd_RHI_thread",QString::number(s->renderThread.load(std::memory_order_acquire),16)},
            {"afterFrameEnd_count",qint64(s->frames.load(std::memory_order_acquire))},
            {"normal_App_main_and_QML_window",true},{"normal_import_method_called",true},{"QML_menu_pointer_exercised",false},
            {"file_picker_accepted",s->filePicked},{"review_widget_pointer_and_key_input",s->allChoices},{"busy_observed",s->busyObserved},
            {"input_preparation_readiness",QJsonObject{{"checks",s->inputChecks},{"wait_checks",s->inputWaitChecks},
                {"actual_pending_prepare_flag_signals",s->pendingPrepareFlagSignals},{"before_picker",s->beforePickerInput},{"before_open_click",s->beforeOpenInput},
                {"pending_flag_is_not_physical_device_or_worker_completion_proof",true}}},
            {"original_source_SHA256",s->originalSHA},{"sidecar_SHA256",s->sidecarSHA},{"original_unchanged",originalUnchanged},{"sidecar_unchanged",sidecarUnchanged},
            {"review_title",s->reviewTitle},{"review_notice",s->reviewNotice},{"conflict_choices",s->conflictChoices},
            {"expected_Develop",s->incoming.toJson()},{"stored_after",s->storedAfter},{"UI_after",s->UIAfter},
            {"full_stored_record_matches",s->durableFullRecord},{"UI_fields_match",s->UIFullRecord},
            {"Develop_undo_restored",s->developUndo},{"stored_undo_row",s->storedUndoRow},{"stored_undo_and_redo_history",s->storedUndo},
            {"annotations_preserved_after_Develop_undo",s->annotationsPreserved},{"annotations_undoable",false},
            {"source_worker_events",s->sourceEvents},{"SQL_commit_events",s->sqlEvents},{"GUI_publication_events",s->publicationEvents},
            {"captures",s->captures},{"budget_limit_bytes",budget.limit},{"peak_operation_reserved_bytes",budget.peak},
            {"settings_isolated",qApp->property("jixellightSmokeSettingsIsolated").toBool()},
            {"new_native_brush_or_RAW_or_fullcolour_or_all_backend_acceptance",false},{"physical_deviceLoss_tablet_OOM_acceptance",false},
            {"atomic_filesystem_snapshot_claim",false},{"Root_or_CI_or_F2_completion",false}};
        QFile output(reportPath);const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Indented);
        const bool saved=output.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&output.write(bytes)==bytes.size()&&output.flush();
        if(!error.isEmpty())for(auto *widget:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QDialog*>(widget))d->reject();
        QTimer::singleShot(0,qApp,[code=passed&&saved?0:1]{QCoreApplication::exit(code);});
    };
    QObject::connect(window,&QQuickWindow::afterFrameEnd,c,[s] {
        s->renderThread.store(reinterpret_cast<quintptr>(QThread::currentThreadId()),std::memory_order_release);s->frames.fetch_add(1,std::memory_order_acq_rel);
    },Qt::DirectConnection);
    QObject::connect(c,&PhotoController::xmpMergeChanged,c,[=]{if(c->xmpMergeBusy())s->busyObserved=true;});
    if(!fixture->isValid()){finish("Cannot create owned QA fixture directory");return;}
    s->original=fixture->filePath("owned-input.png");s->sidecar=fixture->filePath("owned-input.xmp");
    QImage source(192,128,QImage::Format_RGBA64);source.fill(QColor(45,115,180));
    if(!source.save(s->original)){finish("Cannot write owned QA photograph");return;}
    CatalogTags tags;tags.keywords={"incoming"};tags.albums={"External"};tags.label="red";QString failure;
    if(!XmpSidecar::writeNew(s->sidecar,s->incoming,tags,4,"pick",&failure)){finish("Cannot create owned XMP: "+failure);return;}
    s->originalSHA=QString::fromLatin1(fullHash(s->original));s->sidecarSHA=QString::fromLatin1(fullHash(s->sidecar));
    {ProjectDatabase seed;AdjustmentState initial;initial.look.mode="off";EditHistory h;h.initialize(initial);
        if(!seed.create(fixture->filePath("Owned.jlp"),"Owned XMP application validation")
           ||!seed.addOrUpdatePhoto(s->original,initial)||!seed.updateBatch({{s->original,initial}},{{s->original,h}})
           ||!seed.updateCurationBatch({{s->original,{0,"none"}}})||!seed.updateTagsBatch({{s->original,{}}})||!seed.flush()
           ||!seed.recordOriginalIdentity(s->original,{QFileInfo(s->original).size(),s->originalSHA.toLatin1()})) {finish("Cannot prepare owned catalog: "+seed.lastError());return;}
        s->project=seed.projectPath();
    }
    c->setLanguage(language);c->setGpuEnabled(false);window->resize(1540,920);window->setProperty("workspaceIndex",1);window->show();
    if(!c->openProject(QUrl::fromLocalFile(s->project))){finish("Cannot open actual owned project: "+c->statusMessage());return;}
    QObject::connect(timer,&QTimer::timeout,c,[=,keep=fixture] {
        Q_UNUSED(keep);if(s->clock.elapsed()>=60000){finish("Actual XMP application exceeded original60-second boundary");return;}
        if(s->phase==0) {
            QJsonObject readiness;
            if(!inputReady(&readiness)||c->loading()||c->rendering()||!c->previewReady()||s->frames.load(std::memory_order_acquire)==0)return;
            s->beforePickerInput=readiness;
            qInfo()<<"XMP_APP_INPUT_READY_V1 before_picker"<<readiness.value("prepare_pending").toBool()<<readiness.value("prepare_generation").toInteger();
            s->phase=1;
            auto *pickerTimer=new QTimer(c);pickerTimer->setInterval(5);
            const auto pickerStart=s->clock.elapsed();
            auto observed=std::make_shared<QPointer<QFileDialog>>();
            QObject::connect(pickerTimer,&QTimer::timeout,c,[=]{
                if(s->finished||s->filePicked){pickerTimer->stop();return;}
                if(s->clock.elapsed()-pickerStart>=10000){pickerTimer->stop();finish("Normal owned file picker did not actually accept within10seconds");return;}
                for(auto *widget:QApplication::topLevelWidgets())if(auto *picker=qobject_cast<QFileDialog*>(widget);picker&&picker->isVisible()) {
                    if(*observed!=picker) {
                        *observed=picker;
                        QObject::connect(picker,&QDialog::accepted,c,[=]{
                            if(picker->selectedFiles()!=QStringList{s->sidecar}){finish("Normal file picker accepted a different owned target");return;}
                            s->filePicked=true;pickerTimer->stop();
                            qInfo()<<"XMP_APP_PICKER_INPUT_V1 actual_owned_path_and_accepted_signal=true";
                        });
                    }
                    auto *fileName=picker->findChild<QLineEdit*>("fileNameEdit");
                    if(!fileName)return;
                    if(fileName->text()!=s->sidecar) {
                        fileName->setFocus(Qt::OtherFocusReason);fileName->selectAll();
                        QInputMethodEvent input;input.setCommitString(s->sidecar);QCoreApplication::sendEvent(fileName,&input);
                    }
                    if(picker->selectedFiles()!=QStringList{s->sidecar})return;
                    auto *box=picker->findChild<QDialogButtonBox*>();auto *open=box?box->button(QDialogButtonBox::Open):nullptr;
                    if(open&&open->isEnabled()) {
                        QJsonObject readiness;if(!inputReady(&readiness))return;
                        s->beforeOpenInput=readiness;
                        qInfo()<<"XMP_APP_INPUT_READY_V1 before_open_click"<<readiness.value("prepare_pending").toBool()<<readiness.value("prepare_generation").toInteger();
                        click(open);
                    }
                    return;
                }
            });
            pickerTimer->start();
            c->openXmpImportDialog();s->normalActionReturned=true;return;
        }
        if(s->phase==1) {
            auto *dialog=review();if(!dialog){if(s->normalActionReturned&&!c->xmpMergeBusy()&&s->filePicked)finish("Normal XMP review refused: "+c->statusMessage());return;}
            if(QApplication::activeModalWidget()!=dialog){finish("Review does not own actual modal input");return;}
            s->reviewTitle=dialog->windowTitle();if(auto *notice=dialog->findChild<QLabel*>("xmp-merge-notice"))s->reviewNotice=notice->text();
            auto combos=dialog->findChildren<QComboBox*>();s->conflictChoices=combos.size();
            if(combos.size()!=6){finish("Unknown ancestry must require all six conflicting field choices");return;}
            for(auto *combo:combos) {click(combo);key(combo,Qt::Key_End);key(combo,Qt::Key_Return);
                if(combo->currentData().toInt()!=int(XmpMerge::Choice::UseSidecar)){finish("Actual review key input did not select sidecar");return;}}
            s->allChoices=true;if(!addCapture("review",dialog->grab().toImage())){finish("Cannot capture actual owned review");return;}
            auto *box=dialog->findChild<QDialogButtonBox*>();auto *apply=box?box->button(QDialogButtonBox::Apply):nullptr;
            if(!apply||!apply->isEnabled()){finish("Actual complete choices did not enable Apply");return;}
            s->phase=2;click(apply);return;
        }
        if(s->phase==2) {
            if(c->xmpMergeBusy())return;
            if(c->exposure()!=1.25||c->currentRating()!=4||c->currentFlag()!="pick"||c->currentKeywords()!=QStringList{"incoming"}
               ||c->currentAlbums()!=QStringList{"External"}||c->currentColorLabel()!="red") {finish("Actual durable UI did not publish all six chosen fields: "+c->statusMessage());return;}
            s->UIFullRecord=true;s->UIAfter={{"exposure",c->exposure()},{"rating",c->currentRating()},{"flag",c->currentFlag()},
                {"keywords",QJsonArray::fromStringList(c->currentKeywords())},{"albums",QJsonArray::fromStringList(c->currentAlbums())},{"label",c->currentColorLabel()}};
            if(!c->prepareToClose()){finish("New merged row did not flush safely");return;}
            ProjectDatabase read;QVector<ProjectDatabase::SavedPhoto> photos;if(!read.readSnapshot(s->project,&photos)||photos.size()!=1){finish("Cannot independently reopen the durable catalog");return;}
            const auto &p=photos.front();s->durableFullRecord=p.adjustments.toJson()==s->incoming.toJson()&&p.rating==4&&p.flag=="pick"
                &&p.tags.keywords==QStringList{"incoming"}&&p.tags.albums==QStringList{"External"}&&p.tags.label=="red"&&p.history.canUndo();
            s->storedAfter={{"Develop",p.adjustments.toJson()},{"rating",p.rating},{"flag",p.flag},{"tags",p.tags.toJson()},{"history",p.history.toJson()}};
            if(!s->durableFullRecord){finish("Reopened complete chosen record differs");return;}
            s->appliedStartFrame=s->frames.load(std::memory_order_acquire);s->phase=3;window->update();return;
        }
        if(s->phase==3) {
            if(c->rendering()||s->frames.load(std::memory_order_acquire)<=s->appliedStartFrame)return;
            if(!addCapture("applied",window->grabWindow())){finish("Cannot capture actual applied main window");return;}
            if(!c->canUndo()){finish("Merged Develop lacks its original Undo");return;}c->undo();s->phase=4;return;
        }
        if(s->phase==4) {
            if(c->rendering())return;
            s->developUndo=c->exposure()==0.0;s->annotationsPreserved=c->currentRating()==4&&c->currentFlag()=="pick"&&c->currentKeywords()==QStringList{"incoming"}
                &&c->currentAlbums()==QStringList{"External"}&&c->currentColorLabel()=="red";
            if(!s->developUndo||!s->annotationsPreserved||!c->prepareToClose()){finish("Develop undo/annotation policy was not preserved");return;}
            ProjectDatabase read;QVector<ProjectDatabase::SavedPhoto> photos;if(!read.readSnapshot(s->project,&photos)||photos.size()!=1){finish("Cannot reopen stored Undo");return;}
            const auto &p=photos.front();s->storedUndo=p.adjustments.exposure==0&&p.history.canRedo()&&p.rating==4&&p.flag=="pick"&&p.tags.label=="red";
            s->storedUndoRow={{"Develop",p.adjustments.toJson()},{"rating",p.rating},{"flag",p.flag},{"tags",p.tags.toJson()},{"history",p.history.toJson()}};
            if(!s->storedUndo){finish("Reopened Undo/history differs");return;}s->undoStartFrame=s->frames.load(std::memory_order_acquire);s->phase=5;window->update();return;
        }
        if(s->phase==5) {
            if(s->frames.load(std::memory_order_acquire)<=s->undoStartFrame)return;
            if(!addCapture("undo",window->grabWindow())){finish("Cannot capture actual Undo main window");return;}
            s->phase=7;finish({});
        }
    });timer->start();
}
