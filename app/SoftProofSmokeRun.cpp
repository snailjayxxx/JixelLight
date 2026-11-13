#include "app/PhotoController.h"
#include "app/SmokeRun.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/color/SoftProof.h"
#include "core/color/MonitorColorTransform.h"
#include "core/scopes/ScopesEngine.h"
#include "core/project/ProjectDatabase.h"
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
#include <QRgba64>
#include <QCryptographicHash>
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
#include <lcms2.h>
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
QByteArray warmOutput(){
    cmsCIExyY white{.38,.39,1};cmsCIExyYTRIPLE primaries{{.64,.33,1},{.30,.60,1},{.15,.06,1}};
    cmsToneCurve *curves[3]{cmsBuildGamma(nullptr,2.2),cmsBuildGamma(nullptr,2.2),cmsBuildGamma(nullptr,2.2)};
    auto profile=cmsCreateRGBProfile(&white,&primaries,curves);for(auto curve:curves)cmsFreeToneCurve(curve);
    if(!profile)return {};cmsSetDeviceClass(profile,cmsSigOutputClass);
    cmsUInt32Number size=0;cmsSaveProfileToMem(profile,nullptr,&size);QByteArray data(size,Qt::Uninitialized);
    if(!cmsSaveProfileToMem(profile,data.data(),&size))data.clear();cmsCloseProfile(profile);return data;
}
}
void startSoftProofSmokeRun(PhotoController *c,ProcessedImageProvider *provider,QQuickWindow *w,const QString &report,const QString &language){
    const QFileInfo output(report);const QDir dir(output.absolutePath());
    if(output.exists()||output.isSymLink()||!dir.exists()||!dir.entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty()){
        QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;
    }
    struct State {
        QElapsedTimer clock;QTimer *timer=nullptr;int phase=0,lastPhase=-1,frames=0,stableFrame=0,resizes=0,scopeWaits=0;
        bool done=false,initialized=false,prepared=false,projectInteractionFinished=false,admitted=false,exportStarted=false,exportPassed=false,projectPassed=false;
        ulong stamp=5000;quint64 sourceKey=0,revision=0;QString source,profile,project,oldUrl,initialScopeStatus;QJsonArray scopeWaitStates;
        QImage fixture,reference,wide,relative,paper,warning;SoftProof::Profile target;ScopesResult scopes;
        QJsonObject initial,diagnostic,exportResult,windowAdmission,composition,proofProbe;QJsonArray history,pointers,captures,proofs,phases,signature,events;
        QByteArray sourceSha;QSize windowLogical,windowPhysical;double dpr=0;
    };
    auto s=std::make_shared<State>();s->clock.start();s->source=report+".source.png";s->profile=report+".output.icc";
    s->fixture=QImage(307,211,QImage::Format_RGBA64);
    s->fixture.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<211;++y)for(int x=0;x<307;++x){
        // Three large constant patches make the actual alpha-composition check
        // independent of interpolation. The other pixels exercise wide gamut.
        auto value=QRgba64::fromRgba64(quint16(3000+50000*x/306),quint16(2000+46000*y/210),quint16(2000+(x*137+y*53)%42000),65535);
        if(y>75&&y<135){if(x>35&&x<90)value=QRgba64::fromRgba64(8000,24000,8000,0);
            else if(x>120&&x<185)value=QRgba64::fromRgba64(8000,24000,8000,32768);
            else if(x>215&&x<275)value=QRgba64::fromRgba64(8000,24000,8000,65535);}
        reinterpret_cast<QRgba64 *>(s->fixture.scanLine(y))[x]=value;
    }
    const auto icc=warmOutput();QFile profileFile(s->profile);
    if(!s->fixture.save(s->source)||icc.isEmpty()||!profileFile.open(QIODevice::WriteOnly|QIODevice::NewOnly)||profileFile.write(icc)!=icc.size()){
        QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;
    }
    profileFile.close();s->target=SoftProof::Profile::fromIcc(icc);s->sourceSha=fileHash(s->source);
    s->timer=new QTimer(c);s->timer->setInterval(20);
    QObject::connect(w,&QQuickWindow::frameSwapped,c,[s]{++s->frames;},Qt::QueuedConnection);
    const auto item=[w](const QString &name){return findVisual(w->contentItem(),name);};
    const auto intent=[c]{return c->gpuPlan(true).state.toJson();};
    const auto history=[c]{return QJsonArray::fromVariantList(c->editHistory());};
    const auto proofFrame=[=]{return provider->requestImage(c->softProofUrl().mid(QStringLiteral("image://processed/").size()),nullptr,{});};
    const auto unchanged=[=]{return c->gpuSource().cacheKey()==s->sourceKey&&c->renderRevision()==s->revision&&intent()==s->initial&&history()==s->history
        &&c->redHistogram()==s->scopes.red&&c->greenHistogram()==s->scopes.green&&c->blueHistogram()==s->scopes.blue&&c->lumaHistogram()==s->scopes.luma
        &&c->scopesPixelCount()==s->scopes.pixelCount&&c->shadowClipPercent()==s->scopes.shadowClipPercent&&c->highlightClipPercent()==s->scopes.highlightClipPercent;};
    const auto liveInvariants=[=]{return QJsonObject{{"source_key",QString::number(c->gpuSource().cacheKey())},{"revision",QString::number(c->renderRevision())},
        {"source_same",c->gpuSource().cacheKey()==s->sourceKey},{"revision_same",c->renderRevision()==s->revision},{"state_same",intent()==s->initial},{"history_same",history()==s->history},
        {"red_same",c->redHistogram()==s->scopes.red},{"green_same",c->greenHistogram()==s->scopes.green},{"blue_same",c->blueHistogram()==s->scopes.blue},{"luma_same",c->lumaHistogram()==s->scopes.luma},{"shadow_same",c->shadowClipPercent()==s->scopes.shadowClipPercent},{"highlight_same",c->highlightClipPercent()==s->scopes.highlightClipPercent},
        {"pixels",qint64(c->scopesPixelCount())},{"scope_mode",c->scopesStatus()},{"shadow",c->shadowClipPercent()},{"highlight",c->highlightClipPercent()}};};
    const auto trace=[=](const QString &event){s->events.append(QJsonObject{{"event",event},{"at_ms",s->clock.elapsed()},{"phase",s->phase},{"live",liveInvariants()},
        {"proof",QJsonObject::fromVariantMap(c->softProofDiagnostics())}});while(s->events.size()>64)s->events.removeFirst();};
    QObject::connect(c,&PhotoController::gpuFrameChanged,c,[=]{trace("frame_changed");});
    QObject::connect(c,&PhotoController::previewGeometryChanged,c,[=]{trace("geometry_changed");});
    QObject::connect(c,&PhotoController::activityChanged,c,[=]{trace("activity_changed");});
    const auto checkpoint=[=]{writeObject(report+".progress.json",{{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"proof",QJsonObject::fromVariantMap(c->softProofDiagnostics())},{"pointers",s->pointers},{"captures",s->captures}},true);};
    const auto finish=[=](const QString &error){
        if(s->done)return;s->done=true;s->timer->stop();
        QJsonArray commands;for(const auto &value:ActionTrace::instance().snapshot()){const auto row=value.toObject();if(row["action"]=="proof_command")commands.append(row["command"]);}
        const QJsonArray expectedCommands{
            QJsonObject{{"command","proof.profile"},{"source",s->profile}},QJsonObject{{"command","proof.enable"},{"enabled",true}},
            QJsonObject{{"command","proof.paper"},{"enabled",true}},QJsonObject{{"command","proof.gamut-warning"},{"enabled",true}},
            QJsonObject{{"command","proof.black-point"},{"enabled",false}},QJsonObject{{"command","proof.refresh"}},
            QJsonObject{{"command","proof.enable"},{"enabled",false}},QJsonObject{{"command","proof.enable"},{"enabled",true}}};
        const bool commandsPassed=commands==expectedCommands;
        const bool passed=error.isEmpty()&&s->phase==15&&s->exportPassed&&s->projectPassed&&s->proofs.size()==5&&s->captures.size()==4
            &&s->pointers.size()==8&&s->composition["passed"].toBool()&&fileHash(s->source)==s->sourceSha&&s->clock.elapsed()<60000
            &&commandsPassed&&qApp->property("jixellightSmokeSettingsIsolated").toBool()&&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        QJsonObject r{{"passed",passed},{"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"deadline_ms",60000},
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"actual_Main_App_PhotoCanvas",true},{"controller_on_actual_GUI_thread",c->thread()==QThread::currentThread()},
            {"language",language},{"actual_qpa",QGuiApplication::platformName()},{"actual_backend",c->processingBackend()},{"GPU_active",c->gpuActive()},
            {"settings_isolated",qApp->property("jixellightSmokeSettingsIsolated").toBool()},{"source_path",s->source},{"source_sha256",QString::fromLatin1(s->sourceSha)},
            {"source_unchanged",fileHash(s->source)==s->sourceSha},{"initial_whole_Develop",s->initial},{"final_whole_Develop",intent()},
            {"initial_history",s->history},{"final_history",history()},{"source_key",QString::number(s->sourceKey)},{"render_revision",QString::number(s->revision)},
            {"profile_path",s->profile},{"profile_sha256",QString::fromLatin1(fileHash(s->profile))},{"profile_model",s->target.colorModel()},
            {"header_text",item("softProofPanelToggle")?item("softProofPanelToggle")->property("text").toString():QString{}},
            {"presentation_only",true},{"proof_independent_CPU_reference",true},{"exact_scopes_preserved",true},{"original_GPU_parity_covered_by_separate_suite",true},
            {"proof_command_trace_passed",commandsPassed},{"proof_commands",commands},
            {"scope_wait_observations",s->scopeWaits},{"scope_wait_states",s->scopeWaitStates},
            {"scope",QJsonObject{{"status",s->initialScopeStatus},{"pixels",qint64(s->scopes.pixelCount)},{"bins",1024},{"red",QJsonArray::fromVariantList(s->scopes.red)},
                {"green",QJsonArray::fromVariantList(s->scopes.green)},{"blue",QJsonArray::fromVariantList(s->scopes.blue)},{"luma",QJsonArray::fromVariantList(s->scopes.luma)}}},
            {"current_proof",QJsonObject::fromVariantMap(c->softProofDiagnostics())},{"live_invariants",liveInvariants()},{"events",s->events},{"proof_probe",s->proofProbe},{"proofs",s->proofs},{"pointers",s->pointers},{"window_capture_admission",s->windowAdmission},{"window_captures",s->captures},{"alpha_composition",s->composition},
            {"phase_checkpoints",s->phases},{"export",s->exportResult},{"diagnostic",s->diagnostic},{"project_path",s->project},{"durable_edit_history_preserved",s->projectPassed},
            {"physical_device_verified",false},{"real_printer_profile_verified",false},{"native_float_RAW_decoder",false},{"product_development_complete",false}};
        const bool saved=writeObject(report,r);QTimer::singleShot(0,c,[=]{QCoreApplication::exit(passed&&saved?0:1);});
    };
    const auto stable=[=]{
        const QJsonArray signature{QString::number(c->gpuSource().cacheKey()),QString::number(c->renderRevision()),c->softProofUrl(),w->width(),w->height()};
        if(signature!=s->signature){s->signature=signature;s->stableFrame=s->frames;return false;}
        if(s->frames-s->stableFrame<3){w->update();return false;}return true;
    };
    const auto capture=[=](const QString &role){
        for(const auto &entry:s->captures)if(entry.toObject()["role"].toString()==role)return true;
        const QImage image=w->grabWindow();
        if(!s->admitted||QSize(w->width(),w->height())!=s->windowLogical||image.size()!=s->windowPhysical||w->devicePixelRatio()!=s->dpr||image.depth()!=32)return false;
        const auto path=report+"."+role+".window.png";if(!image.save(path))return false;
        s->captures.append(QJsonObject{{"role",role},{"path",path},{"phase",s->phase},{"physical_width",image.width()},{"physical_height",image.height()},
            {"logical_width",w->width()},{"logical_height",w->height()},{"DPR",s->dpr},{"presented_frames",s->frames-s->stableFrame},
            {"source_key",QString::number(c->gpuSource().cacheKey())},{"render_revision",QString::number(c->renderRevision())},{"at_ms",s->clock.elapsed()}});return true;
    };
    const auto mouse=[=](QPointF point,Qt::MouseButtons buttons,Qt::MouseButton changed,QEvent::Type type){
        QWindowSystemInterface::handleMouseEvent(w,s->stamp+=3,QHighDpi::toNativePixels(point,w),QHighDpi::toNativePixels(w->mapToGlobal(point),w),buttons,changed,type);
        QWindowSystemInterface::flushWindowSystemEvents();
    };
    const auto click=[=](const QString &name){
        auto *target=item(name);if(!target)return false;revealItem(target);const auto point=target->mapToScene({target->width()/2,target->height()/2});
        if(!inClips(w,target,point))return false;const auto revision=c->renderRevision();
        mouse(point,Qt::LeftButton,Qt::LeftButton,QEvent::MouseButtonPress);mouse(point,Qt::NoButton,Qt::LeftButton,QEvent::MouseButtonRelease);
        s->pointers.append(QJsonObject{{"target",name},{"x",point.x()},{"y",point.y()},{"QPA_mouse_route",true},{"source_key",QString::number(c->gpuSource().cacheKey())},
            {"render_revision",QString::number(revision)},{"post_event_render_revision",QString::number(c->renderRevision())},{"at_ms",s->clock.elapsed()}});return true;
    };
    const auto verifyProof=[=](const QString &role,const QImage &expected){
        const auto actual=proofFrame();const auto facts=QJsonObject::fromVariantMap(c->softProofDiagnostics());
        s->proofProbe={{"role",role},{"edits_source_scopes_unchanged",unchanged()},{"expected_width",expected.width()},{"actual_width",actual.width()},
            {"expected_pixels_sha256",pixelsHash(expected)},{"actual_pixels_sha256",pixelsHash(actual)},{"facts",facts}};
        if(!unchanged()||expected.isNull()||actual.size()!=expected.size()||visibleBytes(actual)!=visibleBytes(expected)
            ||facts["gpu_staging_source_key"].toString()!=QString::number(s->sourceKey)||facts["parameter_revision"].toString()!=QString::number(s->revision)){
            actual.save(report+".failed-actual-proof.png");expected.save(report+".failed-expected-proof.png");
            const auto preparedExpected=ImagePipeline::floatSource(GeometryState::fromJson(intent()["geometry"].toObject()).apply(s->fixture));
            s->proofProbe["staging_exact"]=visibleBytes(preparedExpected)==visibleBytes(c->gpuSource());
            s->proofProbe["state_exact"]=intent()==s->initial;s->proofProbe["history_exact"]=history()==s->history;
            s->proofProbe["red_histogram_exact"]=c->redHistogram()==s->scopes.red;return false;
        }
        for(const auto &entry:s->proofs)if(entry.toObject()["role"].toString()==role)return true;
        const auto path=report+"."+role+".proof.png";if(!actual.save(path))return false;
        s->proofs.append(QJsonObject{{"role",role},{"path",path},{"RGBA16_exact",true},{"expected_pixels_sha256",pixelsHash(expected)},
            {"actual_pixels_sha256",pixelsHash(actual)},{"facts",facts},{"at_ms",s->clock.elapsed()}});return true;
    };
    const auto verifyComposition=[=]{
        auto *image=item("softProofPreviewImage");if(!image||!image->isVisible()||image->property("status").toInt()!=1)return false;
        const QImage window=w->grabWindow();const auto device=[&](double x){return image->mapToScene({image->width()*x/307,image->height()*105/211})*s->dpr;};
        const auto a=device(62),b=device(153),d=device(245);const auto inside=[&](QPointF p){return p.x()>0&&p.y()>0&&p.x()<window.width()&&p.y()<window.height();};
        if(!inside(a)||!inside(b)||!inside(d))return false;
        const auto background=window.pixelColor(a.toPoint()),partial=window.pixelColor(b.toPoint()),opaque=window.pixelColor(d.toPoint());
        int maximum=0;QJsonArray wanted,observed;
        for(int k=0;k<3;++k){const auto channel=[&](QColor p){return k==0?p.red():k==1?p.green():p.blue();};
            const int expected=qRound((channel(background)+channel(opaque))*.5);wanted.append(expected);observed.append(channel(partial));maximum=std::max(maximum,std::abs(expected-channel(partial)));}
        s->composition={{"passed",maximum<=3},{"max_U8_error",maximum},{"tolerance_U8",3},{"partial_alpha",32768},{"expected",wanted},{"observed",observed},
            {"transparent_point",QJsonArray{a.x(),a.y()}},{"partial_point",QJsonArray{b.x(),b.y()}},{"opaque_point",QJsonArray{d.x(),d.y()}},{"no_double_composition",maximum<=3}};
        return maximum<=3;
    };
    QObject::connect(c,&PhotoController::diagnosticFinished,c,[=](const QString &path){
        if(s->done||s->phase!=13)return;const auto target=report+".diagnostic.zip";
        if(path.isEmpty()||!QFile::copy(path,target)){finish("Cannot retain frozen proof diagnostic");return;}
        s->diagnostic={{"path",target},{"sha256",QString::fromLatin1(fileHash(target))},{"proof",QJsonObject::fromVariantMap(c->softProofDiagnostics())}};
        QFile::remove(path);s->phase=14;
    });
    QObject::connect(c,&PhotoController::exportFinished,c,[=](int succeeded,int failed,bool cancelled){
        if(s->done||s->phase!=12||!s->exportStarted)return;
        if(succeeded!=1||failed||cancelled){finish("Proof-on original-image export failed");return;}
        const QImage image(report+".export.png");const auto actual=image.convertToFormat(QImage::Format_RGBA64);
        if(actual.size()!=s->reference.size()||visibleBytes(actual)!=visibleBytes(s->reference)||image.colorSpace()!=QColorSpace(QColorSpace::SRgb)){
            finish("Soft proof changed original export pixels or ICC");return;}
        s->exportPassed=true;s->exportResult={{"path",report+".export.png"},{"RGBA16_exact",true},{"ICC_srgb",true},{"pixels",qint64(actual.width())*actual.height()},
            {"proof_enabled_during_export",c->softProofEnabled()},{"sha256",QString::fromLatin1(fileHash(report+".export.png"))}};s->phase=13;
    });
    QObject::connect(s->timer,&QTimer::timeout,c,[=]{
        if(s->done)return;if(s->clock.elapsed()>=60000){finish("Soft proof MainApp probe exceeded60000ms");return;}
        if(s->lastPhase!=s->phase){s->lastPhase=s->phase;s->phases.append(QJsonObject{{"phase",s->phase},{"at_ms",s->clock.elapsed()}});checkpoint();}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive){QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        if(!w->isActive()){QWindowSystemInterface::handleFocusWindowChanged(w);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        if(!c->previewReady()||c->loading()||c->rendering()||c->gpuSource().isNull()||(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!c->gpuActive()))return;
        if(s->phase==0&&!s->initialized){s->initialized=true;c->setExposure(.2);c->finishInteraction();c->setExactScopes(true);c->setDisplayColorLut({},"identity-srgb");return;}
        if(!stable())return;
        if(s->phase==0){
            if(!s->prepared){
            const auto expectedStatus=language=="zh_CN"?QString::fromUtf8("全分辨率统计"):QStringLiteral("Full-resolution statistics");
            if(c->scopesStatus()!=expectedStatus){++s->scopeWaits;s->scopeWaitStates.append(QJsonObject{{"status",c->scopesStatus()},{"revision",QString::number(c->renderRevision())},{"pixels",qint64(c->scopesPixelCount())}});while(s->scopeWaitStates.size()>16)s->scopeWaitStates.removeFirst();return;}
            const auto corrected=GeometryState::fromJson(intent()["geometry"].toObject()).apply(s->fixture);
            s->reference=ImagePipeline::processWithPlan(corrected,c->gpuPlan(true));s->scopes=ScopesEngine::analyze(s->reference);
            if(c->scopesPixelCount()!=s->scopes.pixelCount||c->redHistogram().size()!=1024)return;
            s->sourceKey=c->gpuSource().cacheKey();s->revision=c->renderRevision();s->initial=intent();s->history=history();
            if(!unchanged()){finish("Original full scopes do not match reference before proof");return;}s->initialScopeStatus=c->scopesStatus();
            const auto observed=w->grabWindow();s->dpr=w->devicePixelRatio();const QSize logical(w->width(),w->height()),physical(qRound(logical.width()*s->dpr),qRound(logical.height()*s->dpr));
            if(observed.size()!=physical){const QSize fit(qRound(observed.width()/s->dpr),qRound(observed.height()/s->dpr));
                if(s->resizes||fit.width()<w->minimumWidth()||fit.height()<w->minimumHeight()||QSize(qRound(fit.width()*s->dpr),qRound(fit.height()*s->dpr))!=observed.size()){
                    finish("Actual drawable cannot hold the complete proof window");return;}++s->resizes;w->resize(fit);s->signature={};return;}
            s->admitted=true;s->windowLogical=logical;s->windowPhysical=physical;
            s->windowAdmission={{"accepted",true},{"before_any_input",true},{"resize_count",s->resizes},{"logical_width",logical.width()},{"logical_height",logical.height()},
                {"physical_width",physical.width()},{"physical_height",physical.height()},{"DPR",s->dpr},{"presented_frames",s->frames-s->stableFrame}};
            s->wide=ImagePipeline::processWithPlan(corrected,c->gpuPlan(true,ColorManagement::OutputSpace::ProPhotoRgb));s->wide.setColorSpace(SoftProof::workingColorSpace());
            SoftProof::Settings options;s->relative=SoftProof::transform(s->wide,s->target,options).image;options.simulatePaper=true;s->paper=SoftProof::transform(s->wide,s->target,options).image;
            options.gamutWarning=true;s->warning=SoftProof::transform(s->wide,s->target,options).image;
            if(s->relative.isNull()||visibleBytes(s->relative)==visibleBytes(s->paper)||visibleBytes(s->paper)==visibleBytes(s->warning)
                ||!s->reference.save(report+".reference.png")||!s->wide.save(report+".wide.png")){finish("Owned wide proof reference does not exercise paper/gamut");return;}
            if(!c->loadSoftProofProfile(QUrl::fromLocalFile(s->profile))){finish("Actual custom output ICC refused");return;}
            s->prepared=true;
            }
            if(click("softProofPanelToggle"))s->phase=1;return;
        }
        if(s->phase==1){if(click("softProofEnable"))s->phase=2;return;}
        if(s->phase==2){if(!c->softProofReady())return;
            if(!verifyProof("relative",s->relative)||!verifyComposition()||!capture("relative")){finish("Relative proof, original state or actual alpha composition differs");return;}
            if(click("softProofPaper"))s->phase=3;return;}
        if(s->phase==3){if(!c->softProofReady())return;if(!c->softProofPaper()||!verifyProof("paper",s->paper)||!capture("paper")){finish("QPA paper proof differs");return;}
            if(click("softProofGamut"))s->phase=4;return;}
        if(s->phase==4){if(!c->softProofReady())return;if(!c->softProofGamutWarning()||!verifyProof("gamut",s->warning)||!capture("gamut")){finish("QPA gamut warning differs");return;}
            if(click("softProofBlackPoint"))s->phase=5;return;}
        if(s->phase==5){if(!c->softProofReady())return;SoftProof::Settings options;options.simulatePaper=true;options.gamutWarning=true;options.blackPointCompensation=false;
            const auto expected=SoftProof::transform(s->wide,s->target,options).image;if(c->softProofBlackPoint()||!verifyProof("black-point-off",expected)){finish("QPA black point proof differs");return;}
            if(click("softProofRefresh"))s->phase=6;return;}
        if(s->phase==6){if(!c->softProofReady())return;s->oldUrl=c->softProofUrl();
            auto lut=MonitorColorTransform::identityLut();for(int y=0;y<lut.height();++y)for(int x=0;x<lut.width();++x){auto *p=reinterpret_cast<float *>(lut.scanLine(y))+4*x;p[0]=1-p[0];p[1]=1-p[1];p[2]=1-p[2];}
            // Explicitly exercise the existing presentation-only monitor change.
            c->setDisplayColorLut(lut,"owned-proof-invert");const auto before=SoftProof::transform(s->wide,s->target,{SoftProof::Intent::RelativeColorimetric,true,true,false}).image;
            const auto expected=MonitorColorTransform::applyLut(before,lut);
            if(c->softProofUrl()==s->oldUrl||!provider->requestImage(s->oldUrl.mid(18),nullptr,{}).isNull()||!verifyProof("monitor",expected)){
                finish("Monitor change did not affect only the current proof presentation");return;}
            c->setDisplayColorLut({},"identity-srgb");s->phase=7;return;}
        if(s->phase==7){s->oldUrl=c->softProofUrl();if(click("softProofEnable"))s->phase=8;return;}
        if(s->phase==8){if(c->softProofEnabled()||c->softProofReady()||!c->softProofUrl().isEmpty()||!unchanged()
                ||!provider->requestImage(s->oldUrl.mid(18),nullptr,{}).isNull()||!capture("off")){finish("Disabled proof left a stale frame or changed edits/scopes");return;}
            if(click("softProofEnable"))s->phase=9;return;}
        if(s->phase==9){if(!c->softProofReady())return;if(!unchanged()){finish("Re-enabled proof changed edit state");return;}
            if(!c->createProject(QUrl::fromLocalFile(dir.absolutePath()),output.completeBaseName()+"-project")||!c->flushEdits()){finish("Cannot save original proof-on project");return;}
            s->project=c->projectPath();ProjectDatabase read;QVector<ProjectDatabase::SavedPhoto> photos;
            if(!read.readSnapshot(s->project,&photos)||photos.size()!=1||photos[0].adjustments.toJson()!=s->initial){finish("Soft proof leaked into saved Develop");return;}
            s->phase=10;if(!c->openProject(QUrl::fromLocalFile(s->project))){finish("Cannot reopen original proof-on project");return;}return;}
        if(s->phase==10){if(!c->softProofReady())return;
            if(!s->projectInteractionFinished){s->projectInteractionFinished=true;c->finishInteraction();return;}
            if(intent()!=s->initial||history()!=s->history||fileHash(c->currentFile())!=s->sourceSha){finish("Reopened proof project/history differs");return;}
            s->sourceKey=c->gpuSource().cacheKey();s->revision=c->renderRevision();if(!unchanged())return;s->projectPassed=true;s->phase=11;return;}
        if(s->phase==11){if(!c->softProofReady()||!unchanged())return;s->phase=12;return;}
        if(s->phase==12&&!s->exportStarted){s->exportStarted=true;if(!c->exportCurrent(QUrl::fromLocalFile(report+".export.png"),"srgb",100))finish("Proof-on export refused");return;}
        if(s->phase==13&&s->diagnostic.isEmpty()){s->diagnostic={{"pending",true}};if(!c->requestBugReport())finish("Frozen proof diagnostic refused");return;}
        if(s->phase==14){if(!unchanged()){finish("Export/diagnostic mutated proof-on edits/scopes");return;}s->phase=15;finish({});}
    });
    c->setLanguage(language);w->resize(1180,720);w->setProperty("workspaceIndex",1);
    if(!c->importFile(QUrl::fromLocalFile(s->source))){finish("Cannot import owned proof fixture");return;}s->timer->start();
}
