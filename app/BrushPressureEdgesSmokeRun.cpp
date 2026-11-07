#include "app/PhotoController.h"
#include "app/SmokeCaptureCoordinates.h"
#include "core/pipeline/BrushMaskGeometry.h"
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
QQuickItem *findEdgeItem(QQuickItem *item,const QString &name) {
    if(!item)return nullptr;
    if(item->objectName()==name)return item;
    for(auto *child:item->childItems())if(auto *found=findEdgeItem(child,name))return found;
    return nullptr;
}
bool clickEdgeItem(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto p=item->mapToScene({item->width()/2,item->height()/2});
    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,p,p,window->mapToGlobal(p),Qt::LeftButton,
                          type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&event);
    }
    return true;
}
void edgeKey(QQuickWindow *window,int code) {
    QKeyEvent down(QEvent::KeyPress,code,Qt::NoModifier),up(QEvent::KeyRelease,code,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&down);QCoreApplication::sendEvent(window,&up);
}
bool selectEdgeItem(QQuickWindow *window,const QString &name,int index) {
    auto *item=findEdgeItem(window->contentItem(),name);if(!clickEdgeItem(window,item))return false;
    edgeKey(window,Qt::Key_Home);for(int i=0;i<index;++i)edgeKey(window,Qt::Key_Down);edgeKey(window,Qt::Key_Return);
    return item->property("currentIndex").toInt()==index;
}
int edgeHistory(PhotoController *c) {
    const auto rows=c->editHistory();
    for(int i=0;i<rows.size();++i)if(rows[i].toMap()["current"].toBool())return i;
    return -1;
}
QJsonObject edgeMasks(PhotoController *c){return c->gpuPlan(true).state.toJson()["brushMasks"].toObject();}
QByteArray edgeHash(const QString &path) {
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};
    QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};
}
int edgeDelta(QColor a,QColor b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});}
}

