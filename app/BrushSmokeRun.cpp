#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "app/SmokeCaptureCoordinates.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QWheelEvent>
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
#include <QDir>
#include <QFileInfo>
#include "core/pipeline/BrushRenderRequest.h"

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
bool selectFirstStroke(QQuickWindow *window) {
    auto *selector=child(window->contentItem(),"brushStrokeSelector");
    if(!exposed(window,selector)||!click(window,selector))return false;
    key(window,Qt::Key_Home);key(window,Qt::Key_Down);key(window,Qt::Key_Return);
    return selector->property("currentIndex").toInt()==1;
}
QQuickItem *toolFlick(QQuickWindow *window) {
    auto *scroll=child(window->contentItem(),"developToolScroll");
    return scroll?scroll->property("contentItem").value<QQuickItem *>():nullptr;
}
QJsonObject brushControls(QQuickWindow *window) {
    auto *panel=child(window->contentItem(),"developRightPanel"),*flick=toolFlick(window);
    auto *header=child(window->contentItem(),"brushPanelToggle"),*slider=child(window->contentItem(),"brushExposureSlider");
    return {{"pending",panel&&panel->property("brushRevealPending").toBool()},
        {"attempts",panel?panel->property("brushRevealAttempts").toInt():-1},
        {"content_y",flick?flick->property("contentY").toDouble():-1},
        {"content_height",flick?flick->property("contentHeight").toDouble():-1},
        {"viewport_height",flick?flick->height():0},
        {"header_y",flick&&header?header->mapToItem(flick,{0,0}).y():-1},
        {"exposure_y",flick&&slider?slider->mapToItem(flick,{0,0}).y():-1},
        {"critical_visible",exposed(window,header)&&exposed(window,slider)}};
}
bool wheelToToolsStart(QQuickWindow *window) {
    auto *scroll=child(window->contentItem(),"developToolScroll");if(!exposed(window,scroll))return false;
    // The content's4px gutter avoids wheel-enabled sliders/combos.
    const auto point=scroll->mapToScene({4,scroll->height()*.5});
    QWheelEvent event(point,window->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,120*80),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
    QCoreApplication::sendEvent(window,&event);
    QWheelEvent end(point,window->mapToGlobal(point.toPoint()),QPoint(),QPoint(),Qt::NoButton,Qt::NoModifier,Qt::ScrollEnd,false);
    QCoreApplication::sendEvent(window,&end);return true;
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
    return SmokeCaptureCoordinates::sample(image,point,window->effectiveDevicePixelRatio());
}
int scopeInkSamples(const QImage &image,QQuickWindow *window,QQuickItem *plot) {
    if(image.isNull()||!plot)return 0;
    const auto a=plot->mapToScene({0,0}),b=plot->mapToScene({plot->width(),plot->height()});
    const auto rect=SmokeCaptureCoordinates::region(image,QRectF(a,b),window->effectiveDevicePixelRatio());int count=0;
    for(int y=rect.top();!rect.isEmpty()&&y<rect.y()+rect.height();y+=2)
        for(int x=rect.left();x<rect.x()+rect.width();x+=2) {
            const auto p=image.pixelColor(x,y);
            if(p.green()>70&&p.green()>p.red()+12&&p.green()>p.blue()+5)++count;
        }
    return count; // Visibility of this known waveform, not a color-precision gate.
}
QPointF imagePoint(PhotoController *c,QQuickItem *overlay,double x,double y) {
    const auto frame=c->previewFrameRect();
    return overlay->mapToScene({(x-frame.x())/frame.width()*overlay->width(),(y-frame.y())/frame.height()*overlay->height()});
}
int selectedGuideInk(const QImage &image,QQuickWindow *window,PhotoController *c,QQuickItem *overlay) {
    const auto layers=c->brushLayers();if(layers.isEmpty())return 0;
    const auto selection=c->brushStrokeSelection(layers[0].toMap()["id"].toString(),0);int count=0;
    for(const auto &entry:selection["points"].toList()) {
        const auto p=entry.toMap();const auto scene=imagePoint(c,overlay,p["x"].toDouble(),p["y"].toDouble());
        if(!overlay->contains(overlay->mapFromScene(scene)))continue;
        bool yellow=false;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
            const auto color=sample(image,window,scene+QPointF(x,y));
            yellow|=color.isValid()&&color.red()>=220&&color.green()>=190&&color.blue()>=100&&color.blue()<=180
                &&color.red()>=color.green()+15&&color.green()>=color.blue()+35;
        }
        if(yellow)++count;
    }
    return count; // Actual selected centerline pixels, compared with the unselected window.
}
}
void startBrushCanvasSmokeRun(PhotoController *c,QQuickWindow *window,const QString &reportPath) {
    struct State {
        QElapsedTimer clock;int phase=0,settle=0,history=0,delta=0,scopeInk=0;bool finished=false;
        bool transient=false,escape=false,oneUndo=false,display=false,undoRedo=false,reopen=false,zoom=false,scopes=false;
        bool settings=false,photoReady=false;double exposure=1;QJsonObject painted;QColor baseline,lastColor;QRectF frame;QByteArray sourceHash;
        bool selected=false,evDrag=false,toggle=false,lastStroke=false,prune=false,deleteUndo=false,english=false;
        bool pointerReady=false;QString actualHint,expectedHint;
        bool disabledZh=false,disabledEn=false,disabledInput=false;int disabledCursor=-1;QString disabledHint,expectedDisabledHint;
        bool selectedDelete=false;int guideBaseInk=0,guideInk=0,selectedDelta=0;QColor selectedBefore;QJsonObject strokeReduced;
        bool revealed=false,manualScroll=false;int revealHistory=0;QJsonObject revealMasks;QRectF revealFrame;
        double layerExposure=0;QJsonObject layers,extended,edited,reduced,sole;
    };
    auto s=std::make_shared<State>();s->clock.start();auto dir=std::make_shared<QTemporaryDir>();
    auto *timer=new QTimer(c);timer->setInterval(50);const auto source=dir->filePath("owned-brush.png");
    const auto finish=[=](const QString &error) {
        if(s->finished)return;s->finished=true;timer->stop();const bool unchanged=fileHash(source)==s->sourceHash;
        const bool gpu=qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU");
        const bool ok=error.isEmpty()&&s->settings&&s->transient&&s->escape&&s->oneUndo&&s->display&&s->undoRedo&&s->reopen&&s->zoom&&s->scopes
            &&s->photoReady&&s->pointerReady&&s->selected&&s->evDrag&&s->toggle&&s->lastStroke&&s->prune&&s->deleteUndo&&s->english
            &&s->disabledZh&&s->disabledEn&&s->selectedDelete&&s->revealed&&s->scopeInk>=64&&unchanged&&(!gpu||c->gpuActive());
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
            {"disabled_layer_guard",s->disabledZh&&s->disabledEn},{"disabled_layer_zh",s->disabledZh},{"disabled_layer_en",s->disabledEn},
            {"disabled_layer_input_ignored",s->disabledInput},{"disabled_layer_hint",s->disabledHint},{"expected_disabled_layer_hint",s->expectedDisabledHint},
            {"disabled_layer_cursor_shape",s->disabledCursor},
            {"selected_stroke_delete",s->selectedDelete},{"selected_guide_new_ink_samples",s->guideInk},{"selected_delete_rgb_delta",s->selectedDelta},
            {"brush_controls_reveal",s->revealed},{"manual_scroll_respected",s->manualScroll},{"brush_controls",brushControls(window)},
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
        const auto disabledGuard=[&] {
            auto *hint=child(window->contentItem(),"maskToolHint"),*pointer=child(window->contentItem(),"brushPointerArea");
            s->disabledHint=hint?hint->property("text").toString():QString{};
            s->expectedDisabledHint=c->language()=="zh_CN" ? QStringLiteral("请先启用所选笔刷图层。") : QStringLiteral("Enable the selected brush layer first.");
            s->disabledCursor=pointer?pointer->property("cursorShape").toInt():-1;
            const auto before=masks(c);const int history=cursor(c);const auto frame=c->previewFrameRect();
            mouse(window,QEvent::MouseButtonPress,point(.3,.4),Qt::LeftButton,Qt::LeftButton);const bool transient=c->brushDrawing();
            mouse(window,QEvent::MouseMove,point(.6,.4),Qt::NoButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,point(.6,.4),Qt::LeftButton,Qt::NoButton);
            s->disabledInput=!transient&&!c->brushDrawing()&&masks(c)==before&&cursor(c)==history&&c->previewFrameRect()==frame;
            return s->disabledInput&&s->disabledHint==s->expectedDisabledHint&&s->disabledCursor==int(Qt::ForbiddenCursor);
        };
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
            s->baseline=sample(image,window,position);if(!s->baseline.isValid())return;s->photoReady=std::max({s->baseline.red(),s->baseline.green(),s->baseline.blue()})>30;
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
            const auto color=sample(image,window,position);if(!color.isValid())return;s->lastColor=color;s->delta=std::max({std::abs(color.red()-s->baseline.red()),std::abs(color.green()-s->baseline.green()),std::abs(color.blue()-s->baseline.blue())});
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
            // The original stroke can be outside a200% ROI on clipped720px
            // windows. Fit via the visible control before selecting its path.
            if(!click(window,child(window->contentItem(),"fitPreviewButton"))){finish("Cannot fit the source before selecting an earlier stroke");return;}
            s->revealHistory=cursor(c);s->revealMasks=masks(c);
            if(!wheelToToolsStart(window)){finish("Cannot scroll away from brush controls through a native wheel event");return;}s->phase=30;s->settle=0;
        }else if(s->phase==30) {
            auto *flick=toolFlick(window);if(!flick||flick->property("moving").toBool()||++s->settle<3)return;
            const auto geometry=brushControls(window);
            if(geometry["critical_visible"].toBool()||geometry["pending"].toBool()||geometry["content_y"].toDouble()>16){finish("Automatic brush reveal overrode manual scrolling");return;}
            s->revealFrame=c->previewFrameRect();
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))||!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("Cannot re-enter the brush tool after scrolling away");return;}
            s->phase=31;s->settle=0;
        }else if(s->phase==31) {
            if(++s->settle<3)return;const auto geometry=brushControls(window);
            if(!geometry["critical_visible"].toBool()||geometry["pending"].toBool()){finish("Re-entering the brush tool did not reveal its actual controls");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))||!click(window,child(window->contentItem(),"editBrushMaskCanvas"))||!wheelToToolsStart(window)){finish("Cannot interrupt pending reveal with a native wheel");return;}
            s->phase=32;s->settle=0;
        }else if(s->phase==32) {
            auto *flick=toolFlick(window);if(!flick||flick->property("moving").toBool()||++s->settle<3)return;
            const auto geometry=brushControls(window);
            s->manualScroll=!geometry["pending"].toBool()&&!geometry["critical_visible"].toBool()&&geometry["content_y"].toDouble()<=16;
            if(!s->manualScroll){finish("Pending brush reveal fought the user's manual scroll");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))||!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("Cannot restore the brush tool after canceling its reveal");return;}
            s->phase=33;s->settle=0;
        }else if(s->phase==33) {
            if(++s->settle<3)return;const auto geometry=brushControls(window);
            s->revealed=geometry["critical_visible"].toBool()&&!geometry["pending"].toBool()&&masks(c)==s->revealMasks
                &&cursor(c)==s->revealHistory&&c->previewFrameRect()==s->revealFrame;
            if(!s->revealed||!window->grabWindow().save(reportPath+".reveal.png")){finish("Brush reveal changed edit/viewport state or did not restore controls");return;}
            s->phase=22;s->settle=0;
        }else if(s->phase==22) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            const auto image=window->grabWindow();if(image.isNull()||!c->brushReady()||!previewDisplayedReady(c,window))return;
            s->selectedBefore=sample(image,window,imagePoint(c,overlay,.5,.6));if(!s->selectedBefore.isValid())return;
            s->guideBaseInk=selectedGuideInk(image,window,c,overlay);s->history=cursor(c);
            if(!selectFirstStroke(window)){finish("Cannot select an earlier stored stroke through the native selector");return;}s->phase=23;s->settle=0;
        }else if(s->phase==23) {
            if(++s->settle<3)return;const auto image=window->grabWindow();
            s->guideInk=selectedGuideInk(image,window,c,overlay)-s->guideBaseInk;if(s->guideInk<16)return;
            if(masks(c)!=s->edited||cursor(c)!=s->history||!image.save(reportPath+".selected-stroke.png")){finish("Selecting a stored stroke changed edit intent or cannot be captured");return;}
            auto layers=s->edited["layers"].toArray();auto layer=layers[0].toObject();auto strokes=layer["strokes"].toArray();strokes.removeFirst();layer["strokes"]=strokes;layers[0]=layer;
            s->strokeReduced=s->edited;s->strokeReduced["layers"]=layers;
            auto *button=child(window->contentItem(),"removeSelectedBrushStroke");
            if(!exposed(window,button)||!click(window,button)||masks(c)!=s->strokeReduced||cursor(c)!=s->history+1){finish("Selected stroke deletion changed another stroke or is not one Undo");return;}
            s->phase=24;s->settle=0;
        }else if(s->phase==24) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;const auto image=window->grabWindow();
            if(image.isNull()||!c->brushReady()||!previewDisplayedReady(c,window))return;
            const auto color=sample(image,window,imagePoint(c,overlay,.5,.6));if(!color.isValid())return;
            s->selectedDelta=std::max({std::abs(color.red()-s->selectedBefore.red()),std::abs(color.green()-s->selectedBefore.green()),std::abs(color.blue()-s->selectedBefore.blue())});
            if(s->selectedDelta<3)return;
            auto *selector=child(window->contentItem(),"brushStrokeSelector"),*button=child(window->contentItem(),"removeSelectedBrushStroke");
            if(!selector||selector->property("currentIndex").toInt()!=0||!button||button->isEnabled()
                ||!click(window,child(window->contentItem(),"undoEditButton"))||masks(c)!=s->edited){finish("Deleted selection did not clear or native Undo changed intent");return;}
            s->phase=25;s->settle=0;
        }else if(s->phase==25) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;const auto image=window->grabWindow();
            if(image.isNull()||!c->brushReady()||!previewDisplayedReady(c,window))return;
            const auto color=sample(image,window,imagePoint(c,overlay,.5,.6));if(!color.isValid())return;
            if(std::max({std::abs(color.red()-s->selectedBefore.red()),std::abs(color.green()-s->selectedBefore.green()),std::abs(color.blue()-s->selectedBefore.blue())})>2)return;
            if(!click(window,child(window->contentItem(),"redoEditButton"))||masks(c)!=s->strokeReduced||cursor(c)!=s->history+1){finish("Native selected-stroke Redo changed another edit");return;}
            s->phase=26;
        }else if(s->phase==26) {
            s->selectedDelete=click(window,child(window->contentItem(),"undoEditButton"))&&masks(c)==s->edited&&cursor(c)==s->history;
            if(!s->selectedDelete){finish("Cannot restore all strokes after selected-stroke acceptance");return;}s->phase=13;
        }else if(s->phase==13) {
            if(!click(window,child(window->contentItem(),"brushLayerEnabled"))||masks(c)!=firstLayerProperty(s->edited,"enabled",false)) {finish("Native layer disable changed other intent");return;}
            s->phase=14;
        }else if(s->phase==14) {
            s->disabledZh=c->language()=="zh_CN"&&disabledGuard();
            if(!s->disabledZh){finish("Disabled brush layer still advertises paint input in Chinese");return;}
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
                &&exposed(window,child(window->contentItem(),"removeBrushLayer"))&&exposed(window,child(window->contentItem(),"brushExposureSlider"))
                &&exposed(window,child(window->contentItem(),"brushStrokeSelector"));
            if(!s->english){finish("English layer controls are clipped");return;}
            if(!click(window,child(window->contentItem(),"brushLayerEnabled"))){finish("Cannot disable the English brush layer");return;}s->phase=20;
        }else if(s->phase==20) {
            s->disabledEn=c->language()=="en_US"&&disabledGuard();
            if(!s->disabledEn){finish("Disabled brush layer still advertises paint input in English");return;}
            if(!window->grabWindow().save(reportPath+".disabled.png")||!click(window,child(window->contentItem(),"brushLayerEnabled"))){finish("Cannot capture or re-enable the disabled English brush layer");return;}s->phase=21;s->settle=0;
        }else if(s->phase==21) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            auto *hint=child(window->contentItem(),"maskToolHint"),*pointer=child(window->contentItem(),"brushPointerArea");
            if(masks(c)!=s->reduced||!overlay->isEnabled()||!pointer||pointer->property("cursorShape").toInt()!=int(Qt::CrossCursor)
                ||!hint||hint->property("text").toString()!=c->brushStatus()){finish("Re-enabling the brush layer did not restore paint availability");return;}
            auto *plot=child(window->contentItem(),"scopePlotImage");
            if(!plot||plot->property("status").toInt()!=1||plot->property("source").toUrl()!=QUrl(c->scopePlotUrl())){s->settle=0;return;}
            const auto image=window->grabWindow();s->scopeInk=scopeInkSamples(image,window,plot);
            if(!c->brushReady()||!c->scopePlotCurrent()||s->scopeInk<64){s->settle=0;return;}
            if(!image.save(reportPath+".layers.png")){finish("Cannot retain re-enabled English layer controls");return;}finish({});
        }
    });
    c->setLanguage("zh_CN");window->setProperty("workspaceIndex",1);window->resize(1180,800);window->requestActivate();
    QImage image(2203,1207,QImage::Format_RGBA64);image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(12000+x*4,14000+y*5,18000+(x+y)*2,65535);
    if(!dir->isValid()||!image.save(source)||!c->importFile(QUrl::fromLocalFile(source))){finish("Cannot create owned brush fixture");return;}
    s->sourceHash=fileHash(source);timer->start();
}

