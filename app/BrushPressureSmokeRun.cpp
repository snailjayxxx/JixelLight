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
        int cancelStep=0,barrelTransitions=0;bool cancelledBarrelPaint=false,cancelledBarrelErase=false;bool mouse=false,mouseHeld=false;double initialPressure=.2;QElapsedTimer clock;int phase=0,settle=0,moves=0,initialHistory=0;quint64 revision=0;ulong stamp=1000;bool done=false;
        bool cancelPaint=false,cancelErase=false,pressurePaint=false,pressureErase=false,oneUndo=false,undoRedo=false,reopened=false,transient=true;
        int livePaintDelta=0,liveEraseDelta=0,commitPaintDelta=999,commitEraseDelta=999;bool exactExport=false,exportDone=false;int exported=0,failed=0;bool exportCancelled=false;
        QJsonObject initial,painted,erased;QString project;QByteArray sourceHash;QColor baseline,paintColor,liveColor;QImage expected;QJsonArray captures,phases;
        std::shared_ptr<QPointingDevice> pen;std::optional<BM::Stroke> expectedPaint,expectedErase;
    };
    auto s=std::make_shared<State>();s->clock.start();s->sourceHash=hash(source);
    s->mouse=false; // Controlled native tablet packets; legacy mouse has its own gates.s->initialPressure=.2;
    s->pen=std::make_shared<QPointingDevice>("Controlled photo pressure pen",987654329,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->pen.get());auto *timer=new QTimer(c);timer->setInterval(50);
    const auto item=[=](const QString &name){return find(window->contentItem(),name);};
    const auto grab=[=]{QElapsedTimer clock;clock.start();const auto shot=window->grabWindow();const auto ns=clock.nsecsElapsed();++s->windowGrabCalls;s->windowGrabNs+=ns;s->windowGrabMaxNs=std::max(s->windowGrabMaxNs,ns);return shot;};
    const auto capture=[=](const QString &label,const QImage &shot){const auto path=report+"."+label+"-owned.png";if(shot.isNull()||!shot.save(path))return false;
        s->captures.append(QJsonObject{{"label",label},{"path",path},{"width",shot.width()},{"height",shot.height()},{"dpr",window->effectiveDevicePixelRatio()},{"sha256",QString::fromLatin1(hash(path))}});return true;};
    const auto finish=[=](const QString &error){
        if(s->done)return;s->done=true;timer->stop();if(!error.isEmpty())capture("failure",grab());
        const bool isolated=QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool();
        const bool unchanged=hash(source)==s->sourceHash;
        const bool passed=error.isEmpty()&&s->barrelTransitions==8&&s->cancelledBarrelPaint&&s->cancelledBarrelErase&&isolated&&unchanged&&s->cancelPaint&&s->cancelErase&&s->pressurePaint&&s->pressureErase&&s->oneUndo&&s->undoRedo&&s->reopened&&s->transient&&s->exactExport&&s->exportDone&&s->exported==1&&s->failed==0&&!s->exportCancelled&&s->clock.elapsed()<45000&&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        const QJsonObject d{{"passed",passed},{"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"deadline_ms",45000},
            {"test_fixture",true},{"product_UI_acceptance",false},{"actual_photo_app_UI",true},{"physical_tablet_verified",false},{"injection",s->mouse?"QWindow-mouse-events":"Qt-QPA-controlled-pressure-stylus"},{"initial_pressure",s->initialPressure},{"mouse_synthesis_enabled",QCoreApplication::testAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents)},
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"build_version",QStringLiteral(JIXELLIGHT_VERSION)},{"source_kind","owned-rgba64-png"},{"source_alpha_u16",alpha},{"barrel_transitions_held",s->barrelTransitions},{"cancelled_barrel_paint",s->cancelledBarrelPaint},{"cancelled_barrel_erase",s->cancelledBarrelErase},{"primary_lift_only",s->barrelTransitions==8&&s->cancelledBarrelPaint&&s->cancelledBarrelErase},{"source_pixels",size.width()*size.height()},
            {"settings_isolated",isolated},{"source_unchanged",unchanged},{"source_sha256",QString::fromLatin1(s->sourceHash)},
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
            QWindowSystemInterface::handleTabletEvent(window,s->stamp+=20,s->pen.get(),QHighDpi::toNativePixels(local,window),QHighDpi::toNativePixels(global,window),held?Qt::LeftButton:Qt::NoButton,pressure,0,0,0,0,0);
            QWindowSystemInterface::flushWindowSystemEvents();};
        const auto buttons=[&](double x,double pressure,Qt::MouseButtons held){
            const auto local=point(x),global=window->mapToGlobal(local);
            QWindowSystemInterface::handleTabletEvent(window,s->stamp+=20,s->pen.get(),QHighDpi::toNativePixels(local,window),QHighDpi::toNativePixels(global,window),held,pressure,0,0,0,0,0);
            QWindowSystemInterface::flushWindowSystemEvents();
        };
        const auto start=[&]{s->moves=0;s->revision=c->renderRevision();packet(.25,s->initialPressure,true);return c->brushDrawing();};
        const auto advance=[&]{
            if(s->moves>=5)return true;
            if(!c->brushDrawing()){finish("Side button prematurely ended an actual held photo stroke");return false;}
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
        const auto expected=[&](BM::StrokeMode mode){BM::StrokeBuilder builder(512,320,{.08,1,.8},mode);for(auto v:std::array<BM::Dab,5>{{{{.25,.6},s->initialPressure},{{.4,.6},.4},{{.6,.6},.8},{{.75,.6},.6},{{.75,.6},0}}}){if(s->mouse)v.pressure=1;if(!builder.append(v))return std::optional<BM::Stroke>{};}return builder.finish();};
        if(s->phase==0){
            s->initial=masks(c);s->initialHistory=history(c);if(!s->initial.isEmpty()||!click(window,item("editBrushMaskCanvas"))){finish("Cannot enter actual photo brush tool");return;}
            editor->setProperty("radius",.08);editor->setProperty("hardness",1);editor->setProperty("opacity",.8);editor->setProperty("exposure",1);c->setLanguage("en_US");s->phase=1;s->settle=0;
        }else if(s->phase==1){
            if(++s->settle<4)return;const auto shot=read(&s->baseline);if(shot.isNull()||!capture("before",shot)||!start()){finish("Pressure paint did not begin in actual canvas");return;}s->phase=2;
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
            if(++s->settle<4)return;if(masks(c)!=s->erased){finish("Reopened pressure Erase intent differs");return;}s->reopened=true;
            const auto loaded=loadSourceFresh(source,{});s->expected=reference(loaded.image,c->gpuPlan(true));if(s->expected.isNull()||!c->exportCurrent(QUrl::fromLocalFile(output),"srgb",100)){finish("Cannot independently render/export the reopened pressure edit");return;}s->phase=16;
        }else if(s->phase==16){
            if(!s->exportDone)return;const QImage actual(output);const bool exact=actual.colorSpace()==s->expected.colorSpace()&&actual.convertToFormat(QImage::Format_RGBA64)==s->expected;
            if(!exact||!capture("final",grab())){finish("Pressure PNG16 pixels/ICC differ from independent CPU reference");return;}s->exactExport=true;finish({});
        }
    });
    window->setProperty("workspaceIndex",1);window->resize(1540,920);window->requestActivate();
    if(!c->importFile(QUrl::fromLocalFile(source))||!c->createProject(QUrl::fromLocalFile(owned->path()),"PressureOwnershipNative")){finish("Cannot import/create isolated owned pressure project");return;}
    s->project=c->projectPath();timer->start();
}
