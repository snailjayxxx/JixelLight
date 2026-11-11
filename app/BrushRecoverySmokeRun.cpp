#include "app/PhotoController.h"
#include "app/SmokeCaptureCoordinates.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
QQuickItem *findItem(QQuickItem *item,const QString &name) {
    if(!item)return nullptr;
    if(item->objectName()==name)return item;
    for(auto *child:item->childItems())if(auto *found=findItem(child,name))return found;
    return nullptr;
}
bool clickItem(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto point=item->mapToScene({item->width()/2,item->height()/2});
    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,point,point,window->mapToGlobal(point),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&event);
    }
    return true;
}
void sendKey(QQuickWindow *window,int key) {
    QKeyEvent down(QEvent::KeyPress,key,Qt::NoModifier),up(QEvent::KeyRelease,key,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&down);QCoreApplication::sendEvent(window,&up);
}
bool selectItem(QQuickWindow *window,const QString &name,int index) {
    auto *item=findItem(window->contentItem(),name);
    if(!clickItem(window,item))return false;
    sendKey(window,Qt::Key_Home);
    for(int i=0;i<index;++i)sendKey(window,Qt::Key_Down);
    sendKey(window,Qt::Key_Return);
    return item->property("currentIndex").toInt()==index;
}
int history(PhotoController *controller) {
    const auto rows=controller->editHistory();
    for(int i=0;i<rows.size();++i)if(rows[i].toMap()["current"].toBool())return i;
    return -1;
}
QJsonObject masks(PhotoController *controller) {
    return controller->gpuPlan(true).state.toJson()["brushMasks"].toObject();
}
QByteArray sha(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file)?hash.result().toHex():QByteArray{};
}
int delta(QColor a,QColor b) {
    return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
}
}

