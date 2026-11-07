#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/FloatFrameFingerprint.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickRhiItem>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>

namespace {
QQuickRhiItem *deliveryRhiItem(QQuickItem *item) {
    if(!item)return nullptr;
    if(auto *rhi=qobject_cast<QQuickRhiItem *>(item))return rhi;
    for(auto *child:item->childItems())if(auto *rhi=deliveryRhiItem(child))return rhi;
    return nullptr;
}
// Test-owned event filter. It interleaves a real GUI edit/selection before an
// actual queued producer callback, without invoking/faking the callback, GPU
// completion or delivery counters, and without waiting on either owner.
struct FocusedRhiTrigger final {
    std::atomic_bool armed{false},fired{false};
    int mode=0;
    quint64 revision=0;
    std::shared_ptr<NativeBrushTelemetry> facts;
    std::shared_ptr<std::atomic_bool> context;
};
class DeliveryInterleave final:public QObject {
public:
    using QObject::QObject;
    std::shared_ptr<NativeBrushTelemetry> facts;
    quint64 revision=0;
    std::function<void()> action;
    bool armed=false,fired=false;
    bool eventFilter(QObject *,QEvent *event) override {
        if(armed&&event->type()==QEvent::MetaCall&&facts&&facts->lastEnqueueRevision.load(std::memory_order_acquire)==revision){
            armed=false;fired=true;auto run=std::move(action);if(run)run();
        }
        return false;
    }
};
}

