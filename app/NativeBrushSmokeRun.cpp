#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>

void startNativeBrushRouteSmokeRun(PhotoController *c,QQuickWindow *window,const QString &reportPath){
    struct State {QElapsedTimer elapsed;int phase=0;bool finished=false;QString layer;QJsonArray observations,captures;QJsonObject first,copy,checks;quint64 revision=0; qint64 hiddenAt=0;QImage referenceSource;ProcessingPlan referencePlan;};
    const auto state=std::make_shared<State>();state->elapsed.start();
    const auto dir=std::make_shared<QTemporaryDir>();const auto sha=std::make_shared<QByteArray>();
    auto *timer=new QTimer(c);timer->setInterval(25);
    const auto sourcePath=dir->filePath("owned-native.png");const auto outputFolder=QFileInfo(reportPath).absolutePath();QDir().mkpath(outputFolder);
    const auto finish=[=,fixture=dir](const QString &error){
        Q_UNUSED(fixture);
        if(state->finished)return;state->finished=true;timer->stop();QFile file(sourcePath);bool unchanged=false;if(file.open(QIODevice::ReadOnly))unchanged=QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256)==*sha;
        bool all=true;for(auto it=state->checks.constBegin();it!=state->checks.constEnd();++it)all=all&&it.value().isBool()&&it.value().toBool();
        const bool passed=error.isEmpty()&&all&&state->phase==8&&unchanged&&state->elapsed.elapsed()<60000;
        QJsonObject report{{"passed",passed},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"phase",state->phase},{"elapsed_ms",state->elapsed.elapsed()},
            {"actual_qpa",QGuiApplication::platformName()},{"actual_dpr",window->devicePixelRatio()},{"backend",c->processingBackend()},
            {"source_unchanged",unchanged},{"checks",state->checks},{"observations",state->observations},{"captures",state->captures},{"last_trace",c->nativeBrushTrace()},
            {"physical_tablet",false},{"real_RAW",false},{"full_frame_CPU_comparison",false},{"error",error}};
        QSaveFile out(reportPath);const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);const bool saved=out.open(QIODevice::WriteOnly)&&out.write(bytes)==bytes.size()&&out.commit();QCoreApplication::exit(passed&&saved?0:1);
    };
    const auto capture=[=](const QString &name){const auto image=window->grabWindow();const auto path=outputFolder+"/"+name+".png";if(image.isNull()||!image.save(path))return false;state->captures.append(QJsonObject{{"path",path},{"width",image.width()},{"height",image.height()}});return true;};
    const auto native=[=]{return c->nativeBrushTrace().value("current_frame_native").toBool()&&c->gpuActive()&&!c->loading()&&!c->rendering();};
    QObject::connect(timer,&QTimer::timeout,c,[=]{
        if(state->elapsed.elapsed()>=60000){finish(QStringLiteral("Native controller/QtQuick route exceeded the original 60-second GUI budget"));return;}
        if(state->phase==0){
            if(!c->brushReady()||!c->previewReady()||c->loading()||c->rendering())return;
            if(!c->beginBrushStrokeModePressure({},.28,.36,.06,.45,.7,.8,QStringLiteral("paint"),.8)||!c->appendBrushStroke(.52,.56,.6)||!c->appendBrushStroke(.7,.42,.9)){finish("Controlled stroke setup refused");return;}
            state->layer=c->endBrushStroke();if(state->layer.isEmpty()){finish("Controlled stroke commit refused");return;}state->phase=1;return;
        }
        if(state->phase==1){
            if(!native())return;const auto trace=c->nativeBrushTrace();state->first=trace;state->observations.append(trace);state->revision=c->renderRevision();
            state->checks["two_native_compute_dispatches"]=trace.value("coverage_dispatches").toInteger()>0&&trace.value("colour_dispatches").toInteger()>0;
            state->checks["native_source_uploaded_once"]=trace.value("source_uploads").toInteger()==1;
            state->checks["no_CPU_colour_or_mask_upload"]=trace.value("cpu_colour_uploaded").isBool()&&!trace.value("cpu_colour_uploaded").toBool()&&trace.value("cpu_mask_uploaded").isBool()&&!trace.value("cpu_mask_uploaded").toBool();
            const auto gui=trace.value("gui_thread").toString(),rhi=trace.value("rhi_thread").toString(),source=trace.value("source_worker_thread").toString(),frame=trace.value("frame_worker_thread").toString();
            state->checks["actual_threaded_GUI_RHI_worker_roles"]=!gui.isEmpty()&&!rhi.isEmpty()&&!source.isEmpty()&&!frame.isEmpty()&&gui!=rhi&&source!=gui&&source!=rhi&&frame!=gui&&frame!=rhi;
            state->checks["original_monitor_presentation"]=!trace.value("monitor_lut_key").toString().isEmpty()&&trace.value("presentation").toString().contains("premultiplication");
            state->checks["normal_no_finish"]=trace.value("normal_finish_calls").toInteger(-1)==0;
            state->referenceSource=c->gpuSource();state->referencePlan=c->gpuPlan();state->phase=2;if(!c->requestBugReport()){finish("Native diagnostic request refused");return;}return;
        }
        if(state->phase==2){
            if(c->diagnosticBusy())return;const auto diag=c->nativeBrushTrace().value("diagnostic").toObject();state->observations.append(diag);
            const auto samples=diag.value("diagnostic_samples_rgba16").toArray();QString error;
            auto request=BrushRenderRequest::createPrepared(state->referenceSource,state->referencePlan,BrushRenderRequest::Layout::Whole,{},&error,c->brushRenderBudget());
            const auto reference=request?request->render({},&error):BrushImageLease{};int maxRGB=0,maxAlpha=0;bool matched=reference&&samples.size()==32&&diag.value("available").toBool()&&diag.value("producer").toString()=="native-brush-gpu-compute";
            if(matched)for(const auto &value:samples){const auto sample=value.toObject();const int x=sample.value("x").toInt(-1),y=sample.value("y").toInt(-1);const auto rgba=sample.value("rgba16").toArray();if(x<0||y<0||x>=reference.image().width()||y>=reference.image().height()||rgba.size()!=4){matched=false;break;}
                const auto pixel=reinterpret_cast<const QRgba64*>(reference.image().constScanLine(y))[x];const int expected[4]={pixel.red(),pixel.green(),pixel.blue(),pixel.alpha()};for(int ch=0;ch<4;++ch){const int delta=std::abs(expected[ch]-rgba[ch].toInt(-999999));if(ch<3)maxRGB=std::max(maxRGB,delta);else maxAlpha=std::max(maxAlpha,delta);}}
            state->checks["native_diagnostic_32_samples_original_CPU_RGB40_alpha_exact"]=matched&&maxRGB<=40&&maxAlpha==0;
            state->observations.append(QJsonObject{{"diagnostic_samples",samples.size()},{"max_RGB16_error",maxRGB},{"max_alpha16_error",maxAlpha},{"CPU_error",error}});
            state->referenceSource={};if(!capture("native-initial")){finish("Native screenshot unavailable");return;}c->undo();state->phase=3;return;
        }
        if(state->phase==3){if(c->renderRevision()==state->revision||c->loading()||c->rendering()||BrushMaskTileRaster::anyEnabled(c->gpuPlan().state.brushMasks))return;state->checks["undo_preserved_original_intent"]=true;c->redo();state->phase=4;return;}
        if(state->phase==4){if(!native()||c->renderRevision()==state->revision)return;state->checks["redo_reentered_native_route"]=true;state->observations.append(c->nativeBrushTrace());if(!capture("native-redo")){finish("Redo capture failed");return;}state->phase=5;if(!c->createVirtualCopy("Native route copy")){finish("Owned virtual copy failed");return;}return;}
        if(state->phase==5){if(!native())return;const auto trace=c->nativeBrushTrace();if(trace.value("version")==state->first.value("version"))return;state->copy=trace;state->observations.append(trace);
            state->checks["virtual_copy_distinct_actual_delivery"]=trace.value("photo")==state->first.value("photo")&&trace.value("version")!=state->first.value("version")&&trace.value("owner_epoch")!=state->first.value("owner_epoch");
            state->checks["physical_source_key_shared_across_delivery_versions"]=trace.value("input_source_key")==state->first.value("input_source_key");
            if(!capture("native-virtual-copy")){finish("Virtual copy capture failed");return;}state->hiddenAt=state->elapsed.elapsed();window->hide();state->phase=6;return;
        }
        if(state->phase==6){if(state->elapsed.elapsed()-state->hiddenAt<250)return;window->show();state->phase=7;return;}
        if(state->phase==7){if(!native())return;const auto trace=c->nativeBrushTrace();if(trace.value("rhi_generation").toInteger()<=state->copy.value("rhi_generation").toInteger())return;
            state->checks["hidden_window_retired_old_RHI_generation"]=true;state->checks["CPU_canonical_reused_after_owner_retirement"]=trace.value("input_source_key")==state->copy.value("input_source_key");state->observations.append(trace);if(!capture("native-owner-recreated")){finish("Recreated owner capture failed");return;}state->phase=8;finish({});
        }
    });
    window->setProperty("workspaceIndex",1);c->setGpuEnabled(true);QImage image(640,480,QImage::Format_RGBA64);
    for(int y=0;y<image.height();++y){auto*row=reinterpret_cast<QRgba64*>(image.scanLine(y));for(int x=0;x<image.width();++x)row[x]=QRgba64::fromRgba64(quint16(7000+(x*59+y*7)%40000),quint16(4000+(y*77+x*13)%45000),quint16(3000+(x*47+y*37)%42000),65535);}
    if(!dir->isValid()||!image.save(sourcePath)||!c->createProject(QUrl::fromLocalFile(dir->path()),"Owned native brush project")){finish("Cannot create owned native project/source");return;}
    QFile original(sourcePath);if(!original.open(QIODevice::ReadOnly)){finish("Cannot fingerprint owned source");return;}*sha=QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256);
    if(!c->importFile(QUrl::fromLocalFile(sourcePath))){finish("Owned native source import refused");return;}timer->start();
}
