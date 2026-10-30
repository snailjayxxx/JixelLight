#include "app/PhotoController.h"
#include "app/SmokeCaptureCoordinates.h"
#include "core/pipeline/BrushMaskGeometry.h"
#include "core/pipeline/BrushMaskDocument.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/preview/PreviewTasks.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QCoreApplication>
#include <QPointingDevice>
#include <QTabletEvent>
#include "core/pipeline/BrushRenderRequest.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QJSValue>
#include <QTimer>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
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

namespace {
QQuickItem *find(QQuickItem *item,const QString &name){
    if(!item)return nullptr;if(item->objectName()==name)return item;
    for(auto *child:item->childItems())if(auto *result=find(child,name))return result;return nullptr;
}
QQuickItem *findText(QQuickItem *item,const QString &text){
    if(!item)return nullptr;if(item->isVisible()&&item->property("text").toString()==text)return item;
    for(auto *child:item->childItems())if(auto *result=findText(child,text))return result;return nullptr;
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
QByteArray hash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
int delta(QColor a,QColor b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});}
QImage reference(const QImage &source,const ProcessingPlan &plan){
    const auto input=plan.state.geometry.apply(source);QImage out(input.size(),QImage::Format_RGBA64);out.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<input.height();y+=128){const auto strip=ImagePipeline::processRegion(input,plan,{0,y,input.width(),std::min(128,input.height()-y)});if(strip.isNull())return {};
        for(int row=0;row<strip.height();++row)std::memcpy(out.scanLine(y+row),strip.constScanLine(row),size_t(input.width())*8);}
    return out;
}
bool samePlan(const ProcessingPlan &a,const ProcessingPlan &b){
    return a.state.toJson()==b.state.toJson()&&a.encoding==b.encoding&&a.output==b.output&&a.rawSource==b.rawSource
        &&a.baseExposureStops==b.baseExposureStops&&std::memcmp(a.data.data(),b.data.data(),sizeof(a.data))==0;
}
bool sameStroke(const BrushMaskGeometry::Stroke &a,const BrushMaskGeometry::Stroke &b){
    if(a.mode!=b.mode||a.dabs.size()!=b.dabs.size()||a.style.radius!=b.style.radius||a.style.hardness!=b.style.hardness||a.style.opacity!=b.style.opacity)return false;
    for(size_t i=0;i<a.dabs.size();++i)if(std::abs(a.dabs[i].center.x-b.dabs[i].center.x)>1e-9||std::abs(a.dabs[i].center.y-b.dabs[i].center.y)>1e-9||std::abs(a.dabs[i].pressure-b.dabs[i].pressure)>1e-9)return false;return true;
}
}
void startBrushPressureCanvasSmokeRun(PhotoController *c,QQuickWindow *window,const QString &report,bool transparent){
    namespace BM=BrushMaskGeometry;
    const auto owned=std::make_shared<QTemporaryDir>();const auto source=owned->filePath("pressure-owned.png"),output=owned->filePath("pressure-export.png");
    const int alpha=transparent?42000:65535;
    const QSize size(512,320);QImage image(size,QImage::Format_RGBA64);image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(9000+x*20,11000+y*20,16000+x*10,alpha);
    if(!owned->isValid()||!image.save(source)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    struct State {
        int windowGrabCalls=0;qint64 windowGrabNs=0,windowGrabMaxNs=0;
        int focusBootstrapCount=0;bool focusObserved=false,initialWindowActive=false,focusReady=false;
        bool radiusProbeDone=false;int heldRadiusGuards=0;QJsonArray radiusRecords,radiusHintRecords;int localeChecks=0;bool localeUnchanged=true;QJsonArray localeRecords;int cancelStep=0,barrelTransitions=0;bool cancelledBarrelPaint=false,cancelledBarrelErase=false;bool mouse=false,mouseHeld=false;double initialPressure=.2;QElapsedTimer clock;int phase=0,settle=0,moves=0,initialHistory=0;quint64 revision=0;ulong stamp=1000;bool done=false;
        bool lowPressureContinuous=false,lowPressureUndo=false,lowPressureRedo=false,lowPressureReset=false;
        bool lowLimitFinishRefused=false,lowLimitStatusVisible=false,lowLimitUnchanged=false;int lowLimitPackets=0;
        QColor lowBefore,lowAfter;QPointF lowPoint;QJsonObject lowMasks;int lowDelta=0;
        bool cancelPaint=false,cancelErase=false,pressurePaint=false,pressureErase=false,oneUndo=false,undoRedo=false,reopened=false,transient=true;
        int livePaintDelta=0,liveEraseDelta=0,commitPaintDelta=999,commitEraseDelta=999;bool exactExport=false,exportDone=false;int exported=0,failed=0;bool exportCancelled=false;
        QJsonObject initial,painted,erased,commitCheck;QString project;QByteArray sourceHash;QColor baseline,paintColor,liveColor;QImage expected;QJsonArray captures,phases;
        QFutureWatcher<QImage> *expectedJob=nullptr;std::optional<ProcessingPlan> referencePlan;bool referencePlanStable=false;
        qint64 referenceStartedMs=-1,exportQueuedMs=-1;
        QJsonObject eraserTipCommitted;bool useEraser=false,eraserTipStarted=false,eraserTipEmptyRefused=false,eraserTipExact=false,eraserTipUndo=false,eraserTipRedo=false,eraserTipRestored=false,eraserTipPanelStable=false,eraserTipHintLocalized=false,eraserTipHintRestored=false;QString eraserTipHintEnglish,eraserTipHintChinese,eraserTipHintAfterRelease;std::shared_ptr<QPointingDevice> pen,eraser;std::optional<BM::Stroke> expectedPaint,expectedErase;
    };
    auto s=std::make_shared<State>();s->clock.start();s->sourceHash=hash(source);
    s->mouse=false; // Controlled native tablet packets; legacy mouse has its own gates.s->initialPressure=.2;
    s->pen=std::make_shared<QPointingDevice>("Controlled photo pressure pen",987654329,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->pen.get());
    s->eraser=std::make_shared<QPointingDevice>("Controlled eraser end",987654330,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Eraser,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->eraser.get());auto *timer=new QTimer(c);timer->setInterval(50);
    const auto item=[=](const QString &name){return find(window->contentItem(),name);};
    const auto grab=[=]{QElapsedTimer clock;clock.start();const auto shot=window->grabWindow();const auto ns=clock.nsecsElapsed();++s->windowGrabCalls;s->windowGrabNs+=ns;s->windowGrabMaxNs=std::max(s->windowGrabMaxNs,ns);return shot;};
    const auto capture=[=](const QString &label,const QImage &shot){const auto path=report+"."+label+"-owned.png";if(shot.isNull()||!shot.save(path))return false;
        s->captures.append(QJsonObject{{"label",label},{"path",path},{"width",shot.width()},{"height",shot.height()},{"dpr",window->effectiveDevicePixelRatio()},{"sha256",QString::fromLatin1(hash(path))}});return true;};
    const auto finish=[=](const QString &error){
        if(s->done)return;s->done=true;timer->stop();if(!error.isEmpty())capture("failure",grab());
        const bool isolated=QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool();
        const bool unchanged=hash(source)==s->sourceHash;
        const bool passed=error.isEmpty()&&s->referencePlanStable&&s->expectedJob&&s->expectedJob->isFinished()&&s->lowPressureContinuous&&s->lowPressureUndo&&s->lowPressureRedo&&s->lowPressureReset&&s->lowLimitFinishRefused&&s->lowLimitStatusVisible&&s->lowLimitUnchanged&&s->eraserTipStarted&&s->eraserTipEmptyRefused&&s->eraserTipExact&&s->eraserTipUndo&&s->eraserTipRedo&&s->eraserTipRestored&&s->eraserTipPanelStable&&s->eraserTipHintLocalized&&s->eraserTipHintRestored&&s->focusReady&&s->radiusProbeDone&&s->radiusRecords.size()==10&&s->radiusHintRecords.size()==3&&s->heldRadiusGuards==6&&s->localeChecks==6&&s->localeUnchanged&&s->barrelTransitions==8&&s->cancelledBarrelPaint&&s->cancelledBarrelErase&&isolated&&unchanged&&s->cancelPaint&&s->cancelErase&&s->pressurePaint&&s->pressureErase&&s->oneUndo&&s->undoRedo&&s->reopened&&s->transient&&s->exactExport&&s->exportDone&&s->exported==1&&s->failed==0&&!s->exportCancelled&&s->clock.elapsed()<45000&&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        const QJsonObject d{{"window_focus_injection","Qt-QPA-controlled-window-focus"},{"initial_window_active",s->initialWindowActive},{"focus_bootstrap_count",s->focusBootstrapCount},{"window_focus_ready",s->focusReady},{"physical_OS_activation_verified",false},{"commit_check",s->commitCheck},{"radius_probe_done",s->radiusProbeDone},{"radius_key_records",s->radiusRecords},{"radius_hint_records",s->radiusHintRecords},{"radius_held_guards",s->heldRadiusGuards},{"scope_locale_checks",s->localeChecks},{"scope_locale_unchanged",s->localeUnchanged},{"scope_locale_records",s->localeRecords},{"passed",passed},{"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"deadline_ms",45000},
            {"eraser_tip_started",s->eraserTipStarted},{"eraser_tip_empty_refused",s->eraserTipEmptyRefused},{"eraser_tip_exact",s->eraserTipExact},{"eraser_tip_undo",s->eraserTipUndo},{"eraser_tip_redo",s->eraserTipRedo},{"eraser_tip_restored",s->eraserTipRestored},{"eraser_tip_panel_stable",s->eraserTipPanelStable},{"eraser_tip_hint_localized",s->eraserTipHintLocalized},{"eraser_tip_hint_restored",s->eraserTipHintRestored},{"eraser_tip_hint_english",s->eraserTipHintEnglish},{"eraser_tip_hint_chinese",s->eraserTipHintChinese},{"eraser_tip_hint_after_release",s->eraserTipHintAfterRelease},{"test_fixture",true},{"product_UI_acceptance",false},{"actual_photo_app_UI",true},{"physical_tablet_verified",false},{"injection",s->mouse?"QWindow-mouse-events":"Qt-QPA-controlled-pressure-stylus"},{"initial_pressure",s->initialPressure},{"mouse_synthesis_enabled",QCoreApplication::testAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents)},
            {"reference_plan_stable",s->referencePlanStable},{"reference_started_ms",s->referenceStartedMs},{"export_queued_ms",s->exportQueuedMs},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"build_version",QStringLiteral(JIXELLIGHT_VERSION)},{"source_kind","owned-rgba64-png"},{"source_alpha_u16",alpha},{"barrel_transitions_held",s->barrelTransitions},{"cancelled_barrel_paint",s->cancelledBarrelPaint},{"cancelled_barrel_erase",s->cancelledBarrelErase},{"primary_lift_only",s->barrelTransitions==8&&s->cancelledBarrelPaint&&s->cancelledBarrelErase},{"source_pixels",size.width()*size.height()},
            {"settings_isolated",isolated},{"source_unchanged",unchanged},{"source_sha256",QString::fromLatin1(s->sourceHash)},
            {"low_pressure_limit_finish_refused",s->lowLimitFinishRefused},{"low_pressure_limit_status_visible",s->lowLimitStatusVisible},{"low_pressure_limit_unchanged",s->lowLimitUnchanged},{"low_pressure_limit_packets",s->lowLimitPackets},
            {"low_pressure_continuous",s->lowPressureContinuous},{"low_pressure_undo",s->lowPressureUndo},{"low_pressure_redo",s->lowPressureRedo},{"low_pressure_reset",s->lowPressureReset},{"low_pressure_pixel_delta",s->lowDelta},
            {"pressure_paint_reference",s->pressurePaint},{"pressure_erase_reference",s->pressureErase},{"cancel_paint",s->cancelPaint},{"cancel_erase",s->cancelErase},{"one_undo",s->oneUndo},{"undo_redo",s->undoRedo},{"project_reopen",s->reopened},{"held_intent_history_revision_unchanged",s->transient},
            {"live_paint_delta",s->livePaintDelta},{"live_erase_delta",s->liveEraseDelta},{"live_to_commit_paint_delta",s->commitPaintDelta},{"live_to_commit_erase_delta",s->commitEraseDelta},
            {"export_exact_pixels_and_ICC",s->exactExport},{"gpu_active",c->gpuActive()},{"backend",c->processingBackend()},{"dpr",window->effectiveDevicePixelRatio()},
            {"native_gpu_brush",false},{"native_float_raw_decoder",false},{"window_grab_calls",s->windowGrabCalls},{"window_grab_total_ms",s->windowGrabNs/1e6},{"window_grab_max_ms",s->windowGrabMaxNs/1e6},{"captures",s->captures},{"phases",s->phases}};
        QSaveFile f(report);const auto bytes=QJsonDocument(d).toJson(QJsonDocument::Indented);const bool saved=f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
        QTimer::singleShot(0,c,[passed,saved]{QCoreApplication::exit(passed&&saved?0:1);});
    };
    QObject::connect(c,&PhotoController::exportFinished,timer,[=](int success,int failed,bool cancelled){s->exportDone=true;s->exported=success;s->failed=failed;s->exportCancelled=cancelled;});
    QObject::connect(timer,&QTimer::timeout,c,[=,keep=owned]{
        Q_UNUSED(keep);
        if(s->phases.isEmpty()||s->phases.last().toObject()["phase"].toInt()!=s->phase)s->phases.append(QJsonObject{{"phase",s->phase},{"at_ms",s->clock.elapsed()}});
        if(s->clock.elapsed()>=45000){finish("Controlled photo pressure QA exceeded45000ms");return;}
        if(!c->brushReady())return;auto *canvas=item("photoCanvas"),*overlay=item("brushCanvasOverlay");if(!canvas||!overlay||!canvas->property("brushDisplayReady").toBool())return;
        const auto raw=overlay->property("editor");QObject *editor=raw.value<QObject *>();if(!editor&&raw.canConvert<QJSValue>())editor=raw.value<QJSValue>().toQObject();if(!editor){finish("Actual brush editor unavailable");return;}
        const auto point=[&](double x){return overlay->mapToScene({overlay->width()*x,overlay->height()*.6});};
        const auto read=[&](QColor *color){const auto shot=grab();const auto probe=point(.6)+QPointF(0,overlay->property("radiusY").toDouble()*.2);*color=SmokeCaptureCoordinates::sample(shot,probe,window->effectiveDevicePixelRatio());return color->isValid()?shot:QImage{};};
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
            if(s->moves>=5)return true;
            if(!c->brushDrawing()){finish("Side button prematurely ended an actual held photo stroke");return false;}
            if(s->moves==0) {
                const auto radius=editor->property("radius").toDouble();key(window,Qt::Key_BracketRight);key(window,Qt::Key_BracketLeft);
                if(editor->property("radius").toDouble()!=radius||!c->brushDrawing()){finish("RADIUS_CHANGED_DURING_HELD_PEN");return false;}
                ++s->heldRadiusGuards;
            }
            if(s->moves==0)packet(.4,.4,true);
            else if(s->moves==1)buttons(.5,.95,Qt::LeftButton|Qt::RightButton);
            else if(s->moves==2)buttons(.55,.1,Qt::LeftButton);
            else if(s->moves==3)packet(.6,.8,true);
            else packet(.75,.6,true);
            if(!c->brushDrawing()){finish("Side button prematurely ended an actual held photo stroke");return false;}
            if(s->moves==1||s->moves==2)++s->barrelTransitions;
            ++s->moves;return false;
        };
        const auto frozen=[&](const QJsonObject &doc,int cursor){return masks(c)==doc&&history(c)==cursor&&c->renderRevision()==s->revision;};
        const auto expected=[&](BM::StrokeMode mode){BM::StrokeBuilder builder(512,320,{.1,1,.8},mode);for(auto v:std::array<BM::Dab,5>{{{{.25,.6},s->initialPressure},{{.4,.6},.4},{{.6,.6},.8},{{.75,.6},.6},{{.75,.6},0}}}){if(s->mouse)v.pressure=1;if(!builder.append(v))return std::optional<BM::Stroke>{};}return builder.finish();};
        const auto checkLocale=[&](bool full)->QString {
            const auto pixelCount=c->scopesPixelCount();const auto revision=c->renderRevision();
            const auto intent=c->gpuPlan(true).state.toJson();const int cursor=history(c);
            const auto red=c->redHistogram(),green=c->greenHistogram(),blue=c->blueHistogram(),luma=c->lumaHistogram();
            const bool gpu=c->gpuActive();
            auto *label=item("scopeStatusLabel");if(!label)return "SCOPE_LOCALE_MISSING_UI_LABEL";
            for(const auto *language:{"en_US","zh_CN","en_US"}) {
                c->setLanguage(language);
                const bool english=QString::fromLatin1(language)=="en_US";
                const QString expected=full?(english?QStringLiteral("Full-resolution statistics"):QStringLiteral("全分辨率统计")):
                    gpu?(english?QStringLiteral("GPU preview statistics"):QStringLiteral("GPU 预览统计")):
                    (english?QStringLiteral("Preview statistics"):QStringLiteral("预览统计"));
                const auto controllerText=c->scopesStatus(),ui=label->property("text").toString();
                const bool stable=c->scopesPixelCount()==pixelCount&&c->renderRevision()==revision&&c->gpuPlan(true).state.toJson()==intent&&history(c)==cursor
                    &&c->redHistogram()==red&&c->greenHistogram()==green&&c->blueHistogram()==blue&&c->lumaHistogram()==luma&&c->gpuActive()==gpu;
                s->localeUnchanged=s->localeUnchanged&&stable;
                s->localeRecords.append(QJsonObject{{"full",full},{"language",language},{"controller",controllerText},{"UI",ui},{"expected",expected},{"stable",stable},{"pixels",qint64(pixelCount)},{"revision",qint64(revision)}});
                if(controllerText!=expected||ui!=expected||!stable)return QString("SCOPE_LOCALE_MISMATCH expected=%1 controller=%2 UI=%3 stable=%4").arg(expected,controllerText,ui).arg(stable);
                if(!full) {
                    auto *hint=item("brushRadiusHint");const QString expectedHint=(english?QStringLiteral("Radius "):QStringLiteral("半径 "))+QStringLiteral("10.0% · [ / ]");
                    if(!hint||!hint->isVisible()||!hint->property("text").toString().endsWith(expectedHint)||hint->width()>canvas->width()-15)return "RADIUS_CANVAS_HINT_MISSING_OR_CLIPPED";
                    s->radiusHintRecords.append(QJsonObject{{"language",language},{"UI",hint->property("text").toString()},{"expected_suffix",expectedHint},{"width",hint->width()},{"canvas_width",canvas->width()},{"visible",true}});
                }
                ++s->localeChecks;if(!capture(QString("locale-%1-%2").arg(full?"full":"preview").arg(s->localeChecks),grab()))return "SCOPE_LOCALE_CAPTURE_FAILED";
            }
            return {};
        };
        if(s->phase==0){
            if(!s->focusObserved){s->initialWindowActive=window->isActive();s->focusObserved=true;}
            if(!window->isActive()) {
                // The controlled key/tablet test owns Qt's platform focus.
                // Native OS activation policy is a separate, unverified boundary.
                QWindowSystemInterface::handleFocusWindowChanged(window);
                QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
                ++s->focusBootstrapCount;
                if(!window->isActive()){finish("Controlled Qt platform window focus unavailable");return;}
            }
            s->focusReady=window->isActive();
            s->initial=masks(c);s->initialHistory=history(c);if(!s->initial.isEmpty()||!click(window,item("editBrushMaskCanvas"))){finish("Cannot enter actual photo brush tool");return;}
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
            // The old fixed-spacing sampler left an unpainted gap here at
            // constant 0.1 pressure. Inspect that original gap in the actual
            // photo canvas, then exercise Undo/Redo before the existing flow.
            editor->setProperty("radius",.2);editor->setProperty("hardness",1);editor->setProperty("opacity",1);
            s->lowPoint=point(.2+.2*.125*double(size.height())/size.width());
            const auto shot=grab();s->lowBefore=SmokeCaptureCoordinates::sample(shot,s->lowPoint,window->effectiveDevicePixelRatio());
            if(shot.isNull()||!s->lowBefore.isValid()||!capture("low-pressure-before",shot)){finish("Low-pressure baseline capture unavailable");return;}
            packet(.2,.1,true);if(!c->brushDrawing()){finish("Low-pressure stroke did not start");return;}
            packet(.8,.1,true);if(!c->brushDrawing()){finish("Low-pressure stroke ended during motion");return;}
            packet(.8,0,false);if(c->brushDrawing()){finish("Low-pressure stroke did not finish");return;}
            s->phase=60;s->settle=0;
        }else if(s->phase==60){
            if(++s->settle<3)return;
            const auto doc=c->gpuPlan(true).state.brushMasks;
            if(doc.layers.size()!=1||doc.layers[0].strokes.size()!=1||history(c)!=s->initialHistory+1){finish("Low-pressure stroke was not one edit");return;}
            const auto coverage=BM::strokeOpacity(doc.layers[0].strokes[0],{.2+.2*.125*double(size.height())/size.width(),.6},size.width(),size.height());
            const auto shot=grab();s->lowAfter=SmokeCaptureCoordinates::sample(shot,s->lowPoint,window->effectiveDevicePixelRatio());s->lowDelta=delta(s->lowBefore,s->lowAfter);
            s->lowPressureContinuous=coverage&&std::abs(*coverage-.1)<1e-6&&s->lowAfter.isValid()&&s->lowDelta>0;
            if(!s->lowPressureContinuous||!capture("low-pressure-painted",shot)){finish("Low-pressure canvas gap remains");return;}
            s->lowMasks=masks(c);if(!click(window,item("undoEditButton"))){finish("Cannot undo low-pressure stroke");return;}s->phase=61;s->settle=0;
        }else if(s->phase==61){
            if(++s->settle<3)return;const auto shot=grab();const auto color=SmokeCaptureCoordinates::sample(shot,s->lowPoint,window->effectiveDevicePixelRatio());
            s->lowPressureUndo=masks(c)==s->initial&&history(c)==s->initialHistory&&color.isValid()&&delta(color,s->lowBefore)<=2;
            if(!s->lowPressureUndo||!capture("low-pressure-undo",shot)||!click(window,item("redoEditButton"))){finish("Low-pressure Undo did not restore pixels");return;}s->phase=62;s->settle=0;
        }else if(s->phase==62){
            if(++s->settle<3)return;const auto shot=grab();const auto color=SmokeCaptureCoordinates::sample(shot,s->lowPoint,window->effectiveDevicePixelRatio());
            s->lowPressureRedo=masks(c)==s->lowMasks&&history(c)==s->initialHistory+1&&color.isValid()&&delta(color,s->lowAfter)<=2;
            if(!s->lowPressureRedo||!capture("low-pressure-redo",shot)||!click(window,item("undoEditButton"))){finish("Low-pressure Redo did not restore pixels");return;}s->phase=63;s->settle=0;
        }else if(s->phase==63){
            if(++s->settle<3)return;s->lowPressureReset=masks(c)==s->initial&&history(c)==s->initialHistory&&editor->property("layerId").toString().isEmpty();
            if(!s->lowPressureReset){finish("Low-pressure setup did not return to the original document");return;}
            // At exactly the dab ceiling, a final tail point cannot be added.
            // Drive the real overlay to that boundary and require an explanation,
            // with no partial edit, before continuing the existing workflow.
            const double spacing=std::max(.2*.25*.1*std::min(size.width(),size.height()),.5);
            const double path=.6*size.width();
            const int whole=int(std::floor(double(BM::MaxDabsPerStroke-1)*spacing/path));
            const double remainder=(double(BM::MaxDabsPerStroke-1)+.4)*spacing-whole*path;
            const double edge=(whole%2? .8:.2)+(whole%2? -1:1)*remainder/size.width();
            BM::StrokeBuilder bound(size.width(),size.height(),{.2,1,1});
            bool built=bound.append({{.2,.6},.1});
            for(int i=0;i<whole;++i)built=built&&bound.append({{i%2? .2:.8,.6},.1});
            built=built&&bound.append({{edge,.6},.1})&&bound.append({{edge,.6},0});
            const auto completed=bound.finish();
            if(!built||completed||!bound.invalid()){finish("Low-pressure finish boundary reference failed");return;}
            packet(.2,.1,true);if(!c->brushDrawing()){finish("Low-pressure limit probe did not start");return;}
            for(int i=0;i<whole;++i){packet(i%2? .2:.8,.1,true);if(!c->brushDrawing()){finish("Low-pressure limit refused before finish");return;}}
            packet(edge,.1,true);if(!c->brushDrawing()){finish("Low-pressure tail refused before release");return;}
            s->lowLimitPackets=whole+1;packet(edge,0,false);
            s->lowLimitFinishRefused=!c->brushDrawing()&&c->statusMessage()==QStringLiteral("Stroke limit exceeded; use shorter strokes.");
            if(!s->lowLimitFinishRefused){finish("Low-pressure finish refusal lacks feedback");return;}
            s->phase=64;s->settle=0;
        }else if(s->phase==64){
            if(++s->settle<3)return;const QString expected=QStringLiteral("Stroke limit exceeded; use shorter strokes.");
            const auto *label=findText(window->contentItem(),expected);const auto shot=grab();
            const auto color=SmokeCaptureCoordinates::sample(shot,s->lowPoint,window->effectiveDevicePixelRatio());
            s->lowLimitStatusVisible=label&&label->width()>0&&label->height()>0&&c->statusMessage()==expected;
            s->lowLimitUnchanged=masks(c)==s->initial&&history(c)==s->initialHistory&&editor->property("layerId").toString().isEmpty()&&color.isValid()&&delta(color,s->lowBefore)<=2;
            if(!s->lowLimitStatusVisible||!s->lowLimitUnchanged||!capture("low-pressure-limit",shot)){finish("Low-pressure limit feedback or rollback failed");return;}
            editor->setProperty("radius",.08);editor->setProperty("hardness",1);editor->setProperty("opacity",.8);overlay->forceActiveFocus();s->phase=1;s->settle=0;
        }else if(s->phase==1){
            if(++s->settle<4||!c->scopesPixelCount())return;
            if(!s->radiusProbeDone) {
                const auto snapshot=c->gpuPlan(true).state.toJson();const int cursor=history(c);const auto revision=c->renderRevision();
                auto *slider=item("brushRadiusSlider");if(!slider){finish("RADIUS_MISSING_SLIDER");return;}
                const auto radiusKey=[&](int code,Qt::KeyboardModifiers mods=Qt::NoModifier,bool repeat=false){
                    const QString text=code==Qt::Key_BracketLeft?QStringLiteral("["):QStringLiteral("]");
                    QKeyEvent down(QEvent::KeyPress,code,mods,text,repeat,1),up(QEvent::KeyRelease,code,mods,text,false,1);
                    QCoreApplication::sendEvent(window,&down);QCoreApplication::sendEvent(window,&up);
                };
                const auto observed=[&](QString label,double expected){
                    const double value=editor->property("radius").toDouble(),ui=slider->property("value").toDouble();
                    const bool stable=c->gpuPlan(true).state.toJson()==snapshot&&history(c)==cursor&&c->renderRevision()==revision&&!c->brushDrawing();
                    const bool good=std::abs(value-expected)<1e-10&&std::abs(ui-expected)<1e-10&&stable;
                    s->radiusRecords.append(QJsonObject{{"case",label},{"expected",expected},{"radius",value},{"slider",ui},{"focus",overlay->hasActiveFocus()},{"window_active",window->isActive()},{"item_focus",overlay->hasFocus()},{"item_enabled",overlay->isEnabled()},{"state_unchanged",stable},{"passed",good}});return good;
                };
                const double ring=overlay->property("radiusX").toDouble();
                radiusKey(Qt::Key_BracketRight);
                const bool grown=observed("activate-grow",.1);if(!overlay->hasActiveFocus()||!grown){finish("RADIUS_ACTUAL_ACTIVATION_OR_GROW_FAILED");return;}
                if(std::abs(overlay->property("radiusX").toDouble()-ring*1.25)>1e-8){finish("RADIUS_VISIBLE_RING_GEOMETRY_FAILED");return;}
                radiusKey(Qt::Key_BracketLeft);if(!observed("shrink",.08)){finish("RADIUS_SHRINK_FAILED");return;}
                for(const auto mod:{Qt::ShiftModifier,Qt::ControlModifier,Qt::AltModifier,Qt::MetaModifier})radiusKey(Qt::Key_BracketRight,mod);
                if(!observed("modifiers-ignored",.08)){finish("RADIUS_MODIFIER_INTERCEPTION");return;}
                for(int i=0;i<40;++i)radiusKey(Qt::Key_BracketRight,Qt::NoModifier,i>0);
                if(!observed("repeat-max",.5)){finish("RADIUS_MAX_FAILED");return;}
                for(int i=0;i<40;++i)radiusKey(Qt::Key_BracketLeft,Qt::NoModifier,i>0);
                if(!observed("repeat-min",.002)){finish("RADIUS_MIN_FAILED");return;}
                editor->setProperty("radius",.08);
                const auto findInput=[&](auto &&self,QQuickItem *parent)->QQuickItem* {
                    if(parent->property("placeholderText").toString()=="New preset name")return parent;
                    for(auto *child:parent->childItems())if(auto *found=self(self,child))return found;return nullptr;
                };
                auto *input=findInput(findInput,window->contentItem());
                if(!input||!click(window,input)||!input->hasActiveFocus()){finish("RADIUS_ACTUAL_TEXT_FOCUS_FAILED");return;}
                const auto original=input->property("text").toString();radiusKey(Qt::Key_BracketLeft);radiusKey(Qt::Key_BracketRight);
                if(input->property("text").toString()!=original+"[]"||!observed("text-input-isolated",.08)){finish("RADIUS_TEXT_INPUT_INTERCEPTED");return;}
                input->setProperty("text",original);
                if(!click(window,item("editBrushMaskCanvas"))||!click(window,item("editBrushMaskCanvas"))){finish("RADIUS_REENTER_FAILED");return;}
                // Focus handoff is deferred to the next event turn by the product.
                s->radiusProbeDone=true;s->phase=30;return;
            }
            // Wait for the queued GPU scope label; the original locale checks and 45s deadline remain intact.
            if(c->gpuActive()&&c->scopesStatus()!=QStringLiteral("GPU preview statistics"))return;
            const auto localeError=checkLocale(false);if(!localeError.isEmpty()){finish(localeError);return;}
            const auto shot=read(&s->baseline);if(shot.isNull()||!capture("before",shot)||!start()){finish("Pressure paint did not begin in actual canvas");return;}s->phase=2;
        }else if(s->phase==30){
            if(!overlay->hasActiveFocus()||std::abs(editor->property("radius").toDouble()-.08)>1e-10){finish("RADIUS_REENTRY_FOCUS_FAILED");return;}
            s->radiusRecords.append(QJsonObject{{"case","reentry-focus"},{"passed",true},{"radius",.08},{"focus",true}});
            const auto doc=masks(c);const int cursor=history(c);const auto revision=c->renderRevision();
            key(window,Qt::Key_Escape);key(window,Qt::Key_BracketRight);
            const bool inactive=canvas->property("maskTool").toString().isEmpty()&&!overlay->isVisible()&&std::abs(editor->property("radius").toDouble()-.08)<1e-10&&masks(c)==doc&&history(c)==cursor&&c->renderRevision()==revision;
            s->radiusRecords.append(QJsonObject{{"case","idle-exit-inactive"},{"radius",editor->property("radius").toDouble()},{"passed",inactive},{"state_unchanged",inactive}});
            if(!inactive||!click(window,item("editBrushMaskCanvas"))){finish("RADIUS_IDLE_ESCAPE_OR_INACTIVE_KEY_FAILED");return;}
            s->phase=31;return;
        }else if(s->phase==31){
            if(!overlay->hasActiveFocus()){finish("RADIUS_IDLE_ESCAPE_REACTIVATION_FOCUS_FAILED");return;}
            const auto doc=masks(c);const int cursor=history(c);const auto revision=c->renderRevision();
            const auto local=point(.2);QMouseEvent press(QEvent::MouseButtonPress,local,local,window->mapToGlobal(local),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&press);
            if(!c->brushDrawing()){finish("RADIUS_MOUSE_HELD_NOT_STARTED");return;}
            key(window,Qt::Key_BracketRight);key(window,Qt::Key_BracketLeft);
            const bool held=c->brushDrawing()&&std::abs(editor->property("radius").toDouble()-.08)<1e-10;
            key(window,Qt::Key_Escape);QMouseEvent release(QEvent::MouseButtonRelease,local,local,window->mapToGlobal(local),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&release);
            const bool stable=!c->brushDrawing()&&masks(c)==doc&&history(c)==cursor&&c->renderRevision()==revision;
            s->radiusRecords.append(QJsonObject{{"case","mouse-held"},{"radius",editor->property("radius").toDouble()},{"passed",held&&stable},{"state_unchanged",stable}});
            if(!held||!stable){finish("RADIUS_MOUSE_HELD_CHANGED_OR_COMMITTED");return;}
            key(window,Qt::Key_BracketRight);
            const bool next=std::abs(editor->property("radius").toDouble()-.1)<1e-10&&std::abs(item("brushRadiusSlider")->property("value").toDouble()-.1)<1e-10&&history(c)==cursor&&c->renderRevision()==revision;
            s->radiusRecords.append(QJsonObject{{"case","next-stroke-radius"},{"radius",editor->property("radius").toDouble()},{"slider",item("brushRadiusSlider")->property("value").toDouble()},{"passed",next},{"state_unchanged",true}});
            if(!next){finish("RADIUS_NEXT_STROKE_STYLE_NOT_SELECTED");return;}
            s->phase=1;s->settle=0;
        }else if(s->phase==2||s->phase==4||s->phase==9||s->phase==11){
            const bool erase=s->phase>=9,cancel=s->phase==2||s->phase==9;if(!advance())return;
            const auto doc=erase?s->painted:s->initial;const auto cursor=s->initialHistory+(erase?1:0);s->transient=s->transient&&frozen(doc,cursor);
            QColor color;const auto shot=read(&color);if(shot.isNull()||delta(color,erase?s->paintColor:s->baseline)<3||c->brushPreviewUrl().isEmpty())return;
            // Save the exact UI frame whose RGB passed; no input changes precede this capture.
            s->liveColor=color;if(!s->transient||!capture(cancel?(erase?"cancel-erase-held":"cancel-paint-held"):(erase?"erase-held":"paint-held"),shot)){finish("Held pressure input leaked intent/history/revision or capture failed");return;}
            if(cancel){key(window,Qt::Key_Escape);s->cancelStep=0;s->phase=erase?21:20;}
            else{if(erase){s->liveEraseDelta=delta(color,s->paintColor);s->expectedErase=expected(BM::StrokeMode::Erase);}else{s->livePaintDelta=delta(color,s->baseline);s->expectedPaint=expected(BM::StrokeMode::Paint);}packet(.75,0,false);s->phase=erase?12:5;}
        }else if(s->phase==20||s->phase==21){
            const bool erase=s->phase==21;const auto doc=erase?s->painted:s->initial;const int cursor=s->initialHistory+(erase?1:0);
            const auto unchanged=[&]{return !c->brushDrawing()&&frozen(doc,cursor);};
            if(!unchanged()){finish("Cancelled photo stroke resurrected before primary lift");return;}
            QColor color;const auto shot=read(&color);if(shot.isNull()||delta(color,erase?s->paintColor:s->baseline)>2)return;
            if(s->cancelStep==0) {
                const auto radius=editor->property("radius").toDouble();key(window,Qt::Key_BracketRight);key(window,Qt::Key_BracketLeft);
                if(editor->property("radius").toDouble()!=radius||!unchanged()){finish("RADIUS_CHANGED_BEFORE_CANCELLED_TIP_LIFT");return;}
                ++s->heldRadiusGuards;
            }
            if(s->cancelStep==0)buttons(.6,.9,Qt::LeftButton|Qt::RightButton);
            else if(s->cancelStep==1)buttons(.65,.9,Qt::LeftButton);
            else if(s->cancelStep==2)packet(.7,.9,true);
            else if(s->cancelStep==3){
                const auto local=point(.7);QTabletEvent duplicate(QEvent::TabletPress,s->pen.get(),local,window->mapToGlobal(local),.9,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);
                QCoreApplication::sendEvent(window,&duplicate);
            }else packet(.75,0,false);
            if(!unchanged()){finish("Cancelled photo stroke resurrected before primary lift");return;}
            if(s->cancelStep++==4){
                if(!capture(erase?"cancel-erase-guarded":"cancel-paint-guarded",grab())){finish("Cannot inspect cancelled held-owner picture");return;}
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
            s->oneUndo=history(c)==s->initialHistory+(erase?2:1);if(!exact||!s->oneUndo||c->brushDrawing()){
                QJsonObject before,oracle;BrushMaskDocument::toJson(doc,&before);
                if(expectedStroke){BrushMaskDocument::Document expectedDoc;expectedDoc.layers.push_back({"expected",true,1,{*expectedStroke}});BrushMaskDocument::toJson(expectedDoc,&oracle);}
                s->commitCheck={{"exact",exact},{"one_undo",s->oneUndo},{"drawing",c->brushDrawing()},{"history",history(c)},{"expected_history",s->initialHistory+(erase?2:1)},{"before_grab",before},{"after_grab",masks(c)},{"oracle",oracle},{"editor_radius",editor->property("radius").toDouble()}};
                finish("Pressure sequence was duplicated, flattened or committed as multiple history edits");return;
            }
            if(erase){
                s->pressureErase=true;s->erased=masks(c);s->commitEraseDelta=composition;
                // Freeze the exact erase plan before Undo/Redo and reopen. The
                // independent reference uses the original file in parallel
                // with the remaining UI checks; its result is still required.
                const auto frozen=c->gpuPlan(true);s->referencePlan=frozen;s->referenceStartedMs=s->clock.elapsed();
                s->expectedJob=new QFutureWatcher<QImage>(timer);
                s->expectedJob->setFuture(QtConcurrent::run([source,frozen,keep=owned]{
                    Q_UNUSED(keep);const auto loaded=loadSourceFresh(source,{});return reference(loaded.image,frozen);
                }));
            }else{s->pressurePaint=true;s->painted=masks(c);s->paintColor=color;s->commitPaintDelta=composition;}
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
            // The export freezes the same reopened plan while exact-scope and
            // locale checks continue. Neither reference nor export is skipped.
            if(!s->referencePlan||!s->expectedJob||!samePlan(*s->referencePlan,c->gpuPlan(true))){finish("Reopened pressure plan changed before export");return;}
            s->referencePlanStable=true;if(!c->exportCurrent(QUrl::fromLocalFile(output),"srgb",100)){finish("Cannot export the reopened pressure edit");return;}
            s->exportQueuedMs=s->clock.elapsed();s->reopened=true;c->setExactScopes(true);s->phase=17;
        }else if(s->phase==17){
            if(c->scopesPixelCount()!=512*320||!c->scopesStatus().contains("Full-resolution statistics"))return;
            if(!s->referencePlan||!samePlan(*s->referencePlan,c->gpuPlan(true))){finish("Pressure plan changed during exact-scope check");return;}
            const auto localeError=checkLocale(true);if(!localeError.isEmpty()){finish(localeError);return;}s->phase=16;
        }else if(s->phase==16){
            if(!s->exportDone||!s->expectedJob||!s->expectedJob->isFinished())return;
            if(!s->referencePlan||!samePlan(*s->referencePlan,c->gpuPlan(true))){finish("Pressure plan changed before pixel verification");return;}
            s->expected=s->expectedJob->result();if(s->expected.isNull()){finish("Cannot independently render the reopened pressure edit");return;}
            const QImage actual(output);const bool exact=actual.colorSpace()==s->expected.colorSpace()&&actual.convertToFormat(QImage::Format_RGBA64)==s->expected;
            if(!exact||!capture("final",grab())){finish("Pressure PNG16 pixels/ICC differ from independent CPU reference");return;}s->exactExport=true;finish({});
        }
    });
    window->setProperty("workspaceIndex",1);if(qEnvironmentVariableIsSet("JIXELLIGHT_BRUSH_PRESSURE_COMPACT_QA"))window->resize(1180,720);else window->resize(1540,920);window->requestActivate();
    if(!c->importFile(QUrl::fromLocalFile(source))||!c->createProject(QUrl::fromLocalFile(owned->path()),"PressureOwnershipNative")){finish("Cannot import/create isolated owned pressure project");return;}
    s->project=c->projectPath();timer->start();
}
