#include "app/PhotoController.h"
#include "app/SmokeCaptureCoordinates.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QDir>
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
#include <memory>
namespace {
QQuickItem *findItem(QQuickItem *item,const QString &name) {
    if(!item)return nullptr;if(item->objectName()==name)return item;
    for(auto *child:item->childItems())if(auto *found=findItem(child,name))return found;return nullptr;
}
void reveal(QQuickItem *item) {
    for(auto *parent=item?item->parentItem():nullptr;parent;parent=parent->parentItem()) {
        if(!parent->inherits("QQuickFlickable"))continue;
        auto *content=parent->property("contentItem").value<QQuickItem*>();if(!content)continue;
        const auto pos=item->mapToItem(content,{0,0});const double current=parent->property("contentY").toDouble();
        const double maximum=std::max(0.,parent->property("contentHeight").toDouble()-parent->height());
        if(pos.y()<current || pos.y()+item->height()>current+parent->height())
            parent->setProperty("contentY",std::clamp(pos.y()-parent->height()/3,0.,maximum));
    }
}
bool click(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled())return false;reveal(item);
    const auto p=item->mapToScene({item->width()/2,item->height()/2});
    if(p.x()<0||p.y()<0||p.x()>=window->width()||p.y()>=window->height())return false;
    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent e(type,p,p,window->mapToGlobal(p),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&e);
    }
    return true;
}
void key(QQuickWindow *window,int code) {QKeyEvent a(QEvent::KeyPress,code,Qt::NoModifier),b(QEvent::KeyRelease,code,Qt::NoModifier);QCoreApplication::sendEvent(window,&a);QCoreApplication::sendEvent(window,&b);}
bool select(QQuickWindow *w,QQuickItem *i,int index) {if(!click(w,i))return false;key(w,Qt::Key_Home);for(int n=0;n<index;++n)key(w,Qt::Key_Down);key(w,Qt::Key_Return);return i->property("currentIndex").toInt()==index;}
int history(PhotoController *c) {const auto rows=c->editHistory();for(int i=0;i<rows.size();++i)if(rows[i].toMap()["current"].toBool())return i;return -1;}
QJsonObject masks(PhotoController *c) {return c->gpuPlan(true).state.toJson()["brushMasks"].toObject();}
QByteArray sha(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
int delta(QColor a,QColor b) {return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});}
}
// Exercise the actual application with an explicitly selected pen input rule.
void startBrushStabilizationSmokeRun(PhotoController *c,QQuickWindow *window,const QString &report,bool transparent,bool erase) {
    const QFileInfo destination(report);const QDir directory(destination.absolutePath());
    if(report.isEmpty()||QFileInfo::exists(report)||destination.isSymLink()
        ||!directory.entryList({destination.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty()) {
        QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;
    }
    const auto owned=std::make_shared<QTemporaryDir>();if(!owned->isValid()){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    const QSize size(512,320);const QString source=owned->filePath("stabilizer-owned.png");QImage image(size,QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)reinterpret_cast<QRgba64*>(image.scanLine(y))[x]=QRgba64::fromRgba64(9000+x*20,11000+y*20,16000+x*10,transparent?42000:65535);
    if(!image.save(source)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    struct State {
        QElapsedTimer clock;int phase=0,initialHistory=0,rawRefused=0;ulong stamp=1000;
        bool done=false,baseReady=false,focus=false,defaultOff=false,explicitSelection=false,heldDisabled=false,
            allPackets=false,singleUndo=false,changed=false,undo=false,redo=false,mouseRaw=false,offRefusal=false,escaped=false;
        QByteArray sourceSha;QJsonObject initial,applied,mouse;QColor before,after;QPointF mouseStart,mouseEnd;QJsonArray captures,selectedDabs,mouseActual,mouseExpected;double mouseDistance=0;std::size_t stableDabs=0;
        std::shared_ptr<QPointingDevice> pen;
    };
    auto s=std::make_shared<State>();s->clock.start();s->sourceSha=sha(source);
    s->pen=std::make_shared<QPointingDevice>("Controlled opt-in stable pen",987654412,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);QWindowSystemInterface::registerInputDevice(s->pen.get());
    auto *timer=new QTimer(c);timer->setInterval(50);
    const auto item=[=](const QString &n){return findItem(window->contentItem(),n);};
    const auto capture=[=](const QString &label) {
        auto image=window->grabWindow();const auto path=report+"."+label+"-owned.png";if(image.isNull()||!image.save(path,"PNG",85))return false;
        s->captures.append(QJsonObject{{"label",label},{"path",path},{"width",image.width()},{"height",image.height()},{"dpr",window->effectiveDevicePixelRatio()},{"sha256",QString::fromLatin1(sha(path))}});return true;
    };
    const auto finish=[=](const QString &error) {
        if(s->done)return;s->done=true;timer->stop();if(!error.isEmpty())capture("failure");
        const bool isolated=QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool();
        const bool passed=error.isEmpty()&&isolated&&sha(source)==s->sourceSha&&s->focus&&s->defaultOff&&s->explicitSelection&&s->heldDisabled&&s->allPackets&&s->singleUndo&&s->changed&&s->undo&&s->redo&&s->mouseRaw&&s->offRefusal&&s->escaped&&s->captures.size()==8&&s->clock.elapsed()<60000&&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        QJsonObject out{{"passed",passed},{"error",error},{"phase",s->phase},{"elapsed_ms",s->clock.elapsed()},{"deadline_ms",60000},
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"test_fixture",true},{"actual_photo_app_UI",true},{"controlled_qpa_tablet",true},{"window_focus_ready",s->focus},{"settings_isolated",isolated},{"source_unchanged",sha(source)==s->sourceSha},
            {"source_width",512},{"source_height",320},{"source_kind","owned-rgba64-png"},{"source_pixels",512*320},{"source_alpha_u16",transparent?42000:65535},{"source_sha256",QString::fromLatin1(s->sourceSha)},
            {"actual_qpa",QGuiApplication::platformName()},{"dpr",window->effectiveDevicePixelRatio()},{"backend",c->processingBackend()},{"injection","Qt-QPA-controlled-pressure-stylus"},{"window_focus_injection","Qt-QPA-controlled-window-focus"},{"gpu_active",c->gpuActive()},{"language",c->language()},{"mode",erase?"erase":"paint"},
            {"actual_app_controlled_QPA",true},{"default_off",s->defaultOff},{"explicit_checkbox_selected",s->explicitSelection},{"checkbox_disabled_while_held",s->heldDisabled},
            {"all9000_packets_accepted_when_selected",s->allPackets},{"selected_final_dabs",qint64(s->stableDabs)},{"single_undo",s->singleUndo},{"canvas_pixel_changed",s->changed},
            {"undo_pixel_and_history_restored",s->undo},{"redo_pixel_and_history_restored",s->redo},{"selected_checkbox_keeps_mouse_raw",s->mouseRaw},{"mouse_actual_dabs",s->mouseActual},{"mouse_expected_dabs",s->mouseExpected},
            {"mouse_start_source",QJsonArray{s->mouseStart.x(),s->mouseStart.y()}},{"mouse_end_source",QJsonArray{s->mouseEnd.x(),s->mouseEnd.y()}},{"mouse_move_source_pixels",s->mouseDistance},
            {"selected_dabs",s->selectedDabs},{"selected_style",QJsonObject{{"radius",.04},{"hardness",1},{"opacity",.7}}},
            {"single_stroke_dab_limit",qint64(BrushMaskGeometry::MaxDabsPerStroke)},{"document_dab_limit",qint64(BrushMaskDocument::MaxTotalDabs)},
            {"position_deadband_source_pixels",.5},{"selected_input_rule","half-image-pixel-anchor"},{"off_mode_original_refusal",s->offRefusal},
            {"off_mode_first_refused_packet",s->rawRefused},{"escape_discards_offer_keeps_brush",s->escaped},{"captures",s->captures},
            {"raw_unfiltered_jitter_fixed",false},{"selected_rule_changes_raw_intent",true},{"physical_tablet_verified",false},{"stabilizer_specific_Sony_verified",false},{"production_accepted",false},{"physical_OS_activation_verified",false},{"native_gpu_brush",false},{"native_float_raw_decoder",false},{"continuous_unique_jitter_fixed",false}};
        QSaveFile file(report);const auto bytes=QJsonDocument(out).toJson();const bool saved=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();QCoreApplication::exit(passed&&saved?0:1);
    };
    QObject::connect(timer,&QTimer::timeout,c,[=,keep=owned] {
        Q_UNUSED(keep);if(s->clock.elapsed()>=60000){finish("Pen stabilization UI timed out");return;}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive){QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        if(!window->isActive()){QWindowSystemInterface::handleFocusWindowChanged(window);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        s->focus=true;if(!c->brushReady())return;auto *canvas=item("photoCanvas"),*overlay=item("brushCanvasOverlay");if(!canvas||!canvas->property("brushDisplayReady").toBool())return;
        if(s->phase==0){if(!click(window,item("editBrushMaskCanvas"))){finish("Cannot enter brush canvas");return;}s->phase=1;return;}
        if(!overlay||!overlay->isVisible()||!overlay->isEnabled())return;
        auto raw=overlay->property("editor");QObject *editor=raw.value<QObject*>();if(!editor&&raw.canConvert<QJSValue>())editor=raw.value<QJSValue>().toQObject();if(!editor){finish("Missing brush editor");return;}
        auto *choice=item("brushPenStabilization");
        const auto local=[&](double x){return QPointF(overlay->width()*x,overlay->height()*.6);};
        const auto scene=[&](double x){return overlay->mapToScene(local(x));};
        const auto pixel=[&]{return SmokeCaptureCoordinates::sample(window->grabWindow(),scene(.5)+QPointF(0,overlay->property("radiusY").toDouble()*.2),window->effectiveDevicePixelRatio());};
        const auto packet=[&](double x,double pressure,bool held){auto p=scene(x),g=window->mapToGlobal(p);QWindowSystemInterface::handleTabletEvent(window,s->stamp+=20,s->pen.get(),QHighDpi::toNativePixels(p,window),QHighDpi::toNativePixels(g,window),held?Qt::LeftButton:Qt::NoButton,pressure,0,0,0,0,0);QWindowSystemInterface::flushWindowSystemEvents();};
        if(s->phase==7){const auto doc=c->gpuPlan(true).state.brushMasks;if(doc.layers.size()!=1||doc.layers[0].strokes.size()!=1){finish("Missing erase base");return;}if(!select(window,item("brushModeSelector"),1)||editor->property("mode").toString()!="erase"){finish("Cannot select erase");return;}s->baseReady=true;s->phase=1;return;}
        if(s->phase==1){
            if(!choice||!choice->isVisible()||!choice->isEnabled())return;s->defaultOff=!choice->property("checked").toBool()&&!editor->property("stabilizePen").toBool();if(!s->defaultOff){finish("Stable pen was not default off");return;}
            editor->setProperty("radius",.04);editor->setProperty("hardness",1);editor->setProperty("opacity",.7);editor->setProperty("exposure",1);
            if(erase&&!s->baseReady){packet(.5,.8,true);if(!c->brushDrawing()){finish("Cannot create erase base");return;}packet(.5,0,false);s->phase=7;return;}
            s->initial=masks(c);s->initialHistory=history(c);s->before=pixel();if(!s->before.isValid()||!click(window,choice)){finish("Cannot select stable pen checkbox");return;}
            s->explicitSelection=choice->property("checked").toBool()&&editor->property("stabilizePen").toBool();if(!s->explicitSelection||!capture("selected")){finish("Stable pen choice did not bind");return;}
            packet(.5,.5,true);if(!c->brushDrawing()){finish("Selected pen start refused");return;}s->allPackets=true;
            for(int i=1;i<=9000;++i){packet(.5+2e-8*i,i%2?.501:.5,true);if(!c->brushDrawing()){s->allPackets=false;break;}}
            s->heldDisabled=!choice->isEnabled()&&choice->property("checked").toBool()&&overlay->property("inputHeld").toBool()&&!c->brushRecoveryAvailable()&&masks(c)==s->initial&&history(c)==s->initialHistory;
            if(!s->allPackets||!s->heldDisabled||!capture("held")){finish("Selected stream failed or choice changed while held");return;}
            packet(.5+2e-8*9000,0,false);const auto doc=c->gpuPlan(true).state.brushMasks;
            s->singleUndo=!c->brushDrawing()&&!c->brushRecoveryAvailable()&&doc.layers.size()==1&&doc.layers[0].strokes.size()==size_t(erase?2:1)&&history(c)==s->initialHistory+1;
            if(s->singleUndo){const auto &stroke=doc.layers[0].strokes.back();s->stableDabs=stroke.dabs.size();s->singleUndo=s->stableDabs==2&&stroke.mode==(erase?BrushMaskGeometry::StrokeMode::Erase:BrushMaskGeometry::StrokeMode::Paint)&&stroke.dabs[0].pressure==.501&&stroke.dabs[1].pressure==0;
                for(const auto &dab:stroke.dabs)s->selectedDabs.append(QJsonArray{dab.center.x,dab.center.y,dab.pressure});}
            s->applied=masks(c);if(!s->singleUndo){finish("Selected stream was not one ordinary bounded stroke");return;}s->phase=2;return;
        }
        if(s->phase==2){s->after=pixel();s->changed=s->after.isValid()&&delta(s->after,s->before)>=3;if(!s->changed)return;if(!capture("applied")||!click(window,item("undoEditButton"))){finish("Cannot capture/undo selected pen stroke");return;}s->phase=3;return;}
        if(s->phase==3){s->undo=masks(c)==s->initial&&history(c)==s->initialHistory&&pixel().isValid()&&delta(pixel(),s->before)==0;if(!s->undo)return;if(!capture("undo")||!click(window,item("redoEditButton"))){finish("Cannot redo selected pen stroke");return;}s->phase=4;return;}
        if(s->phase==4){s->redo=masks(c)==s->applied&&history(c)==s->initialHistory+1&&pixel().isValid()&&delta(pixel(),s->after)==0;if(!s->redo)return;if(!capture("redo")){finish("Cannot capture redo");return;}
            const QString mouseTargetBefore=editor->property("layerId").toString();
            editor->setProperty("radius",.002);const auto startLocal=local(.4),endLocal=local(.4+.49/512);
            const auto mouse=[&](QEvent::Type type,QPointF p){auto point=overlay->mapToScene(p);QMouseEvent e(type,point,point,window->mapToGlobal(point),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&e);};
            mouse(QEvent::MouseButtonPress,startLocal);const bool mouseBegan=c->brushDrawing();const auto actualStart=overlay->property("cursorPoint").toPointF();
            mouse(QEvent::MouseMove,endLocal);const bool mouseMoved=c->brushDrawing();const auto actualEnd=overlay->property("cursorPoint").toPointF();
            mouse(QEvent::MouseButtonRelease,endLocal);const auto doc=c->gpuPlan(true).state.brushMasks;
            s->mouseStart=c->maskCanvasPoint(actualStart.x(),actualStart.y(),overlay->width(),overlay->height());
            s->mouseEnd=c->maskCanvasPoint(actualEnd.x(),actualEnd.y(),overlay->width(),overlay->height());
            s->mouseDistance=std::hypot((s->mouseEnd.x()-s->mouseStart.x())*size.width(),(s->mouseEnd.y()-s->mouseStart.y())*size.height());
            BrushMaskGeometry::StrokeBuilder oracle(size.width(),size.height(),{.002,1,.7},erase?BrushMaskGeometry::StrokeMode::Erase:BrushMaskGeometry::StrokeMode::Paint);
            const bool oracleOkay=oracle.append({{s->mouseStart.x(),s->mouseStart.y()},1})&&oracle.append({{s->mouseEnd.x(),s->mouseEnd.y()},1})&&oracle.append({{s->mouseEnd.x(),s->mouseEnd.y()},1});const auto expected=oracle.finish();
            if(expected)for(const auto &dab:expected->dabs)s->mouseExpected.append(QJsonArray{dab.center.x,dab.center.y,dab.pressure});
            const QString mouseTargetAfter=editor->property("layerId").toString();
            const auto target=std::find_if(doc.layers.begin(),doc.layers.end(),[&](const auto &layer){return layer.id==mouseTargetAfter;});
            s->mouseRaw=mouseBegan&&mouseMoved&&s->mouseDistance>0&&s->mouseDistance<=.5&&oracleOkay&&expected&&expected->dabs.size()>1&&choice->property("checked").toBool()&&history(c)==s->initialHistory+2&&target!=doc.layers.end()&&!target->strokes.empty();
            if(s->mouseRaw){
                const auto &stroke=target->strokes.back();for(const auto &dab:stroke.dabs)s->mouseActual.append(QJsonArray{dab.center.x,dab.center.y,dab.pressure});s->mouseRaw=stroke.dabs.size()==expected->dabs.size();
                for(size_t i=0;s->mouseRaw&&i<stroke.dabs.size();++i)s->mouseRaw=stroke.dabs[i].center.x==expected->dabs[i].center.x&&stroke.dabs[i].center.y==expected->dabs[i].center.y&&stroke.dabs[i].pressure==expected->dabs[i].pressure;
                auto remainder=doc;const auto index=size_t(target-doc.layers.begin());
                if(mouseTargetBefore.isEmpty()) {s->mouseRaw=s->mouseRaw&&target->strokes.size()==1;remainder.layers.erase(remainder.layers.begin()+index);}
                else {s->mouseRaw=s->mouseRaw&&mouseTargetBefore==mouseTargetAfter;remainder.layers[index].strokes.pop_back();}
                QJsonObject restored;s->mouseRaw=s->mouseRaw&&BrushMaskDocument::toJson(remainder,&restored)&&restored==s->applied;
            }
            if(!s->mouseRaw){finish("Checkbox selection changed the raw mouse path");return;}s->mouse=masks(c);s->phase=5;return;
        }
        if(s->phase==5){if(!capture("mouse")||!click(window,choice)||choice->property("checked").toBool()||editor->property("stabilizePen").toBool()){finish("Cannot turn selected rule off");return;}
            editor->setProperty("radius",.04);packet(.5,.5,true);if(!c->brushDrawing()){finish("Raw pen start refused too early");return;}
            for(int i=1;i<=9000;++i){packet(.5+2e-8*i,i%2?.501:.5,true);if(!c->brushDrawing()){s->rawRefused=i;break;}}
            s->offRefusal=s->rawRefused>=8000&&s->rawRefused<=9000&&c->brushRecoveryAvailable()&&masks(c)==s->mouse&&history(c)==s->initialHistory+2&&!choice->isEnabled();
            if(!s->offRefusal||!capture("raw-refused")){finish("Turning rule off bypassed original capacity refusal");return;}packet(.5+2e-8*s->rawRefused,0,false);s->phase=6;return;
        }
        if(s->phase==6){if(overlay->property("inputHeld").toBool())return;key(window,Qt::Key_Escape);s->escaped=!c->brushRecoveryAvailable()&&masks(c)==s->mouse&&history(c)==s->initialHistory+2&&editor->property("active").toBool()&&choice->isEnabled()&&!choice->property("checked").toBool();if(!s->escaped||!capture("finished")){finish("Cannot discard raw offer while retaining brush");return;}finish({});}
    });
    c->setLanguage(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")?"zh_CN":"en_US");window->setProperty("workspaceIndex",1);window->resize(1180,720);window->requestActivate();
    if(!c->importFile(QUrl::fromLocalFile(source))){finish("Cannot import owned stabilizer image");return;}timer->start();
}
