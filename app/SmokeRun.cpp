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
#include <memory>

void startSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath, const QString &screenshotPath) {
    struct State { QElapsedTimer elapsed; int phase=0, edits=0; quint64 croppedScopePixels=0; bool lookEnabled=false; bool gridVisited=false, filmstripPresent=false, restoredDevelop=false, curationPassed=false, catalogPassed=false, historyPassed=false, geometryPassed=false; };
    auto state=std::make_shared<State>();state->elapsed.start();
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
            window->resize(1540,920);
            window->setProperty("workspaceIndex", 1);
            state->phase=21;
        } else if(state->phase==21) {
            if(auto *canvas=window->findChild<QQuickItem *>(QStringLiteral("photoCanvas"))) {
                canvas->setProperty("zoom", 1.0);
                state->restoredDevelop=true;
                state->phase=22;
            }
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
                state->phase=3;
            }
        } else if(state->phase==3 && ready) { controller->setExactScopes(true);state->phase=4; }
        else if(state->phase==4 && ready) {
            const auto meta=controller->currentMetadata();
            complete=controller->scopesPixelCount()==meta.value("pixelWidth").toULongLong()*meta.value("pixelHeight").toULongLong();
        }
        if(state->lookEnabled)
            complete=complete && !controller->referenceBusy() && !controller->cameraReferenceUrl().isEmpty();
        if(!complete && state->elapsed.elapsed()<60000) return;
        timer->stop();
        const bool gpuRequired=qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU");
        const bool workspaceOk=state->gridVisited && state->filmstripPresent && state->restoredDevelop && state->curationPassed && state->catalogPassed && state->historyPassed && state->geometryPassed;
        bool ok=complete && workspaceOk && (!gpuRequired || controller->gpuActive());
        auto report=PerformanceRecorder::snapshot();
        report["look_validation_required"]=state->lookEnabled;
        report["ui_library_visited"]=state->gridVisited;
        report["ui_filmstrip_found"]=state->filmstripPresent;
        report["ui_develop_restored"]=state->restoredDevelop;
        report["ui_curation_passed"]=state->curationPassed;
        report["ui_catalog_passed"]=state->catalogPassed;
        report["ui_history_passed"]=state->historyPassed;
        report["ui_geometry_passed"]=state->geometryPassed;
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
