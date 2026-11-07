#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "app/SmokeCaptureCoordinates.h"
#include "core/color/MonitorColorTransform.h"
#include "core/gpu/GpuPreviewItem.h"
#include "core/pipeline/BrushRenderRequest.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>

namespace {
GpuPreviewItem *monitorItem(QQuickItem *item){
    if(!item)return nullptr;if(auto *gpu=qobject_cast<GpuPreviewItem *>(item))return gpu;
    for(auto *child:item->childItems())if(auto *gpu=monitorItem(child))return gpu;return nullptr;
}
struct MonitorReference {
    BrushImageLease working;QImage identity,p3,expectedIdentity,expectedP3;QString profileKey,error;Qt::HANDLE worker=nullptr;
};
struct SceneFrames {std::atomic<quint64> count{0};std::atomic<quintptr> thread{0};};
QByteArray monitorFileHash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
}

void startNativeBrushMonitorSmokeRun(PhotoController *c,QQuickWindow *window,const QString &reportPath) {
    struct State {QElapsedTimer clock;int phase=0,index=0;bool finished=false,referenceReady=false;quint64 startFrames=0,revision=0,submissions=0;
        qint64 sourceKey=0;QJsonObject initialTrace;QByteArray stateBytes,sourceSHA;QJsonArray rows,captures;MonitorReference reference;};
    const auto s=std::make_shared<State>();s->clock.start();const auto fixture=std::make_shared<QTemporaryDir>();
    const auto source=fixture->filePath("owned-native-monitor.png");const auto frames=std::make_shared<SceneFrames>();
    QObject::connect(window,&QQuickWindow::afterFrameEnd,window,[frames]{frames->thread.store(reinterpret_cast<quintptr>(QThread::currentThreadId()),std::memory_order_release);frames->count.fetch_add(1,std::memory_order_release);},Qt::DirectConnection);
    auto *timer=new QTimer(c);timer->setInterval(25);auto *watcher=new QFutureWatcher<MonitorReference>(c);
    const auto finish=[=,keep=fixture](const QString &error){
        Q_UNUSED(keep);if(s->finished)return;s->finished=true;timer->stop();const auto budget=c->brushRenderBudget()->snapshot();
        const bool unchanged=monitorFileHash(source)==s->sourceSHA;
        const bool passed=error.isEmpty()&&s->phase==4&&s->rows.size()==3&&unchanged&&s->clock.elapsed()<60000&&budget.limit==3LL*1024*1024*1024&&budget.peak<=budget.limit;
        const QJsonObject report{{"passed",passed},{"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"GUI_deadline_ms",60000},
            {"actual_qpa",QGuiApplication::platformName()},{"actual_DPR",window->effectiveDevicePixelRatio()},{"actual_backend",c->processingBackend()},
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"owned_source_unchanged",unchanged},{"source_sha256",QString::fromLatin1(s->sourceSHA)},
            {"actual_controller_QtQuick_native_frame",true},{"initial_trace",s->initialTrace},{"final_trace",c->nativeBrushTrace()},
            {"actual_afterFrameEnd_RHI_thread",QString::number(frames->thread.load(std::memory_order_acquire),16)},
            {"reference_worker",QString::number(reinterpret_cast<quintptr>(s->reference.worker),16)},
            {"identity_P3_identity",s->rows},{"captures",s->captures},{"budget_limit_bytes",budget.limit},{"peak_operation_reserved_bytes",budget.peak},
            {"controlled_owned_monitor_selection",true},{"OS_monitor_profile_or_preferences_changed",false},{"physical_nonidentity_monitor_calibration",false},
            {"initial_trace_monitor_key_is_initial_publication_snapshot",true},{"full_RAW_or_all_backend_or_package_acceptance",false},{"Root_modified",false}};
        QSaveFile f(reportPath);const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);const bool saved=f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();QCoreApplication::exit(passed&&saved?0:1);
    };
    const auto apply=[=](GpuPreviewItem *gpu){
        const bool p3=s->index==1;gpu->m_displayColorLut=p3?s->reference.p3:s->reference.identity;
        gpu->m_displayColorLutKey=p3?QStringLiteral("owned-icc:")+s->reference.profileKey:QStringLiteral("owned-native-identity");
        gpu->m_displayColorProfileName=p3?QStringLiteral("Owned Display P3 diagnostic"):QStringLiteral("Owned sRGB identity diagnostic");
        c->setDisplayColorLut(gpu->m_displayColorLut,gpu->m_displayColorLutKey);
        s->startFrames=frames->count.load(std::memory_order_acquire);gpu->update();window->update();
    };
    QObject::connect(watcher,&QFutureWatcher<MonitorReference>::finished,c,[=]{s->reference=watcher->result();s->referenceReady=true;});
    QObject::connect(timer,&QTimer::timeout,c,[=,keep=fixture]{
        Q_UNUSED(keep);if(s->clock.elapsed()>=60000){finish("Actual native monitor scene exceeded original60-second GUI boundary");return;}
        auto *gpu=monitorItem(window->contentItem());
        if(s->phase==0){
            if(c->loading()||c->rendering()||!c->brushReady()||!gpu)return;
            if(!c->beginBrushStrokeModePressure({},.25,.25,.055,.42,.7,.7,"paint",.8)||!c->appendBrushStroke(.45,.32,.6)||c->endBrushStroke().isEmpty()){finish("Cannot commit controlled native monitor stroke");return;}
            s->phase=1;return;
        }
        if(s->phase==1){
            const auto trace=c->nativeBrushTrace();if(!trace["current_frame_native"].toBool()||c->loading()||c->rendering()||!gpu)return;
            s->initialTrace=trace;s->revision=c->renderRevision();s->sourceKey=c->gpuSource().cacheKey();s->stateBytes=QJsonDocument(c->gpuPlan().state.toJson()).toJson(QJsonDocument::Compact);
            s->submissions=c->nativeBrushView().facts->computeSubmissions.load(std::memory_order_acquire);
            const auto pixels=c->gpuSource();const auto plan=c->gpuPlan();const auto budget=c->brushRenderBudget();
            watcher->setFuture(QtConcurrent::run([=]{MonitorReference r;r.worker=QThread::currentThreadId();
                auto request=BrushRenderRequest::createPrepared(pixels,plan,BrushRenderRequest::Layout::Whole,{},&r.error,budget);if(request)r.working=request->render({},&r.error);
                if(!r.working)return r;r.identity=MonitorColorTransform::identityLut();
                const auto profile=MonitorColorTransform::profileFromColorSpace(QColorSpace::fromIccProfile(ColorManagement::iccProfile(ColorManagement::OutputSpace::DisplayP3)),"owned Display P3");
                if(!profile.valid){r.error="Controlled ICC invalid";return r;}r.profileKey=profile.key;r.p3=MonitorColorTransform::srgbToMonitorLut(profile.icc,33,&r.error);
                r.expectedIdentity=MonitorColorTransform::applyLut(r.working.image(),r.identity);r.expectedP3=MonitorColorTransform::applyLut(r.working.image(),r.p3);return r;
            }));s->phase=2;return;
        }
        if(s->phase==2){if(!s->referenceReady)return;if(!s->reference.error.isEmpty()||!s->reference.working||s->reference.expectedP3.isNull()||!gpu){finish("Independent native monitor reference failed: "+s->reference.error);return;}
            apply(gpu);s->phase=3;return;}
        if(s->phase==3){
            if(!gpu||frames->count.load(std::memory_order_acquire)<=s->startFrames)return;
            const auto shot=window->grabWindow();if(shot.isNull()){finish("Actual native monitor window readback failed");return;}
            const auto &expected=s->index==1?s->reference.expectedP3:s->reference.expectedIdentity;
            int maximum=0;QJsonArray samples;
            for(const auto uv:{QPointF(.8,.75),QPointF(.8,.2),QPointF(.12,.8)}){
                const auto point=gpu->mapToScene({gpu->width()*uv.x(),gpu->height()*uv.y()});const auto actual=SmokeCaptureCoordinates::sample(shot,point,window->effectiveDevicePixelRatio());
                const auto p=expected.pixelColor(std::clamp(int(uv.x()*expected.width()),0,expected.width()-1),std::clamp(int(uv.y()*expected.height()),0,expected.height()-1));
                if(!actual.isValid()||!p.isValid()){finish("Native monitor scene sample outside content");return;}
                const int error=std::max({std::abs(actual.red()-p.red()),std::abs(actual.green()-p.green()),std::abs(actual.blue()-p.blue()),std::abs(actual.alpha()-p.alpha())});maximum=std::max(maximum,error);
                samples.append(QJsonObject{{"u",uv.x()},{"v",uv.y()},{"actual_RGBA8",QJsonArray{actual.red(),actual.green(),actual.blue(),actual.alpha()}},{"expected_RGBA8",QJsonArray{p.red(),p.green(),p.blue(),p.alpha()}},{"max_error",error}});
            }
            const auto trace=c->nativeBrushTrace();const bool intact=trace["current_frame_native"].toBool()&&c->renderRevision()==s->revision&&c->gpuSource().cacheKey()==s->sourceKey
                &&QJsonDocument(c->gpuPlan().state.toJson()).toJson(QJsonDocument::Compact)==s->stateBytes
                &&c->nativeBrushView().facts->computeSubmissions.load(std::memory_order_acquire)==s->submissions;
            if(maximum>1||!intact){finish(QStringLiteral("Native monitor scene mismatch %1/255 or processing intent changed").arg(maximum));return;}
            const auto path=QFileInfo(reportPath).absolutePath()+QStringLiteral("/native-monitor-scene-%1.png").arg(s->index);
            if(!shot.save(path)){finish("Cannot save owned monitor scene evidence");return;}
            s->captures.append(QJsonObject{{"path",path},{"width",shot.width()},{"height",shot.height()}});
            s->rows.append(QJsonObject{{"profile",s->index==1?"DisplayP3":"identity"},{"profile_key",gpu->displayColorLutKey()},
                {"max_RGBA8_error",maximum},{"source_revision_state_and_compute_count_unchanged",intact},{"sample_points",samples},
                {"frames_since_selection",qint64(frames->count.load(std::memory_order_acquire)-s->startFrames)}});
            if(++s->index<3){apply(gpu);return;}
            const auto identity=s->rows[0].toObject()["sample_points"].toArray();const auto p3=s->rows[1].toObject()["sample_points"].toArray();const auto restored=s->rows[2].toObject()["sample_points"].toArray();
            if(identity!=restored||identity==p3){finish("Controlled ICC did not change and exactly restore actual native scene pixels");return;}
            if(frames->thread.load(std::memory_order_acquire)==reinterpret_cast<quintptr>(QThread::currentThreadId())
                ||s->reference.worker==QThread::currentThreadId()){finish("Native monitor scene did not use separate RHI and CPU reference roles");return;}
            s->phase=4;finish({});
        }
    });
    window->setProperty("workspaceIndex",1);window->resize(1540,920);c->setGpuEnabled(true);
    QImage image(128,96,QImage::Format_RGBA64);image.fill(QColor::fromRgba64(12000,39000,55000,65535));image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    if(!fixture->isValid()||!image.save(source)||!c->createProject(QUrl::fromLocalFile(fixture->path()),"OwnedNativeMonitor")||!c->importFile(QUrl::fromLocalFile(source))){finish("Cannot create isolated monitor source/project");return;}
    s->sourceSHA=monitorFileHash(source);timer->start();
}
