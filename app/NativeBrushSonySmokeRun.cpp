#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/scopes/ScopesEngine.h"
#include "app/SmokeCaptureCoordinates.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <filesystem>
#ifdef Q_OS_MACOS
void beginGuiSmokeExportActivity();
#endif

namespace {
QByteArray sonyHash(const QString &path) {
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);const auto bytes=f.size();qint64 read=0;
    while(read<bytes){const auto block=f.read(std::min<qint64>(1024*1024,bytes-read));if(block.isEmpty())return {};hash.addData(block);read+=block.size();}
    return f.size()==bytes?hash.result().toHex():QByteArray{};
}
QQuickItem *sonyItem(QQuickItem *root,const QString &name) {
    if(!root)return nullptr;if(root->objectName()==name)return root;
    for(auto *child:root->childItems())if(auto *found=sonyItem(child,name))return found;
    return nullptr;
}
bool saveSonyJson(const QString &path,const QJsonObject &object) {
    QSaveFile f(path);const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Indented);
    return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
}
struct SonyOracle {
    ScopesResult scopes;std::array<ScopePlotResult,3> plots;
    bool previewMatched=false;int maxRGB=0,maxAlpha=0;QString error,thread;
    qint64 elapsedMs=0;
};
}

// Separate genuine-RAW integration QA. Existing Sony pressure/owned cases and
// all original producers remain byte-identical; no physical pen is simulated.
void startNativeBrushSonySmokeRun(PhotoController *c,QQuickWindow *window,const QString &source,const QString &reportPath) {
    // Refuse every report/capture/export alias before hashing or writing.
    // This uses filesystem identity as well as spelling, including hard links.
    const auto nativePath=[](const QString &path) {
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(path.toStdString());
#endif
    };
    const auto aliases=[&](const QString &path) {
        std::error_code error;
        return QFileInfo(path).absoluteFilePath()==QFileInfo(source).absoluteFilePath()
            ||std::filesystem::equivalent(nativePath(path),nativePath(source),error);
    };
    bool unsafe=aliases(reportPath)||aliases(reportPath+".checkpoint.json")||aliases(reportPath+".frozen.json")||aliases(reportPath+".export.png");
    for(const auto *label:{"waveform","parade","vectorscope"})unsafe=unsafe||aliases(reportPath+"."+label+"-controls.png");
    if(unsafe){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    constexpr qint64 FullPixels=32947264;
    const QByteArray requiredSHA="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";
    struct State {
        QElapsedTimer clock,exportClock;int phase=0;bool finished=false,oracleDone=false,oracleStarted=false,exportDone=false;
        int exports=0,failures=0;bool cancelled=false,neutralRequested=false;QString initialLookMode; qint64 guiMs=0;quint64 revision=0;
        QString layer;QJsonObject checks,trace,diagnostic;QJsonArray captures,phases,plotProofs;
        SonyOracle oracle;
    };
    const auto s=std::make_shared<State>();s->clock.start();
    const auto owned=std::make_shared<QTemporaryDir>();auto *timer=new QTimer(c);timer->setInterval(25);
    auto *watcher=new QFutureWatcher<SonyOracle>(c);
    const auto output=reportPath+".export.png";
    const auto finish=[=,keep=owned](const QString &error) {
        Q_UNUSED(keep);if(s->finished)return;s->finished=true;timer->stop();
        if(!error.isEmpty())c->cancelExport();
        bool all=!s->checks.isEmpty();for(auto it=s->checks.constBegin();it!=s->checks.constEnd();++it)all=all&&it.value().isBool()&&it.value().toBool();
        const auto budget=c->brushRenderBudget()->snapshot();const bool unchanged=false; // Full source safety is independently checked after this GUI process closes.
        const bool gui=error.isEmpty()&&s->phase==6&&all&&s->guiMs>0&&s->guiMs<60000
            &&s->exportClock.isValid()&&s->exportClock.elapsed()<120000&&s->exportDone&&s->exports==1&&!s->failures&&!s->cancelled
            &&budget.limit==3LL*1024*1024*1024&&budget.peak>0&&budget.peak<=budget.limit;
        const QJsonObject result{{"passed",false},{"gui_and_export_write_passed",gui},{"external_pixel_verification_pending",true},
            {"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"gui_elapsed_ms",s->guiMs},{"gui_deadline_ms",60000},
            {"export_elapsed_ms",s->exportClock.isValid()?s->exportClock.elapsed():0},{"export_deadline_ms",120000},
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"actual_qpa",QGuiApplication::platformName()},
            {"actual_DPR",window->effectiveDevicePixelRatio()},{"actual_backend",c->processingBackend()},
            {"source_sha256",QString::fromLatin1(requiredSHA)},{"source_bytes",QFileInfo(source).size()},
            {"source_is_raw",c->currentIsRaw()},{"source_pixels",FullPixels},{"source_unchanged",unchanged},{"external_source_verification_pending",true},
            {"explicit_supported_Look_mode","off"},{"initial_import_Look_mode",s->initialLookMode},
            {"settings_isolated",QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool()},
            {"checks",s->checks},{"native_trace",s->trace},{"diagnostic",s->diagnostic},{"last_trace",c->nativeBrushTrace()},
            {"histogram_bins_per_channel",QJsonArray{c->m_scopes.red.size(),c->m_scopes.green.size(),c->m_scopes.blue.size(),c->m_scopes.luma.size()}},{"original_CPU_scope_pixels",qint64(c->scopesPixelCount())},{"original_CPU_plot_pixels",qint64(c->scopePlotPixels())},
            {"independent_original_CPU_reference_ms",s->oracle.elapsedMs},{"independent_original_CPU_reference_thread",s->oracle.thread},
            {"native_preview_sample_RGB16_max_error",s->oracle.maxRGB},{"native_preview_sample_alpha16_max_error",s->oracle.maxAlpha},
            {"native_comparison_samples",32},{"full_native_frame_CPU_comparison",false},
            {"three_plot_proofs",s->plotProofs},{"captures",s->captures},{"phases",s->phases},
            {"operation_budget_bytes",budget.limit},{"operation_peak_reserved_bytes",budget.peak},{"operation_end_charged_bytes",budget.charged},
            {"physical_tablet_verified",false},{"native_float_RAW_decoder",false},{"full_unfiltered_raw_jitter_fixed",false},
            {"nonidentity_monitor_ICC_verified",false},{"all_backend_or_package_accepted",false},{"Root_native_GPU_accepted",false}};
        const bool saved=saveSonyJson(reportPath,result);QCoreApplication::exit(gui&&saved?0:1);
    };
    const auto capture=[=](const QString &label) {
        auto *panel=sonyItem(window->contentItem(),"developRightPanel");const auto shot=window->grabWindow();if(!panel||shot.isNull())return false;
        const auto a=panel->mapToScene({0,0}),b=panel->mapToScene({panel->width(),panel->height()});
        const auto region=SmokeCaptureCoordinates::region(shot,QRectF(a,b),window->effectiveDevicePixelRatio());
        if(region.isEmpty()||region.width()>=shot.width())return false;
        const auto image=shot.copy(region);const auto path=reportPath+"."+label+"-controls.png";
        QByteArray bytes;QBuffer buffer(&bytes);if(!buffer.open(QIODevice::WriteOnly)||!image.save(&buffer,"PNG"))return false;
        QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return false;
        s->captures.append(QJsonObject{{"label",label},{"path",path},{"width",image.width()},{"height",image.height()},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}});return true;
    };
    QObject::connect(watcher,&QFutureWatcher<SonyOracle>::finished,c,[=] {
        s->oracle=watcher->result();s->oracleDone=true;
    });
    QObject::connect(c,&PhotoController::exportFinished,c,[=](int success,int failed,bool cancelled) {
        s->exportDone=true;s->exports=success;s->failures=failed;s->cancelled=cancelled;
    });
    QObject::connect(timer,&QTimer::timeout,c,[=,keep=owned] {
        Q_UNUSED(keep);
        if((s->phase<6&&s->clock.elapsed()>=60000)||(s->phase==6&&s->exportClock.elapsed()>=120000)) {
            finish("New native Sony integration exceeded an unchanged original60/120-second boundary");return;
        }
        if(s->phases.isEmpty()||s->phases.last().toObject()["phase"].toInt()!=s->phase) {
            s->phases.append(QJsonObject{{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()}});
            saveSonyJson(reportPath+".checkpoint.json",QJsonObject{{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"accepted",false}});
        }
        if(s->phase==0) {
            if(!c->brushReady()||c->loading()||c->rendering()||c->m_fullSource.size()!=QSize(7028,4688))return;
            if(!s->neutralRequested){s->initialLookMode=c->currentState().look.mode;c->setLookMode("off");s->neutralRequested=true;return;}
            if(c->currentState().look.mode!="off"){finish("Explicit supported Look-off selection failed");return;}
            if(!c->currentIsRaw()||!c->beginBrushStrokeModePressure({},.27,.38,.045,.42,.7,.85,"paint",.2)
                ||!c->appendBrushStroke(.46,.53,.8)||!c->appendBrushStroke(.68,.44,.55)) {finish("Cannot commit new controlled Sony brush intent");return;}
            s->layer=c->endBrushStroke();if(s->layer.isEmpty()){finish("New Sony committed stroke refused");return;}
            s->phase=1;return;
        }
        if(s->phase==1) {
            const auto trace=c->nativeBrushTrace();if(!trace["current_frame_native"].toBool()||c->loading()||c->rendering())return;
            s->trace=trace;s->revision=c->renderRevision();
            s->checks["actual_native_source_two_dispatches_no_CPU_upload"]=trace["producer"]=="native-brush-gpu-compute"
                &&trace["coverage_dispatches"].toInteger()>0&&trace["colour_dispatches"].toInteger()>0
                &&trace["source_uploads"].toInteger()==1&&trace["cpu_colour_uploaded"].isBool()&&!trace["cpu_colour_uploaded"].toBool()
                &&trace["cpu_mask_uploaded"].isBool()&&!trace["cpu_mask_uploaded"].toBool();
            s->checks["explicit_Look_off_supported_native_policy"]=c->currentState().look.mode=="off";
            s->checks["actual_current_target_source_revision"]=trace["current_delivery_matches"].toBool()&&trace["parameter_revision"].toInteger()==qint64(s->revision);
            const auto gui=trace["gui_thread"].toString(),rhi=trace["rhi_thread"].toString(),src=trace["source_worker_thread"].toString(),frame=trace["frame_worker_thread"].toString();
            s->checks["distinct_GUI_RHI_CPU_worker_roles"]=!gui.isEmpty()&&!rhi.isEmpty()&&!src.isEmpty()&&!frame.isEmpty()&&gui!=rhi&&src!=gui&&src!=rhi&&frame!=gui&&frame!=rhi;
            s->checks["ordinary_native_finish_zero"]=trace["normal_finish_calls"].toInteger(-1)==0;
            if(!c->requestBugReport()){finish("New actual native Sony diagnostic refused");return;}s->phase=2;return;
        }
        if(s->phase==2) {
            if(c->diagnosticBusy())return;s->diagnostic=c->nativeBrushTrace()["diagnostic"].toObject();
            if(!s->diagnostic["available"].toBool()||s->diagnostic["producer"]!="native-brush-gpu-compute") {finish("Native Sony working-output diagnostic unavailable");return;}
            c->setExactScopes(true);c->setScopeMode("waveform");s->phase=3;return;
        }
        if(s->phase==3&&!s->oracleStarted) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=FullPixels||c->scopePlotPixels()!=FullPixels
                ||c->m_fullScopeJob->busy()||c->m_scopeJob->busy()||c->m_plotJob->busy()||c->m_render->busy())return;
            s->oracleStarted=true;
            const QImage original=c->m_fullSource,prepared=c->gpuSource();const auto fullPlan=c->gpuPlan(true),visiblePlan=c->gpuPlan();
            const auto samples=s->diagnostic["diagnostic_samples_rgba16"].toArray();const auto budget=c->brushRenderBudget();
            watcher->setFuture(QtConcurrent::run([original,prepared,fullPlan,visiblePlan,samples,budget] {
                SonyOracle result;QElapsedTimer clock;clock.start();result.thread=QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()),16);
                auto preview=BrushRenderRequest::createPrepared(prepared,visiblePlan,BrushRenderRequest::Layout::Whole,{},&result.error,budget);
                auto rendered=preview?preview->render({},&result.error):BrushImageLease{};
                result.previewMatched=bool(rendered)&&samples.size()==32;
                if(rendered)for(const auto &value:samples) {
                    const auto sample=value.toObject();const auto rgba=sample["rgba16"].toArray();const int x=sample["x"].toInt(-1),y=sample["y"].toInt(-1);
                    if(x<0||y<0||x>=rendered.image().width()||y>=rendered.image().height()||rgba.size()!=4){result.previewMatched=false;break;}
                    const auto pixel=reinterpret_cast<const QRgba64*>(rendered.image().constScanLine(y))[x];
                    const int expected[4]={pixel.red(),pixel.green(),pixel.blue(),pixel.alpha()};
                    for(int ch=0;ch<4;++ch){const int delta=std::abs(expected[ch]-rgba[ch].toInt(-999999));if(ch<3)result.maxRGB=std::max(result.maxRGB,delta);else result.maxAlpha=std::max(result.maxAlpha,delta);}
                }
                rendered={};preview.reset();
                auto request=BrushRenderRequest::createOriginal(original,fullPlan,fullPlan.state.geometry,BrushRenderRequest::Layout::Stripes128,{},&result.error,budget);
                if(request)result.scopes=ScopesEngine::analyzeAdmitted(*request,{},&result.error,&result.plots);
                result.elapsedMs=clock.elapsed();return result;
            }));
            return;
        }
        if(s->phase>=3&&s->phase<=5) {
            if(!s->oracleDone||!c->scopePlotCurrent()||c->scopesPixelCount()!=FullPixels||c->scopePlotPixels()!=FullPixels)return;
            const int i=s->phase-3;const QStringList modes{"waveform","parade","vectorscope"};const auto &expected=s->oracle.plots[size_t(i)];
            if(!s->oracle.error.isEmpty()||!s->oracle.scopes.error.isEmpty()||!expected.error.isEmpty()){finish("Independent original CPU oracle failed: "+s->oracle.error);return;}
            const bool histogram=c->m_scopes.red.size()==1024&&c->m_scopes.green.size()==1024&&c->m_scopes.blue.size()==1024&&c->m_scopes.luma.size()==1024
                &&c->m_scopes.red==s->oracle.scopes.red&&c->m_scopes.green==s->oracle.scopes.green
                &&c->m_scopes.blue==s->oracle.scopes.blue&&c->m_scopes.luma==s->oracle.scopes.luma
                &&c->m_scopes.shadowClipPercent==s->oracle.scopes.shadowClipPercent&&c->m_scopes.highlightClipPercent==s->oracle.scopes.highlightClipPercent
                &&s->oracle.scopes.pixelCount==FullPixels&&c->m_scopesRevision==s->revision;
            const bool plot=c->scopeMode()==modes[i]&&c->m_scopePlot.image==expected.image&&expected.pixels==FullPixels&&c->renderRevision()==s->revision;
            s->checks["full_original_CPU_histogram_exact_at_"+modes[i]]=histogram;
            s->checks["full_original_CPU_"+modes[i]+"_image_exact"]=plot;
            s->plotProofs.append(QJsonObject{{"mode",modes[i]},{"pixels",FullPixels},{"histogram_exact",histogram},{"image_exact",plot},{"scope_rank",c->m_scopesRank},{"revision",qint64(s->revision)}});
            if(!histogram||!plot){finish("Complete original CPU histogram/plot did not match independent admitted strip oracle");return;}
            if(!capture(modes[i])){finish("Cannot save controls-only Sony plot evidence");return;}
            if(i<2){c->setScopeMode(modes[i+1]);++s->phase;return;}
            s->checks["native_preview_samples_original_RGB40_alpha_exact"]=s->oracle.previewMatched&&s->oracle.maxRGB<=40&&s->oracle.maxAlpha==0;
            s->checks["independent_CPU_reference_not_on_GUI_or_RHI"]=s->oracle.thread!=s->trace["gui_thread"].toString()&&s->oracle.thread!=s->trace["rhi_thread"].toString();
            s->checks["complete_CPU_scope_rank_survives_native_preview"]=c->m_scopesRank==1&&c->exactScopes();
            const auto frozen=c->gpuPlan(true);const auto packed=QByteArray(reinterpret_cast<const char*>(frozen.data.data()),sizeof(frozen.data));
            const QJsonObject snapshot{{"source_sha256",QString::fromLatin1(requiredSHA)},{"state",c->currentState().toJson()},{"base_exposure_stops",frozen.baseExposureStops},
                {"plan_data_sha256",QString::fromLatin1(QCryptographicHash::hash(packed,QCryptographicHash::Sha256).toHex())},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)}};
            if(!saveSonyJson(reportPath+".frozen.json",snapshot)||!c->flushEdits()){finish("Cannot freeze new native Sony exact export intent");return;}
            s->guiMs=s->clock.elapsed();s->exportClock.start();
#ifdef Q_OS_MACOS
            beginGuiSmokeExportActivity();
#endif
            if(!c->exportCurrent(QUrl::fromLocalFile(output),"srgb",100)){finish("Cannot queue new native Sony exact export");return;}
            s->phase=6;return;
        }
        if(s->phase==6&&s->exportDone)finish({});
    });
    if(!owned->isValid()||QFileInfo(source).size()!=69120000||sonyHash(source)!=requiredSHA) {finish("Pinned original Sony RAW differs");return;}
    window->setProperty("workspaceIndex",1);window->resize(1540,920);c->setGpuEnabled(true);
    if(!c->createProject(QUrl::fromLocalFile(owned->path()),"NativeSonyCommittedPreview")||!c->importFile(QUrl::fromLocalFile(source))) {finish("Cannot create isolated native Sony project");return;}
    timer->start();
}
