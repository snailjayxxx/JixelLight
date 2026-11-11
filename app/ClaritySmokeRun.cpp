#include "app/PhotoController.h"
#include "app/SmokeRun.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/scopes/ScopesEngine.h"
#include "core/project/ProjectDatabase.h"
#include "diagnostics/PerformanceRecorder.h"
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
#include <QRgba64>
#include <QCryptographicHash>
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
namespace {
QQuickItem *findVisual(QQuickItem *p,const QString &name){if(!p)return nullptr;if(p->objectName()==name)return p;for(auto *c:p->childItems())if(auto *f=findVisual(c,name))return f;return nullptr;}
void revealItem(QQuickItem *p){for(auto *a=p?p->parentItem():nullptr;a;a=a->parentItem()){if(!a->inherits("QQuickFlickable"))continue;auto *content=a->property("contentItem").value<QQuickItem*>();if(!content)continue;const double y=p->mapToItem(content,QPointF{}).y(),maximum=std::max(0.,a->property("contentHeight").toDouble()-a->height());a->setProperty("contentY",std::clamp(y-80.,0.,maximum));}}
bool inClips(QQuickWindow *w,QQuickItem *p,QPointF q){if(!p||!p->isVisible()||!p->isEnabled()||p->width()<=0||p->height()<=0||q.x()<0||q.y()<0||q.x()>=w->width()||q.y()>=w->height())return false;for(auto *a=p->parentItem();a;a=a->parentItem()){if(a->clip()&&!a->contains(a->mapFromScene(q)))return false;if(a->property("moving").toBool()||a->property("flicking").toBool()||a->property("dragging").toBool())return false;}QQuickItem *hit=w->contentItem();for(int depth=0;hit&&depth<64;++depth){if(hit==p)return true;hit=hit->childAt(hit->mapFromScene(q).x(),hit->mapFromScene(q).y());}return false;}
QByteArray fileHash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
QByteArray visibleBytes(const QImage &image){QByteArray b;for(int y=0;y<image.height();++y)b.append(reinterpret_cast<const char*>(image.constScanLine(y)),image.width()*image.depth()/8);return b;}
QString pixelsHash(const QImage &image){return QString::fromLatin1(QCryptographicHash::hash(visibleBytes(image),QCryptographicHash::Sha256).toHex());}
bool writeObject(const QString &path,const QJsonObject &j,bool replace=false){if(!replace&&(QFileInfo::exists(path)||QFileInfo(path).isSymLink()))return false;QSaveFile f(path);const auto b=QJsonDocument(j).toJson(QJsonDocument::Indented);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
}
void startClaritySmokeRun(PhotoController *c,QQuickWindow *w,const QString &report,const QString &language){
    const QFileInfo output(report);const QDir dir(output.absolutePath());if(output.exists()||output.isSymLink()||!dir.exists()||!dir.entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty()){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    struct State {QElapsedTimer clock;QTimer *timer=nullptr;int phase=0,frames=0,stableFrame=0,lastPhase=-1,history=0,plot=0;bool done=false,windowReady=false,activeInteractionFinished=false,initialized=false,exportStarted=false,exportPassed=false;int windowResizes=0;double windowDpr=0;QSize initialLogical,firstPhysical,admittedLogical,admittedPhysical;ulong stamp=5000;QJsonArray signature,pointers,warmups,phases,plots,windowCaptures;QJsonObject initial,first,active,resetState,staging,scope,diagnostic,windowAdmission,exportResult;QImage fixture,colorReference;QByteArray sourceSha;QString source,project;double cacheBefore=0,cacheDelta=0;};
    auto s=std::make_shared<State>();s->clock.start();s->source=report+".source.png";s->fixture=QImage(307,211,QImage::Format_RGBA64);s->fixture.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    std::mt19937 rng(75913);std::normal_distribution<double> noise(0,750);for(int y=0;y<s->fixture.height();++y)for(int x=0;x<s->fixture.width();++x)reinterpret_cast<QRgba64*>(s->fixture.scanLine(y))[x]=QRgba64::fromRgba64(quint16(std::clamp(14000+noise(rng),0.,65535.)),quint16(std::clamp(17000+noise(rng),0.,65535.)),quint16(std::clamp(19000+noise(rng),0.,65535.)),(x+2*y)%17?quint16(47000):quint16(0));
    if(!s->fixture.save(s->source)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}s->sourceSha=fileHash(s->source);s->timer=new QTimer(c);s->timer->setInterval(20);QObject::connect(w,&QQuickWindow::frameSwapped,c,[s]{++s->frames;},Qt::QueuedConnection);
    const auto intent=[c]{return c->gpuPlan(true).state.toJson();};const auto item=[w](const QString &name){return findVisual(w->contentItem(),name);};
    const auto finish=[=](const QString &error){if(s->done)return;s->done=true;s->timer->stop();const bool passed=error.isEmpty()&&s->phase==16&&s->exportPassed&&s->cacheDelta>0&&s->windowReady&&s->windowCaptures.size()==4&&s->pointers.size()==3&&s->plots.size()==3&&fileHash(s->source)==s->sourceSha&&s->clock.elapsed()<60000&&qApp->property("jixellightSmokeSettingsIsolated").toBool()&&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());QJsonObject r{{"passed",passed},{"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"deadline_ms",60000},{"actual_Main_App_PhotoCanvas",true},{ "controller_on_actual_GUI_thread",c->thread()==QThread::currentThread()&&c->thread()==QCoreApplication::instance()->thread()},{"language",language},{"actual_qpa",QGuiApplication::platformName()},{"actual_backend",c->processingBackend()},{"GPU_active",c->gpuActive()},{"DPR",w->devicePixelRatio()},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"settings_isolated",qApp->property("jixellightSmokeSettingsIsolated").toBool()},{"source_path",s->source},{"source_sha256",QString::fromLatin1(s->sourceSha)},{"source_unchanged",fileHash(s->source)==s->sourceSha},{"clarity_header_text",item("clarityToggle")?item("clarityToggle")->property("text").toString():QString{}},{"clarity_reset_text",item("clarityReset")?item("clarityReset")->property("text").toString():QString{}},{"undo_steps_to_first",2},{"export",s->exportResult},{"reset_whole_Develop",s->resetState},{"initial_whole_Develop",s->initial},{"first_whole_Develop",s->first},{"active_whole_Develop",s->active},{"staging",s->staging},{"scope",s->scope},{"scope_plots",s->plots},{"clarity_cache_hit_delta",s->cacheDelta},{"diagnostic",s->diagnostic},{"pointers",s->pointers},{"window_capture_admission",s->windowAdmission},{"window_captures",s->windowCaptures},{"stable_presented_warmups",s->warmups},{"phase_checkpoints",s->phases},{"project_path",s->project},{"GPU_full_readback_fingerprint_only_not_new_perpixel40proof",true},{"physical_device_verified",false},{"native_float_RAW_decoder",false},{"normal_Root_or_CI_accepted",false},{"product_development_complete",false}};const bool saved=writeObject(report,r);QTimer::singleShot(0,c,[=]{QCoreApplication::exit(passed&&saved?0:1);});};
    const auto checkpoint=[=]{writeObject(report+".progress.json",{{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"pointers",s->pointers},{"warmups",s->warmups},{"initial",s->initial},{"first",s->first},{"active",s->active},{"staging",s->staging},{"scope",s->scope},{"plots",s->plots},{"project",s->project}},true);};
    const auto cacheCounter=[=]{return PerformanceRecorder::snapshot()["counters"].toObject()[c->gpuActive()?"gpu_spatial_clarity_filter_hits":"cpu_preview_clarity_hit"].toDouble();};
    const auto stable=[=]{QJsonArray a{QString::number(c->gpuSource().cacheKey()),QString::number(c->renderRevision()),w->width(),w->height(),c->previewDisplaySize().width(),c->previewDisplaySize().height()};if(a!=s->signature){s->signature=a;s->stableFrame=s->frames;return false;}if(s->frames-s->stableFrame<3){w->update();return false;}s->warmups.append(QJsonObject{{"phase",s->phase},{"presented_frames",s->frames-s->stableFrame},{"signature",a}});return true;};
    const auto admitWindow=[=]{
        const double dpr=w->devicePixelRatio();
        if(!std::isfinite(dpr)||dpr<=0){finish("Invalid actual window DPR before input");return;}
        const QSize logical(w->width(),w->height()),expected(qRound(logical.width()*dpr),qRound(logical.height()*dpr));
        const QImage observed=w->grabWindow();
        if(observed.isNull()){w->update();return;}
        if(s->firstPhysical.isEmpty()){s->initialLogical=logical;s->firstPhysical=observed.size();}
        if(observed.size()!=expected){
            const QSize fit(qRound(observed.width()/dpr),qRound(observed.height()/dpr));
            if(s->windowResizes||observed.width()>expected.width()||observed.height()>expected.height()
                ||fit.width()<w->minimumWidth()||fit.height()<w->minimumHeight()
                ||fit.width()<=0||fit.height()<=0||QSize(qRound(fit.width()*dpr),qRound(fit.height()*dpr))!=observed.size()){
                finish("Actual drawable cannot contain the complete minimum-size window before input");return;
            }
            ++s->windowResizes;w->resize(fit);s->signature={};s->stableFrame=s->frames;w->update();return;
        }
        if(observed.depth()!=32||c->gpuSource().isNull()||!c->gpuSource().cacheKey()){
            finish("Actual complete window capture or current source unavailable before input");return;
        }
        s->windowReady=true;s->windowDpr=dpr;s->admittedLogical=logical;s->admittedPhysical=observed.size();
        s->windowAdmission={{"accepted",true},{"phase",0},{"input_started",false},{"resize_count",s->windowResizes},
            {"initial_logical_size",QJsonArray{s->initialLogical.width(),s->initialLogical.height()}},
            {"first_observed_physical_size",QJsonArray{s->firstPhysical.width(),s->firstPhysical.height()}},
            {"logical_width",logical.width()},{"logical_height",logical.height()},
            {"physical_width",observed.width()},{"physical_height",observed.height()},{"DPR",dpr},
            {"presented_frames",s->frames-s->stableFrame},{"at_ms",s->clock.elapsed()},
            {"source_key",QString::number(c->gpuSource().cacheKey())},{"render_revision",QString::number(c->renderRevision())},
            {"GUI_thread_id",QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()))}};
    };
    const auto captureWindow=[=](const QString &path,const QString &role){
        const auto source=c->gpuSource().cacheKey();const auto revision=c->renderRevision();
        const QImage image=w->grabWindow();
        if(!s->windowReady||!source||c->gpuSource().isNull()||source!=c->gpuSource().cacheKey()||revision!=c->renderRevision()
            ||w->devicePixelRatio()!=s->windowDpr||QSize(w->width(),w->height())!=s->admittedLogical
            ||image.size()!=s->admittedPhysical||image.depth()!=32||!image.save(path))return false;
        s->windowCaptures.append(QJsonObject{{"role",role},{"phase",s->phase},{"path",path},
            {"logical_width",w->width()},{"logical_height",w->height()},{"physical_width",image.width()},{"physical_height",image.height()},
            {"DPR",w->devicePixelRatio()},{"image_bits_per_pixel",image.depth()},{"presented_frames",s->frames-s->stableFrame},
            {"source_key",QString::number(source)},{"render_revision",QString::number(revision)},
            {"GUI_thread_id",QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()))},{"at_ms",s->clock.elapsed()}});
        return true;
    };
    const auto mouse=[=](QPointF p,Qt::MouseButtons buttons,Qt::MouseButton changed,QEvent::Type type){const auto global=w->mapToGlobal(p);QWindowSystemInterface::handleMouseEvent(w,s->stamp+=3,QHighDpi::toNativePixels(p,w),QHighDpi::toNativePixels(global,w),buttons,changed,type);QWindowSystemInterface::flushWindowSystemEvents();};
    const auto pointer=[=](QQuickItem *target,double ratio,const QString &key){if(!target)return false;revealItem(target);const auto start=target->mapToScene({target->width()/2,target->height()/2}),end=target->mapToScene({target->width()*ratio,target->height()/2});if(!inClips(w,target,start)||!inClips(w,target,end))return false;const auto sourceBefore=c->gpuSource().cacheKey();const auto revisionBefore=c->renderRevision();if(!sourceBefore||c->gpuSource().isNull())return false;mouse(start,Qt::LeftButton,Qt::LeftButton,QEvent::MouseButtonPress);if(ratio!=.5)mouse(end,Qt::LeftButton,Qt::NoButton,QEvent::MouseMove);mouse(end,Qt::NoButton,Qt::LeftButton,QEvent::MouseButtonRelease);s->pointers.append(QJsonObject{{"target",key},{"local_x",end.x()},{"local_y",end.y()},{"global_x",w->mapToGlobal(end).x()},{"global_y",w->mapToGlobal(end).y()},{"QPA_mouse_route",true},{"source_key",QString::number(sourceBefore)},{"render_revision",QString::number(revisionBefore)},{"post_event_source_key",QString::number(c->gpuSource().cacheKey())},{"post_event_render_revision",QString::number(c->renderRevision())},{"file",c->currentFile()},{"at_ms",s->clock.elapsed()},{"GUI_thread_id",QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()))}});return true;};
    const auto sameStaging=[=]{const auto g=GeometryState::fromJson(intent()["geometry"].toObject());const auto corrected=g.apply(s->fixture);const auto expected=ImagePipeline::floatSource(corrected);const auto actual=c->gpuSource();if(corrected.isNull()||actual.size()!=expected.size()||actual.format()!=QImage::Format_RGBA32FPx4||visibleBytes(actual)!=visibleBytes(expected))return false;s->staging={{"corrected_width",corrected.width()},{"corrected_height",corrected.height()},{"complete_pixels",qint64(corrected.width())*corrected.height()},{"actual_FP32_staging_sha256",pixelsHash(actual)},{"expected_FP32_staging_sha256",pixelsHash(expected)},{"ICC_sha256",QString::fromLatin1(QCryptographicHash::hash(actual.colorSpace().iccProfile(),QCryptographicHash::Sha256).toHex())},{"bit_exact",true}};return true;};
    QObject::connect(c,&PhotoController::diagnosticFinished,c,[=](const QString &path){if(s->done||s->phase!=13)return;const auto target=report+".diagnostic.zip";if(path.isEmpty()||!QFile::copy(path,target)){finish("Cannot retain actual frozen diagnostic bundle");return;}s->diagnostic={{"path",target},{"sha256",QString::fromLatin1(fileHash(target))},{"request_revision",QString::number(c->renderRevision())},{"source_key",QString::number(c->gpuSource().cacheKey())}};QFile::remove(path);s->phase=14;checkpoint();});
    QObject::connect(c,&PhotoController::exportFinished,c,[=](int succeeded,int failed,bool cancelled){if(s->done||s->phase!=14||!s->exportStarted)return;if(succeeded!=1||failed||cancelled){finish("Actual clarity export failed");return;}s->phase=15;checkpoint();});
    QObject::connect(s->timer,&QTimer::timeout,c,[=]{if(s->done)return;if(s->clock.elapsed()>=60000){finish("Clarity MainApp probe exceeded60000ms");return;}if(s->lastPhase!=s->phase){s->lastPhase=s->phase;s->phases.append(QJsonObject{{"phase",s->phase},{"at_ms",s->clock.elapsed()}});checkpoint();}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive){QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}if(!w->isActive()){QWindowSystemInterface::handleFocusWindowChanged(w);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        if(!c->previewReady()||c->loading()||c->rendering()||c->gpuSource().isNull()||(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!c->gpuActive()))return;
        if(s->phase==0&&!s->initialized){s->initialized=true;c->setExposure(.2);c->setGrainAmount(17);c->setDenoiseLuma(25);c->setDenoiseChroma(35);c->finishInteraction();s->signature={};s->stableFrame=s->frames;return;}
        if(!stable())return;
        if(s->phase==0&&!s->windowReady){admitWindow();return;}
        if(s->phase==0){s->initial=intent();s->history=c->editHistory().size();s->cacheBefore=cacheCounter();if(!sameStaging()){finish("Initial source staging differs");return;}auto *toggle=item("clarityToggle");if(!toggle)return;QQuickItem *panel=toggle;while(panel&&panel->metaObject()->indexOfProperty("clarityExpanded")<0)panel=panel->parentItem();if(!panel){finish("Missing actual clarity panel");return;}if(!panel->property("clarityExpanded").toBool()){revealItem(toggle);const auto p=toggle->mapToScene({toggle->width()/2,toggle->height()/2});if(!inClips(w,toggle,p))return;mouse(p,Qt::LeftButton,Qt::LeftButton,QEvent::MouseButtonPress);mouse(p,Qt::NoButton,Qt::LeftButton,QEvent::MouseButtonRelease);if(!panel->property("clarityExpanded").toBool())return;}s->phase=1;return;}
        if(s->phase==1){if(pointer(item("clarityAmountSlider"),.76,"positiveAmount"))s->phase=2;return;}
        if(s->phase==2){if(intent()["clarity"].toObject()["amount"].toDouble()<25){finish("First QPA slider did not apply positive clarity");return;}c->finishInteraction();s->first=intent();if(!sameStaging()){finish("First slider changed source staging");return;}s->phase=3;return;}
        if(s->phase==3){if(pointer(item("clarityAmountSlider"),.25,"negativeAmount"))s->phase=4;return;}
        if(s->phase==4){if(!s->activeInteractionFinished){c->finishInteraction();s->cacheBefore=cacheCounter();c->setExposure(.3);c->finishInteraction();s->activeInteractionFinished=true;s->signature={};s->stableFrame=s->frames;w->update();return;}s->active=intent();s->resetState=s->active;s->resetState.remove("clarity");s->cacheDelta=cacheCounter()-s->cacheBefore;if(s->cacheDelta<=0){finish("Downstream exposure edit did not reuse actual clarity stage");return;}if(s->active["clarity"].toObject()["amount"].toDouble()>-25||c->editHistory().size()!=s->history+3||!sameStaging()){finish("Negative slider/history or complete staging differs");return;}const auto g=GeometryState::fromJson(s->active["geometry"].toObject());const auto corrected=g.apply(s->fixture);s->colorReference=ImagePipeline::processWithPlan(corrected,c->gpuPlan(true));if(s->colorReference.isNull()||!corrected.save(report+".geometry.png")||!s->colorReference.save(report+".color-reference.png")||!captureWindow(report+".active.png","active")){finish("Cannot retain current source/pixel/window evidence");return;}c->setExactScopes(true);s->phase=5;return;}
        if(s->phase==5){const auto reference=ScopesEngine::analyze(s->colorReference);if(c->scopesStatus()!=(language=="zh_CN"?QString::fromUtf8("全分辨率统计"):QStringLiteral("Full-resolution statistics"))||c->scopesPixelCount()!=reference.pixelCount)return;if(c->redHistogram()!=reference.red||c->greenHistogram()!=reference.green||c->blueHistogram()!=reference.blue||c->lumaHistogram()!=reference.luma||c->shadowClipPercent()!=reference.shadowClipPercent||c->highlightClipPercent()!=reference.highlightClipPercent){finish("Current full scopes differ from complete CPU reference");return;}s->scope={{"pixels",qint64(reference.pixelCount)},{"bins",1024},{"four_histograms_exact",true},{"clipping_exact",true},{"red",QJsonArray::fromVariantList(reference.red)},{"green",QJsonArray::fromVariantList(reference.green)},{"blue",QJsonArray::fromVariantList(reference.blue)},{"luma",QJsonArray::fromVariantList(reference.luma)}};s->plot=0;c->setScopeMode("waveform");s->phase=6;return;}
        if(s->phase==6){if(!c->scopePlotCurrent()||c->scopePlotPixels()!=quint64(s->colorReference.width())*s->colorReference.height())return;const QStringList modes{"waveform","parade","vectorscope"};s->plots.append(QJsonObject{{"mode",modes[s->plot]},{"pixels",QString::number(c->scopePlotPixels())},{"current",true},{"full_resolution",true}});if(++s->plot<3){c->setScopeMode(modes[s->plot]);return;}c->undo();c->undo();s->phase=7;return;}
        if(s->phase==7){if(intent()!=s->first||!sameStaging()||!captureWindow(report+".undo.png","undo")){finish("Whole Develop Undo/source preview differs");return;}c->redo();c->redo();s->phase=8;return;}
        if(s->phase==8){if(intent()!=s->active||!sameStaging()||!captureWindow(report+".redo.png","redo")){finish("Whole Develop Redo/source preview differs");return;}if(pointer(item("clarityReset"),.5,"clarityReset"))s->phase=9;return;}
        if(s->phase==9){if(intent()!=s->resetState||!sameStaging()||!captureWindow(report+".reset.png","reset")){finish("Clarity reset changed unrelated whole Develop settings");return;}c->undo();s->phase=10;return;}
        if(s->phase==10){if(intent()!=s->active||!sameStaging()){finish("Undo reset lost active whole Develop");return;}const double cacheAfter=cacheCounter();s->cacheDelta=cacheAfter-s->cacheBefore;s->staging["clarity_cache_hit_delta"]=s->cacheDelta;if(cacheAfter<=s->cacheBefore){finish("No actual prepared cache reuse observed");return;}if(!c->createProject(QUrl::fromLocalFile(dir.absolutePath()),output.completeBaseName()+"-project")||!c->flushEdits()){finish("Cannot durably save active clarity/history");return;}s->project=c->projectPath();ProjectDatabase read;QVector<ProjectDatabase::SavedPhoto> photos;if(!read.readSnapshot(s->project,&photos)||photos.size()!=1||photos[0].adjustments.toJson()!=s->active){finish("Durable whole Develop differs from actual saved intent");return;}s->phase=11;if(!c->openProject(QUrl::fromLocalFile(s->project))){finish("Cannot reopen actual clarity project");return;}return;}
        if(s->phase==11){if(intent()!=s->active||!c->canRedo()||!sameStaging()||fileHash(c->currentFile())!=s->sourceSha){finish("Reopened whole Develop/history/source differs");return;}c->redo();s->phase=12;return;}
        if(s->phase==12){if(intent()!=s->resetState||!sameStaging()){finish("Reopened durable redo differs");return;}c->undo();s->phase=13;return;}
        if(s->phase==13&&s->diagnostic.isEmpty()){if(intent()!=s->active||!sameStaging()){finish("Reopened durable undo differs");return;}s->diagnostic={{"pending",true}};if(!c->requestBugReport()){finish("Frozen current GPU/CPU diagnostic request refused");return;}return;}
        if(s->phase==14&&!s->exportStarted){s->exportStarted=true;if(!c->exportCurrent(QUrl::fromLocalFile(report+".export.png"),"srgb",100)){finish("Actual clarity export request refused");return;}return;}
        if(s->phase==15){const QImage image(report+".export.png");const auto actual=image.convertToFormat(QImage::Format_RGBA64);if(image.isNull()||actual.size()!=s->colorReference.size()||visibleBytes(actual)!=visibleBytes(s->colorReference)||image.colorSpace()!=QColorSpace(QColorSpace::SRgb)){finish("Actual PNG16 clarity export differs from complete reference");return;}s->exportPassed=true;s->exportResult={{"path",report+".export.png"},{"sha256",QString::fromLatin1(fileHash(report+".export.png"))},{"RGBA16_exact",true},{"ICC_srgb",true},{"pixels",qint64(actual.width())*actual.height()}};s->phase=16;checkpoint();finish({});return;}
    });
    c->setLanguage(language);w->resize(language=="en_US"?1440:1180,language=="en_US"?940:780);w->setProperty("workspaceIndex",1);if(!c->importFile(QUrl::fromLocalFile(s->source))){finish("Cannot import owned clarity MainApp fixture");return;}s->timer->start();
}