void startSonyBrushCanvasSmokeRun(PhotoController *c,QQuickWindow *window,const QString &sourcePath,const QString &reportPath) {
    // Full photographs stay beside the PRIVATE report supplied by the wrapper.
    // Only numerical evidence and the cropped controls panel are published.
    const QFileInfo input(sourcePath),output(reportPath);
    if(QDir::cleanPath(input.absoluteFilePath())==QDir::cleanPath(output.absoluteFilePath())
        ||(!output.canonicalFilePath().isEmpty()&&output.canonicalFilePath()==input.canonicalFilePath())) {
        QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;
    }
    struct State {
        QElapsedTimer clock;int phase=0,settle=0,history=0,delta=0,undoDelta=999,redoDelta=999,ink=0;
        bool done=false,transient=false,escape=false,oneUndo=false,display=false,undoRedo=false,reopen=false,loadedCancel=false,scopeControls=false,scopes=false,ready=false;
        bool selectedDelete=false;int guideBaseInk=0,guideInk=0,selectionHistory=0;
        double exposure=0;QByteArray sourceHash;QJsonObject initial,painted,captures;QColor baseline,paintedColor;
    };
    auto s=std::make_shared<State>();s->clock.start();auto *timer=new QTimer(c);timer->setInterval(50);
    const auto difference=[](const QColor &a,const QColor &b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});};
    const auto capture=[=](const QString &label,const QImage &image) {
        auto *panel=child(window->contentItem(),"developRightPanel");if(image.isNull()||!panel||!panel->isVisible())return false;
        const auto a=panel->mapToScene({0,0}),b=panel->mapToScene({panel->width(),panel->height()});
        const auto rect=SmokeCaptureCoordinates::region(image,QRectF(a,b),window->effectiveDevicePixelRatio());
        if(rect.isEmpty())return false;
        const auto full=reportPath+"."+label+".png",controls=reportPath+"."+label+"-controls.png";
        if(!image.save(full)||!image.copy(rect).save(controls))return false;
        s->captures[label]=QJsonObject{{"sha256",QString::fromLatin1(fileHash(full))},{"controls_sha256",QString::fromLatin1(fileHash(controls))},
            {"width",image.width()},{"height",image.height()},{"controls_width",rect.width()},{"controls_height",rect.height()},
            {"window_width",window->width()},{"window_height",window->height()},{"scene_width",window->contentItem()->width()},
            {"scene_height",window->contentItem()->height()},{"device_scale",window->effectiveDevicePixelRatio()},{"image_dpr",image.devicePixelRatio()}};return true;
    };
    const auto finish=[=](const QString &error) {
        if(s->done)return;s->done=true;timer->stop();if(!error.isEmpty())capture("failure",window->grabWindow());
        const auto budget=BrushRenderBudget::sharedDefault()->snapshot();const bool unchanged=fileHash(sourcePath)==s->sourceHash;
        const bool admitted=budget.limit==BrushRenderBudget::DefaultBytes&&budget.peak>0&&budget.peak<=budget.limit&&budget.charged<=budget.limit;
        const bool full=c->currentIsRaw()&&c->brushFrameSize()==QSize(7028,4688);
        const bool ok=error.isEmpty()&&s->transient&&s->escape&&s->oneUndo&&s->display&&s->undoRedo&&s->reopen&&s->loadedCancel
            &&s->scopeControls&&s->scopes&&s->ready&&s->selectedDelete&&brushControls(window)["critical_visible"].toBool()
            &&!brushControls(window)["pending"].toBool()&&admitted&&unchanged&&full&&s->clock.elapsed()<60000
            &&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        const QJsonObject report{{"passed",ok},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"elapsed_ms",s->clock.elapsed()},
            {"deadline_ms",60000},{"phase",s->phase},{"error",error},{"synthetic_fixture",false},{"source_override",false},
            {"source_kind","public-sony-arw"},{"source_bytes",input.size()},{"source_sha256",QString::fromLatin1(s->sourceHash)},
            {"settings_isolated",QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool()},
            {"source_pixels",32947264},{"source_width",c->brushFrameSize().width()},{"source_height",c->brushFrameSize().height()},
            {"source_is_raw",c->currentIsRaw()},{"source_unchanged",unchanged},{"live_uncommitted",s->transient},{"escape_cancel",s->escape},
            {"one_undo",s->oneUndo},{"display_changed",s->display},{"display_rgb_delta",s->delta},{"undo_redo",s->undoRedo},
            {"undo_rgb_delta",s->undoDelta},{"redo_rgb_delta",s->redoDelta},{"project_reopen",s->reopen},{"loaded_layer_cancel",s->loadedCancel},
            {"selected_stroke_delete",s->selectedDelete},{"selected_guide_new_ink_samples",s->guideInk},
            {"brush_controls_reveal",brushControls(window)["critical_visible"].toBool()&&!brushControls(window)["pending"].toBool()},
            {"brush_controls",brushControls(window)},
            {"scope_controls",s->scopeControls},{"full_scopes",s->scopes},{"scope_pixels",qint64(c->scopesPixelCount())},
            {"scope_plot_pixels",qint64(c->scopePlotPixels())},{"scope_display_ink_samples",s->ink},{"brush_pointer_ready",s->ready},
            {"layer_exposure",s->exposure},{"budget_admitted",admitted},{"operation_budget_bytes",budget.limit},
            {"operation_peak_reserved_bytes",budget.peak},{"operation_live_charged_bytes",budget.charged},
            {"backend",c->processingBackend()},{"gpu_active",c->gpuActive()},{"producer","cpu-reference"},
            {"native_gpu_brush",false},{"native_float_raw_decoder",false},{"captures",s->captures}};
        QSaveFile file(reportPath);const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);
        const bool saved=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();QCoreApplication::exit(ok&&saved?0:1);
    };
    QObject::connect(timer,&QTimer::timeout,c,[=] {
        if(s->clock.elapsed()>=60000){finish("Real Sony brush GUI exceeded 60000ms");return;}
        if(!c->brushReady()||!previewDisplayedReady(c,window))return;
        if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!c->gpuActive())return;
        auto *canvas=child(window->contentItem(),"photoCanvas"),*overlay=child(window->contentItem(),"brushCanvasOverlay");if(!canvas||!overlay)return;
        if(!c->currentIsRaw()||c->brushFrameSize()!=QSize(7028,4688)){finish("The pinned Sony source was not loaded at its full native dimensions");return;}
        const auto point=[&](double x,double y){return overlay->mapToScene({overlay->width()*x,overlay->height()*y});};
        const auto readFrame=[&](QColor *color) {
            const auto p=point(.5,.6);const auto image=window->grabWindow();
            if(image.isNull()||!c->brushReady()||!previewDisplayedReady(c,window)||p!=point(.5,.6))return QImage{};
            *color=sample(image,window,p);return color->isValid()?image:QImage{};
        };
        if(s->phase==0) {
            s->initial=c->gpuPlan(true).state.toJson();s->history=cursor(c);
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas")))return;s->phase=1;s->settle=0;
        }else if(s->phase==1) {
            if(!overlay->isEnabled()||++s->settle<3)return;auto *slider=child(window->contentItem(),"brushExposureSlider");
            if(!exposed(window,slider)){finish("Large-image brush controls are not visible");return;}
            const auto p=slider->mapToScene({slider->width()*.62,slider->height()/2});mouse(window,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);
            s->exposure=slider->property("value").toDouble();if(s->exposure<.25||s->exposure>1.5){finish("Cannot set the native brush exposure control");return;}s->phase=2;
        }else if(s->phase==2) {
            if(!overlay->isEnabled())return;mouse(window,QEvent::MouseButtonPress,point(.3,.6),Qt::LeftButton,Qt::LeftButton);mouse(window,QEvent::MouseMove,point(.5,.6),Qt::NoButton,Qt::LeftButton);
            s->transient=c->brushDrawing()&&masks(c).isEmpty()&&cursor(c)==s->history;key(window,Qt::Key_Escape);mouse(window,QEvent::MouseButtonRelease,point(.5,.6),Qt::LeftButton,Qt::NoButton);
            s->escape=!c->brushDrawing()&&masks(c).isEmpty()&&cursor(c)==s->history;
            if(!s->transient||!s->escape){finish("Real-source transient stroke/Escape changed committed intent");return;}s->phase=3;s->settle=0;
        }else if(s->phase==3) {
            if(!overlay->isEnabled()||++s->settle<3)return;const auto image=readFrame(&s->baseline);if(image.isNull())return;
            const int maximum=std::max({s->baseline.red(),s->baseline.green(),s->baseline.blue()});if(maximum<30||maximum>245){finish("The real photograph probe is not a visible midtone");return;}
            if(!capture("before",image)){finish("Cannot retain private native baseline evidence");return;}
            mouse(window,QEvent::MouseButtonPress,point(.3,.6),Qt::LeftButton,Qt::LeftButton);
            for(int i=1;i<=8;++i)mouse(window,QEvent::MouseMove,point(.3+.4*i/8,.6),Qt::NoButton,Qt::LeftButton);
            mouse(window,QEvent::MouseButtonRelease,point(.7,.6),Qt::LeftButton,Qt::NoButton);s->painted=masks(c);
            const auto doc=c->gpuPlan(true).state.brushMasks;s->oneUndo=cursor(c)==s->history+1&&doc.layers.size()==1&&doc.layers[0].strokes.size()==1&&std::abs(doc.layers[0].exposureEv-s->exposure)<1e-9;
            if(!s->oneUndo){finish("Real-source pointer stroke is not one committed Undo");return;}s->phase=4;
        }else if(s->phase==4) {
            const auto image=readFrame(&s->paintedColor);if(image.isNull())return;s->delta=difference(s->paintedColor,s->baseline);if(s->delta<3)return;
            s->display=true;if(!capture("painted",image)||!click(window,child(window->contentItem(),"undoEditButton"))){finish("Cannot capture painted source or click native Undo");return;}s->phase=5;
        }else if(s->phase==5) {
            QColor color;const auto image=readFrame(&color);if(image.isNull())return;s->undoDelta=difference(color,s->baseline);
            if(!masks(c).isEmpty()||cursor(c)!=s->history){finish("Native real-source Undo retained intent");return;}if(s->undoDelta>2)return;
            if(!capture("undo",image)||!click(window,child(window->contentItem(),"redoEditButton"))){finish("Cannot capture Undo or click native Redo");return;}s->phase=6;
        }else if(s->phase==6) {
            QColor color;const auto image=readFrame(&color);if(image.isNull())return;s->redoDelta=difference(color,s->paintedColor);if(s->redoDelta>2)return;
            s->undoRedo=masks(c)==s->painted&&cursor(c)==s->history+1;
            if(!s->undoRedo||!selectFirstLayer(window)){finish("Real-source Redo changed intent or cannot select its restored layer");return;}s->phase=12;
        }else if(s->phase==12) {
            QColor color;const auto image=readFrame(&color);if(image.isNull())return;
            s->guideBaseInk=selectedGuideInk(image,window,c,overlay);s->selectionHistory=cursor(c);
            if(!selectFirstStroke(window)){finish("Cannot select the stored stroke on the full Sony source");return;}s->phase=13;s->settle=0;
        }else if(s->phase==13) {
            if(++s->settle<3)return;QColor color;const auto image=readFrame(&color);if(image.isNull())return;
            s->guideInk=selectedGuideInk(image,window,c,overlay)-s->guideBaseInk;if(s->guideInk<16)return;
            if(masks(c)!=s->painted||cursor(c)!=s->selectionHistory||!capture("selected-stroke",image)){finish("Selecting the Sony stroke changed intent or cannot be captured");return;}
            auto *button=child(window->contentItem(),"removeSelectedBrushStroke");
            if(!exposed(window,button)||!click(window,button)||!masks(c).isEmpty()||cursor(c)!=s->selectionHistory+1){finish("Selected Sony stroke deletion did not prune its layer as one Undo");return;}s->phase=14;
        }else if(s->phase==14) {
            QColor color;const auto image=readFrame(&color);if(image.isNull()||difference(color,s->baseline)>2)return;
            if(!click(window,child(window->contentItem(),"undoEditButton"))||masks(c)!=s->painted||cursor(c)!=s->selectionHistory){finish("Native Undo did not restore the selected Sony stroke");return;}s->phase=15;
        }else if(s->phase==15) {
            QColor color;const auto image=readFrame(&color);if(image.isNull()||difference(color,s->paintedColor)>2)return;
            s->selectedDelete=masks(c)==s->painted&&cursor(c)==s->selectionHistory;
            if(!s->selectedDelete||!c->createProject(QUrl::fromLocalFile(output.absolutePath()),"SonyBrushCanvas")||!c->flushEdits()){finish("Cannot persist the real-source painted project");return;}
            if(!c->openProject(QUrl::fromLocalFile(c->projectPath()))){finish("Cannot reopen the real-source painted project");return;}s->phase=7;
        }else if(s->phase==7) {
            QColor color;const auto image=readFrame(&color);if(image.isNull()||difference(color,s->paintedColor)>2)return;s->reopen=masks(c)==s->painted;
            auto unrelated=c->gpuPlan(true).state.toJson();unrelated.remove("brushMasks");if(!s->reopen||unrelated!=s->initial){finish("Reopening changed brush or unrelated adjustment intent");return;}
            if(!capture("reopen",image)||!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("Cannot retain reopened UI or re-enter brush mode");return;}s->phase=8;
        }else if(s->phase==8) {
            if(!overlay->isEnabled())return;if(!selectFirstLayer(window)){finish("Cannot select the reopened real-source brush layer");return;}s->phase=9;s->settle=0;
        }else if(s->phase==9) {
            if(!overlay->isEnabled()||++s->settle<3)return;const auto before=cursor(c);
            mouse(window,QEvent::MouseButtonPress,point(.2,.45),Qt::LeftButton,Qt::LeftButton);const bool drawing=c->brushDrawing();key(window,Qt::Key_Escape);mouse(window,QEvent::MouseButtonRelease,point(.2,.45),Qt::LeftButton,Qt::NoButton);
            s->loadedCancel=drawing&&!c->brushDrawing()&&masks(c)==s->painted&&cursor(c)==before;
            auto *choice=child(window->contentItem(),"scopeModeChoice");if(!s->loadedCancel||!exposed(window,choice)||!click(window,choice)){finish("Restored layer input/cancel or scope selector failed");return;}
            key(window,Qt::Key_Home);key(window,Qt::Key_Down);key(window,Qt::Key_Return);s->phase=10;s->settle=0;
        }else if(s->phase==10) {
            if(++s->settle<3)return;auto *toggle=child(window->contentItem(),"exactScopesCheck");
            if(c->scopeMode()!="waveform"||!exposed(window,toggle)||!click(window,toggle)){finish("Cannot choose full waveform through native controls");return;}
            s->scopeControls=c->exactScopes();if(!s->scopeControls){finish("Native full-resolution checkbox did not enable exact statistics");return;}s->phase=11;s->settle=0;
        }else if(s->phase==11) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=32947264||c->scopePlotPixels()!=32947264||++s->settle<3)return;
            auto *plot=child(window->contentItem(),"scopePlotImage"),*hint=child(window->contentItem(),"maskToolHint");
            if(!plot||plot->property("status").toInt()!=1||plot->property("source").toUrl()!=QUrl(c->scopePlotUrl()))return;
            QColor color;const auto image=readFrame(&color);if(image.isNull()||!c->scopePlotCurrent())return;s->ink=scopeInkSamples(image,window,plot);if(s->ink<64)return;
            s->ready=overlay->isEnabled()&&hint&&hint->property("text").toString()==c->brushStatus();s->scopes=c->exactScopes()&&c->scopeMode()=="waveform"&&masks(c)==s->painted;
            if(!s->ready||!capture("final",image)){finish("Restored large-source canvas is not ready or cannot be captured");return;}finish({});
        }
    });
    s->sourceHash=fileHash(sourcePath);
    if(input.size()!=69120000||s->sourceHash!="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d") {
        QTimer::singleShot(0,c,[=]{finish("The required public Sony07 ARW is missing or its SHA256 differs");});return;
    }
    window->setProperty("workspaceIndex",1);window->resize(1180,800);window->requestActivate();c->setExactScopes(false);c->setScopeMode("histogram");
    if(!c->importFile(QUrl::fromLocalFile(input.absoluteFilePath()))){QTimer::singleShot(0,c,[=]{finish("Cannot import the real Sony source");});return;}timer->start();
}