void startBrushPressureEdgesSmokeRun(PhotoController *c,QQuickWindow *window,const QString &report,bool transparent) {
    namespace BM=BrushMaskGeometry;
    const auto owned=std::make_shared<QTemporaryDir>();
    if(!owned->isValid()){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    const auto source=owned->filePath("pressure-edges-owned.png");
    const QSize size(512,320);const int alpha=transparent?42000:65535;
    QImage image(size,QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(9000+x*20,11000+y*20,16000+x*10,alpha);
    if(!image.save(source)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    struct State {
        QElapsedTimer clock;int phase=0,settle=0,initialHistory=0;ulong stamp=1000;bool done=false;
        quint64 initialRevision=0;QByteArray sourceHash;QJsonObject initial,pulseMasks;
        QVariantList redoHistory;QColor baseline,pulseColor;QJsonArray captures;
        bool focusReady=false,zeroNew=false,zeroNewPixels=false,pulseDelivered=false,pulsePeak=false,
            pulsePixels=false,oneUndo=false,undoPixels=false,zeroRedo=false,redo=false,redoPixels=false,
            zeroExistingPaint=false,zeroExistingErase=false,zeroExistingPixels=false;
        double maxPressure=0;int pulseDelta=0;std::shared_ptr<QPointingDevice> pen;
    };
    auto s=std::make_shared<State>();s->clock.start();s->sourceHash=edgeHash(source);
    s->pen=std::make_shared<QPointingDevice>("Controlled pressure edge pen",987654351,
        QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->pen.get());
    auto *timer=new QTimer(c);timer->setInterval(50);
    const auto item=[=](const QString &name){return findEdgeItem(window->contentItem(),name);};
    const auto capture=[=](const QString &label,const QImage &shot) {
        const auto path=report+"."+label+"-owned.png";
        if(shot.isNull()||!shot.save(path,"PNG",85))return false;
        s->captures.append(QJsonObject{{"label",label},{"path",path},{"width",shot.width()},
            {"height",shot.height()},{"dpr",window->effectiveDevicePixelRatio()},
            {"sha256",QString::fromLatin1(edgeHash(path))}});
        return true;
    };
    const auto finish=[=](const QString &error) {
        if(s->done)return;s->done=true;timer->stop();
        const bool isolated=QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool();
        const bool unchanged=edgeHash(source)==s->sourceHash;
        const bool passed=error.isEmpty()&&isolated&&unchanged&&s->focusReady&&s->zeroNew&&s->zeroNewPixels
            &&s->pulseDelivered&&s->pulsePeak&&s->pulsePixels&&s->oneUndo&&s->undoPixels&&s->zeroRedo
            &&s->redo&&s->redoPixels&&s->zeroExistingPaint&&s->zeroExistingErase&&s->zeroExistingPixels
            &&s->captures.size()==6&&s->clock.elapsed()<60000
            &&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        const QJsonObject out{{"passed",passed},{"error",error},{"elapsed_ms",s->clock.elapsed()},
            {"deadline_ms",60000},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"test_fixture",true},{"source_kind","owned-rgba64-png"},{"source_alpha_u16",alpha},
            {"source_pixels",size.width()*size.height()},{"source_unchanged",unchanged},
            {"source_sha256",QString::fromLatin1(s->sourceHash)},{"settings_isolated",isolated},
            {"actual_photo_app_UI",true},{"controlled_qpa_tablet",true},{"window_focus_ready",s->focusReady},
            {"physical_OS_activation_verified",false},{"physical_tablet_verified",false},
            {"native_gpu_brush",false},{"native_float_raw_decoder",false},{"production_accepted",false},
            {"gpu_active",c->gpuActive()},{"backend",c->processingBackend()},
            {"actual_qpa",QGuiApplication::platformName()},{"injection","Qt-QPA-controlled-pressure-stylus"},
            {"dpr",window->effectiveDevicePixelRatio()},{"zero_new_noop",s->zeroNew},
            {"zero_new_pixels_unchanged",s->zeroNewPixels},{"positive_move_delivered",s->pulseDelivered},
            {"stationary_peak_preserved",s->pulsePeak},{"stationary_peak_pressure",s->maxPressure},
            {"stationary_pixel_changed",s->pulsePixels},{"stationary_pixel_delta",s->pulseDelta},
            {"single_undo",s->oneUndo},{"undo_pixels_restored",s->undoPixels},{"zero_kept_redo",s->zeroRedo},
            {"redo_restored",s->redo},{"redo_pixels_restored",s->redoPixels},
            {"zero_existing_paint_noop",s->zeroExistingPaint},{"zero_existing_erase_noop",s->zeroExistingErase},
            {"zero_existing_pixels_unchanged",s->zeroExistingPixels},{"captures",s->captures}};
        QSaveFile file(report);
        if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(out).toJson())<0||!file.commit()){
            QCoreApplication::exit(2);return;
        }
        QCoreApplication::exit(passed?0:1);
    };
    QObject::connect(timer,&QTimer::timeout,c,[=,keep=owned] {
        Q_UNUSED(keep);
        if(s->clock.elapsed()>=60000){finish("Pressure edge UI exceeded 60000ms");return;}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive){
            QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);
            QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;
        }
        if(!window->isActive()){
            QWindowSystemInterface::handleFocusWindowChanged(window);
            QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;
        }
        s->focusReady=true;
        if(!c->brushReady())return;
        auto *canvas=item("photoCanvas"),*overlay=item("brushCanvasOverlay");
        if(!canvas||!canvas->property("brushDisplayReady").toBool())return;
        if(s->phase==0){
            s->initial=edgeMasks(c);s->initialHistory=edgeHistory(c);s->initialRevision=c->renderRevision();
            if(!s->initial.isEmpty()||!clickEdgeItem(window,item("editBrushMaskCanvas"))){finish("Cannot enter empty brush canvas");return;}
            s->phase=1;s->settle=0;return;
        }
        if(!overlay||!overlay->isVisible()||!overlay->isEnabled())return;
        auto raw=overlay->property("editor");QObject *editor=raw.value<QObject *>();
        if(!editor&&raw.canConvert<QJSValue>())editor=raw.value<QJSValue>().toQObject();
        if(!editor){finish("Brush editor unavailable");return;}
        const auto point=[&](double x){return overlay->mapToScene({overlay->width()*x,overlay->height()*.6});};
        const auto probe=[&]{return point(.5)+QPointF(0,overlay->property("radiusY").toDouble()*.2);};
        const auto pixel=[&](const QImage &shot){return SmokeCaptureCoordinates::sample(shot,probe(),window->effectiveDevicePixelRatio());};
        const auto packet=[&](double x,double pressure,bool held){
            const auto local=point(x),global=window->mapToGlobal(local);
            QWindowSystemInterface::handleTabletEvent(window,s->stamp+=20,s->pen.get(),
                QHighDpi::toNativePixels(local,window),QHighDpi::toNativePixels(global,window),
                held?Qt::LeftButton:Qt::NoButton,pressure,0,0,0,0,0);
            QWindowSystemInterface::flushWindowSystemEvents();
        };
        const auto zeroStroke=[&]{
            packet(.3,0,true);if(!c->brushDrawing())return false;
            packet(.7,0,true);if(!c->brushDrawing())return false;
            packet(.7,0,false);return !c->brushDrawing();
        };
        if(s->phase==1){
            if(++s->settle<2)return;
            editor->setProperty("radius",.2);editor->setProperty("hardness",1);
            editor->setProperty("opacity",1);editor->setProperty("exposure",1);
            s->initialRevision=c->renderRevision();
            const auto shot=window->grabWindow();s->baseline=pixel(shot);
            if(!s->baseline.isValid()||!capture("before",shot)){finish("Cannot capture initial canvas");return;}
            s->zeroNew=zeroStroke()&&edgeMasks(c)==s->initial&&edgeHistory(c)==s->initialHistory
                &&c->renderRevision()==s->initialRevision&&editor->property("layerId").toString().isEmpty();
            if(!s->zeroNew){finish("Zero-pressure stroke changed empty layer or history");return;}
            s->phase=2;s->settle=0;return;
        }
        if(s->phase==2){
            if(++s->settle<2)return;
            const auto shot=window->grabWindow();const auto color=pixel(shot);
            s->zeroNewPixels=color.isValid()&&edgeDelta(color,s->baseline)<=2;
            if(!s->zeroNewPixels||!capture("zero-new",shot)){finish("Zero-pressure canvas changed");return;}
            packet(.5,0,true);if(!c->brushDrawing()){finish("Zero-start pulse did not begin");return;}
            packet(.5,.8,true);s->pulseDelivered=c->brushDrawing()&&std::abs(overlay->property("currentPressure").toDouble()-.8)<1e-6;
            packet(.5,0,false);if(!s->pulseDelivered||c->brushDrawing()){finish("Positive pulse was not delivered or did not finish");return;}
            s->phase=3;s->settle=0;return;
        }
        if(s->phase==3){
            if(++s->settle<2)return;
            const auto doc=c->gpuPlan(true).state.brushMasks;
            if(doc.layers.size()!=1||doc.layers[0].strokes.size()!=1){finish("Positive pulse did not produce one stroke");return;}
            const auto &stroke=doc.layers[0].strokes[0];for(const auto &dab:stroke.dabs)s->maxPressure=std::max(s->maxPressure,dab.pressure);
            const auto opacity=BM::strokeOpacity(stroke,{.5,.6},size.width(),size.height());
            s->pulsePeak=std::abs(s->maxPressure-.8)<1e-9&&opacity&&*opacity>0;
            s->oneUndo=edgeHistory(c)==s->initialHistory+1&&edgeMasks(c)!=s->initial;
            const auto shot=window->grabWindow();s->pulseColor=pixel(shot);
            s->pulseDelta=s->pulseColor.isValid()?edgeDelta(s->pulseColor,s->baseline):0;s->pulsePixels=s->pulseDelta>=3;
            s->pulseMasks=edgeMasks(c);
            if(!s->pulsePeak||!s->oneUndo||!s->pulsePixels||!capture("pulse",shot)||!clickEdgeItem(window,item("undoEditButton"))){finish("Stationary pulse lost coverage, visible pixel or history");return;}
            s->phase=4;s->settle=0;return;
        }
        if(s->phase==4){
            if(++s->settle<2)return;
            const auto shot=window->grabWindow();const auto color=pixel(shot);
            s->undoPixels=color.isValid()&&edgeDelta(color,s->baseline)<=2&&edgeMasks(c)==s->initial&&edgeHistory(c)==s->initialHistory;
            if(!s->undoPixels||!capture("undo",shot)){finish("Pulse undo did not restore canvas");return;}
            const auto beforeHistory=c->editHistory();const auto revision=c->renderRevision();
            s->zeroRedo=zeroStroke()&&edgeMasks(c)==s->initial&&edgeHistory(c)==s->initialHistory
                &&c->editHistory()==beforeHistory&&c->renderRevision()==revision;
            if(!s->zeroRedo||!clickEdgeItem(window,item("redoEditButton"))){finish("Zero stroke lost redo history");return;}
            s->phase=5;s->settle=0;return;
        }
        if(s->phase==5){
            if(++s->settle<2)return;
            const auto shot=window->grabWindow();const auto color=pixel(shot);
            s->redo=edgeMasks(c)==s->pulseMasks&&edgeHistory(c)==s->initialHistory+1;
            s->redoPixels=color.isValid()&&edgeDelta(color,s->pulseColor)<=2;
            if(!s->redo||!s->redoPixels||!capture("redo",shot)||!selectEdgeItem(window,"brushLayerSelector",1)){
                finish("Pulse redo or layer selection failed");return;
            }
            s->phase=6;s->settle=0;return;
        }
        if(s->phase==6){
            if(++s->settle<2)return;
            const auto before=edgeMasks(c);const int history=edgeHistory(c);const auto revision=c->renderRevision();
            s->zeroExistingPaint=zeroStroke()&&edgeMasks(c)==before&&edgeHistory(c)==history&&c->renderRevision()==revision;
            if(!s->zeroExistingPaint||!selectEdgeItem(window,"brushModeSelector",1)){finish("Zero paint changed an existing layer or mode selection failed");return;}
            s->phase=7;s->settle=0;return;
        }
        if(s->phase==7){
            if(++s->settle<2)return;
            const auto before=edgeMasks(c);const int history=edgeHistory(c);const auto revision=c->renderRevision();
            s->zeroExistingErase=zeroStroke()&&edgeMasks(c)==before&&edgeHistory(c)==history&&c->renderRevision()==revision;
            if(!s->zeroExistingErase){finish("Zero erase changed an existing layer");return;}
            s->phase=8;s->settle=0;return;
        }
        if(s->phase==8){
            if(++s->settle<2)return;
            const auto shot=window->grabWindow();const auto color=pixel(shot);
            s->zeroExistingPixels=color.isValid()&&edgeDelta(color,s->pulseColor)<=2&&edgeMasks(c)==s->pulseMasks;
            if(!s->zeroExistingPixels||!capture("zero-existing",shot)){finish("Zero stroke changed an existing canvas");return;}
            finish({});
        }
    });
    window->setProperty("workspaceIndex",1);window->resize(1280,800);window->requestActivate();
    c->setLanguage("en_US");c->setExactScopes(false);
    if(!c->importFile(QUrl::fromLocalFile(source))){finish("Cannot import isolated pressure edge fixture");return;}
    timer->start();
}
