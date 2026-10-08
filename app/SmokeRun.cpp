#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "diagnostics/PerformanceRecorder.h"
#include "diagnostics/LoggingEngine.h"
#include <QCoreApplication>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJSValue>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <memory>

namespace {
QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (!item) return nullptr;
    if (item->objectName()==name) return item;
    for (auto *child : item->childItems()) if (auto *found=visualChild(child,name)) return found;
    return nullptr;
}
bool clickItem(QQuickWindow *window,QQuickItem *item) {
    if (!item || !item->isVisible() || !item->isEnabled()) return false;
    const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    const auto global=window->mapToGlobal(point.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,point,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&press);
    QMouseEvent release(QEvent::MouseButtonRelease,point,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&release); return true;
}
}

void startSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath, const QString &screenshotPath) {
    struct State { QElapsedTimer elapsed; int phase=0, edits=0; quint64 croppedScopePixels=0, straightenedScopePixels=0; double straightenDegrees=0; bool straightenPassed=false; bool lookEnabled=false; bool gridVisited=false, filmstripPresent=false, presetActionsPresent=false, restoredDevelop=false, curationPassed=false, catalogPassed=false, catalogDatesPassed=false, copiesPassed=false, historyPassed=false, geometryPassed=false, interactiveCropPassed=false; };
    auto state=std::make_shared<State>();state->elapsed.start();
    auto cropTrace=std::make_shared<QJsonObject>();
    auto scopePlots=std::make_shared<QJsonObject>();
    auto copyTrace=std::make_shared<QJsonObject>();
    auto transferTrace=std::make_shared<QJsonObject>();
    auto importFixture=std::make_shared<QTemporaryDir>();
    auto *timer=new QTimer(controller);timer->setInterval(50);
    QObject::connect(timer,&QTimer::timeout,controller,[=] {
        const bool ready=controller->previewReady() && !controller->loading() && !controller->rendering();
        bool complete=false;
        if(state->phase==0 && ready) {
            // Every alpha.8 RAW smoke exercises the new look and independent reference UI.
            state->lookEnabled=controller->currentIsRaw();
            if(state->lookEnabled) {
                controller->setLookCode("FL");controller->setLookParameter("sharpness",5);controller->setLookParameter("clarity",4);
                controller->setLookParameter("fade",2);controller->setShowCameraReference(true);
            }
            state->phase=1;
        }
        else if(state->phase==1) {
            controller->setExposure((state->edits%11-5)/10.0);
            controller->setSaturation(state->edits%25);
            controller->setColorMix(5,1,state->edits%19-9);
            ++state->edits;
            if(controller->library().size()>1 && (state->edits==10 || state->edits==20)) controller->selectPhoto(state->edits==10?1:0);
            if(state->edits>=30) {
                controller->setExposure(.4);controller->setSaturation(15);controller->setContrast(10);
                controller->finishInteraction();state->phase=2;
            }
        } else if(state->phase==2 && ready) {
            // Exercise new curation metadata without changing the RAW pixels.
            controller->setRating(4);
            controller->setFlag(QStringLiteral("pick"));
            state->curationPassed = controller->currentRating() == 4
                && controller->currentFlag() == QStringLiteral("pick");
            state->filmstripPresent = window->findChild<QQuickItem *>(QStringLiteral("jixelMainFilmstrip")) != nullptr;
            // Verify that the new Library page can be instantiated while
            // preserving the current photo, then return to the GPU canvas.
            const auto exposure = controller->exposure();
            state->catalogPassed = controller->setPhotoSelection({controller->currentIndex()})
                && controller->setSelectionKeywords("fusion, smoke")
                && controller->addSelectionToAlbum("Smoke validation")
                && controller->addSelectionToAlbum("Second album")
                && controller->setSelectionLabel("blue")
                && controller->exposure() == exposure;
            window->resize(1180,720);
            window->setProperty("workspaceIndex", 0);
            state->phase=20;
        } else if(state->phase==20) {
            auto *workspace = window->findChild<QQuickItem *>(QStringLiteral("libraryWorkspace"));
            auto *grid = window->findChild<QQuickItem *>(QStringLiteral("libraryPhotoGrid"));
            state->gridVisited = workspace && grid;
            if (state->gridVisited) {
                workspace->setProperty("filterMode", 1); // Picks
                state->curationPassed = state->curationPassed && grid->property("count").toInt() == 1;
            }
            if (workspace) {
                workspace->setProperty("searchText", "SMOKE");
                workspace->setProperty("filterAlbum", "Smoke validation");
                workspace->setProperty("filterLabel", "blue");
                workspace->setProperty("sortMode", 2);
            }
            state->phase=201;
        } else if(state->phase==201) {
            auto *workspace = window->findChild<QQuickItem *>(QStringLiteral("libraryWorkspace"));
            auto *grid = window->findChild<QQuickItem *>(QStringLiteral("libraryPhotoGrid"));
            state->catalogPassed = state->catalogPassed && workspace && grid
                && grid->property("count").toInt() == 1 && grid->height() > 100;
            const auto albumFilter = window->findChild<QQuickItem *>(QStringLiteral("catalogAlbumFilter"));
            state->catalogPassed = state->catalogPassed && albumFilter && albumFilter->property("count").toInt() == 3;
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath + ".library.png");
            if (workspace) workspace->setProperty("searchText", "no-matching-photo-xyz");
            state->phase=202;
        } else if(state->phase==202) {
            auto *grid = window->findChild<QQuickItem *>(QStringLiteral("libraryPhotoGrid"));
            state->catalogPassed = state->catalogPassed && grid && grid->property("count").toInt() == 0;
            const auto originalIndex = controller->currentIndex();
            const auto originalCount = controller->library().size();
            const auto originalExposure = controller->exposure();
            const auto source = controller->currentFile();
            state->copiesPassed = controller->createVirtualCopy("Smoke version") && !controller->canUndo()
                && controller->currentFile() == source;
            state->copiesPassed = state->copiesPassed && controller->renameCurrentVirtualCopy("Smoke renamed")
                && controller->library()[controller->currentIndex()].toMap().value("versionName").toString() == "Smoke renamed"
                && controller->createVirtualCopy("Temporary smoke version") && controller->removeCurrentVirtualCopy()
                && controller->library().size() == originalCount+1;
            const auto copyIndex = controller->currentIndex();
            controller->setExposure(-.6); controller->finishInteraction();
            controller->selectPhoto(originalIndex);
            state->copiesPassed = state->copiesPassed && controller->exposure() == originalExposure;
            controller->selectPhoto(copyIndex);
            state->copiesPassed = state->copiesPassed && controller->exposure() == -.6;
            controller->setExposure(originalExposure); controller->finishInteraction();
            if (auto *workspace = window->findChild<QQuickItem *>(QStringLiteral("libraryWorkspace"))) {
                workspace->setProperty("filterMode",0); workspace->setProperty("searchText",QString());
                workspace->setProperty("filterAlbum",QString()); workspace->setProperty("filterLabel","none"); workspace->setProperty("sortMode",4);
            }
            state->phase=203;
        } else if(state->phase==203 || state->phase==204) {
            auto *workspace = window->findChild<QQuickItem *>(QStringLiteral("libraryWorkspace"));
            const auto rows = workspace ? workspace->property("visiblePhotos").value<QJSValue>().toVariant().toList() : QVariantList{};
            const bool sorted = !rows.isEmpty() && rows[0].toMap().value("index").toInt() == controller->currentIndex();
            if (state->phase==203) {
                state->catalogDatesPassed = sorted;
                if (workspace) workspace->setProperty("sortMode",5);
                state->phase=204; return;
            }
            state->catalogDatesPassed = state->catalogDatesPassed && sorted;
            for (const auto &row : controller->library()) state->catalogDatesPassed = state->catalogDatesPassed && row.toMap().value("captureChecked").toBool();
            const auto sort = window->findChild<QQuickItem *>(QStringLiteral("catalogSortMode"));
            state->catalogDatesPassed = state->catalogDatesPassed && sort && sort->property("count").toInt() == 6;
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath + ".versions.png");
            window->resize(1540,920);
            window->setProperty("workspaceIndex", 1);
            state->phase=21;
        } else if(state->phase==21) {
            if(auto *canvas=window->findChild<QQuickItem *>(QStringLiteral("photoCanvas"))) {
                canvas->setProperty("zoom", 1.0);
                state->restoredDevelop=true;
                const auto actions=window->findChild<QQuickItem *>(QStringLiteral("namedPresetActions"));
                if (actions) QMetaObject::invokeMethod(actions,"clicked",Qt::DirectConnection);
                state->phase=211;
            }
        } else if(state->phase==211) {
            const auto menu=window->findChild<QObject *>(QStringLiteral("namedPresetMenu"));
            state->presetActionsPresent=menu && menu->property("visible").toBool();
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath + ".presets.png");
            if (menu) QMetaObject::invokeMethod(menu,"close",Qt::DirectConnection);
            state->phase=22;
        } else if(state->phase==22 && ready) {
            const double originalExposure = controller->exposure();
            controller->setExposure(1.2); controller->finishInteraction();
            controller->undo();
            state->historyPassed = std::abs(controller->exposure()-originalExposure)<1e-9 && controller->canRedo();
            controller->redo();
            state->historyPassed = state->historyPassed && std::abs(controller->exposure()-1.2)<1e-9;
            controller->undo();
            controller->rotatePhoto(1);
            state->phase=23;
        } else if(state->phase==23 && ready) {
            state->geometryPassed = controller->geometry().value("quarterTurns").toInt()==1;
            const auto button=window->findChild<QQuickItem *>(QStringLiteral("beginInteractiveCrop"));
            if (button) QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            state->interactiveCropPassed=controller->cropEditing();
            cropTrace->insert("began",controller->cropEditing());
            state->phase=231;
        } else if(state->phase==231 && ready) {
            const auto overlay=window->findChild<QQuickItem *>(QStringLiteral("interactiveCropOverlay"));
            // Repeater delegates live in the visual tree, which can differ
            // from QObject ownership used by findChild.
            const auto handle=visualChild(overlay,QStringLiteral("cropHandle4"));
            state->interactiveCropPassed=state->interactiveCropPassed && overlay && handle && overlay->isVisible();
            cropTrace->insert("overlay",overlay!=nullptr); cropTrace->insert("handle",handle!=nullptr);
            cropTrace->insert("visible",overlay && overlay->isVisible());
            if (handle && overlay) {
                // Full-frame corner centers lie on the canvas clip edge. Hit
                // the visible interior of the handle, as a real pointer would.
                const auto origin=handle->mapToScene(QPointF(handle->width()/2-3,handle->height()/2-3));
                const auto destination=origin-QPointF(overlay->width()/4,overlay->height()/4);
                cropTrace->insert("origin_x",origin.x()); cropTrace->insert("origin_y",origin.y());
                auto send=[&](QEvent::Type type,QPointF position,Qt::MouseButton button,Qt::MouseButtons buttons) {
                    QMouseEvent event(type,position,window->mapToGlobal(position.toPoint()),button,buttons,Qt::NoModifier);
                    QCoreApplication::sendEvent(window,&event);
                };
                send(QEvent::MouseButtonPress,origin,Qt::LeftButton,Qt::LeftButton);
                send(QEvent::MouseMove,destination,Qt::NoButton,Qt::LeftButton);
                send(QEvent::MouseButtonRelease,destination,Qt::LeftButton,Qt::NoButton);
            }
            state->phase=232;
        } else if(state->phase==232 && ready) {
            const auto overlay=window->findChild<QQuickItem *>(QStringLiteral("interactiveCropOverlay"));
            const auto selection=overlay ? overlay->property("selection").toRectF() : QRectF{};
            cropTrace->insert("width",selection.width()); cropTrace->insert("height",selection.height());
            state->interactiveCropPassed=state->interactiveCropPassed && selection.width()>.5 && selection.width()<.99
                && selection.height()>.5 && selection.height()<.99;
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".crop.png");
            const auto apply=window->findChild<QQuickItem *>(QStringLiteral("applyInteractiveCrop"));
            if (apply) QMetaObject::invokeMethod(apply,"clicked",Qt::DirectConnection);
            state->interactiveCropPassed=state->interactiveCropPassed && !controller->cropEditing();
            cropTrace->insert("applied",!controller->cropEditing());
            state->phase=233;
        } else if(state->phase==233 && ready) {
            state->interactiveCropPassed=state->interactiveCropPassed && controller->geometry().value("width").toDouble()<1;
            controller->undo();
            state->interactiveCropPassed=state->interactiveCropPassed && controller->geometry().value("width").toDouble()==1
                && controller->geometry().value("quarterTurns").toInt()==1;
            controller->setCrop(0,0,.5,1);
            controller->setExactScopes(true);
            state->phase=24;
        } else if(state->phase==24 && ready) {
            const auto meta=controller->currentMetadata();
            const auto expected=(meta.value("pixelWidth").toULongLong()+1)/2 * meta.value("pixelHeight").toULongLong();
            if(controller->scopesPixelCount()==expected) {
                state->croppedScopePixels=controller->scopesPixelCount();
                state->geometryPassed = state->geometryPassed && controller->geometry().value("width").toDouble()==.5;
                controller->undo(); // crop
                controller->undo(); // rotation
                state->geometryPassed = state->geometryPassed && controller->geometry().value("quarterTurns").toInt()==0
                    && controller->geometry().value("width").toDouble()==1;
                state->phase=25;
            }
        } else if(state->phase==25 && ready) {
            auto *slider=window->findChild<QQuickItem *>(QStringLiteral("straightenAngle"));
            if (slider && slider->isVisible() && slider->isEnabled()) {
                const auto point=slider->mapToScene(QPointF(slider->width()*.56,slider->height()/2));
                const auto global=window->mapToGlobal(point.toPoint());
                QMouseEvent press(QEvent::MouseButtonPress,point,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QCoreApplication::sendEvent(window,&press);
                QMouseEvent release(QEvent::MouseButtonRelease,point,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QCoreApplication::sendEvent(window,&release);
            }
            controller->finishInteraction();
            state->straightenDegrees=controller->geometry().value("straighten").toDouble();
            state->straightenPassed=std::abs(state->straightenDegrees)>0 && std::abs(state->straightenDegrees)<15;
            state->phase=26;
        } else if(state->phase==26 && ready) {
            const auto meta=controller->currentMetadata(); GeometryState g; g.straighten=state->straightenDegrees;
            const auto size=g.straightenedSize({meta.value("pixelWidth").toInt(),meta.value("pixelHeight").toInt()});
            if (controller->scopesPixelCount()==quint64(size.width())*size.height()) {
                state->straightenedScopePixels=controller->scopesPixelCount();
                if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".straighten.png");
                controller->undo(); state->straightenPassed=state->straightenPassed && !controller->geometry().contains("straighten");
                state->phase=3;
            }
        } else if(state->phase==3 && ready) { controller->setExactScopes(true);state->phase=4; }
        else if(state->phase==4 && ready) {
            const auto meta=controller->currentMetadata();
            if (controller->scopesPixelCount()==meta.value("pixelWidth").toULongLong()*meta.value("pixelHeight").toULongLong()) {
                controller->setScopeMode("waveform"); state->phase=41;
            }
        } else if ((state->phase==41 || state->phase==42 || state->phase==43) && ready && controller->scopePlotCurrent()) {
            const auto meta=controller->currentMetadata();
            const auto expected=meta.value("pixelWidth").toULongLong()*meta.value("pixelHeight").toULongLong();
            const auto view=window->findChild<QQuickItem *>(QStringLiteral("scopePlotView"));
            const auto mode=controller->scopeMode();
            scopePlots->insert(mode,view && view->isVisible() && !controller->scopePlotUrl().isEmpty() && controller->scopePlotPixels()==expected);
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+"."+mode+".png");
            if (state->phase==41) controller->setScopeMode("parade");
            else if (state->phase==42) controller->setScopeMode("vectorscope");
            else controller->setScopeMode("histogram");
            ++state->phase;
        } else if (state->phase==44 && ready) {
            auto *menu=window->findChild<QObject *>(QStringLiteral("importActionsMenu"));
            copyTrace->insert("menu_opened",menu && QMetaObject::invokeMethod(menu,"open"));
            state->phase=45;
        } else if (state->phase==45 && ready) {
            auto *menu=window->findChild<QObject *>(QStringLiteral("importActionsMenu"));
            auto *item=window->findChild<QQuickItem *>(QStringLiteral("copyImportAction"));
            copyTrace->insert("menu_visible",menu && menu->property("visible").toBool() && item && item->isVisible() && item->isEnabled());
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".import-menu.png");
            if (menu) QMetaObject::invokeMethod(menu,"close");
            QImage fixture(64,48,QImage::Format_RGB32); fixture.fill(QColor("#658797"));
            const bool prepared=importFixture->isValid() && QDir(importFixture->path()).mkdir("copies") && fixture.save(importFixture->filePath("copy-test.png"));
            copyTrace->insert("before",controller->library().size());
            copyTrace->insert("started",prepared && controller->copyImport({QUrl::fromLocalFile(importFixture->filePath("copy-test.png"))},QUrl::fromLocalFile(importFixture->filePath("copies"))));
            state->phase=46;
        } else if (state->phase==46 && ready && !controller->copyImportBusy()) {
            QFile source(importFixture->filePath("copy-test.png")),destination(importFixture->filePath("copies/copy-test.png"));
            copyTrace->insert("content_equal",source.open(QIODevice::ReadOnly) && destination.open(QIODevice::ReadOnly) && source.readAll()==destination.readAll());
            copyTrace->insert("catalog_added",controller->library().size()==copyTrace->value("before").toInt()+1);
            transferTrace->insert("source_index",controller->currentIndex());
            transferTrace->insert("source_exposure",controller->exposure());
            controller->setPhotoSelection({controller->currentIndex(),controller->library().size()-1});
            transferTrace->insert("opened",clickItem(window,window->findChild<QQuickItem *>(QStringLiteral("openTransferSync"))));
            state->phase=47;
        } else if (state->phase==47 && ready) {
            auto *dialog=window->findChild<QObject *>(QStringLiteral("adjustmentTransferDialog"));
            auto *geometry=visualChild(window->contentItem(),QStringLiteral("transferGroup_geometry"));
            transferTrace->insert("default_geometry_excluded",geometry && !geometry->property("checked").toBool());
            transferTrace->insert("visible",dialog && dialog->property("visible").toBool() && dialog->property("targetCount").toInt()==1);
            transferTrace->insert("none_clicked",clickItem(window,visualChild(window->contentItem(),QStringLiteral("transferSelectNone"))));
            transferTrace->insert("exposure_clicked",clickItem(window,visualChild(window->contentItem(),QStringLiteral("transferGroup_exposure"))));
            state->phase=48;
        } else if (state->phase==48 && ready) {
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".sync-dialog.png");
            transferTrace->insert("language",controller->language());
            controller->setLanguage("en_US"); window->resize(1180,720); state->phase=481;
        } else if (state->phase==481 && ready) {
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".sync-dialog-en.png");
            transferTrace->insert("applied",clickItem(window,visualChild(window->contentItem(),QStringLiteral("transferApplyButton"))));
            controller->setLanguage(transferTrace->value("language").toString()); window->resize(1540,920);
            state->phase=49;
        } else if (state->phase==49 && ready) {
            auto *dialog=window->findChild<QObject *>(QStringLiteral("adjustmentTransferDialog"));
            transferTrace->insert("closed",dialog && !dialog->property("visible").toBool());
            controller->selectPhoto(controller->library().size()-1);
            const auto expected=transferTrace->value("source_exposure").toDouble();
            const bool changed=controller->exposure()==expected && controller->saturation()==0 && controller->canUndo();
            controller->undo(); const bool undone=controller->exposure()==0; controller->redo();
            transferTrace->insert("isolated_undo",changed && undone && controller->exposure()==expected);
            controller->selectPhoto(transferTrace->value("source_index").toInt());
            state->phase=50;
        } else if (state->phase==50 && ready) {
            // Restoring the source starts a new asynchronous histogram. Preview
            // readiness alone does not mean its full-resolution counts arrived.
            const auto meta=controller->currentMetadata();
            const auto expected=meta.value("pixelWidth").toULongLong()*meta.value("pixelHeight").toULongLong();
            const bool scopesReady=expected>0 && controller->scopesPixelCount()==expected;
            transferTrace->insert("final_scopes",scopesReady);
            complete=scopesReady && scopePlots->value("waveform").toBool() && scopePlots->value("parade").toBool() && scopePlots->value("vectorscope").toBool();
        }
        if(state->lookEnabled)
            complete=complete && !controller->referenceBusy() && !controller->cameraReferenceUrl().isEmpty();
        if(!complete && state->elapsed.elapsed()<60000) return;
        timer->stop();
        const bool gpuRequired=qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU");
        const bool workspaceOk=state->gridVisited && state->filmstripPresent && state->presetActionsPresent && state->restoredDevelop && state->curationPassed && state->catalogPassed && state->catalogDatesPassed && state->copiesPassed && state->historyPassed && state->geometryPassed && state->interactiveCropPassed && state->straightenPassed;
        const bool copyOk=copyTrace->value("menu_opened").toBool() && copyTrace->value("menu_visible").toBool() && copyTrace->value("started").toBool()
            && copyTrace->value("content_equal").toBool() && copyTrace->value("catalog_added").toBool();
        bool transferOk=true;
        for (const auto &key : {"opened","visible","default_geometry_excluded","none_clicked","exposure_clicked","applied","closed","isolated_undo","final_scopes"})
            transferOk=transferOk && transferTrace->value(key).toBool();
        bool ok=complete && workspaceOk && copyOk && transferOk && (!gpuRequired || controller->gpuActive());
        auto report=PerformanceRecorder::snapshot();
        report["look_validation_required"]=state->lookEnabled;
        report["ui_library_visited"]=state->gridVisited;
        report["ui_filmstrip_found"]=state->filmstripPresent;
        report["ui_preset_actions_found"]=state->presetActionsPresent;
        report["ui_develop_restored"]=state->restoredDevelop;
        report["ui_curation_passed"]=state->curationPassed;
        report["ui_catalog_passed"]=state->catalogPassed;
        report["ui_catalog_dates_passed"]=state->catalogDatesPassed;
        report["ui_virtual_copies_passed"]=state->copiesPassed;
        report["ui_history_passed"]=state->historyPassed;
        report["ui_geometry_passed"]=state->geometryPassed;
        report["ui_interactive_crop_passed"]=state->interactiveCropPassed;
        report["ui_interactive_crop_trace"]=*cropTrace;
        report["ui_scope_plots"]=*scopePlots;
        report["ui_copy_import_passed"]=copyOk;
        report["ui_copy_import_trace"]=*copyTrace;
        report["ui_selective_sync_passed"]=transferOk;
        report["ui_selective_sync_trace"]=*transferTrace;
        report["ui_straighten_passed"]=state->straightenPassed;
        report["ui_straighten_degrees"]=state->straightenDegrees;
        report["ui_straighten_scope_pixels"]=qint64(state->straightenedScopePixels);
        report["ui_geometry_crop_scope_pixels"]=qint64(state->croppedScopePixels);
        report["look"]=QJsonObject::fromVariantMap(controller->lookState());
        report["reference"]=QJsonObject::fromVariantMap(controller->cameraReferenceInfo());
        report["source_commit"]=QStringLiteral(JIXELLIGHT_GIT_COMMIT);
        report["build_version"]=QCoreApplication::applicationVersion();
        report["smoke_passed"]=ok;
        report["phase"]=state->phase;report["edits"]=state->edits;
        report["backend"]=controller->processingBackend();report["gpu_active"]=controller->gpuActive();
        report["preview_ready"]=controller->previewReady();report["scopes_mode"]=controller->scopesStatus();
        report["scope_pixels"]=qint64(controller->scopesPixelCount());report["render_revision"]=qint64(controller->renderRevision());
        report["elapsed_ms"]=state->elapsed.elapsed();
        if(!screenshotPath.isEmpty()) report["screenshot_saved"]=window->grabWindow().save(screenshotPath);
        QSaveFile output(reportPath);
        if(!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(report).toJson())<0 || !output.commit()) ok=false;
        LoggingEngine::flush();
        QCoreApplication::exit(ok?0:2);
    });
    timer->start();
}