void startNativeBrushDeliverySmokeRun(PhotoController *c,QQuickWindow *window,const QString &reportPath,const QString &focus){
    struct State {
        QElapsedTimer clock;QStringList selected;int next=0,phase=0;bool finished=false;
        QString name,layerA,layerB,hardLayer;QJsonArray cases,captures,observations;QJsonObject before;
        std::shared_ptr<NativeBrushTelemetry> oldFacts;
        std::shared_ptr<std::atomic_bool> oldContext;
        quint64 accepted=0,rejected=0,expectedRevision=0; qint64 hiddenAt=0;
        QImage frozenSource;ProcessingPlan frozenPlan;QMap<QString,QByteArray> sourceHashes;QJsonObject atSignal;
    };
    const auto s=std::make_shared<State>();s->clock.start();s->selected=focus=="all"?QStringList{"cache","revision","photo","project","hard","detail","lut"}:QStringList{focus};
    const auto trigger=std::make_shared<FocusedRhiTrigger>();
    const auto fixture=std::make_shared<QTemporaryDir>();auto *timer=new QTimer(c);timer->setInterval(10);
    auto *interleave=new DeliveryInterleave(c);c->installEventFilter(interleave);
    const auto folder=QFileInfo(reportPath).absolutePath();QDir().mkpath(folder);
    const auto traceCurrent=[=](bool native){const auto t=c->nativeBrushTrace();return t.value("current_delivery_matches").toBool()&&(!native||t.value("current_frame_native").toBool())&&!c->loading()&&!c->rendering()&&c->gpuActive();};
    const auto finish=[=,keep=fixture](const QString &error){
        Q_UNUSED(keep);
        if(s->finished)return;s->finished=true;timer->stop();interleave->armed=false;bool unchanged=true;
        for(auto it=s->sourceHashes.cbegin();it!=s->sourceHashes.cend();++it){QFile f(it.key());unchanged=unchanged&&f.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256)==it.value();}
        const bool passed=error.isEmpty()&&s->next==s->selected.size()&&unchanged&&s->clock.elapsed()<60000;
        QJsonObject report{{"passed",passed},{"selected_cases",QJsonArray::fromStringList(s->selected)},{"completed_cases",s->cases},{"elapsed_ms",s->clock.elapsed()},
            {"phase",s->phase},{"active_case",s->name},{"error",error},{"actual_qpa",QGuiApplication::platformName()},{"actual_DPR",window->devicePixelRatio()},
            {"actual_backend",c->processingBackend()},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"two_owned_sources_unchanged",unchanged},{"owned_source_count",s->sourceHashes.size()},
            {"observations",s->observations},{"captures",s->captures},{"last_trace",c->nativeBrushTrace()},
            {"last_obsolete_delivery_facts",s->oldFacts?s->oldFacts->snapshot():QJsonObject{}},{"last_event_filter_fired",interleave->fired},
            {"synthesized_producer_callback",false},{"product_or_QA_owner_wait",false},{"real_RAW",false},{"physical_device_loss_or_OOM",false},{"nonidentity_ICC",false},{"all_F0_F6_complete",false}};
        QSaveFile out(reportPath);const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);const bool saved=out.open(QIODevice::WriteOnly)&&out.write(bytes)==bytes.size()&&out.commit();QCoreApplication::exit(passed&&saved?0:1);
    };
    const auto capture=[=](const QString &name){const auto image=window->grabWindow();const auto path=folder+"/"+name+".png";if(image.isNull()||!image.save(path))return false;s->captures.append(QJsonObject{{"path",path},{"width",image.width()},{"height",image.height()}});return true;};
    const auto complete=[=](QJsonObject facts){facts.insert("name",s->name);facts.insert("passed",true);s->cases.append(facts);++s->next;s->name.clear();s->phase=2;};
    const auto layer=[=]{return c->currentIndex()==0?s->layerA:s->layerB;};
    const auto stroke=[=](double hard){if(!c->beginBrushStrokeModePressure({},.22,.3,.07,hard,.7,.8,"paint",.8)||!c->appendBrushStroke(.51,.61,.65)||!c->appendBrushStroke(.76,.38,.9))return QString{};return c->endBrushStroke();};
    const auto arm=[=](std::function<void()> action){
        const auto view=c->nativeBrushView();s->before=c->nativeBrushTrace();s->oldFacts=view.facts;s->oldContext=view.context;
        if(!s->oldFacts||!s->oldContext){finish("No actual native delivery context to interleave");return false;}
        s->accepted=s->oldFacts->guiAccepted.load(std::memory_order_acquire);s->rejected=s->oldFacts->guiRejected.load(std::memory_order_acquire);
        if(!c->setBrushLayerExposure(layer(),.91)){finish("Cannot prepare actual queued native edit");return false;}
        s->expectedRevision=c->renderRevision();interleave->facts=s->oldFacts;interleave->revision=s->expectedRevision;interleave->fired=false;interleave->action=std::move(action);interleave->armed=true;return true;
    };
    const auto requestDiagnostic=[=]{s->frozenSource=c->gpuSource();s->frozenPlan=c->gpuPlan();s->expectedRevision=c->renderRevision();return c->requestBugReport()!=0;};
    // Use the public after-sync boundary, independent of node connection order.
    // The GUI requests a real item update before arming this atomic-only fault;
    // Qt6.8.3 updatePaintNode has synchronized it and scheduled its next render.
    QObject::connect(window,&QQuickWindow::afterSynchronizing,c,[=]{
        if(!trigger->armed.load(std::memory_order_acquire)||trigger->mode!=2)return;
        if(trigger->fired.exchange(true,std::memory_order_acq_rel))return;
        trigger->armed.store(false,std::memory_order_release);
        const bool active=trigger->context&&trigger->context->load(std::memory_order_acquire);
        if(trigger->context)trigger->context->store(false,std::memory_order_release);
        const QJsonObject facts{{"window_signal","afterSynchronizing"},{"callback_thread",QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()),16)},
            {"controlled_atomic_context_retirement",true},{"context_was_active",active},{"product_facts_at_signal",trigger->facts->snapshot()}};
        QMetaObject::invokeMethod(c,[=]{if(s->finished)return;s->atSignal=facts;c->nativeBrushSuspend(true);c->nativeBrushSuspend(false);s->phase=17;},Qt::QueuedConnection);
    },Qt::DirectConnection);
    QObject::connect(window,&QQuickWindow::afterFrameEnd,c,[=]{
        if(!trigger->armed.load(std::memory_order_acquire)||trigger->mode!=1)return;
        if(trigger->facts->lastSubmittedRevision.load(std::memory_order_acquire)!=trigger->revision
            ||trigger->facts->lastEnqueueRevision.load(std::memory_order_acquire)==trigger->revision)return;
        if(trigger->fired.exchange(true,std::memory_order_acq_rel))return;
        trigger->armed.store(false,std::memory_order_release);
        const QJsonObject facts{{"window_signal","afterFrameEnd"},{"callback_thread",QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()),16)},
            {"recorded_revision",qint64(trigger->revision)},{"compute_recorded_before_publication",true},{"product_facts_at_signal",trigger->facts->snapshot()}};
        QMetaObject::invokeMethod(c,[=]{if(s->finished)return;s->atSignal=facts;c->selectPhoto(1);s->phase=15;},Qt::QueuedConnection);
    },Qt::DirectConnection);
    QObject::connect(timer,&QTimer::timeout,c,[=]{
        if(s->clock.elapsed()>=60000){finish("New delivery/fallback QA exceeded the unchanged 60-second GUI budget");return;}
        if(s->phase==0){if(!c->brushReady()||c->loading()||c->rendering())return;s->layerA=stroke(.45);if(s->layerA.isEmpty()){finish("Owned initial soft layer refused");return;}s->phase=1;return;}
        if(s->phase==1){if(!traceCurrent(true))return;s->phase=2;return;}
        if(s->phase==2){
            if(s->next==s->selected.size()){finish({});return;}
            if(!c->brushReady()||c->loading()||c->rendering())return;
            if(!s->hardLayer.isEmpty()){if(!c->removeBrushLayer(s->hardLayer)){finish("Cannot remove owned hard fixture layer");return;}s->hardLayer.clear();return;}
            if(c->lookState().value("mode").toString()!="off"){c->setLookMode("off");return;}
            if(!traceCurrent(true))return;
            s->name=s->selected[s->next];s->before=c->nativeBrushTrace();
            if(s->name=="pending"||s->name=="retired-render"){
                const auto view=c->nativeBrushView();s->oldFacts=view.facts;s->oldContext=view.context;
                if(!s->oldFacts||!s->oldContext){finish("No actual context for focused retirement observation");return;}
                s->accepted=s->oldFacts->guiAccepted.load(std::memory_order_acquire);s->rejected=s->oldFacts->guiRejected.load(std::memory_order_acquire);
                if(!c->setBrushLayerExposure(layer(),.93)){finish("Focused retirement edit refused");return;}
                trigger->facts=s->oldFacts;trigger->context=s->oldContext;trigger->revision=c->renderRevision();
                trigger->mode=s->name=="pending"?1:2;trigger->fired.store(false,std::memory_order_relaxed);s->phase=14;trigger->armed.store(true,std::memory_order_release);
                if(trigger->mode==2){auto *item=deliveryRhiItem(window->contentItem());if(!item){finish("No actual RHI item for the controlled after-sync fault");return;}item->update();window->update();}
                return;
            }
            if(s->name=="cache"){
                s->oldFacts=c->nativeBrushView().facts;s->hiddenAt=s->clock.elapsed();window->hide();s->phase=3;return;
            }
            if(s->name=="revision"){
                s->phase=5;if(!arm([=]{c->setBrushLayerExposure(layer(),.31);s->expectedRevision=c->renderRevision();}))return;return;
            }
            if(s->name=="photo"){
                s->phase=7;if(!arm([=]{c->selectPhoto(1);s->phase=8;}))return;return;
            }
            if(s->name=="project"){
                s->phase=10;if(!arm([=]{if(!c->createProject(QUrl::fromLocalFile(fixture->path()),"Second owned delivery project")){finish("Owned project switch refused");return;}s->phase=11;}))return;return;
            }
            if(s->name=="hard"){
                s->hardLayer=stroke(1);if(s->hardLayer.isEmpty()){finish("Hard policy fixture refused");return;}
            }else if(s->name=="detail"){
                c->setLookMode("manual");c->setLookCode("FL");c->setLookParameter("clarity",3);
                const auto detail=c->gpuPlan().data[ProcessingPlan::LookDetail];if(detail.x==0&&detail.y==0&&detail.z==0&&detail.w==0){finish("Look detail policy fixture was not active");return;}
            }else if(s->name=="lut"){
                if(!c->loadLookProfile(QUrl::fromLocalFile(fixture->filePath("owned.cube")))||!c->gpuPlan().state.look.lut){finish("Owned calibrated LUT fixture not active");return;}
            }else{finish("Unknown delivery case");return;}
            s->phase=12;return;
        }
        if(s->phase==3){if(s->clock.elapsed()-s->hiddenAt<200)return;window->show();s->phase=4;return;}
        if(s->phase==4){
            if(!traceCurrent(true))return;const auto t=c->nativeBrushTrace();if(t.value("rhi_generation").toInteger()<=s->before.value("rhi_generation").toInteger())return;
            const bool serial=t.value("source_capture_serial").toInteger()>0&&t.value("source_capture_serial")==s->before.value("source_capture_serial");
            const bool hit=t.value("controller_source_captures")==s->before.value("controller_source_captures")&&t.value("controller_source_cache_hits").toInteger()>s->before.value("controller_source_cache_hits").toInteger();
            const bool released=s->oldFacts&&s->oldFacts->retiredOwnersReleased.load(std::memory_order_acquire)>0;
            if(!serial||!hit||!released){finish("Direct canonical serial/cache-hit/retired-owner facts did not prove warm source reuse");return;}
            if(!capture("native-direct-cache")){finish("Cache screenshot failed");return;}complete({{"before",s->before},{"after",t},{"direct_capture_serial_retained",serial},{"no_new_canonical_capture_and_cache_hit",hit},{"old_GPU_owner_released",released}});return;
        }
        if(s->phase==5){
            if(!interleave->fired||!traceCurrent(true)||c->renderRevision()!=s->expectedRevision)return;
            const bool rejected=s->oldFacts->guiRejected.load(std::memory_order_acquire)>s->rejected;
            const auto t=c->nativeBrushTrace();if(!rejected||t.value("source_uploads")!=s->before.value("source_uploads")||t.value("source_capture_serial")!=s->before.value("source_capture_serial")){finish("Actual obsolete revision was not rejected while preserving native input");return;}
            s->observations.append(t);s->phase=6;if(!requestDiagnostic()){finish("Latest native diagnostic request refused");return;}return;
        }
        if(s->phase==6){
            if(c->diagnosticBusy())return;const auto diag=c->nativeBrushTrace().value("diagnostic").toObject();const auto samples=diag.value("diagnostic_samples_rgba16").toArray();QString error;
            auto request=BrushRenderRequest::createPrepared(s->frozenSource,s->frozenPlan,BrushRenderRequest::Layout::Whole,{},&error,c->brushRenderBudget());const auto ref=request?request->render({},&error):BrushImageLease{};int rgb=0,alpha=0;
            bool ok=ref&&diag.value("available").toBool()&&diag.value("parameter_revision").toInteger()==qint64(s->expectedRevision)&&samples.size()==32;
            if(ok)for(auto v:samples){const auto a=v.toObject();const int x=a.value("x").toInt(-1),y=a.value("y").toInt(-1);const auto q=a.value("rgba16").toArray();if(x<0||y<0||x>=ref.image().width()||y>=ref.image().height()||q.size()!=4){ok=false;break;}
                const auto pix=reinterpret_cast<const QRgba64*>(ref.image().constScanLine(y))[x];const int expected[4]={pix.red(),pix.green(),pix.blue(),pix.alpha()};for(int ch=0;ch<4;++ch){const int d=std::abs(expected[ch]-q[ch].toInt(-999999));if(ch<3)rgb=std::max(rgb,d);else alpha=std::max(alpha,d);}}
            s->frozenSource={};if(!ok||rgb>40||alpha){finish("Latest post-rejection native samples do not match original CPU under RGB40/alpha-exact");return;}
            if(!capture("native-latest-revision")){finish("Revision capture failed");return;}complete({{"actual_Qt_MetaCall_interleave",true},{"obsolete_revision",qint64(interleave->revision)},{"latest_revision",qint64(s->expectedRevision)},{"obsolete_facts",s->oldFacts->snapshot()},{"GUI_rejections_before",qint64(s->rejected)},{"CPU_samples",32},{"max_RGB16_error",rgb},{"max_alpha16_error",alpha},{"actual_diagnostic",diag}});return;
        }
        if(s->phase==7||s->phase==10)return;
        if(s->phase==8){if(c->loading()||c->rendering()||!c->brushReady())return;s->layerB=stroke(.4);if(s->layerB.isEmpty()){finish("Second actual photo soft layer refused");return;}s->phase=9;return;}
        if(s->phase==9||s->phase==11){
            if(!traceCurrent(true))return;const auto t=c->nativeBrushTrace();const bool photo=s->phase==9;
            const bool obsolete=s->oldFacts->guiRejected.load(std::memory_order_acquire)>s->rejected&&s->oldFacts->guiAccepted.load(std::memory_order_acquire)==s->accepted&&s->oldContext&&!s->oldContext->load(std::memory_order_acquire);
            const bool released=s->oldFacts->retiredOwnersReleased.load(std::memory_order_acquire)>0;
            const bool target=photo?t.value("photo")!=s->before.value("photo")&&t.value("input_source_key")!=s->before.value("input_source_key"):
                t.value("catalog")!=s->before.value("catalog")&&t.value("photo")==s->before.value("photo")&&t.value("version")==s->before.value("version")&&t.value("input_source_key")==s->before.value("input_source_key")&&t.value("source_capture_serial")==s->before.value("source_capture_serial");
            if(!obsolete||!released||!target){finish("Actual queued photo/project callback or owner/source delivery guard was not proved");return;}
            if(!capture(photo?"native-new-photo":"native-new-project")){finish("Target capture failed");return;}complete({{"actual_Qt_MetaCall_interleave",true},{"obsolete_facts",s->oldFacts->snapshot()},{"GUI_accepted_before",qint64(s->accepted)},{"GUI_rejected_before",qint64(s->rejected)},{"obsolete_context_inactive",true},{"old_GPU_owner_released",released},{"before",s->before},{"after",t}});return;
        }
        if(s->phase==12){
            if(!traceCurrent(false))return;const auto t=c->nativeBrushTrace();if(t.value("producer").toString()!="cpu-reference-upload"||!t.value("cpu_reference_uploaded").toBool()||t.value("current_frame_native").toBool()){finish("Unsupported brush policy did not use the complete original CPU producer");return;}
            s->observations.append(t);s->phase=13;if(!requestDiagnostic()){finish("Complete CPU fallback diagnostic refused");return;}return;
        }
        if(s->phase==14)return;
        if(s->phase==15){if(c->loading()||c->rendering()||!c->brushReady())return;s->layerB=stroke(.4);if(s->layerB.isEmpty()){finish("Pending-frame target B setup refused");return;}s->phase=16;return;}
        if(s->phase==16||s->phase==17){
            if(!traceCurrent(true))return;const auto after=c->nativeBrushTrace();const auto old=s->oldFacts->snapshot();
            const bool contextRetired=s->oldContext&&!s->oldContext->load(std::memory_order_acquire);
            const bool ownerReleased=s->oldFacts->retiredOwnersReleased.load(std::memory_order_acquire)>0;
            if(!contextRetired||!ownerReleased){finish("Focused old context/GPU owner did not retire");return;}
            if(s->phase==16){
                const auto observed=s->atSignal.value("product_facts_at_signal").toObject();
                if(s->atSignal.value("window_signal").toString()!="afterFrameEnd"||observed.value("last_submitted_revision").toInteger()!=qint64(trigger->revision)
                    ||observed.value("last_enqueue_revision").toInteger()==qint64(trigger->revision)||s->oldFacts->guiAccepted.load(std::memory_order_acquire)!=s->accepted
                    ||after.value("photo")==s->before.value("photo")||after.value("input_source_key")==s->before.value("input_source_key")){
                    finish("Frame-end pre-publication selection did not preserve its real temporal/target guards");return;
                }
                if(!capture("native-pending-new-photo")){finish("Pending-target capture failed");return;}
                complete({{"actual_afterFrameEnd_before_publication",true},{"actual_GUI_photo_selection",true},{"at_signal",s->atSignal},{"old_facts",old},
                    {"GUI_accepted_before",qint64(s->accepted)},{"obsolete_context_inactive",true},{"old_GPU_owner_released",true},{"before",s->before},{"after",after},
                    {"physical_GPU_busy_or_device_loss_claim",false}});return;
            }
            if(s->atSignal.value("window_signal").toString()!="afterSynchronizing"||!s->atSignal.value("context_was_active").toBool()
                ||s->oldFacts->renderRetiredBlocked.load(std::memory_order_acquire)==0||after.value("source_capture_serial")!=s->before.value("source_capture_serial")
                ||after.value("input_source_key")!=s->before.value("input_source_key")){
                finish("Controlled retired snapshot did not exercise the real clear-and-return render guard");return;
            }
            if(!capture("native-retired-render-recovered")){finish("Retired-render recovery capture failed");return;}
            complete({{"actual_afterSynchronizing_before_item_render",true},{"controlled_atomic_transport_injection",true},{"actual_renderer_retired_guard_executed",true},{"at_signal",s->atSignal},{"old_facts",old},
                {"obsolete_context_inactive",true},{"old_GPU_owner_released",true},{"before",s->before},{"after",after},{"actual_GUI_race_or_physical_loss_claim",false}});return;
        }
        if(s->phase==13){
            if(c->diagnosticBusy())return;const auto diag=c->nativeBrushTrace().value("diagnostic").toObject();QString error;auto req=BrushRenderRequest::createPrepared(s->frozenSource,s->frozenPlan,BrushRenderRequest::Layout::Whole,{},&error,c->brushRenderBudget());const auto ref=req?req->render({},&error):BrushImageLease{};
            const auto fp=ref?FloatFrameFingerprint::capture(ImagePipeline::floatSource(ref.image())):QJsonObject{};
            const bool exact=ref&&fp.value("available").toBool()&&diag.value("available").toBool()&&diag.value("producer").toString()=="cpu-reference-upload"&&diag.value("pixel_sha256")==fp.value("pixel_sha256")&&diag.value("pixels")==fp.value("pixels")&&diag.value("nonfinite_rgba")==fp.value("nonfinite_rgba")&&diag.value("parameter_revision").toInteger()==qint64(s->expectedRevision);
            s->frozenSource={};if(!exact){s->observations.append(QJsonObject{{"expected_CPU",fp},{"actual",diag},{"error",error}});finish("Complete original CPU fallback FP32 readback was not bit-exact");return;}
            if(!capture("complete-CPU-"+s->name)){finish("Fallback capture failed");return;}complete({{"producer","cpu-reference-upload"},{"entire_small_frame_float4_bit_exact",true},{"pixels_compared",fp.value("pixels")},{"expected_CPU",fp},{"actual_diagnostic",diag}});return;
        }
    });
    window->setProperty("workspaceIndex",1);c->setGpuEnabled(true);
    for(int n=0;n<2;++n){QImage image(n?704:640,n?512:416,QImage::Format_RGBA64);for(int y=0;y<image.height();++y){auto*row=reinterpret_cast<QRgba64*>(image.scanLine(y));for(int x=0;x<image.width();++x)row[x]=QRgba64::fromRgba64(quint16(5000+(x*43+y*19+n*5000)%47000),quint16(3000+(y*71+x*11+n*7000)%48000),quint16(7000+(x*29+y*47+n*9000)%43000),65535);}
        const auto path=fixture->filePath(n?"owned-B.png":"owned-A.png");if(!image.save(path)){finish("Owned source creation failed");return;}QFile file(path);if(!file.open(QIODevice::ReadOnly)){finish("Owned source fingerprint failed");return;}s->sourceHashes[path]=QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);}
    QFile cube(fixture->filePath("owned.cube"));if(!cube.open(QIODevice::WriteOnly)){finish("Owned LUT creation failed");return;}
    QByteArray data="TITLE \"Owned delivery LUT\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";for(int b=0;b<2;++b)for(int g=0;g<2;++g)for(int r=0;r<2;++r)data+=QByteArray::number(.8*r+.1*g)+' '+QByteArray::number(.9*g+.05*b)+' '+QByteArray::number(.85*b+.05*r)+'\n';cube.write(data);cube.close();
    if(!fixture->isValid()||!c->createProject(QUrl::fromLocalFile(fixture->path()),"First owned delivery project")||!c->importFile(QUrl::fromLocalFile(fixture->filePath("owned-A.png")))||!c->importFile(QUrl::fromLocalFile(fixture->filePath("owned-B.png")))){finish("Owned project/import setup failed");return;}c->selectPhoto(0);timer->start();
}
