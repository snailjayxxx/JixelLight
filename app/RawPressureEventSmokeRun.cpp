#include "app/PhotoController.h"
#include "app/SmokeRun.h"
#include "core/pipeline/ProcessingPlan.h"
#include "tests/brush_tile_oracle/BrushTileOriginalPipeline.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QUrl>
#include <QPointingDevice>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QJSValue>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QColorSpace>
#include <QRgba64>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>

#ifndef JIXELLIGHT_GIT_COMMIT
#define JIXELLIGHT_GIT_COMMIT "unknown"
#endif
namespace {
QQuickItem *findItem(QQuickItem *item,const QString &name) {
    if(!item)return nullptr;if(item->objectName()==name)return item;
    for(auto *child:item->childItems())if(auto *found=findItem(child,name))return found;return nullptr;
}
void reveal(QQuickItem *item) {
    for(auto *parent=item?item->parentItem():nullptr;parent;parent=parent->parentItem()) {
        if(!parent->inherits("QQuickFlickable"))continue;
        auto *content=parent->property("contentItem").value<QQuickItem*>();if(!content)continue;
        const auto p=item->mapToItem(content,{0,0});const auto y=parent->property("contentY").toDouble();
        const auto maximum=std::max(0.,parent->property("contentHeight").toDouble()-parent->height());
        if(p.y()<y||p.y()+item->height()>y+parent->height())parent->setProperty("contentY",std::clamp(p.y()-parent->height()/3,0.,maximum));
    }
}
bool click(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled())return false;reveal(item);
    const auto p=item->mapToScene({item->width()/2,item->height()/2});
    if(p.x()<0||p.y()<0||p.x()>=window->width()||p.y()>=window->height())return false;
    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,p,p,window->mapToGlobal(p),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&event);
    }return true;
}
void key(QQuickWindow *window,int code) {
    QKeyEvent down(QEvent::KeyPress,code,Qt::NoModifier),up(QEvent::KeyRelease,code,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&down);QCoreApplication::sendEvent(window,&up);
}
bool selectMode(QQuickWindow *w,QQuickItem *item,int index) {
    if(!click(w,item))return false;key(w,Qt::Key_Home);for(int i=0;i<index;++i)key(w,Qt::Key_Down);key(w,Qt::Key_Return);return item->property("currentIndex").toInt()==index;
}
int history(PhotoController *c) {const auto rows=c->editHistory();for(int i=0;i<rows.size();++i)if(rows[i].toMap()["current"].toBool())return i;return -1;}
QByteArray fileHash(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
QByteArray pixelBytes(const QImage &im) {QByteArray bytes;for(int y=0;y<im.height();++y)bytes.append(reinterpret_cast<const char*>(im.constScanLine(y)),im.width()*im.depth()/8);return bytes;}
QByteArray hash(const QByteArray &bytes) {return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();}
bool writeJson(const QString &path,const QJsonObject &object) {
    if(QFileInfo::exists(path)||QFileInfo(path).isSymLink())return false;
    QSaveFile f(path);const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Indented);
    return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
}
bool sameStroke(const BrushMaskGeometry::Stroke &a,const BrushMaskGeometry::Stroke &b) {
    if(a.mode!=b.mode||a.dabs.size()!=b.dabs.size())return false;
    for(auto pair:{std::pair{a.style.radius,b.style.radius},std::pair{a.style.hardness,b.style.hardness},std::pair{a.style.opacity,b.style.opacity}})
        if(std::bit_cast<quint64>(pair.first)!=std::bit_cast<quint64>(pair.second))return false;
    for(std::size_t i=0;i<a.dabs.size();++i)for(auto pair:{std::pair{a.dabs[i].center.x,b.dabs[i].center.x},std::pair{a.dabs[i].center.y,b.dabs[i].center.y},std::pair{a.dabs[i].pressure,b.dabs[i].pressure}})
        if(std::bit_cast<quint64>(pair.first)!=std::bit_cast<quint64>(pair.second))return false;
    return true;
}
}
class RawPressureEventProbe {
public:
static void start(PhotoController *c,QQuickWindow *window,const QString &report,const QString &language) {
    const QFileInfo output(report);const QDir directory(output.absolutePath());
    if(output.suffix().compare("json",Qt::CaseInsensitive)!=0||output.exists()||output.isSymLink()
       ||!directory.exists()||!directory.entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty()) {
        QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;
    }
    constexpr int Packets=7003;const QSize size(307,211);const QString source=report+".source.png";
    QImage fixture(size,QImage::Format_RGBA64);
    fixture.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)
        reinterpret_cast<QRgba64*>(fixture.scanLine(y))[x]=QRgba64::fromRgba64(9000+x*61,13000+y*71,17000+(x+y)*19,(x+2*y)%17?quint16(47000):quint16(0));
    if(!fixture.save(source)){QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;}
    struct State {
        QElapsedTimer clock;int phase=0,round=0,next=0,beforeHistory=0,guiTicks=0,ticksAtStart=0;ulong stamp=2000;
        int presentedFrames=0,stableSinceFrame=-1;QJsonArray stableSignature,warmups;
        bool done=false,baseline=false;quint64 gesture=0,epoch=0,catalog=0,firstPublished=0,finalPublished=0; qint64 sourceKey=0;QString photo;
        double maximumSendMs=0;QPointF lastLocal,lastMapped;QByteArray sourceSha;QJsonObject before,applied;
        QJsonArray packets,rounds,phases;std::set<std::pair<double,double>> positions;
        std::unique_ptr<BrushMaskGeometry::StrokeBuilder> reference;
        std::optional<BrushMaskGeometry::Stroke> expected;
        std::shared_ptr<QPointingDevice> pen;QTimer *timer=nullptr,*ticks=nullptr;
    };
    auto s=std::make_shared<State>();s->clock.start();s->sourceSha=fileHash(source);
    s->pen=std::make_shared<QPointingDevice>("Controlled finite raw pressure trace",987654521,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->pen.get());
    s->timer=new QTimer(c);s->timer->setInterval(4);s->ticks=new QTimer(c);s->ticks->setInterval(5);
    QObject::connect(s->ticks,&QTimer::timeout,c,[s]{++s->guiTicks;});s->ticks->start();
    QObject::connect(window,&QQuickWindow::frameSwapped,c,[s]{++s->presentedFrames;},Qt::QueuedConnection);
    const auto doc=[c]{return c->gpuPlan(true).state.toJson()["brushMasks"].toObject();};
    const auto item=[window](const QString &name){return findItem(window->contentItem(),name);};
    const auto finish=[=](const QString &error) {
        if(s->done)return;s->done=true;s->timer->stop();s->ticks->stop();
        const bool isolated=qApp->property("jixellightSmokeSettingsIsolated").toBool();
        const bool passed=error.isEmpty()&&s->rounds.size()==2&&s->warmups.size()==2&&isolated&&fileHash(source)==s->sourceSha&&s->clock.elapsed()<60000
            &&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        QJsonObject waiting{{"gesture_current",c->brushGestureCurrent()},
            {"gesture_present",bool(c->m_brushGesture)},{"published_sample",QString::number(c->m_brushPreviewSample)},
            {"worker_busy",c->m_brushPreviewJob&&c->m_brushPreviewJob->busy()},
            {"generation",QString::number(c->m_prepareGeneration)},{"revision",QString::number(c->m_requestedRevision)},
            {"source_key",QString::number(c->m_fullSource.cacheKey())},{"presented_frames",s->presentedFrames}};
        if(c->m_brushGesture)waiting["gesture_sample"]=QString::number(c->m_brushGesture->sample);
        const QJsonObject result{{"stable_layout_warmups",s->warmups},{"waiting_state",waiting},{"passed",passed},{"error",error},{"phase",s->phase},{"round",s->round},{"elapsed_ms",s->clock.elapsed()},{"deadline_ms",60000},
            {"actual_backend",c->processingBackend()},{"dpr",window->devicePixelRatio()},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"language",language},{"actual_qpa",QGuiApplication::platformName()},
            {"actual_photo_app_UI",true},{"controlled_qpa_tablet",true},{"settings_isolated",isolated},{"source_path",source},{"source_file_sha256",QString::fromLatin1(s->sourceSha)},
            {"source_unchanged",fileHash(source)==s->sourceSha},{"GPU_active",c->gpuActive()},{"GUI_ticks",s->guiTicks},{"maximum_send_ms",s->maximumSendMs},
            {"rounds",s->rounds},{"phases",s->phases},{"physical_tablet_verified",false},{"physical_OS_activation_verified",false},
            {"arbitrary_or_unbounded_raw_jitter_accepted",false},{"normal_Root_or_CI_accepted",false},{"native_float_RAW_decoder",false},{"product_development_complete",false}};
        const bool saved=writeJson(report,result);QTimer::singleShot(0,c,[passed,saved]{QCoreApplication::exit(passed&&saved?0:1);});
    };
    const auto send=[=](QQuickItem *overlay,QPointF local,double pressure,bool held) {
        const auto scene=overlay->mapToScene(local);const auto global=window->mapToGlobal(scene);QElapsedTimer elapsed;elapsed.start();
        QWindowSystemInterface::handleTabletEvent(window,s->stamp+=2,s->pen.get(),QHighDpi::toNativePixels(scene,window),QHighDpi::toNativePixels(global,window),held?Qt::LeftButton:Qt::NoButton,pressure,0,0,0,0,0);
        QWindowSystemInterface::flushWindowSystemEvents();s->maximumSendMs=std::max(s->maximumSendMs,elapsed.nsecsElapsed()/1e6);
    };
    const auto append=[=](QQuickItem *overlay,QObject *editor,int index) {
        const double t=double(index)/(Packets-1);
        const QPointF local(overlay->width()*(.47+.35/size.width()*t),overlay->height()*(.53+.125/size.height()*std::sin(index*.121)));
        constexpr double pressures[]={.25,.875,.375,1.,.125,.75,.5,0.};const double pressure=index==Packets-1?.125:pressures[index%8];
        send(overlay,local,pressure,true);
        if(!c->m_brushGesture||c->m_brushGesture->movementStabilizer||editor->property("stabilizePen").toBool()
           ||c->brushRecoveryAvailable()||c->m_brushGesture->sample!=quint64(index+1))return QString("Raw event not accepted exactly once at%1").arg(index);
        const auto cursor=overlay->property("cursorPoint").toPointF();const auto p=c->maskCanvasPoint(cursor.x(),cursor.y(),overlay->width(),overlay->height());
        if(overlay->property("currentPressure").toDouble()!=pressure||!c->m_brushGesture->builder->repeatsPreviousInput({{p.x(),p.y()},pressure}))return QString("Delivered point/pressure differs at%1").arg(index);
        if(!s->positions.emplace(p.x(),p.y()).second)return QString("Delivered positions lost uniqueness at%1").arg(index);
        if(!s->reference->append({{p.x(),p.y()},pressure}))return QString("Original sampler refused valid bounded input at%1").arg(index);
        if(!index){s->gesture=c->m_brushGesture->gesture;s->epoch=c->m_brushGesture->epoch;s->catalog=c->m_brushGesture->catalog;s->sourceKey=c->m_brushGesture->sourceKey;s->photo=c->m_brushGesture->photoKey;}
        if(c->m_brushGesture->gesture!=s->gesture||c->m_brushGesture->epoch!=s->epoch||c->m_brushGesture->catalog!=s->catalog
           ||c->m_brushGesture->sourceKey!=s->sourceKey||c->m_brushGesture->photoKey!=s->photo||doc()!=s->before||history(c)!=s->beforeHistory)return QString("Held target/source/history changed");
        s->packets.append(QJsonArray{index,p.x(),p.y(),pressure,QString::number(s->stamp)});s->lastLocal=local;s->lastMapped=p;return QString{};
    };
    QObject::connect(s->timer,&QTimer::timeout,c,[=] {
        if(s->done)return;
        if(s->phases.isEmpty()||s->phases.last().toObject()["phase"].toInt()!=s->phase)s->phases.append(QJsonObject{{"phase",s->phase},{"round",s->round},{"at_ms",s->clock.elapsed()}});
        if(s->clock.elapsed()>=60000){finish("Finite raw-event probe exceeded60000ms");return;}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive){QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        if(!window->isActive()){QWindowSystemInterface::handleFocusWindowChanged(window);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);return;}
        auto *canvas=item("photoCanvas"),*overlay=item("brushCanvasOverlay");
        if(!canvas||!overlay||!canvas->property("brushDisplayReady").toBool())return;
        if(s->phase==0){if(!c->brushReady())return;if(!click(window,item("editBrushMaskCanvas"))){finish("Cannot enter actual brush canvas");return;}s->phase=1;return;}
        if(!overlay->isVisible()||!overlay->isEnabled())return;
        auto raw=overlay->property("editor");QObject *editor=raw.value<QObject*>();if(!editor&&raw.canConvert<QJSValue>())editor=raw.value<QJSValue>().toQObject();
        if(!editor){finish("Actual brush editor unavailable");return;}
        if(s->phase==1) {
            if(!c->brushReady())return;auto *choice=item("brushPenStabilization");
            if(!choice||!choice->isVisible())return;
            if(choice->property("checked").toBool()||editor->property("stabilizePen").toBool()){finish("Raw stream requires default-off pen rule");return;}
            editor->setProperty("radius",.006);editor->setProperty("hardness",.43);editor->setProperty("opacity",.72);editor->setProperty("exposure",.625);
            if(s->round==1) {
                const auto layers=c->gpuPlan(true).state.brushMasks.layers;
                if(layers.size()!=1||!selectMode(window,item("brushLayerSelector"),1)
                   ||editor->property("layerId").toString()!=layers[0].id
                   ||!selectMode(window,item("brushModeSelector"),1)) {
                    finish("Cannot explicitly select actual existing layer and Erase controls");return;
                }
            }
            s->before=doc();s->beforeHistory=history(c);s->packets={};s->positions.clear();s->next=0;s->ticksAtStart=s->guiTicks;
            const auto mode=s->round?BrushMaskGeometry::StrokeMode::Erase:BrushMaskGeometry::StrokeMode::Paint;
            if(editor->property("mode").toString()!=(s->round?"erase":"paint")){finish("Actual brush mode mismatch");return;}
            if(c->brushFrameSize()!=size){finish("Actual full-source frame changed");return;}
            s->stableSignature={};s->stableSinceFrame=-1;s->phase=8;window->update();return;
        }
        if(s->phase==8) {
            window->update();
            if(!c->brushReady()||c->m_refineTimer.isActive()){s->stableSinceFrame=-1;return;}
            const auto origin=overlay->mapToScene(QPointF(0,0));
            const QJsonArray signature{c->m_viewport.width(),c->m_viewport.height(),c->m_devicePixelRatio,
                overlay->width(),overlay->height(),origin.x(),origin.y(),
                QString::number(c->m_prepareGeneration),QString::number(c->m_requestedRevision),
                QString::number(c->m_fullSource.cacheKey())};
            if(signature!=s->stableSignature||s->stableSinceFrame<0){s->stableSignature=signature;s->stableSinceFrame=s->presentedFrames;return;}
            if(s->presentedFrames-s->stableSinceFrame<3)return;
            if(doc()!=s->before||history(c)!=s->beforeHistory){finish("Baseline changed before raw input while layout settled");return;}
            s->warmups.append(QJsonObject{{"round",s->round},{"stable_presented_frames",s->presentedFrames-s->stableSinceFrame},
                {"generation",QString::number(c->m_prepareGeneration)},{"revision",QString::number(c->m_requestedRevision)},
                {"source_key",QString::number(c->m_fullSource.cacheKey())},{"viewport_width",c->m_viewport.width()},
                {"viewport_height",c->m_viewport.height()},{"no_input_while_settling",true}});
            const auto mode=s->round?BrushMaskGeometry::StrokeMode::Erase:BrushMaskGeometry::StrokeMode::Paint;
            s->reference=std::make_unique<BrushMaskGeometry::StrokeBuilder>(size.width(),size.height(),BrushMaskGeometry::Style{editor->property("radius").toDouble(),editor->property("hardness").toDouble(),editor->property("opacity").toDouble()},mode);
            const auto error=append(overlay,editor,0);if(!error.isEmpty()){finish(error);return;}s->next=1;s->phase=2;return;
        }
        if(s->phase==2) {
            if(!c->brushGestureCurrent()||c->m_brushGesture->sample!=1){finish("Initial raw preview owner or sample changed before publication");return;}
            if(c->m_brushPreviewSample!=1||c->m_brushPreviewJob->busy())return;
            s->firstPublished=c->m_brushPreviewSample;s->phase=3;return;
        }
        if(s->phase==3) {
            for(int i=0;i<32&&s->next<Packets;++i,++s->next){const auto error=append(overlay,editor,s->next);if(!error.isEmpty()){finish(error);return;}}
            if(s->next==Packets)s->phase=4;return;
        }
        if(s->phase==4) {
            if(c->m_brushPreviewSample!=Packets||c->m_brushPreviewJob->busy())return;
            if(s->positions.size()!=Packets||s->guiTicks<=s->ticksAtStart||!c->m_brushGesture){finish("Incomplete raw trace/publication");return;}
            if(!s->reference->append({{s->lastMapped.x(),s->lastMapped.y()},0})){finish("Original release endpoint refused");return;}
            s->expected=s->reference->finish();if(!s->expected||s->expected->dabs.size()<2000){finish("Original final stroke refused or insufficient varying-pressure dab coverage");return;}
            s->finalPublished=c->m_brushPreviewSample;
            send(overlay,s->lastLocal,0,false);s->phase=5;return;
        }
        if(s->phase==5) {
            if(!c->brushReady())return;const auto state=c->gpuPlan(true).state;const auto &document=state.brushMasks;
            if(c->brushDrawing()||c->brushRecoveryAvailable()||history(c)!=s->beforeHistory+1||document.layers.size()!=1||document.layers.back().strokes.size()!=std::size_t(s->round+1)
               ||!sameStroke(document.layers.back().strokes.back(),*s->expected)){finish("Committed stroke differs from complete original raw sampler");return;}
            QJsonObject serialized;BrushMaskDocument::Document restored;
            if(!BrushMaskDocument::toJson(document,&serialized)||!BrushMaskDocument::fromJson(serialized,&restored)
               ||!sameStroke(restored.layers.back().strokes.back(),*s->expected)){finish("Raw stroke persistence changed original dabs");return;}
            s->applied=doc();
            QJsonObject checkpoint{{"passed",true},{"round",s->round},{"held_packets",Packets},{"unique_delivered_positions",int(s->positions.size())},{"original_sampler_dabs",int(s->expected->dabs.size())},
                {"release_pressure",0},{"single_history_commit",true},{"default_stabilizer_off",true},{"actual_selected_layer",editor->property("layerId").toString()},{"sample1_published_before_final",s->firstPublished==1&&s->firstPublished<s->finalPublished},{"first_published_sample",QString::number(s->firstPublished)},{"final_published_sample",QString::number(s->finalPublished)},
                {"gesture",QString::number(s->gesture)},{"epoch",QString::number(s->epoch)},{"catalog",QString::number(s->catalog)},{"source_key",QString::number(s->sourceKey)},{"photo_key",s->photo},
                {"trace",s->packets},{"trace_sha256",QString::fromLatin1(hash(QJsonDocument(s->packets).toJson(QJsonDocument::Compact)))},{"before",s->before},{"applied",s->applied},{"held_GUI_ticks",s->guiTicks-s->ticksAtStart},
                {"source_file_sha256",QString::fromLatin1(s->sourceSha)},{"reference_uses_public_delivered_QML_source_mapping",true},{"physical_or_arbitrary_jitter_claim",false}};
            if(!writeJson(report+QString(".round%1-stream.json").arg(s->round),checkpoint)){finish("Cannot preserve raw stream checkpoint");return;}
            if(!click(window,item("undoEditButton"))){finish("Cannot invoke actual Undo");return;}s->phase=6;return;
        }
        if(s->phase==6){if(!c->brushReady())return;if(doc()!=s->before||history(c)!=s->beforeHistory){finish("Actual Undo changed raw baseline");return;}if(!click(window,item("redoEditButton"))){finish("Cannot invoke actual Redo");return;}s->phase=7;return;}
        if(s->phase==7) {
            if(!c->brushReady())return;if(doc()!=s->applied||history(c)!=s->beforeHistory+1){finish("Actual Redo changed complete raw stroke");return;}
            auto plan=c->gpuPlan(true);plan.setFrameRect({0,0,1,1});
            const auto actual=ImagePipeline::processWithPlan(c->m_fullSource,plan,{},false);
            const auto original=BrushTileOriginalPipeline::processWithPlan(c->m_fullSource,plan,{},false);
            const auto bytes=pixelBytes(actual),expected=pixelBytes(original);
            if(actual.isNull()||original.isNull()||actual.size()!=size||actual.size()!=original.size()||actual.format()!=original.format()||bytes!=expected){finish("Complete raw-stroke CPU pixels differ from strict original");return;}
            const auto path=report+QString(".round%1-pixels.png").arg(s->round);
            if(actual.depth()!=64||!actual.save(path,"PNG")){finish("Cannot save exact raw-stroke PNG16");return;}
            const auto shot=window->grabWindow();const auto capture=report+QString(".round%1-window.png").arg(s->round);
            if(shot.isNull()||!shot.save(capture,"PNG",85)){finish("Cannot save actual raw-stroke window");return;}
            QJsonObject completed{{"passed",true},{"round",s->round},{"mode",s->round?"erase":"paint"},{"held_packets",Packets},{"sampler_dabs",int(s->expected->dabs.size())},
                {"complete_RGBA64_pixels",qint64(actual.width())*actual.height()},{"pixel_sha256",QString::fromLatin1(hash(bytes))},{"original_pixel_sha256",QString::fromLatin1(hash(expected))},
                {"PNG16_path",path},{"PNG16_sha256",QString::fromLatin1(fileHash(path))},{"window_path",capture},{"window_sha256",QString::fromLatin1(fileHash(capture))},
                {"Undo_Redo_and_persistence_bit_exact",true},{"source_unchanged",fileHash(source)==s->sourceSha},{"physical_or_arbitrary_jitter_claim",false}};
            if(!writeJson(report+QString(".round%1-complete.json").arg(s->round),completed)){finish("Cannot preserve completed raw round");return;}
            s->rounds.append(completed);if(++s->round==2){finish({});return;}s->phase=1;return;
        }
    });
    c->setLanguage(language);window->setProperty("workspaceIndex",1);window->resize(1180,720);window->requestActivate();
    if(!c->importFile(QUrl::fromLocalFile(source))){finish("Cannot import owned raw-event fixture");return;}s->timer->start();
}
};
void startRawPressureEventSmokeRun(PhotoController *c,QQuickWindow *window,const QString &report,const QString &language) {
    RawPressureEventProbe::start(c,window,report,language);
}
