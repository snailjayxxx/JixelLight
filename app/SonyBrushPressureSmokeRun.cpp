#include "app/PhotoController.h"
#include "diagnostics/PerformanceRecorder.h"
#ifdef Q_OS_MACOS
void beginGuiSmokeExportActivity();
#endif
#include "app/SmokeCaptureCoordinates.h"
#include "core/pipeline/BrushMaskGeometry.h"
#include "core/pipeline/BrushMaskDocument.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/BrushRenderRequest.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QFileInfo>
#include <QBuffer>
#include "core/preview/PreviewTasks.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QCoreApplication>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QJSValue>
#include <QTimer>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QColorSpace>
#include <QRgba64>
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
#include <memory>
#include <array>
#include <optional>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace {
QQuickItem *find(QQuickItem *item,const QString &name){
    if(!item)return nullptr;if(item->objectName()==name)return item;
    for(auto *child:item->childItems())if(auto *result=find(child,name))return result;return nullptr;
}
bool click(QQuickWindow *window,QQuickItem *item){
    if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto point=item->mapToScene({item->width()/2,item->height()/2});
    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,point,point,window->mapToGlobal(point),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&event);
    }return true;
}
void key(QQuickWindow *window,int code){
    QKeyEvent down(QEvent::KeyPress,code,Qt::NoModifier),up(QEvent::KeyRelease,code,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&down);QCoreApplication::sendEvent(window,&up);
}
bool select(QQuickWindow *window,const QString &name,int index){
    auto *item=find(window->contentItem(),name);if(!click(window,item))return false;
    key(window,Qt::Key_Home);for(int i=0;i<index;++i)key(window,Qt::Key_Down);key(window,Qt::Key_Return);
    return item->property("currentIndex").toInt()==index;
}
int history(PhotoController *c){const auto rows=c->editHistory();for(int i=0;i<rows.size();++i)if(rows[i].toMap()["current"].toBool())return i;return -1;}
QJsonObject masks(PhotoController *c){return c->gpuPlan(true).state.toJson()["brushMasks"].toObject();}
QByteArray hash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};const auto size=f.size();if(size<0)return {};QCryptographicHash h(QCryptographicHash::Sha256);qint64 read=0;
    while(read<size){const auto bytes=f.read(std::min<qint64>(1024*1024,size-read));if(bytes.isEmpty())return {};h.addData(bytes);read+=bytes.size();}if(f.size()!=size)return {};return h.result().toHex();}
