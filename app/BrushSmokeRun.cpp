#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QRgba64>

namespace {
QQuickItem *child(QQuickItem *item,const QString &name) {
    if(!item)return nullptr;if(item->objectName()==name)return item;
    for(auto *c:item->childItems())if(auto *found=child(c,name))return found;return nullptr;
}
void mouse(QQuickWindow *window,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,point,point,window->mapToGlobal(point.toPoint()),button,buttons,Qt::NoModifier);QCoreApplication::sendEvent(window,&event);
}
bool click(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto p=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    mouse(window,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);return true;
}
bool exposed(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto point=item->mapToScene({item->width()/2,item->height()/2});
    if(!QRectF(0,0,window->width(),window->height()).contains(point))return false;
    for(auto *p=item->parentItem();p;p=p->parentItem())if(p->clip()&&!p->contains(p->mapFromScene(point)))return false;
    return true;
}
void key(QQuickWindow *window,int value) {
    QKeyEvent press(QEvent::KeyPress,value,Qt::NoModifier);QCoreApplication::sendEvent(window,&press);
    QKeyEvent release(QEvent::KeyRelease,value,Qt::NoModifier);QCoreApplication::sendEvent(window,&release);
}
bool selectFirstLayer(QQuickWindow *window) {
    auto *selector=child(window->contentItem(),"brushLayerSelector");
    if(!exposed(window,selector)||!click(window,selector))return false;
    key(window,Qt::Key_Home);key(window,Qt::Key_Down);key(window,Qt::Key_Return);
    return selector->property("currentIndex").toInt()==1;
}
QJsonObject firstLayerProperty(QJsonObject document,const QString &name,const QJsonValue &value) {
    auto layers=document["layers"].toArray();auto layer=layers[0].toObject();layer[name]=value;layers[0]=layer;document["layers"]=layers;return document;
}
bool previewDisplayedReady(PhotoController *c,QQuickWindow *window) {
    if(c->gpuActive())return true; // The actual GPU result is checked in the window capture.
    auto *image=child(window->contentItem(),"processedPreviewImage");
    return image&&image->isVisible()&&image->property("status").toInt()==1
        &&image->property("source").toUrl()==QUrl(c->previewUrl());
}
int cursor(PhotoController *c){const auto h=c->editHistory();for(int i=0;i<h.size();++i)if(h[i].toMap()["current"].toBool())return i;return -1;}
QJsonObject masks(PhotoController *c){return c->gpuPlan(true).state.toJson()["brushMasks"].toObject();}
QByteArray fileHash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
QColor sample(const QImage &image,QQuickWindow *window,QPointF point) {
    return image.pixelColor(std::clamp(int(point.x()*image.width()/window->width()),0,image.width()-1),
                            std::clamp(int(point.y()*image.height()/window->height()),0,image.height()-1));
}
int scopeInkSamples(const QImage &image,QQuickWindow *window,QQuickItem *plot) {
    if(image.isNull()||!plot)return 0;
    const auto a=plot->mapToScene({0,0}),b=plot->mapToScene({plot->width(),plot->height()});
    const double sx=double(image.width())/window->width(),sy=double(image.height())/window->height();int count=0;
    for(int y=std::max(0,int(a.y()*sy));y<std::min(image.height(),int(b.y()*sy));y+=2)
        for(int x=std::max(0,int(a.x()*sx));x<std::min(image.width(),int(b.x()*sx));x+=2) {
            const auto p=image.pixelColor(x,y);
            if(p.green()>70&&p.green()>p.red()+12&&p.green()>p.blue()+5)++count;
        }
    return count; // Visibility of this known waveform, not a color-precision gate.
}
}
void startBrushCanvasSmokeRun(PhotoController *c,QQuickWindow *window,const QString &reportPath) {
    struct State {
        QElapsedTimer clock;int phase=0,settle=0,history=0,delta=0,scopeInk=0;bool finished=false;
        bool transient=false,escape=false,oneUndo=false,display=false,undoRedo=false,reopen=false,zoom=false,scopes=false;
        bool settings=false,photoReady=false;double exposure=1;QJsonObject painted;QColor baseline,lastColor;QRectF frame;QByteArray sourceHash;
        bool selected=false,evDrag=false,toggle=false,lastStroke=false,prune=false,deleteUndo=false,english=false;
        bool pointerReady=false;QString actualHint,expectedHint;
        double layerExposure=0;QJsonObject layers,extended,edited,reduced,sole;
    };
    auto s=std::make_shared<State>();s->clock.start();auto dir=std::make_shared<QTemporaryDir>();
    auto *timer=new QTimer(c);timer->setInterval(50);const auto source=dir->filePath("owned-brush.png");
    const auto finish=[=](const QString &error) {
        if(s->finished)return;s->finished=true;timer->stop();const bool unchanged=fileHash(source)==s->sourceHash;
        const bool gpu=qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU");
        const bool ok=error.isEmpty()&&s->settings&&s->transient&&s->escape&&s->oneUndo&&s->display&&s->undoRedo&&s->reopen&&s->zoom&&s->scopes
            &&s->photoReady&&s->pointerReady&&s->selected&&s->evDrag&&s->toggle&&s->lastStroke&&s->prune&&s->deleteUndo&&s->english&&s->scopeInk>=64&&unchanged&&(!gpu||c->gpuActive());
        if(!error.isEmpty())window->grabWindow().save(reportPath+".failure.png");
        const QJsonObject report{{"passed",ok},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"elapsed_ms",s->clock.elapsed()},
            {"phase",s->phase},{"error",error},{"synthetic_fixture",true},{"source_pixels",2659021},{"source_unchanged",unchanged},
            {"live_uncommitted",s->transient},{"escape_cancel",s->escape},{"one_undo",s->oneUndo},{"display_changed",s->display},
            {"display_rgb_delta",s->delta},{"undo_redo",s->undoRedo},{"project_reopen",s->reopen},{"zoom_roi",s->zoom},
            {"settings_visible",s->settings},{"pointer_exposure",s->exposure},
            {"photo_display_ready",s->photoReady},{"display_baseline_rgb",QJsonArray{s->baseline.red(),s->baseline.green(),s->baseline.blue()}},
            {"display_last_rgb",QJsonArray{s->lastColor.red(),s->lastColor.green(),s->lastColor.blue()}},
            {"layer_selection",s->selected},{"layer_ev_drag_one_undo",s->evDrag},{"layer_toggle",s->toggle},{"remove_last_stroke",s->lastStroke},
            {"empty_layer_pruned_undo",s->prune},{"delete_layer_undo",s->deleteUndo},{"english_controls_visible",s->english},
            {"scope_display",s->scopeInk>=64},{"scope_display_ink_samples",s->scopeInk},
            {"brush_pointer_ready",s->pointerReady},{"brush_hint",s->actualHint},{"expected_brush_hint",s->expectedHint},
            {"full_scopes",s->scopes},{"scope_pixels",qint64(c->scopesPixelCount())},{"gpu_active",c->gpuActive()},
            {"backend",c->processingBackend()},{"producer","cpu-reference"},{"native_gpu_brush",false},{"native_float_raw_decoder",false}};
        QSaveFile output(reportPath);const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);
        const bool saved=output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.commit();QCoreApplication::exit(ok&&saved?0:1);
    };
    QObject::connect(timer,&QTimer::timeout,c,[=] {
        if(s->clock.elapsed()>=45000){finish("Brush pointer integration timed out");return;}
        if(!c->brushReady()){s->settle=0;return;}
        if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!c->gpuActive())return;
        auto *canvas=child(window->contentItem(),"photoCanvas"),*overlay=child(window->contentItem(),"brushCanvasOverlay");
        if(!canvas||!overlay)return;
        const auto point=[&](double x,double y){return overlay->mapToScene({overlay->width()*x,overlay->height()*y});};
        if(s->phase==0) {
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas")))return;
            s->phase=1;s->settle=0;
        }else if(s->phase==1) {
            if(canvas->property("maskTool").toString()!="brush"||!overlay->isEnabled()||++s->settle<3)return;
            auto *selector=child(window->contentItem(),"brushLayerSelector"),*slider=child(window->contentItem(),"brushExposureSlider");
            if(!exposed(window,selector)||!exposed(window,slider)){finish("Brush tool did not reveal its editable controls");return;}
            const auto p=slider->mapToScene({slider->width()*.67,slider->height()/2});
            mouse(window,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);
            s->exposure=slider->property("value").toDouble();s->settings=s->exposure>.5&&s->exposure<2&&std::abs(s->exposure-1)>.01&&masks(c).isEmpty();s->phase=9;return;
        }else if(s->phase==9) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window))return;
            s->history=cursor(c);mouse(window,QEvent::MouseButtonPress,point(.25,.6),Qt::LeftButton,Qt::LeftButton);
            mouse(window,QEvent::MouseMove,point(.5,.6),Qt::NoButton,Qt::LeftButton);
            s->transient=c->brushDrawing()&&masks(c).isEmpty()&&cursor(c)==s->history;
            if(!s->transient){finish("Pointer press did not create a transient gesture");return;}
            QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(window,&key);
            QKeyEvent release(QEvent::KeyRelease,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(window,&release);
            mouse(window,QEvent::MouseButtonRelease,point(.5,.6),Qt::LeftButton,Qt::NoButton);
            s->escape=!c->brushDrawing()&&masks(c).isEmpty()&&cursor(c)==s->history;
            if(!s->escape){finish("Native Escape did not abandon the transient stroke");return;}
            s->phase=2;s->settle=0;
        }else if(s->phase==2) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||++s->settle<3)return;
            const auto position=point(.5,.6);
            const auto image=window->grabWindow();if(image.isNull()){finish("Cannot read baseline window");return;}
            if(!c->brushReady()||!previewDisplayedReady(c,window)||position!=point(.5,.6)){s->settle=0;return;}
            s->baseline=sample(image,window,position);s->photoReady=std::max({s->baseline.red(),s->baseline.green(),s->baseline.blue()})>30;
            if(!s->photoReady){s->settle=0;return;} // Do not accept an unloaded black image as the baseline.
            if(!image.save(reportPath+".before.png")){finish("Cannot save brush baseline");return;}
            mouse(window,QEvent::MouseButtonPress,point(.25,.6),Qt::LeftButton,Qt::LeftButton);
            for(int i=1;i<=5;++i)mouse(window,QEvent::MouseMove,point(.25+.5*i/5,.6),Qt::NoButton,Qt::LeftButton);
            mouse(window,QEvent::MouseButtonRelease,point(.75,.6),Qt::LeftButton,Qt::NoButton);
            s->painted=masks(c);s->oneUndo=cursor(c)==s->history+1&&!s->painted.isEmpty();
            const auto doc=c->gpuPlan(true).state.brushMasks;
            s->settings=s->settings&&!doc.layers.empty()&&std::abs(doc.layers[0].exposureEv-s->exposure)<1e-9;
            if(!s->oneUndo){finish("Completed pointer stroke is not one undo entry");return;}
            c->setExactScopes(true);c->setScopeMode("waveform");s->phase=3;s->settle=0;
        }else if(s->phase==3) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||!previewDisplayedReady(c,window)||++s->settle<3)return;
            const auto position=point(.5,.6);
            const auto image=window->grabWindow();if(image.isNull()){finish("Cannot read painted window");return;}
            if(!c->brushReady()||!c->scopePlotCurrent()||!previewDisplayedReady(c,window)||position!=point(.5,.6)){s->settle=0;return;}
            const auto color=sample(image,window,position);s->lastColor=color;s->delta=std::max({std::abs(color.red()-s->baseline.red()),std::abs(color.green()-s->baseline.green()),std::abs(color.blue()-s->baseline.blue())});
            s->display=s->delta>=3;s->scopes=true;
            if(!s->display){s->settle=0;return;} // The asynchronous item may still be presenting its previous texture.
            if(!image.save(reportPath+".painted.png")){finish("Cannot save actual painted display");return;}
            if(!click(window,child(window->contentItem(),"undoEditButton"))){finish("Cannot click Undo");return;}
            s->phase=4;
        }else if(s->phase==4) {
            if(!masks(c).isEmpty()){finish("Native Undo retained the painted stroke");return;}
            if(!click(window,child(window->contentItem(),"redoEditButton"))){finish("Cannot click Redo");return;}
            s->phase=5;
        }else if(s->phase==5) {
            s->undoRedo=masks(c)==s->painted;
            if(!s->undoRedo||!c->createProject(QUrl::fromLocalFile(dir->path()),"BrushPointer")||!c->flushEdits()){finish("Cannot persist painted project");return;}
            const auto project=c->projectPath();if(!c->openProject(QUrl::fromLocalFile(project))){finish("Cannot reopen painted project");return;}
            s->phase=6;s->settle=0;
        }else if(s->phase==6) {
            if(++s->settle<3)return;s->reopen=masks(c)==s->painted;
            if(!s->reopen||!window->grabWindow().save(reportPath+".reopen.png")){finish("Reopened UI lost painted intent");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("Cannot re-enter brush tool");return;}
            c->setExactScopes(true);c->setScopeMode("waveform");
            canvas->setProperty("zoom",2.0);s->phase=7;s->settle=0;
        }else if(s->phase==7) {
            s->frame=c->previewFrameRect();if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||s->frame.width()>=.98||++s->settle<3)return;
            mouse(window,QEvent::MouseButtonPress,point(.25,.6),Qt::LeftButton,Qt::LeftButton);
            mouse(window,QEvent::MouseMove,point(.75,.6),Qt::NoButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,point(.75,.6),Qt::LeftButton,Qt::NoButton);
            const auto doc=c->gpuPlan(true).state.brushMasks;
            if(doc.layers.size()!=2){finish("Zoom pointer stroke did not create the selected new layer");return;}
            const auto &dabs=doc.layers.back().strokes.back().dabs;
            s->zoom=std::abs(dabs.front().center.x-(s->frame.x()+.25*s->frame.width()))<1e-6
                &&std::abs(dabs.back().center.x-(s->frame.x()+.75*s->frame.width()))<1e-6
                &&std::abs(dabs.front().center.y-(s->frame.y()+.6*s->frame.height()))<1e-6;
            s->phase=8;s->settle=0;
        }else if(s->phase==8) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            if(!window->grabWindow().save(reportPath+".zoom.png")){finish("Cannot save zoomed brush UI");return;}
            s->layers=masks(c);
            if(!selectFirstLayer(window)){finish("Native layer selector did not choose the first saved layer");return;}
            s->phase=10;s->settle=0;
        }else if(s->phase==10) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||++s->settle<3)return;
            mouse(window,QEvent::MouseButtonPress,point(.3,.4),Qt::LeftButton,Qt::LeftButton);
            mouse(window,QEvent::MouseMove,point(.6,.4),Qt::NoButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,point(.6,.4),Qt::LeftButton,Qt::NoButton);
            s->extended=masks(c);auto remaining=s->extended;auto layers=remaining["layers"].toArray();
            if(layers.size()!=2||layers[0].toObject()["strokes"].toArray().size()!=2){finish("Selected layer did not receive the new pointer stroke");return;}
            auto layer=layers[0].toObject();auto strokes=layer["strokes"].toArray();strokes.removeLast();layer["strokes"]=strokes;layers[0]=layer;remaining["layers"]=layers;
            s->selected=remaining==s->layers;if(!s->selected){finish("Appending to the selected layer changed previous intent");return;}
            s->phase=11;
        }else if(s->phase==11) {
            auto *slider=child(window->contentItem(),"brushExposureSlider");if(!exposed(window,slider)){finish("Selected layer EV slider is clipped");return;}
            s->history=cursor(c);
            const auto p=[&](double x){return slider->mapToScene({slider->width()*x,slider->height()/2});};
            mouse(window,QEvent::MouseButtonPress,p(.65),Qt::LeftButton,Qt::LeftButton);
            for(const auto x:{.7,.73,.78})mouse(window,QEvent::MouseMove,p(x),Qt::NoButton,Qt::LeftButton);
            mouse(window,QEvent::MouseButtonRelease,p(.78),Qt::LeftButton,Qt::NoButton);s->layerExposure=slider->property("value").toDouble();s->phase=12;
        }else if(s->phase==12) {
            s->edited=firstLayerProperty(s->extended,"exposure",s->layerExposure);
            s->evDrag=cursor(c)==s->history+1&&masks(c)==s->edited&&std::abs(s->layerExposure-s->exposure)>.05;
            if(!s->evDrag||!click(window,child(window->contentItem(),"undoEditButton"))||masks(c)!=s->extended
                ||!click(window,child(window->contentItem(),"redoEditButton"))||masks(c)!=s->edited){finish("Native layer EV drag is not one isolated Undo/Redo");return;}
            s->phase=13;
        }else if(s->phase==13) {
            if(!click(window,child(window->contentItem(),"brushLayerEnabled"))||masks(c)!=firstLayerProperty(s->edited,"enabled",false)) {finish("Native layer disable changed other intent");return;}
            s->phase=14;
        }else if(s->phase==14) {
            s->toggle=click(window,child(window->contentItem(),"brushLayerEnabled"))&&masks(c)==s->edited;
            if(!s->toggle){finish("Native layer re-enable lost saved intent");return;}s->phase=15;
        }else if(s->phase==15) {
            s->history=cursor(c);s->reduced=firstLayerProperty(s->layers,"exposure",s->layerExposure);
            auto *button=child(window->contentItem(),"removeLastBrushStroke");
            s->lastStroke=exposed(window,button)&&click(window,button)&&masks(c)==s->reduced&&cursor(c)==s->history+1;
            if(!s->lastStroke){finish("Remove last stroke did not preserve previous strokes and other layers");return;}s->phase=16;
        }else if(s->phase==16) {
            auto *button=child(window->contentItem(),"removeLastBrushStroke");auto layers=s->reduced["layers"].toArray();layers.removeFirst();s->sole=s->reduced;s->sole["layers"]=layers;
            s->history=cursor(c);s->prune=click(window,button)&&masks(c)==s->sole&&cursor(c)==s->history+1&&!button->isEnabled();
            if(!s->prune||!click(window,child(window->contentItem(),"undoEditButton"))||masks(c)!=s->reduced){finish("Removing the only stroke did not prune/undo its exact layer");return;}
            s->phase=17;
        }else if(s->phase==17) {
            if(!selectFirstLayer(window)){finish("Cannot select restored brush layer");return;}s->phase=18;s->settle=0;
        }else if(s->phase==18) {
            if(++s->settle<3)return;s->history=cursor(c);
            s->deleteUndo=click(window,child(window->contentItem(),"removeBrushLayer"))&&masks(c)==s->sole&&cursor(c)==s->history+1
                &&click(window,child(window->contentItem(),"undoEditButton"))&&masks(c)==s->reduced;
            if(!s->deleteUndo){finish("Native layer deletion/Undo changed an unrelated layer");return;}
            if(!selectFirstLayer(window)){finish("Cannot select final restored layer");return;}
            c->setLanguage("en_US");s->phase=19;s->settle=0;
        }else if(s->phase==19) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            auto *plot=child(window->contentItem(),"scopePlotImage");
            if(!plot||plot->property("status").toInt()!=1||plot->property("source").toUrl()!=QUrl(c->scopePlotUrl())){s->settle=0;return;}
            auto *hint=child(window->contentItem(),"maskToolHint");s->actualHint=hint?hint->property("text").toString():QString{};s->expectedHint=c->brushStatus();
            s->pointerReady=overlay->isEnabled()&&hint&&s->actualHint==s->expectedHint;
            if(!s->pointerReady){finish("Canvas brush availability did not refresh after completed processing");return;}
            const auto image=window->grabWindow();
            // A grab can process a late layout/viewport update. Require the
            // current complete plot and visible ink after the actual capture.
            s->scopeInk=scopeInkSamples(image,window,plot);
            if(!c->brushReady()||!c->scopePlotCurrent()||s->scopeInk<64){s->settle=0;return;}
            s->english=c->language()=="en_US"&&exposed(window,child(window->contentItem(),"removeLastBrushStroke"))
                &&exposed(window,child(window->contentItem(),"removeBrushLayer"))&&exposed(window,child(window->contentItem(),"brushExposureSlider"));
            if(!s->english||!image.save(reportPath+".layers.png")){finish("English layer controls are clipped or cannot be captured");return;}finish({});
        }
    });
    window->setProperty("workspaceIndex",1);window->resize(1180,800);window->requestActivate();
    QImage image(2203,1207,QImage::Format_RGBA64);image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(12000+x*4,14000+y*5,18000+(x+y)*2,65535);
    if(!dir->isValid()||!image.save(source)||!c->importFile(QUrl::fromLocalFile(source))){finish("Cannot create owned brush fixture");return;}
    s->sourceHash=fileHash(source);timer->start();
}