void startBrushRecoverySmokeRun(PhotoController *c,QQuickWindow *window,const QString &report,
                               bool transparent,bool erase) {
    const auto owned=std::make_shared<QTemporaryDir>();
    if(!owned->isValid()){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    const auto source=owned->filePath("recovery-owned.png");
    const QSize size(512,320);const int alpha=transparent?42000:65535;
    QImage image(size,QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(9000+x*20,11000+y*20,16000+x*10,alpha);
    if(!image.save(source)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    struct State {
        QElapsedTimer clock;int phase=0,initialHistory=0;ulong stamp=1000;
        int refused[3]={0,0,0};std::size_t recoveredDabs=0;
        bool done=false,baseReady=false,focus=false,heldDisabled=false,releaseEnabled=false,
            blockedNew=false,controlsBlocked=false,explicitApply=false,pixelChanged=false,
            undo=false,redo=false,discard=false,discardRedo=false,escape=false,controlsRestored=false;
        QByteArray sourceSha;QJsonObject initial,applied;QColor before,after;
        QVariantList redoHistory;QJsonArray captures;std::shared_ptr<QPointingDevice> pen;
    };
    auto s=std::make_shared<State>();s->clock.start();s->sourceSha=sha(source);
    s->pen=std::make_shared<QPointingDevice>("Controlled recovery pen",987654399,
        QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->pen.get());
    auto *timer=new QTimer(c);timer->setInterval(50);
    const auto item=[=](const QString &name){return findItem(window->contentItem(),name);};
    const auto capture=[=](const QString &label) {
        const auto shot=window->grabWindow();const auto path=report+"."+label+"-owned.png";
        if(shot.isNull()||!shot.save(path,"PNG",85))return false;
        s->captures.append(QJsonObject{{"label",label},{"path",path},{"width",shot.width()},
            {"height",shot.height()},{"dpr",window->effectiveDevicePixelRatio()},
            {"sha256",QString::fromLatin1(sha(path))}});
        return true;
    };
    const auto finish=[=](const QString &error) {
        if(s->done)return;s->done=true;timer->stop();
        const bool isolated=QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool();
        const bool unchanged=sha(source)==s->sourceSha;
        const bool passed=error.isEmpty()&&isolated&&unchanged&&s->focus&&s->heldDisabled&&s->releaseEnabled
            &&s->blockedNew&&s->controlsBlocked&&s->explicitApply&&s->pixelChanged&&s->undo&&s->redo
            &&s->discard&&s->discardRedo&&s->escape&&s->controlsRestored&&s->captures.size()==10
            &&s->clock.elapsed()<60000&&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        const QJsonObject out{{"passed",passed},{"error",error},{"phase",s->phase},
            {"elapsed_ms",s->clock.elapsed()},{"deadline_ms",60000},
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"test_fixture",true},
            {"source_kind","owned-rgba64-png"},{"source_pixels",size.width()*size.height()},
            {"source_alpha_u16",alpha},{"source_sha256",QString::fromLatin1(s->sourceSha)},
            {"source_unchanged",unchanged},{"settings_isolated",isolated},{"actual_photo_app_UI",true},
            {"controlled_qpa_tablet",true},{"window_focus_ready",s->focus},
            {"physical_tablet_verified",false},{"physical_OS_activation_verified",false},
            {"native_gpu_brush",false},{"native_float_raw_decoder",false},{"production_accepted",false},
            {"continuous_unique_jitter_fixed",false},{"actual_qpa",QGuiApplication::platformName()},
            {"language",c->language()},{"recovered_mode",erase?"erase":"paint"},
            {"gpu_active",c->gpuActive()},{"backend",c->processingBackend()},
            {"injection","Qt-QPA-controlled-pressure-stylus"},{"dpr",window->effectiveDevicePixelRatio()},
            {"held_action_disabled",s->heldDisabled},{"action_enabled_after_lift",s->releaseEnabled},
            {"new_stroke_blocked_until_decision",s->blockedNew},{"brush_controls_blocked_until_decision",s->controlsBlocked},
            {"explicit_apply_one_undo",s->explicitApply},{"canvas_pixel_changed",s->pixelChanged},
            {"undo_restores_pixels_and_history",s->undo},{"redo_restores_pixels_and_history",s->redo},
            {"explicit_discard_unchanged",s->discard},{"discard_preserves_redo",s->discardRedo},
            {"escape_keeps_brush_tool",s->escape},{"brush_controls_restored",s->controlsRestored},
            {"recovered_dabs",qint64(s->recoveredDabs)},
            {"single_stroke_dab_limit",qint64(BrushMaskGeometry::MaxDabsPerStroke)},
            {"document_dab_limit",qint64(BrushMaskDocument::MaxTotalDabs)},
            {"first_rejected_packet",s->refused[0]},{"second_rejected_packet",s->refused[1]},
            {"third_rejected_packet",s->refused[2]},{"captures",s->captures}};
        QSaveFile file(report);const auto bytes=QJsonDocument(out).toJson();
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){
            QCoreApplication::exit(2);return;
        }
        QCoreApplication::exit(passed?0:1);
    };
    QObject::connect(timer,&QTimer::timeout,c,[=,keep=owned] {
        Q_UNUSED(keep);
        if(s->clock.elapsed()>=60000){finish("Recovery UI exceeded 60000ms");return;}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive){
            QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);
            QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;
        }
        if(!window->isActive()){
            QWindowSystemInterface::handleFocusWindowChanged(window);
            QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;
        }
        s->focus=true;if(!c->brushReady())return;
        auto *canvas=item("photoCanvas"),*overlay=item("brushCanvasOverlay");
        if(!canvas||!canvas->property("brushDisplayReady").toBool())return;
        if(s->phase==0){
            s->initial=masks(c);s->initialHistory=history(c);
            if(!s->initial.isEmpty()||!clickItem(window,item("editBrushMaskCanvas"))){finish("Cannot enter empty brush canvas");return;}
            s->phase=1;return;
        }
        if(!overlay||!overlay->isVisible()||!overlay->isEnabled())return;
        auto raw=overlay->property("editor");QObject *editor=raw.value<QObject *>();
        if(!editor&&raw.canConvert<QJSValue>())editor=raw.value<QJSValue>().toQObject();
        if(!editor){finish("Brush editor unavailable");return;}
        const auto point=[&](double x){return overlay->mapToScene({overlay->width()*x,overlay->height()*.6});};
        const auto pixel=[&] {
            return SmokeCaptureCoordinates::sample(window->grabWindow(),
                point(.5)+QPointF(0,overlay->property("radiusY").toDouble()*.2),window->effectiveDevicePixelRatio());
        };
        const auto packet=[&](double x,double pressure,bool held) {
            const auto local=point(x),global=window->mapToGlobal(local);
            QWindowSystemInterface::handleTabletEvent(window,s->stamp+=20,s->pen.get(),
                QHighDpi::toNativePixels(local,window),QHighDpi::toNativePixels(global,window),
                held?Qt::LeftButton:Qt::NoButton,pressure,0,0,0,0,0);
            QWindowSystemInterface::flushWindowSystemEvents();
        };
        const auto exceed=[&](int &refused) {
            packet(.5,.5,true);if(!c->brushDrawing())return false;
            // Unique positions and alternating pressure peaks reproduce the
            // unresolved subpixel-capacity case without relaxing its refusal.
            for(int i=1;i<=9000;++i){
                packet(.5+2e-8*i,i%2?.501:.5,true);
                if(!c->brushDrawing()){refused=i;return true;}
            }
            return false;
        };
        const auto lift=[&](int refused){packet(.5+2e-8*refused,0,false);};
        const auto controlsEnabled=[&] {
            auto *radius=item("brushRadiusSlider"),*mode=item("brushModeSelector"),*layer=item("newBrushLayerCanvas");
            return radius&&radius->isEnabled()&&mode&&mode->isEnabled()&&layer&&layer->isEnabled();
        };
        if(s->phase==10){
            const auto doc=c->gpuPlan(true).state.brushMasks;
            if(doc.layers.size()!=1||doc.layers[0].strokes.size()!=1){finish("Base paint stroke missing");return;}
            s->initial=masks(c);s->initialHistory=history(c);
            if(!selectItem(window,"brushModeSelector",1)||editor->property("mode").toString()!="erase"){
                finish("Cannot select eraser mode");return;
            }
            s->baseReady=true;s->phase=1;return;
        }
        if(s->phase==1){
            editor->setProperty("radius",.04);editor->setProperty("hardness",1);
            editor->setProperty("opacity",.7);editor->setProperty("exposure",1);
            if(erase&&!s->baseReady){
                packet(.5,.8,true);if(!c->brushDrawing()){finish("Cannot start base paint stroke");return;}
                packet(.5,0,false);s->phase=10;return;
            }
            s->before=pixel();
            if(!s->before.isValid()||!capture("before")||!exceed(s->refused[0])){finish("Cannot create first refused gesture");return;}
            auto *panel=item("brushRecoveryPanel"),*apply=item("applyBrushRecoveryButton");
            s->heldDisabled=c->brushRecoveryAvailable()&&overlay->property("inputHeld").toBool()
                &&panel&&panel->isVisible()&&apply&&!apply->isEnabled()
                &&masks(c)==s->initial&&history(c)==s->initialHistory;
            if(!s->heldDisabled||!capture("held")){finish("Recovery was not safely offered while held");return;}
            lift(s->refused[0]);s->phase=2;return;
        }
        if(s->phase==2){
            auto *apply=item("applyBrushRecoveryButton");
            s->releaseEnabled=!overlay->property("inputHeld").toBool()&&apply&&apply->isVisible()&&apply->isEnabled();
            if(!s->releaseEnabled)return;
            packet(.3,.5,true);packet(.3,0,false);
            s->blockedNew=!c->brushDrawing()&&c->brushRecoveryAvailable()
                &&masks(c)==s->initial&&history(c)==s->initialHistory;
            const auto radius=editor->property("radius").toDouble();sendKey(window,Qt::Key_BracketRight);
            auto *slider=item("brushRadiusSlider"),*mode=item("brushModeSelector"),*layer=item("newBrushLayerCanvas");
            s->controlsBlocked=slider&&!slider->isEnabled()&&mode&&!mode->isEnabled()&&layer&&!layer->isEnabled()
                &&editor->property("radius").toDouble()==radius&&c->brushRecoveryAvailable();
            if(!s->blockedNew||!s->controlsBlocked||!capture("available")||!clickItem(window,apply)){
                finish("Cannot preserve or explicitly apply recovery");return;
            }
            const auto doc=c->gpuPlan(true).state.brushMasks;
            if(doc.layers.size()==1&&doc.layers[0].strokes.size()==std::size_t(erase?2:1)){
                const auto &stroke=doc.layers[0].strokes.back();s->recoveredDabs=stroke.dabs.size();
                s->explicitApply=!c->brushRecoveryAvailable()&&s->recoveredDabs>0
                    &&s->recoveredDabs<=BrushMaskGeometry::MaxDabsPerStroke
                    &&stroke.mode==(erase?BrushMaskGeometry::StrokeMode::Erase:BrushMaskGeometry::StrokeMode::Paint)
                    &&history(c)==s->initialHistory+1;
            }
            s->applied=masks(c);
            if(!s->explicitApply){finish("Recovery was not one bounded edit");return;}
            s->phase=3;return;
        }
        if(s->phase==3){
            s->after=pixel();s->pixelChanged=s->after.isValid()&&delta(s->after,s->before)>=3;
            if(!s->pixelChanged)return;
            if(!capture("applied")||!clickItem(window,item("undoEditButton"))){finish("Cannot capture or undo recovered edit");return;}
            s->phase=4;return;
        }
        if(s->phase==4){
            const auto color=pixel();
            s->undo=masks(c)==s->initial&&history(c)==s->initialHistory&&color.isValid()&&delta(color,s->before)<=2;
            if(!s->undo)return;
            s->redoHistory=c->editHistory();
            if(!capture("undo")||!exceed(s->refused[1])){finish("Cannot create refused gesture with redo available");return;}
            lift(s->refused[1]);s->phase=5;return;
        }
        if(s->phase==5){
            auto *discard=item("discardBrushRecoveryButton");
            if(overlay->property("inputHeld").toBool()||!discard||!discard->isEnabled())return;
            if(!capture("discard-offer")||!clickItem(window,discard)){finish("Cannot explicitly discard recovery");return;}
            s->discard=!c->brushRecoveryAvailable()&&masks(c)==s->initial&&history(c)==s->initialHistory;
            s->discardRedo=c->editHistory()==s->redoHistory;
            if(!s->discard||!s->discardRedo){finish("Discard changed edit or redo branch");return;}
            s->phase=6;return;
        }
        if(s->phase==6){
            const auto color=pixel();if(!color.isValid()||delta(color,s->before)>2)return;
            if(!capture("discarded")||!clickItem(window,item("redoEditButton"))){finish("Cannot redo after discarded recovery");return;}
            s->phase=7;return;
        }
        if(s->phase==7){
            const auto color=pixel();
            s->redo=masks(c)==s->applied&&history(c)==s->initialHistory+1&&color.isValid()&&delta(color,s->after)<=2;
            if(!s->redo)return;
            if(!capture("redo")||!exceed(s->refused[2])){finish("Cannot create recovery for Escape");return;}
            lift(s->refused[2]);s->phase=8;return;
        }
        if(s->phase==8){
            auto *discard=item("discardBrushRecoveryButton");
            if(overlay->property("inputHeld").toBool()||!discard||!discard->isEnabled())return;
            if(!capture("escape-offer")){finish("Cannot capture Escape offer");return;}
            const auto before=c->editHistory();sendKey(window,Qt::Key_Escape);
            s->escape=!c->brushRecoveryAvailable()&&masks(c)==s->applied&&c->editHistory()==before
                &&canvas->property("maskTool").toString()=="brush";
            s->controlsRestored=controlsEnabled();
            if(!s->escape||!s->controlsRestored){finish("Escape changed edit or left controls blocked");return;}
            s->phase=9;return;
        }
        if(s->phase==9){
            const auto color=pixel();if(!color.isValid()||delta(color,s->after)>2)return;
            if(!capture("escaped")){finish("Cannot capture escaped recovery");return;}
            finish({});
        }
    });
    c->setLanguage(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")?"zh_CN":"en_US");
    window->setProperty("workspaceIndex",1);window->resize(1180,720);window->requestActivate();
    if(!c->importFile(QUrl::fromLocalFile(source))){finish("Cannot import recovery source");return;}
    timer->start();
}