int delta(QColor a,QColor b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});}
QImage reference(const QImage &source,const ProcessingPlan &plan,const CancelToken &cancel){
    QString error;auto request=BrushRenderRequest::createOriginal(source,plan,plan.state.geometry,BrushRenderRequest::Layout::Stripes128,cancel,&error);
    if(!request)return {};QImage out(request->size(),QImage::Format_RGBA64);out.setColorSpace(ColorManagement::colorSpace(plan.output));if(out.isNull())return {};
    if(!request->visitStrips([&](const BrushImageLease &lease,int y){const auto &strip=lease.image();
        for(int row=0;row<strip.height();++row)std::memcpy(out.scanLine(y+row),strip.constScanLine(row),size_t(out.width())*8);return !cancelled(cancel);},cancel,&error))return {};
    return out;
}
bool sameStroke(const BrushMaskGeometry::Stroke &a,const BrushMaskGeometry::Stroke &b){
    if(a.mode!=b.mode||a.dabs.size()!=b.dabs.size()||a.style.radius!=b.style.radius||a.style.hardness!=b.style.hardness||a.style.opacity!=b.style.opacity)return false;
    for(size_t i=0;i<a.dabs.size();++i)if(std::abs(a.dabs[i].center.x-b.dabs[i].center.x)>1e-9||std::abs(a.dabs[i].center.y-b.dabs[i].center.y)>1e-9||std::abs(a.dabs[i].pressure-b.dabs[i].pressure)>1e-9)return false;return true;
}
}
void startSonyBrushPressureCanvasSmokeRun(PhotoController *c,QQuickWindow *window,const QString &source,const QString &report){
    namespace BM=BrushMaskGeometry;
    const auto native=[](const QString &path){
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(path.toStdString());
#endif
    };
    const auto alias=[&](const QString &path){std::error_code error;return QFileInfo(source).absoluteFilePath()==QFileInfo(path).absoluteFilePath()||std::filesystem::equivalent(native(source),native(path),error);};
    bool aliases=alias(report)||alias(report+".checkpoint.json")||alias(report+".export.png")||alias(report+".frozen.json");for(const auto *label:{"before","cancel-paint-held","paint-held","painted","reopened-paint","cancel-erase-held","erase-held","erased","final","failure","cancel-paint-guarded","cancel-erase-guarded"})aliases=aliases||alias(report+"."+label+"-controls.png");
    if(aliases){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}

    const auto owned=std::make_shared<QTemporaryDir>();const auto output=report+".export.png";const QSize size(7028,4688);
    if(!owned->isValid()||QFileInfo(source).size()!=69120000||hash(source)!="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d") {
        QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;
    }
    struct State {
        int windowGrabCalls=0;qint64 windowGrabNs=0,windowGrabMaxNs=0;
        QElapsedTimer exportClock;qint64 guiElapsed=0;bool exportActivity=false,guiSourceChecked=false,guiComplete=false,fullScopes=false;QFutureWatcher<QImage> *expectedJob=nullptr;CancelToken cancel=std::make_shared<std::atomic_bool>(false);int cancelStep=0,barrelTransitions=0;bool ownership=false,cancelledBarrelPaint=false,cancelledBarrelErase=false;bool mouse=false,mouseHeld=false;double initialPressure=.2;QElapsedTimer clock;int phase=0,settle=0,moves=0,initialHistory=0;quint64 revision=0;ulong stamp=1000;bool done=false;
        bool cancelPaint=false,cancelErase=false,pressurePaint=false,pressureErase=false,oneUndo=false,undoRedo=false,reopened=false,transient=true;
        int livePaintDelta=0,liveEraseDelta=0,commitPaintDelta=999,commitEraseDelta=999;bool exactExport=false,exportDone=false;int exported=0,failed=0;bool exportCancelled=false;
        QJsonObject initial,painted,erased;QString project;QByteArray sourceHash;QColor baseline,paintColor,liveColor;QImage expected;QJsonArray captures,phases;
        QJsonObject eraserTipCommitted;bool useEraser=false,eraserTipStarted=false,eraserTipEmptyRefused=false,eraserTipExact=false,eraserTipUndo=false,eraserTipRedo=false,eraserTipRestored=false,eraserTipPanelStable=false,eraserTipHintLocalized=false,eraserTipHintRestored=false;QString eraserTipHintEnglish,eraserTipHintChinese,eraserTipHintAfterRelease;std::shared_ptr<QPointingDevice> pen,eraser;std::optional<BM::Stroke> expectedPaint,expectedErase;
    };
    auto s=std::make_shared<State>();s->clock.start();s->sourceHash=hash(source);
    s->mouse=false;s->ownership=true;s->initialPressure=.2;
    s->pen=std::make_shared<QPointingDevice>("Controlled photo pressure pen",987654329,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->pen.get());
    s->eraser=std::make_shared<QPointingDevice>("Controlled eraser end",987654330,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Eraser,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->eraser.get());auto *timer=new QTimer(c);timer->setInterval(50);
    const auto item=[=](const QString &name){return find(window->contentItem(),name);};
    const auto grab=[=]{QElapsedTimer clock;clock.start();const auto shot=window->grabWindow();const auto ns=clock.nsecsElapsed();++s->windowGrabCalls;s->windowGrabNs+=ns;s->windowGrabMaxNs=std::max(s->windowGrabMaxNs,ns);return shot;};
    const auto capture=[=](const QString &label,const QImage &shot){auto *panel=item("developRightPanel");if(shot.isNull()||!panel)return false;
        const auto a=panel->mapToScene({0,0}),b=panel->mapToScene({panel->width(),panel->height()});const auto rect=SmokeCaptureCoordinates::region(shot,QRectF(a,b),window->effectiveDevicePixelRatio());
        const auto path=report+"."+label+"-controls.png";if(rect.isEmpty())return false;QByteArray bytes;QBuffer buffer(&bytes);
        if(!buffer.open(QIODevice::WriteOnly)||!shot.copy(rect).save(&buffer,"PNG"))return false;QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return false;
        s->captures.append(QJsonObject{{"label",label},{"path",path},{"width",shot.width()},{"height",shot.height()},{"controls_width",rect.width()},{"controls_height",rect.height()},{"dpr",window->effectiveDevicePixelRatio()},{"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}});return true;};
    const auto finish=[=](const QString &error){
        if(s->done)return;s->done=true;timer->stop();if(!error.isEmpty()){s->cancel->store(true);c->cancelExport();capture("failure",grab());}
        const bool isolated=QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool();
        const bool unchanged=s->guiSourceChecked; // Driver rehashes the original after this process exits.
        const auto budget=BrushRenderBudget::sharedDefault()->snapshot();const bool admitted=budget.limit==3LL*1024*1024*1024&&budget.peak>0&&budget.peak<=budget.limit&&budget.charged<=budget.limit;
        const bool nativePassed=error.isEmpty()&&s->eraserTipStarted&&s->eraserTipEmptyRefused&&s->eraserTipExact&&s->eraserTipUndo&&s->eraserTipRedo&&s->eraserTipRestored&&s->eraserTipPanelStable&&s->eraserTipHintLocalized&&s->eraserTipHintRestored&&(!s->ownership||(s->barrelTransitions==8&&s->cancelledBarrelPaint&&s->cancelledBarrelErase))&&isolated&&unchanged&&admitted&&s->guiSourceChecked&&c->currentIsRaw()&&c->brushFrameSize()==size&&s->cancelPaint&&s->cancelErase&&s->pressurePaint&&s->pressureErase&&s->oneUndo&&s->undoRedo&&s->reopened&&s->transient&&s->exportDone&&s->exported==1&&s->failed==0&&!s->exportCancelled&&s->guiComplete&&s->fullScopes&&s->guiElapsed<60000&&s->exportClock.isValid()&&s->exportClock.elapsed()<120000&&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        const QJsonObject d{{"passed",false},{"gui_and_export_write_passed",nativePassed},{"external_pixel_verification_pending",true},{"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"gui_elapsed_ms",s->guiElapsed},{"gui_deadline_ms",60000},{"export_elapsed_ms",s->exportClock.isValid()?s->exportClock.elapsed():0},{"export_deadline_ms",120000},
            {"eraser_tip_started",s->eraserTipStarted},{"eraser_tip_empty_refused",s->eraserTipEmptyRefused},{"eraser_tip_exact",s->eraserTipExact},{"eraser_tip_undo",s->eraserTipUndo},{"eraser_tip_redo",s->eraserTipRedo},{"eraser_tip_restored",s->eraserTipRestored},{"eraser_tip_panel_stable",s->eraserTipPanelStable},{"eraser_tip_hint_localized",s->eraserTipHintLocalized},{"eraser_tip_hint_restored",s->eraserTipHintRestored},{"eraser_tip_hint_english",s->eraserTipHintEnglish},{"eraser_tip_hint_chinese",s->eraserTipHintChinese},{"eraser_tip_hint_after_release",s->eraserTipHintAfterRelease},{"ownership_mode",s->ownership},{"barrel_transitions_held",s->barrelTransitions},{"cancelled_barrel_paint",s->cancelledBarrelPaint},{"cancelled_barrel_erase",s->cancelledBarrelErase},{"primary_lift_only",s->barrelTransitions==8&&s->cancelledBarrelPaint&&s->cancelledBarrelErase},{"test_fixture",true},{"product_UI_acceptance",false},{"actual_photo_app_UI",true},{"physical_tablet_verified",false},{"injection",s->mouse?"QWindow-mouse-events":"Qt-QPA-controlled-pressure-stylus"},{"initial_pressure",s->initialPressure},{"mouse_synthesis_enabled",QCoreApplication::testAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents)},
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"build_version",QStringLiteral(JIXELLIGHT_VERSION)},{"source_kind","public-sony-arw"},{"source_bytes",QFileInfo(source).size()},{"source_is_raw",c->currentIsRaw()},{"synthetic_fixture",false},{"source_override",false},{"full_scopes",s->fullScopes},{"source_unchanged_at_GUI_completion",s->guiSourceChecked},{"budget_admitted",admitted},{"scope_pixels",qint64(c->scopesPixelCount())},{"scope_plot_pixels",qint64(c->scopePlotPixels())},{"photographs_published",false},{"smoke_user_requested_export_activity",s->exportActivity},{"activity_lifetime","export-through-normal-process-teardown"},{"performance",PerformanceRecorder::snapshot()},{"source_pixels",size.width()*size.height()},
            {"settings_isolated",isolated},{"source_unchanged",unchanged},{"source_sha256",QString::fromLatin1(s->sourceHash)},
            {"pressure_paint_reference",s->pressurePaint},{"pressure_erase_reference",s->pressureErase},{"cancel_paint",s->cancelPaint},{"cancel_erase",s->cancelErase},{"one_undo",s->oneUndo},{"undo_redo",s->undoRedo},{"project_reopen",s->reopened},{"held_intent_history_revision_unchanged",s->transient},
            {"live_paint_delta",s->livePaintDelta},{"live_erase_delta",s->liveEraseDelta},{"live_to_commit_paint_delta",s->commitPaintDelta},{"live_to_commit_erase_delta",s->commitEraseDelta},
            {"export_exact_pixels_and_ICC",s->exactExport},{"gpu_active",c->gpuActive()},{"backend",c->processingBackend()},{"dpr",window->effectiveDevicePixelRatio()},
            {"operation_budget_bytes",BrushRenderBudget::sharedDefault()->snapshot().limit},{"operation_peak_reserved_bytes",BrushRenderBudget::sharedDefault()->snapshot().peak},{"operation_live_charged_bytes",BrushRenderBudget::sharedDefault()->snapshot().charged},{"native_gpu_brush",false},{"native_float_raw_decoder",false},{"window_grab_calls",s->windowGrabCalls},{"window_grab_total_ms",s->windowGrabNs/1e6},{"window_grab_max_ms",s->windowGrabMaxNs/1e6},{"captures",s->captures},{"phases",s->phases}};
        QSaveFile f(report);const auto bytes=QJsonDocument(d).toJson(QJsonDocument::Indented);const bool saved=f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
        QTimer::singleShot(0,c,[nativePassed,saved]{QCoreApplication::exit(nativePassed&&saved?0:1);});
    };
    QObject::connect(c,&PhotoController::exportFinished,timer,[=](int success,int failed,bool cancelled){s->exportDone=true;s->exported=success;s->failed=failed;s->exportCancelled=cancelled;qInfo()<<"PRIVATE_SONY_EXPORT_FINISHED"<<success<<failed<<cancelled<<s->exportClock.elapsed();});
    QObject::connect(timer,&QTimer::timeout,c,[=,keep=owned]{
        Q_UNUSED(keep);
        if(s->phases.isEmpty()||s->phases.last().toObject()["phase"].toInt()!=s->phase){
            s->phases.append(QJsonObject{{"phase",s->phase},{"at_ms",s->clock.elapsed()}});
            QSaveFile checkpoint(report+".checkpoint.json");const auto bytes=QJsonDocument(QJsonObject{{"test_fixture",true},{"accepted",false},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"gui_complete",s->guiComplete},{"gui_elapsed_ms",s->guiElapsed},{"phases",s->phases}}).toJson();
            if(!checkpoint.open(QIODevice::WriteOnly)||checkpoint.write(bytes)!=bytes.size()||!checkpoint.commit()){finish("Cannot persist private phase checkpoint");return;}
        }
        if(!s->guiComplete&&s->clock.elapsed()>=60000){finish("Controlled Sony pressure GUI exceeded original60000ms");return;}
        if(s->guiComplete&&s->exportClock.elapsed()>=120000){finish("Controlled Sony pressure export/reference exceeded120s");return;}
        if(!c->brushReady())return;auto *canvas=item("photoCanvas"),*overlay=item("brushCanvasOverlay");if(!canvas||!overlay||!canvas->property("brushDisplayReady").toBool())return;
        const auto raw=overlay->property("editor");QObject *editor=raw.value<QObject *>();if(!editor&&raw.canConvert<QJSValue>())editor=raw.value<QJSValue>().toQObject();if(!editor){finish("Actual brush editor unavailable");return;}
        const auto point=[&](double x){return overlay->mapToScene({overlay->width()*x,overlay->height()*.6});};
        const auto read=[&](QColor *color){const auto shot=grab();const auto probe=point(.5)+QPointF(0,overlay->property("radiusY").toDouble()*.2);*color=SmokeCaptureCoordinates::sample(shot,probe,window->effectiveDevicePixelRatio());return color->isValid()?shot:QImage{};};
        const auto packet=[&](double x,double pressure,bool held){const auto local=point(x),global=window->mapToGlobal(local);
            if(s->mouse){const auto type=held?(s->mouseHeld?QEvent::MouseMove:QEvent::MouseButtonPress):QEvent::MouseButtonRelease;
                QMouseEvent event(type,local,local,global,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,held?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&event);s->mouseHeld=held;return;}
            QWindowSystemInterface::handleTabletEvent(window,s->stamp+=20,s->useEraser?s->eraser.get():s->pen.get(),QHighDpi::toNativePixels(local,window),QHighDpi::toNativePixels(global,window),held?Qt::LeftButton:Qt::NoButton,pressure,0,0,0,0,0);
            QWindowSystemInterface::flushWindowSystemEvents();};
        const auto buttons=[&](double x,double pressure,Qt::MouseButtons held){
            const auto local=point(x),global=window->mapToGlobal(local);
            QWindowSystemInterface::handleTabletEvent(window,s->stamp+=20,s->useEraser?s->eraser.get():s->pen.get(),QHighDpi::toNativePixels(local,window),QHighDpi::toNativePixels(global,window),held,pressure,0,0,0,0,0);
            QWindowSystemInterface::flushWindowSystemEvents();
        };
        const auto start=[&]{s->moves=0;s->revision=c->renderRevision();packet(.25,s->initialPressure,true);return c->brushDrawing();};
        const auto advance=[&]{
            if(!s->ownership){const double xs[]{.4,.6,.75},ps[]{.4,.8,.6};if(s->moves<3){packet(xs[s->moves],ps[s->moves],true);++s->moves;return false;}return true;}
            if(s->moves>=5)return true;
            if(!c->brushDrawing()){finish("Side button prematurely ended an actual held Sony stroke");return false;}
            if(s->moves==0)packet(.4,.4,true);
            else if(s->moves==1)buttons(.5,.95,Qt::LeftButton|Qt::RightButton);
            else if(s->moves==2)buttons(.55,.1,Qt::LeftButton);
            else if(s->moves==3)packet(.6,.8,true);
            else packet(.75,.6,true);
            if(!c->brushDrawing()){finish("Side button prematurely ended an actual held Sony stroke");return false;}
            if(s->moves==1||s->moves==2)++s->barrelTransitions;
            ++s->moves;return false;
        };
        const auto frozen=[&](const QJsonObject &doc,int cursor){return masks(c)==doc&&history(c)==cursor&&c->renderRevision()==s->revision;};
        const auto expected=[&](BM::StrokeMode mode){BM::StrokeBuilder builder(7028,4688,{.08,1,.8},mode);for(auto v:std::array<BM::Dab,5>{{{{.25,.6},s->initialPressure},{{.4,.6},.4},{{.6,.6},.8},{{.75,.6},.6},{{.75,.6},0}}}){if(s->mouse)v.pressure=1;if(!builder.append(v))return std::optional<BM::Stroke>{};}return builder.finish();};
        if(s->phase==0){
            if(c->brushFrameSize()!=size||!c->currentIsRaw()){finish("Pinned Sony was not decoded at native full dimensions/type");return;}s->initial=masks(c);s->initialHistory=history(c);if(!s->initial.isEmpty()||!click(window,item("editBrushMaskCanvas"))){finish("Cannot enter actual photo brush tool");return;}
            editor->setProperty("radius",.08);editor->setProperty("hardness",1);editor->setProperty("opacity",.8);editor->setProperty("exposure",1);c->setLanguage("en_US");s->phase=50;s->settle=0;
        }else if(s->phase==50){
            if(++s->settle<2)return;
            if(!editor->property("layerId").toString().isEmpty()||editor->property("mode").toString()!="paint"){finish("Empty eraser baseline target changed");return;}
            const auto before=masks(c);const int cursor=history(c);const auto revision=c->renderRevision();s->useEraser=true;
            packet(.25,.2,true);
            const bool rejected=!c->brushDrawing()&&masks(c)==before&&history(c)==cursor&&c->renderRevision()==revision&&c->statusMessage()==QStringLiteral("Select an existing brush layer to erase first.");
            packet(.25,0,false);s->useEraser=false;
            s->eraserTipEmptyRefused=rejected&&!c->brushDrawing()&&masks(c)==before&&history(c)==cursor&&c->renderRevision()==revision&&editor->property("mode").toString()=="paint";
            if(!s->eraserTipEmptyRefused){finish("Empty eraser unexpectedly painted or lacked explicit feedback");return;}
            s->phase=1;s->settle=0;
        }else if(s->phase==1){
            if(++s->settle<4)return;const auto shot=read(&s->baseline);if(shot.isNull()||!capture("before",shot)||!start()){finish("Pressure paint did not begin in actual canvas");return;}s->phase=2;
        }else if(s->phase==2||s->phase==4||s->phase==9||s->phase==11){
            const bool erase=s->phase>=9,cancel=s->phase==2||s->phase==9;if(!advance())return;
            const auto doc=erase?s->painted:s->initial;const auto cursor=s->initialHistory+(erase?1:0);s->transient=s->transient&&frozen(doc,cursor);
            QColor color;const auto shot=read(&color);if(shot.isNull()||delta(color,erase?s->paintColor:s->baseline)<3||c->brushPreviewUrl().isEmpty())return;
            // Save the exact UI frame whose RGB passed; no input changes precede this capture.
            s->liveColor=color;if(!s->transient||!capture(cancel?(erase?"cancel-erase-held":"cancel-paint-held"):(erase?"erase-held":"paint-held"),shot)){finish("Held pressure input leaked intent/history/revision or capture failed");return;}
            if(cancel){key(window,Qt::Key_Escape);if(s->ownership){s->cancelStep=0;s->phase=erase?21:20;}else{packet(.75,0,false);s->phase=erase?10:3;}}
            else{if(erase){s->liveEraseDelta=delta(color,s->paintColor);s->expectedErase=expected(BM::StrokeMode::Erase);}else{s->livePaintDelta=delta(color,s->baseline);s->expectedPaint=expected(BM::StrokeMode::Paint);}packet(.75,0,false);s->phase=erase?12:5;}
        }else if(s->phase==20||s->phase==21){
            const bool erase=s->phase==21;const auto doc=erase?s->painted:s->initial;const int cursor=s->initialHistory+(erase?1:0);
            const auto unchanged=[&]{return !c->brushDrawing()&&frozen(doc,cursor);};
            if(!unchanged()){finish("Cancelled Sony stroke resurrected before primary lift");return;}
            QColor color;const auto shot=read(&color);if(shot.isNull()||delta(color,erase?s->paintColor:s->baseline)>2)return;
            if(s->cancelStep==0)buttons(.6,.9,Qt::LeftButton|Qt::RightButton);
            else if(s->cancelStep==1)buttons(.65,.9,Qt::LeftButton);
            else if(s->cancelStep==2)packet(.7,.9,true);
            else if(s->cancelStep==3){
                const auto local=point(.7);QTabletEvent duplicate(QEvent::TabletPress,s->pen.get(),local,window->mapToGlobal(local),.9,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);
                QCoreApplication::sendEvent(window,&duplicate);
            }else packet(.75,0,false);
            if(!unchanged()){finish("Cancelled Sony stroke resurrected before primary lift");return;}
            if(s->cancelStep++==4){
                if(!capture(erase?"cancel-erase-guarded":"cancel-paint-guarded",grab())){finish("Cannot inspect Sony cancelled held-owner picture");return;}
                if(erase)s->cancelledBarrelErase=true;else s->cancelledBarrelPaint=true;s->phase=erase?10:3;
            }
        }else if(s->phase==3||s->phase==10){
            const bool erase=s->phase==10;QColor color;const auto shot=read(&color);if(shot.isNull()||delta(color,erase?s->paintColor:s->baseline)>2)return;
            const bool cancel=!c->brushDrawing()&&masks(c)==(erase?s->painted:s->initial)&&history(c)==s->initialHistory+(erase?1:0);
            if(!cancel||!start()){finish("Pressure Esc failed to restore the committed picture or restart");return;}if(erase)s->cancelErase=true;else s->cancelPaint=true;s->phase=erase?11:4;
        }else if(s->phase==5||s->phase==12){
            const bool erase=s->phase==12;const auto doc=c->gpuPlan(true).state.brushMasks;QColor color;const auto shot=read(&color);if(shot.isNull())return;
            const auto expectedStroke=erase?s->expectedErase:s->expectedPaint;
            const bool exact=expectedStroke&&doc.layers.size()==1&&doc.layers[0].strokes.size()==size_t(erase?2:1)&&sameStroke(doc.layers[0].strokes.back(),*expectedStroke)&&std::abs(doc.layers[0].strokes.back().dabs.front().pressure-s->initialPressure)<1e-9;
            const int composition=delta(color,s->liveColor);if(composition>2)return;
            s->oneUndo=history(c)==s->initialHistory+(erase?2:1);if(!exact||!s->oneUndo||c->brushDrawing()){finish("Pressure sequence was duplicated, flattened or committed as multiple history edits");return;}
            if(erase){s->pressureErase=true;s->erased=masks(c);s->commitEraseDelta=composition;}else{s->pressurePaint=true;s->painted=masks(c);s->paintColor=color;s->commitPaintDelta=composition;}
            if(!capture(erase?"erased":"painted",shot)||!click(window,item("undoEditButton"))){finish("Cannot inspect committed pressure picture or click Undo");return;}s->phase=erase?13:6;
        }else if(s->phase==6||s->phase==13){
            const bool erase=s->phase==13;QColor color;const auto shot=read(&color);if(shot.isNull()||delta(color,erase?s->paintColor:s->baseline)>2)return;
            if(masks(c)!=(erase?s->painted:s->initial)||history(c)!=s->initialHistory+(erase?1:0)||!click(window,item("redoEditButton"))){finish("Actual pressure Undo failed");return;}s->phase=erase?14:7;
        }else if(s->phase==7||s->phase==14){
            const bool erase=s->phase==14;QColor color;const auto shot=read(&color);if(shot.isNull()||delta(color,erase?s->liveColor:s->paintColor)>2)return;
            if(masks(c)!=(erase?s->erased:s->painted)||history(c)!=s->initialHistory+(erase?2:1)||!c->flushEdits()||!c->openProject(QUrl::fromLocalFile(s->project))){finish("Actual pressure Redo/persistence failed");return;}s->undoRedo=true;s->phase=erase?15:8;s->settle=0;
        }else if(s->phase==8){
            if(++s->settle<4)return;if(masks(c)!=s->painted||!click(window,item("editBrushMaskCanvas"))||!select(window,"brushLayerSelector",1)||!select(window,"brushModeSelector",1)){finish("Pressure project reopen/actual Erase selection failed");return;}
            if(!capture("reopened-paint",grab())||!start()){finish("Cannot start reopened pressure Erase");return;}s->phase=9;
        }else if(s->phase==15){
            if(++s->settle<4)return;if(masks(c)!=s->erased){finish("Reopened pressure Erase intent differs");return;}
            // Preserve the original pen-mode Erase test, then add a separate eraser-end stroke.
            if(!click(window,item("editBrushMaskCanvas"))||!select(window,"brushLayerSelector",1)||!select(window,"brushModeSelector",0)){finish("Cannot prepare eraser-end target");return;}
            editor->setProperty("radius",.08);editor->setProperty("hardness",1);editor->setProperty("opacity",.8);editor->setProperty("exposure",1);
            s->useEraser=true;packet(.25,.6,true);
            s->eraserTipStarted=c->brushDrawing()&&editor->property("mode").toString()=="paint"&&overlay->property("erasing").toBool();
            auto *hint=item("maskToolHint");
            s->eraserTipHintEnglish=hint?hint->property("text").toString():QString();
            c->setLanguage("zh_CN");s->eraserTipHintChinese=hint?hint->property("text").toString():QString();c->setLanguage("en_US");
            s->eraserTipHintLocalized=s->eraserTipHintEnglish==QStringLiteral("Drag to erase the selected layer, release to apply · Esc cancels the stroke")&&s->eraserTipHintChinese==QStringLiteral("拖动擦除所选图层，松开应用 · Esc 取消当前笔触")&&c->brushDrawing()&&editor->property("mode").toString()=="paint";
            packet(.25,0,false);s->useEraser=false;
            s->eraserTipPanelStable=editor->property("mode").toString()=="paint";
            if(!s->eraserTipStarted||!s->eraserTipPanelStable||!s->eraserTipHintLocalized){finish("Eraser tip mode, panel or localized hint mismatch");return;}
            s->phase=51;
        }else if(s->phase==51){
            // This phase is reached only after brushReady and displayed preview are current.
            // Keep the existing overall deadline while waiting for the post-commit refresh.
            auto *hint=item("maskToolHint");s->eraserTipHintAfterRelease=hint?hint->property("text").toString():QString();
            s->eraserTipHintRestored=s->eraserTipHintAfterRelease==QStringLiteral("Drag to paint, release to apply · Esc cancels the stroke")&&!c->brushDrawing()&&editor->property("mode").toString()=="paint";
            if(!s->eraserTipHintRestored){finish("Eraser tip released but Paint hint not restored");return;}
            BM::StrokeBuilder expected(size.width(),size.height(),{.08,1,.8},BM::StrokeMode::Erase);
            if(!expected.append({{.25,.6},.6})||!expected.append({{.25,.6},0})){finish("Cannot build eraser-end reference");return;}
            const auto stroke=expected.finish();const auto doc=c->gpuPlan(true).state.brushMasks;
            s->eraserTipExact=stroke&&doc.layers.size()==1&&doc.layers[0].strokes.size()==3&&sameStroke(doc.layers[0].strokes.back(),*stroke)&&history(c)==s->initialHistory+3&&!c->brushDrawing();
            if(!s->eraserTipExact){finish("Eraser-end stroke differs from reference or is not one history step");return;}
            s->eraserTipCommitted=masks(c);if(!click(window,item("undoEditButton"))){finish("Cannot undo eraser-end stroke");return;}s->phase=52;
        }else if(s->phase==52){
            s->eraserTipUndo=masks(c)==s->erased&&history(c)==s->initialHistory+2;
            if(!s->eraserTipUndo||!click(window,item("redoEditButton"))){finish("Eraser-end Undo failed");return;}s->phase=53;
        }else if(s->phase==53){
            s->eraserTipRedo=masks(c)==s->eraserTipCommitted&&history(c)==s->initialHistory+3;
            if(!s->eraserTipRedo||!click(window,item("undoEditButton"))){finish("Eraser-end Redo failed");return;}s->phase=54;
        }else if(s->phase==54){
            s->eraserTipRestored=masks(c)==s->erased&&history(c)==s->initialHistory+2&&editor->property("mode").toString()=="paint";
            if(!s->eraserTipRestored){finish("Eraser-end final Undo did not restore original intent");return;}
            s->reopened=true;
            if(!select(window,"scopeModeChoice",1)){finish("Cannot choose actual Sony waveform control");return;}
            s->phase=17;s->settle=0;
        }else if(s->phase==17){
            if(++s->settle<4)return;if(!c->exactScopes()&&!click(window,item("exactScopesCheck"))){finish("Cannot enable actual exact Sony scope control");return;}s->phase=18;
        }else if(s->phase==18){
            auto *plot=item("scopePlotImage");if(!c->scopePlotCurrent()||c->scopesPixelCount()!=32947264||c->scopePlotPixels()!=32947264||!plot||plot->property("status").toInt()!=1||plot->property("source").toUrl()!=QUrl(c->scopePlotUrl()))return;
            s->fullScopes=c->scopeMode()=="waveform"&&c->exactScopes()&&masks(c)==s->erased;if(!s->fullScopes||!capture("final",grab())){finish("Sony pressure full-source scope intent/capture differs");return;}
            s->guiSourceChecked=hash(source)==s->sourceHash;if(!s->guiSourceChecked){finish("Original Sony changed before GUI completion");return;}
            const auto frozen=c->gpuPlan(true);const auto packed=QByteArray(reinterpret_cast<const char *>(frozen.data.data()),sizeof(frozen.data));
            const QJsonObject snapshot{{"test_fixture",true},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"build_version",QStringLiteral(JIXELLIGHT_VERSION)},{"source_sha256",QString::fromLatin1(s->sourceHash)},
                {"state",c->currentState().toJson()},{"base_exposure_stops",frozen.baseExposureStops},{"plan_data_sha256",QString::fromLatin1(QCryptographicHash::hash(packed,QCryptographicHash::Sha256).toHex())}};
            QSaveFile saved(report+".frozen.json");const auto bytes=QJsonDocument(snapshot).toJson();if(!saved.open(QIODevice::WriteOnly)||saved.write(bytes)!=bytes.size()||!saved.commit()){finish("Cannot freeze exact export input for independent verification");return;}
            s->guiElapsed=s->clock.elapsed();if(s->guiElapsed>=60000){finish("Sony pressure GUI final validation exceeded60000ms");return;}
            s->guiComplete=true;s->exportClock.start();
#ifdef Q_OS_MACOS
            beginGuiSmokeExportActivity();s->exportActivity=true;
#endif
            if(!c->exportCurrent(QUrl::fromLocalFile(output),"srgb",100)){finish("Cannot queue the native Sony pressure export");return;}s->phase=22;
        }else if(s->phase==22){
            if(!s->exportDone)return;if(s->exported!=1||s->failed||s->exportCancelled||QFileInfo(output).size()<=0){finish("Native Sony pressure export failed");return;}
            // The driver closes this GUI process before starting an independent
            // admitted-strip/PNG verifier. Its entire export+verification wall
            // time is still required to fit the original120s slice.
            finish({});
        }

    });
    window->setProperty("workspaceIndex",1);window->resize(1540,920);window->requestActivate();
    if(!c->importFile(QUrl::fromLocalFile(source))||!c->createProject(QUrl::fromLocalFile(owned->path()),"SonyPressureNative")){finish("Cannot import/create isolated owned pressure project");return;}
    s->project=c->projectPath();timer->start();
}
