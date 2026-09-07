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
    struct State { QElapsedTimer elapsed; int phase=0, edits=0; bool lookEnabled=false; };
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
            if(auto *canvas=window->findChild<QQuickItem *>(QStringLiteral("photoCanvas"))) canvas->setProperty("zoom",1.0);
            state->phase=3;
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
        bool ok=complete && (!gpuRequired || controller->gpuActive());
        auto report=PerformanceRecorder::snapshot();
        report["look_validation_required"]=state->lookEnabled;
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
