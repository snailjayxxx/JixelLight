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
#include <QImageWriter>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QRgba64>
#include <QDir>
#include <QFileInfo>
#include <QPointingDevice>
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
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
        {"automatic_owner",panel&&panel->property("brushAutoPositionOwned").toBool()},
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
        bool panChecked=false,panReadonly=false,panMapping=false,panStartedBrush=false;
        int panHistory=0;QJsonObject panState;QRectF beforePan;QPointF panCenter;QJsonObject panRecord;
        bool panGuardsChecked=false,panGeometryChecked=false,panSwitchCancelled=false;QJsonArray panGuards,panViews,panHints;
        int panOriginalIndex=-1;
        QJsonObject panViewState,panWindowCheck;int panViewHistory=0;QPointF panViewStart,panViewExpected,panAfterBasic;QRectF panViewFrame;
        QSize panViewSize;double panViewZoom=0;QString panViewTag,panViewMask;bool panViewOwnership=false;ulong panStamp=1000;int focusBootstrapCount=0;
        std::shared_ptr<QPointingDevice> panPen;
        QElapsedTimer clock;int phase=0,settle=0,history=0,delta=0,scopeInk=0;bool finished=false;
        int windowGrabCalls=0,captureEncodeCalls=0;qint64 windowGrabNs=0,windowGrabMaxNs=0,captureEncodeNs=0;
        bool capacityPreviewStatistics=false,capacityExactStatistics=false,scopeControlsReadonly=true;
        bool transient=false,escape=false,oneUndo=false,display=false,undoRedo=false,reopen=false,zoom=false,scopes=false;
        bool settings=false,photoReady=false;double exposure=1;QJsonObject painted;QColor baseline,lastColor;QRectF frame;QByteArray sourceHash;
        bool selected=false,evDrag=false,toggle=false,lastStroke=false,prune=false,deleteUndo=false,english=false;
        bool pointerReady=false;QString actualHint,expectedHint;
        bool disabledZh=false,disabledEn=false,disabledInput=false;int disabledCursor=-1;QString disabledHint,expectedDisabledHint;
        bool selectedDelete=false;int guideBaseInk=0,guideInk=0,selectedDelta=0;QColor selectedBefore;QJsonObject strokeReduced;
        bool revealed=false,manualScroll=false,manualLocale=false,localeRequest=false;int revealHistory=0,revealWaitTicks=0;QJsonObject revealMasks;QRectF revealFrame;
        bool capacity=false,capacityRecovery=false,capacityTargets=false,capacityExisting=false;int capacityHistory=0;
        QJsonObject capacityProbe,capacityFull,capacityRemoved;QJsonArray capacityCases,capacityReadyCases;
        double layerExposure=0;QJsonObject layers,extended,edited,reduced,sole;
    };
    auto s=std::make_shared<State>();s->clock.start();auto dir=std::make_shared<QTemporaryDir>();
    s->panPen=std::make_shared<QPointingDevice>("Controlled pan ownership pen",987654330,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(s->panPen.get());
    auto *timer=new QTimer(c);timer->setInterval(50);const auto source=dir->filePath("owned-brush.png");
    const auto grab=[=] {
        QElapsedTimer captureClock;captureClock.start();const auto image=window->grabWindow();const auto ns=captureClock.nsecsElapsed();
        ++s->windowGrabCalls;s->windowGrabNs+=ns;s->windowGrabMaxNs=std::max(s->windowGrabMaxNs,ns);return image;
    };
    const auto save=[=](const QImage &image,const QString &path) {
        // Evidence stays lossless; default PNG compression needlessly consumes
        // the same GUI budget that must cover actual input and full statistics.
        QImageWriter writer(path,"PNG");writer.setCompression(1);QElapsedTimer encodeClock;encodeClock.start();
        const bool written=writer.write(image);s->captureEncodeNs+=encodeClock.nsecsElapsed();++s->captureEncodeCalls;return written;
    };
    const auto finish=[=](const QString &error) {
        if(s->finished)return;s->finished=true;timer->stop();const bool unchanged=fileHash(source)==s->sourceHash;
        const bool gpu=qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU");
        const bool ok=error.isEmpty()&&s->panChecked&&s->panReadonly&&s->panMapping&&s->panGuardsChecked&&s->panGeometryChecked&&s->panGuards.size()==14&&s->panViews.size()==3&&s->panHints.size()==2&&s->settings&&s->transient&&s->escape&&s->oneUndo&&s->display&&s->undoRedo&&s->reopen&&s->zoom&&s->scopes
            &&s->capacityPreviewStatistics&&s->capacityExactStatistics&&s->scopeControlsReadonly&&s->photoReady&&s->pointerReady&&s->selected&&s->evDrag&&s->toggle&&s->lastStroke&&s->prune&&s->deleteUndo&&s->english
            &&s->disabledZh&&s->disabledEn&&s->selectedDelete&&s->revealed&&s->manualLocale&&s->localeRequest&&s->capacity&&s->capacityRecovery&&s->capacityTargets&&s->scopeInk>=64&&unchanged&&(!gpu||c->gpuActive());
        if(!error.isEmpty())save(grab(),reportPath+".failure.png");
        const QJsonObject report{{"pan_protocol_version",1},{"logical_window_size",QJsonArray{window->width(),window->height()}},{"pan_hint_records",s->panHints},{"window_focus_injection","Qt-QPA-controlled-window-focus"},{"focus_bootstrap_count",s->focusBootstrapCount},{"physical_OS_activation_verified",false},{"pan_window_check",s->panWindowCheck},{"pan_guards_checked",s->panGuardsChecked},{"pan_geometry_checked",s->panGeometryChecked},{"pan_guard_records",s->panGuards},{"pan_view_records",s->panViews},{"pan_checked",s->panChecked},{"pan_readonly",s->panReadonly},{"pan_mapping",s->panMapping},{"pan_started_brush",s->panStartedBrush},{"pan_record",s->panRecord},{"product_UI_acceptance",false},{"passed",ok},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"elapsed_ms",s->clock.elapsed()},
            {"capture_png_compression",1},{"capture_encode_calls",s->captureEncodeCalls},{"capture_encode_total_ms",s->captureEncodeNs/1e6},
            {"window_grab_calls",s->windowGrabCalls},{"window_grab_total_ms",s->windowGrabNs/1e6},{"window_grab_max_ms",s->windowGrabMaxNs/1e6},
            {"capacity_preview_statistics",s->capacityPreviewStatistics},{"capacity_exact_statistics_restored",s->capacityExactStatistics},{"scope_controls_readonly",s->scopeControlsReadonly},
            {"exact_scopes_requested",c->exactScopes()},{"scope_plot_current",c->scopePlotCurrent()},{"scope_plot_pixels",qint64(c->scopePlotPixels())},
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
            {"manual_locale_scroll_preserved",s->manualLocale},{"language_reveal_requested",s->localeRequest},
            {"reveal_wait_ticks",s->revealWaitTicks},
            {"capacity_guard",s->capacity&&s->capacityRecovery&&s->capacityTargets},{"capacity_probe",s->capacityProbe},
            {"capacity_target_specific",s->capacityTargets},{"capacity_recovery",s->capacityRecovery},
            {"capacity_cases",s->capacityCases},{"capacity_ready_cases",s->capacityReadyCases},
            {"full_scopes",s->scopes},{"scope_pixels",qint64(c->scopesPixelCount())},{"gpu_active",c->gpuActive()},
            {"backend",c->processingBackend()},{"producer","cpu-reference"},{"native_gpu_brush",false},{"native_float_raw_decoder",false}};
        QSaveFile output(reportPath);const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);
        const bool saved=output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.commit();QCoreApplication::exit(ok&&saved?0:1);
    };
    QObject::connect(timer,&QTimer::timeout,c,[=] {
        if(s->clock.elapsed()>=45000){finish("Brush pointer integration timed out");return;}
        if(!window->isActive()) {
            // Controlled platform focus, like the tablet packets below. This
            // exercises actual QQuickWindow/QML state, not OS activation policy.
            QWindowSystemInterface::handleFocusWindowChanged(window);
            QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
            ++s->focusBootstrapCount;s->settle=0;return;
        }
        if(!c->brushReady()){s->settle=0;return;}
        if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!c->gpuActive())return;
        auto *canvas=child(window->contentItem(),"photoCanvas"),*overlay=child(window->contentItem(),"brushCanvasOverlay");
        if(!canvas||!overlay)return;
        const auto point=[&](double x,double y){return overlay->mapToScene({overlay->width()*x,overlay->height()*y});};
        const auto space=[&](QEvent::Type type,Qt::KeyboardModifiers mods=Qt::NoModifier,bool repeat=false) {
            QKeyEvent event(type,Qt::Key_Space,mods,QStringLiteral(" "),repeat,1);QCoreApplication::sendEvent(window,&event);
        };
        const auto navigation=[&]{return overlay->property("navigationActive").toBool();};
        const auto pen=[&](QPointF local,Qt::MouseButtons buttons) {
            QWindowSystemInterface::handleTabletEvent(window,s->panStamp+=20,s->panPen.get(),QHighDpi::toNativePixels(local,window),QHighDpi::toNativePixels(window->mapToGlobal(local),window),buttons,buttons.testFlag(Qt::LeftButton)?.4:0,0,0,0,0,0);
            QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
        };
        const auto beginViewPan=[&](QString tag,QPointF delta,bool brush=true) {
            s->panViewTag=tag;s->panViewState=c->gpuPlan(true).state.toJson();s->panViewHistory=cursor(c);
            s->panViewMask=brush?"brush":"";s->panViewOwnership=false;
            s->panViewStart={canvas->property("centerX").toDouble(),canvas->property("centerY").toDouble()};
            s->panViewFrame=c->previewFrameRect();s->panViewSize=c->brushFrameSize();s->panViewZoom=canvas->property("zoom").toDouble();
            const double dpr=window->effectiveDevicePixelRatio();
            s->panViewExpected=s->panViewStart+QPointF(s->panViewFrame.width()<1?-delta.x()*dpr/(s->panViewSize.width()*s->panViewZoom):0,s->panViewFrame.height()<1?-delta.y()*dpr/(s->panViewSize.height()*s->panViewZoom):0);
            if(brush){overlay->forceActiveFocus();space(QEvent::KeyPress);}const auto from=point(.5,.6);
            mouse(window,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
            auto *pan=child(window->contentItem(),"canvasPanPointer");
            s->panViewOwnership=pan&&pan->isEnabled()&&pan->property("pressed").toBool()&&pan->property("cursorShape").toInt()==int(Qt::ClosedHandCursor)
                &&canvas->property("maskTool").toString()==s->panViewMask&&!c->brushDrawing()&&navigation()==brush;
            mouse(window,QEvent::MouseMove,from+delta,Qt::NoButton,Qt::LeftButton);
            mouse(window,QEvent::MouseButtonRelease,from+delta,Qt::LeftButton,Qt::NoButton);if(brush)space(QEvent::KeyRelease);
        };
        const auto completeViewPan=[&] {
            const QPointF actual(canvas->property("centerX").toDouble(),canvas->property("centerY").toDouble());
            const bool same=!c->brushDrawing()&&!navigation()&&c->gpuPlan(true).state.toJson()==s->panViewState&&cursor(c)==s->panViewHistory&&canvas->property("maskTool").toString()==s->panViewMask;
            const bool mapping=std::abs(actual.x()-s->panViewExpected.x())<1e-9&&std::abs(actual.y()-s->panViewExpected.y())<1e-9&&c->previewFrameRect()!=s->panViewFrame;
            auto *hint=child(window->contentItem(),"brushRadiusHint");const QString expected=c->language()=="zh_CN"?QStringLiteral("空格＋拖动平移"):QStringLiteral("Space + drag to pan");
            const bool visibleCaption=hint&&exposed(window,hint)&&hint->property("text").toString().startsWith(expected)&&hint->width()<=canvas->width()-15;
            const bool caption=s->panViewMask=="brush"?visibleCaption:(hint&&!hint->isVisible());
            s->panViews.append(QJsonObject{{"case",s->panViewTag},{"mask_tool",s->panViewMask},{"pointer_owned_closed_hand",s->panViewOwnership},{"document_history_unchanged",same},{"mapping_exact",mapping},{"center_before",QJsonArray{s->panViewStart.x(),s->panViewStart.y()}},{"frame_before",QJsonArray{s->panViewFrame.x(),s->panViewFrame.y(),s->panViewFrame.width(),s->panViewFrame.height()}},{"actual_center",QJsonArray{actual.x(),actual.y()}},{"expected_center",QJsonArray{s->panViewExpected.x(),s->panViewExpected.y()}},{"output_size",QJsonArray{s->panViewSize.width(),s->panViewSize.height()}},{"zoom",s->panViewZoom},{"dpr",window->effectiveDevicePixelRatio()},{"hint_visible_bounded",visibleCaption},{"hint_policy_correct",caption},{"hint",hint?hint->property("text").toString():QString{}}});
            return same&&mapping&&caption&&s->panViewOwnership&&save(grab(),reportPath+".pan-"+s->panViewTag+".png");
        };
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
        const auto capacityBlocked=[&](const QString &tag,const QString &expected,int expectedDabs,bool newAllowed,bool capture=false) {
            auto *hint=child(window->contentItem(),"maskToolHint"),*pointer=child(window->contentItem(),"brushPointerArea"),*add=child(window->contentItem(),"newBrushLayerCanvas");
            const auto before=masks(c);const auto history=cursor(c);const auto frame=c->previewFrameRect();
            int dabs=0;for(const auto &layer:c->brushLayers())dabs+=layer.toMap()["dabs"].toInt();
            const auto message=hint?hint->property("text").toString():QString{};const int shape=pointer?pointer->property("cursorShape").toInt():-1;
            const bool newEnabled=add&&add->isEnabled(),visible=exposed(window,hint);
            mouse(window,QEvent::MouseButtonPress,point(.3,.4),Qt::LeftButton,Qt::LeftButton);const bool started=c->brushDrawing();
            mouse(window,QEvent::MouseMove,point(.6,.4),Qt::NoButton,Qt::LeftButton);
            const bool captured=!capture||save(grab(),reportPath+".capacity.png");
            if(started)key(window,Qt::Key_Escape);mouse(window,QEvent::MouseButtonRelease,point(.6,.4),Qt::LeftButton,Qt::NoButton);
            const bool documentSame=masks(c)==before,historySame=cursor(c)==history,roiSame=c->previewFrameRect()==frame;
            const bool ignored=!started&&!c->brushDrawing()&&documentSame&&historySame&&roiSame;
            const QJsonObject record{{"case",tag},{"language",c->language()},{"dabs",dabs},{"layers",c->brushLayers().size()},{"cursor_shape",shape},
                {"hint",message},{"hint_visible",visible},{"transient_started",started},{"input_ignored",ignored},
                {"document_unchanged",documentSame},{"history_unchanged",historySame},{"roi_unchanged",roiSame},{"new_layer_enabled",newEnabled}};
            s->capacityCases.append(record);if(capture)s->capacityProbe=record;
            return captured&&visible&&dabs==expectedDabs&&shape==int(Qt::ForbiddenCursor)&&message==expected&&ignored&&add&&newEnabled==newAllowed;
        };
        const auto capacityReady=[&](const QString &tag,bool newAllowed) {
            auto *hint=child(window->contentItem(),"maskToolHint"),*pointer=child(window->contentItem(),"brushPointerArea"),*add=child(window->contentItem(),"newBrushLayerCanvas");
            const auto before=masks(c);const auto history=cursor(c);const auto frame=c->previewFrameRect();
            const bool cross=pointer&&pointer->property("cursorShape").toInt()==int(Qt::CrossCursor);
            const bool message=hint&&hint->property("text").toString()==c->brushStatus()&&exposed(window,hint);
            mouse(window,QEvent::MouseButtonPress,point(.3,.4),Qt::LeftButton,Qt::LeftButton);const bool started=c->brushDrawing();
            mouse(window,QEvent::MouseMove,point(.6,.4),Qt::NoButton,Qt::LeftButton);
            if(started)key(window,Qt::Key_Escape);mouse(window,QEvent::MouseButtonRelease,point(.6,.4),Qt::LeftButton,Qt::NoButton);
            const bool preserved=!c->brushDrawing()&&masks(c)==before&&cursor(c)==history&&c->previewFrameRect()==frame&&canvas->property("maskTool").toString()=="brush";
            const bool newEnabled=add&&add->isEnabled();
            s->capacityReadyCases.append(QJsonObject{{"case",tag},{"drawing_started",started},{"state_preserved",preserved},{"cross_cursor",cross},{"ready_hint",message},{"new_layer_enabled",newEnabled}});
            return started&&preserved&&cross&&message&&add&&newEnabled==newAllowed;
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
            const auto image=grab();if(image.isNull()){finish("Cannot read baseline window");return;}
            if(!c->brushReady()||!previewDisplayedReady(c,window)||position!=point(.5,.6)){s->settle=0;return;}
            s->baseline=sample(image,window,position);if(!s->baseline.isValid())return;s->photoReady=std::max({s->baseline.red(),s->baseline.green(),s->baseline.blue()})>30;
            if(!s->photoReady){s->settle=0;return;} // Do not accept an unloaded black image as the baseline.
            if(!save(image,reportPath+".before.png")){finish("Cannot save brush baseline");return;}
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
            const auto image=grab();if(image.isNull()){finish("Cannot read painted window");return;}
            if(!c->brushReady()||!c->scopePlotCurrent()||!previewDisplayedReady(c,window)||position!=point(.5,.6)){s->settle=0;return;}
            const auto color=sample(image,window,position);if(!color.isValid())return;s->lastColor=color;s->delta=std::max({std::abs(color.red()-s->baseline.red()),std::abs(color.green()-s->baseline.green()),std::abs(color.blue()-s->baseline.blue())});
            s->display=s->delta>=3;s->scopes=true;
            if(!s->display){s->settle=0;return;} // The asynchronous item may still be presenting its previous texture.
            if(!save(image,reportPath+".painted.png")){finish("Cannot save actual painted display");return;}
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
            if(!s->reopen||!save(grab(),reportPath+".reopen.png")){finish("Reopened UI lost painted intent");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("Cannot re-enter brush tool");return;}
            c->setExactScopes(true);c->setScopeMode("waveform");
            canvas->setProperty("zoom",2.0);s->phase=7;s->settle=0;
        }else if(s->phase==7) {
            s->frame=c->previewFrameRect();if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||s->frame.width()>=.98||++s->settle<3)return;
            if(!s->panChecked) {
                s->panState=c->gpuPlan(true).state.toJson();s->panHistory=cursor(c);s->beforePan=c->previewFrameRect();
                s->panCenter={canvas->property("centerX").toDouble(),canvas->property("centerY").toDouble()};
                overlay->forceActiveFocus();
                QKeyEvent down(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier);QCoreApplication::sendEvent(window,&down);
                const auto from=point(.5,.6);mouse(window,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
                s->panStartedBrush=c->brushDrawing();
                if(s->panStartedBrush){key(window,Qt::Key_Escape);mouse(window,QEvent::MouseButtonRelease,from,Qt::LeftButton,Qt::NoButton);finish("Space-drag navigation started an actual brush stroke");return;}
                mouse(window,QEvent::MouseMove,from+QPointF(24,-12),Qt::NoButton,Qt::LeftButton);
                QKeyEvent up(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);QCoreApplication::sendEvent(window,&up);
                mouse(window,QEvent::MouseMove,from+QPointF(48,-24),Qt::NoButton,Qt::LeftButton);
                mouse(window,QEvent::MouseButtonRelease,from+QPointF(48,-24),Qt::LeftButton,Qt::NoButton);
                s->phase=901;s->settle=0;return;
            }
            mouse(window,QEvent::MouseButtonPress,point(.25,.6),Qt::LeftButton,Qt::LeftButton);
            mouse(window,QEvent::MouseMove,point(.75,.6),Qt::NoButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,point(.75,.6),Qt::LeftButton,Qt::NoButton);
            const auto doc=c->gpuPlan(true).state.brushMasks;
            if(doc.layers.size()!=2){finish("Zoom pointer stroke did not create the selected new layer");return;}
            const auto &dabs=doc.layers.back().strokes.back().dabs;
            s->zoom=std::abs(dabs.front().center.x-(s->frame.x()+.25*s->frame.width()))<1e-6
                &&std::abs(dabs.back().center.x-(s->frame.x()+.75*s->frame.width()))<1e-6
                &&std::abs(dabs.front().center.y-(s->frame.y()+.6*s->frame.height()))<1e-6;
            s->phase=8;s->settle=0;
        }else if(s->phase==901) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||!c->scopePlotCurrent()||++s->settle<3)return;
            const double dpr=window->effectiveDevicePixelRatio();
            const QPointF wanted=s->panCenter+QPointF(-48*dpr/(2203*2.0),24*dpr/(1207*2.0));
            const QPointF actual(canvas->property("centerX").toDouble(),canvas->property("centerY").toDouble());
            s->panReadonly=!c->brushDrawing()&&c->gpuPlan(true).state.toJson()==s->panState&&cursor(c)==s->panHistory&&canvas->property("maskTool").toString()=="brush";
            s->panMapping=std::abs(actual.x()-wanted.x())<1e-9&&std::abs(actual.y()-wanted.y())<1e-9&&c->previewFrameRect()!=s->beforePan;
            s->panRecord={{"actual_center",QJsonArray{actual.x(),actual.y()}},{"expected_center",QJsonArray{wanted.x(),wanted.y()}},{"dpr",dpr},{"source_width",2203},{"source_height",1207},{"zoom",2},{"space_released_before_mouse_lift",true},{"document_history_unchanged",s->panReadonly},{"view_mapping_exact",s->panMapping}};
            if(!s->panReadonly||!s->panMapping||!save(grab(),reportPath+".pan.png")){finish("Space-drag navigation changed intent/history or viewport mapping differs");return;}
            s->panChecked=true;s->panAfterBasic=actual;s->phase=920;s->settle=0;
        }else if(s->phase==920) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||++s->settle<3)return;
            const auto snapshot=c->gpuPlan(true).state.toJson();const int history=cursor(c);const auto center=QPointF(canvas->property("centerX").toDouble(),canvas->property("centerY").toDouble());
            auto *editor=canvas->property("brushEditor").value<QObject*>();if(!editor){finish("PAN_EDITOR_MISSING");return;}
            const double radius=editor->property("radius").toDouble();
            const auto stable=[&]{return !c->brushDrawing()&&c->gpuPlan(true).state.toJson()==snapshot&&cursor(c)==history&&QPointF(canvas->property("centerX").toDouble(),canvas->property("centerY").toDouble())==center&&canvas->property("maskTool").toString()=="brush";};
            const auto guard=[&](QString tag,bool passed){s->panGuards.append(QJsonObject{{"case",tag},{"passed",passed},{"navigation",navigation()},{"drawing",c->brushDrawing()},{"document_history_unchanged",c->gpuPlan(true).state.toJson()==snapshot&&cursor(c)==history}});return passed;};
            overlay->forceActiveFocus();bool modifiers=true;
            for(const auto mod:{Qt::ShiftModifier,Qt::ControlModifier,Qt::AltModifier,Qt::MetaModifier}){space(QEvent::KeyPress,mod);modifiers&=!navigation();space(QEvent::KeyRelease,mod);}
            if(!guard("modifiers-ignored",modifiers&&stable())){finish("PAN_MODIFIERS_CAPTURED");return;}
            space(QEvent::KeyPress,Qt::NoModifier,true);
            if(!guard("orphan-repeat-ignored",!navigation()&&stable())){finish("PAN_REPEAT_CAPTURED");return;}
            space(QEvent::KeyPress);space(QEvent::KeyRelease,Qt::NoModifier,true);
            if(!guard("repeat-release-retains-navigation",navigation()&&stable())){finish("PAN_REPEAT_RELEASE_LOST_OWNER");return;}
            key(window,Qt::Key_BracketRight);key(window,Qt::Key_BracketLeft);
            if(!guard("radius-held-during-navigation",navigation()&&editor->property("radius").toDouble()==radius&&stable())){finish("PAN_RADIUS_CHANGED");return;}
            key(window,Qt::Key_Escape);space(QEvent::KeyRelease);
            if(!guard("escape-keeps-brush-mode",!navigation()&&stable())){finish("PAN_ESCAPE_EXITED_BRUSH");return;}
            space(QEvent::KeyPress);
            const auto findInput=[&](auto&& self,QQuickItem *parent)->QQuickItem* {if(parent->property("placeholderText").toString()=="新预设名称"||parent->property("placeholderText").toString()=="New preset name")return parent;for(auto *child:parent->childItems())if(auto *found=self(self,child))return found;return nullptr;};
            auto *input=findInput(findInput,window->contentItem());if(!input||!click(window,input)){finish("PAN_TEXT_INPUT_MISSING");return;}
            space(QEvent::KeyRelease);const auto text=input->property("text").toString();space(QEvent::KeyPress);space(QEvent::KeyRelease);
            const bool textOwned=input->hasActiveFocus()&&input->property("text").toString()==text+" "&&!navigation()&&stable();input->setProperty("text",text);
            if(!guard("textfield-owns-space-focus-loss",textOwned)){finish("PAN_TEXTFIELD_INTERCEPTED");return;}
            overlay->forceActiveFocus();const auto from=point(.35,.4);
            mouse(window,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);const bool mouseStarted=c->brushDrawing();space(QEvent::KeyPress);bool mouseHeld=c->brushDrawing()&&!navigation();
            key(window,Qt::Key_Escape);space(QEvent::KeyPress);mouseHeld&=!navigation()&&!c->brushDrawing();mouse(window,QEvent::MouseButtonRelease,from,Qt::LeftButton,Qt::NoButton);space(QEvent::KeyPress,Qt::NoModifier,true);mouseHeld&=!navigation();space(QEvent::KeyRelease);
            if(!guard("mouse-held-and-cancelled-until-lift",mouseStarted&&mouseHeld&&stable())){finish("PAN_MOUSE_OWNER_LOST");return;}
            pen(from,Qt::LeftButton);const bool penStarted=c->brushDrawing();space(QEvent::KeyPress);bool penHeld=c->brushDrawing()&&!navigation();key(window,Qt::Key_Escape);
            pen(from,Qt::LeftButton|Qt::RightButton);space(QEvent::KeyPress);penHeld&=!navigation()&&!c->brushDrawing();pen(from,Qt::LeftButton);pen(from,Qt::NoButton);space(QEvent::KeyPress,Qt::NoModifier,true);penHeld&=!navigation();space(QEvent::KeyRelease);
            if(!guard("pen-held-and-cancelled-primary-lift",penStarted&&penHeld&&stable())){finish("PAN_PEN_OWNER_LOST");return;}
            space(QEvent::KeyPress);pen(from,Qt::LeftButton);const bool blocked=navigation()&&!c->brushDrawing();space(QEvent::KeyRelease);pen(from+QPointF(12,0),Qt::LeftButton);bool ignored=!navigation()&&!c->brushDrawing();pen(from,Qt::NoButton);pen(from,Qt::LeftButton);const bool fresh=c->brushDrawing();key(window,Qt::Key_Escape);pen(from,Qt::NoButton);
            if(!guard("pen-during-pan-ignored-until-lift-fresh-press",blocked&&ignored&&fresh&&stable())){finish("PAN_IGNORED_PEN_RESURRECTED");return;}
            space(QEvent::KeyPress);mouse(window,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
            s->panWindowCheck={{"active_before",window->isActive()},{"navigation_before",navigation()},{"center_before",QJsonArray{center.x(),center.y()}},{"overlay_focus_before",overlay->hasActiveFocus()}};
            QWindowSystemInterface::handleFocusWindowChanged(nullptr);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
            s->panWindowCheck["active_after_deactivation"]=window->isActive();s->panWindowCheck["navigation_after_deactivation"]=navigation();
            const bool deactivated=!window->isActive()&&!navigation();mouse(window,QEvent::MouseMove,from+QPointF(30,0),Qt::NoButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,from+QPointF(30,0),Qt::LeftButton,Qt::NoButton);
            QWindowSystemInterface::handleFocusWindowChanged(window);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);space(QEvent::KeyRelease);
            s->panWindowCheck["active_after_reactivation"]=window->isActive();s->panWindowCheck["center_after"]=QJsonArray{canvas->property("centerX").toDouble(),canvas->property("centerY").toDouble()};s->panWindowCheck["stable"]=stable();
            if(!guard("window-deactivation-cancels-held-pan",s->panWindowCheck["active_before"].toBool()&&deactivated&&stable())){finish("PAN_WINDOW_DEACTIVATION_LEAKED");return;}
            overlay->forceActiveFocus();space(QEvent::KeyPress);canvas->setProperty("maskTool",QString{});const bool modeCancelled=!navigation();canvas->setProperty("maskTool","brush");overlay->forceActiveFocus();space(QEvent::KeyRelease);
            if(!guard("mode-exit-cancels-space",modeCancelled&&stable())){finish("PAN_MODE_EXIT_LEAKED");return;}
            space(QEvent::KeyPress);const bool cropStarted=c->beginCrop();const bool cropCancelled=cropStarted&&!navigation()&&canvas->property("maskTool").toString().isEmpty();c->cancelCrop();canvas->setProperty("maskTool","brush");space(QEvent::KeyRelease);
            if(!guard("crop-entry-cancels-space",cropCancelled&&c->gpuPlan(true).state.toJson()==snapshot&&cursor(c)==history)){finish("PAN_CROP_ENTRY_LEAKED");return;}
            s->phase=921;s->settle=0;
        }else if(s->phase==921) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||++s->settle<3)return;
            overlay->forceActiveFocus();space(QEvent::KeyPress);const bool ignored=!navigation()&&canvas->property("zoom").toDouble()==0;space(QEvent::KeyRelease);
            s->panGuards.append(QJsonObject{{"case","fit-ignores-space"},{"passed",ignored},{"navigation",navigation()},{"drawing",c->brushDrawing()},{"document_history_unchanged",c->gpuPlan(true).state.toJson()==s->panState&&cursor(c)==s->panHistory}});
            if(!ignored||s->panGuards.size()!=13){finish("PAN_FIT_OR_GUARD_COUNT");return;}
            canvas->setProperty("zoom",1.0);s->phase=922;s->settle=0;
        }else if(s->phase==922) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||++s->settle<3)return;
            beginViewPan("100-percent",{12,-8});s->phase=923;s->settle=0;
        }else if(s->phase==923) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||!c->scopePlotCurrent()||++s->settle<3)return;
            if(!completeViewPan()){finish("PAN_100_PERCENT_MAPPING");return;}
            c->rotatePhoto(1);c->setCrop(.1,.12,.75,.7);canvas->setProperty("zoom",2.0);s->phase=924;s->settle=0;
        }else if(s->phase==924) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||++s->settle<3)return;
            if(c->brushFrameSize()==QSize(2203,1207)){finish("PAN_GEOMETRY_OUTPUT_NOT_CHANGED");return;}
            beginViewPan("rotated-cropped",{26,-16});s->phase=925;s->settle=0;
        }else if(s->phase==925) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||!c->scopePlotCurrent()||++s->settle<3)return;
            if(!completeViewPan()){finish("PAN_GEOMETRY_OUTPUT_MAPPING");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))||!canvas->property("maskTool").toString().isEmpty()){finish("PAN_PLAIN_MODE_NOT_ENTERED");return;}
            beginViewPan("plain-rotated-cropped",{18,-10},false);s->phase=9251;s->settle=0;
        }else if(s->phase==9251) {
            if(!previewDisplayedReady(c,window)||!c->scopePlotCurrent()||++s->settle<3)return;
            if(!completeViewPan()){finish("PAN_PLAIN_GEOMETRY_OUTPUT_MAPPING");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("PAN_PLAIN_BRUSH_REENTRY_FAILED");return;}
            c->undo();c->undo();canvas->setProperty("centerX",s->panAfterBasic.x());canvas->setProperty("centerY",s->panAfterBasic.y());s->phase=926;s->settle=0;
        }else if(s->phase==926) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            if(c->gpuPlan(true).state.toJson()!=s->panState||cursor(c)!=s->panHistory){finish("PAN_GEOMETRY_UNDO_NOT_RESTORED");return;}
            c->setLanguage("en_US");s->phase=927;s->settle=0;
        }else if(s->phase==927||s->phase==928) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||++s->settle<3)return;
            auto *hint=child(window->contentItem(),"brushRadiusHint");const QString language=s->phase==927?"en_US":"zh_CN";
            const QString expected=language=="en_US"?QStringLiteral("Space + drag to pan"):QStringLiteral("空格＋拖动平移");
            const bool visible=c->language()==language&&hint&&exposed(window,hint)&&hint->property("text").toString().startsWith(expected)&&hint->width()<=canvas->width()-15;
            s->panHints.append(QJsonObject{{"language",language},{"text",hint?hint->property("text").toString():QString{}},{"visible_bounded",visible},{"width",hint?hint->width():-1},{"canvas_width",canvas->width()}});
            if(!visible||!save(grab(),reportPath+".pan-hint-"+language+".png")){finish("PAN_BILINGUAL_HINT_MISSING_OR_CLIPPED");return;}
            if(s->phase==927){c->setLanguage("zh_CN");s->phase=928;s->settle=0;return;}
            QImage secondary(64,48,QImage::Format_RGBA64);secondary.fill(QColor(80,90,100));secondary.setColorSpace(QColorSpace(QColorSpace::SRgb));
            const auto path=dir->filePath("owned-pan-source-switch.png");if(!secondary.save(path)){finish("PAN_SOURCE_FIXTURE_FAILED");return;}
            s->panOriginalIndex=c->currentIndex();overlay->forceActiveFocus();space(QEvent::KeyPress);const auto from=point(.5,.6);mouse(window,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
            const bool started=navigation()&&!c->brushDrawing();const bool imported=c->importFile(QUrl::fromLocalFile(path));
            // Import preserves an existing selection. Select the owned second
            // photo explicitly so this probes an actual source transition.
            if(imported)c->selectPhoto(1);
            s->panSwitchCancelled=started&&imported&&c->currentIndex()!=s->panOriginalIndex&&!navigation()&&!c->brushDrawing()&&canvas->property("maskTool").toString().isEmpty();
            mouse(window,QEvent::MouseMove,from+QPointF(30,0),Qt::NoButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,from+QPointF(30,0),Qt::LeftButton,Qt::NoButton);space(QEvent::KeyRelease);
            if(!s->panSwitchCancelled){finish("PAN_SOURCE_SWITCH_RETAINED_OWNER");return;}
            s->phase=929;s->settle=0;
        }else if(s->phase==929) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            if(c->currentFile()!=dir->filePath("owned-pan-source-switch.png")||navigation()||c->brushDrawing()){finish("PAN_SOURCE_SWITCH_RESURRECTED");return;}
            c->selectPhoto(s->panOriginalIndex);s->phase=930;s->settle=0;
        }else if(s->phase==930) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            const bool preserved=s->panSwitchCancelled&&c->currentFile()==source&&c->gpuPlan(true).state.toJson()==s->panState&&cursor(c)==s->panHistory&&!navigation()&&!c->brushDrawing();
            s->panGuards.append(QJsonObject{{"case","source-switch-cancels-held-pan-original-preserved"},{"passed",preserved},{"navigation",navigation()},{"drawing",c->brushDrawing()},{"document_history_unchanged",preserved}});
            if(!preserved||!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("PAN_SOURCE_REENTRY_CHANGED_ORIGINAL");return;}
            canvas->setProperty("centerX",s->panAfterBasic.x());canvas->setProperty("centerY",s->panAfterBasic.y());s->phase=931;s->settle=0;
        }else if(s->phase==931) {
            if(!overlay->isEnabled()||!previewDisplayedReady(c,window)||!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            s->panGuardsChecked=true;s->panGeometryChecked=true;s->phase=7;s->settle=0;
        }else if(s->phase==8) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            if(!save(grab(),reportPath+".zoom.png")){finish("Cannot save zoomed brush UI");return;}
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
            // QML's bounded reveal uses its own timer. Observe completion
            // before asserting visibility, within the unchanged45s deadline.
            if(geometry["pending"].toBool()){++s->revealWaitTicks;return;}
            if(!geometry["critical_visible"].toBool()||geometry["pending"].toBool()){finish("Re-entering the brush tool did not reveal its actual controls");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))||!click(window,child(window->contentItem(),"editBrushMaskCanvas"))||!wheelToToolsStart(window)){finish("Cannot interrupt pending reveal with a native wheel");return;}
            s->phase=32;s->settle=0;
        }else if(s->phase==32) {
            auto *flick=toolFlick(window);if(!flick||flick->property("moving").toBool()||++s->settle<3)return;
            const auto geometry=brushControls(window);
            s->manualScroll=!geometry["pending"].toBool()&&!geometry["critical_visible"].toBool()&&geometry["content_y"].toDouble()<=16;
            if(!s->manualScroll){finish("Pending brush reveal fought the user's manual scroll");return;}
            c->setLanguage("en_US");s->phase=35;s->settle=0;
        }else if(s->phase==35) {
            if(++s->settle<3)return;const auto geometry=brushControls(window);
            if(c->language()!="en_US"||geometry["automatic_owner"].toBool()||geometry["pending"].toBool()
                ||geometry["critical_visible"].toBool()||geometry["content_y"].toDouble()>16){finish("English reflow overrode the user's manual scroll");return;}
            c->setLanguage("zh_CN");s->phase=36;s->settle=0;
        }else if(s->phase==36) {
            if(++s->settle<3)return;const auto geometry=brushControls(window);
            s->manualLocale=c->language()=="zh_CN"&&!geometry["automatic_owner"].toBool()&&!geometry["pending"].toBool()
                &&!geometry["critical_visible"].toBool()&&geometry["content_y"].toDouble()<=16;
            if(!s->manualLocale){finish("Chinese reflow overrode the user's manual scroll");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))||!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("Cannot restore the brush tool after canceling its reveal");return;}
            s->phase=33;s->settle=0;
        }else if(s->phase==33) {
            if(++s->settle<3)return;const auto geometry=brushControls(window);
            if(geometry["pending"].toBool()){++s->revealWaitTicks;return;}
            s->revealed=geometry["critical_visible"].toBool()&&!geometry["pending"].toBool()&&masks(c)==s->revealMasks
                &&cursor(c)==s->revealHistory&&c->previewFrameRect()==s->revealFrame;
            if(!s->revealed||!save(grab(),reportPath+".reveal.png")){finish("Brush reveal changed edit/viewport state or did not restore controls");return;}
            s->phase=22;s->settle=0;
        }else if(s->phase==22) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            const auto image=grab();if(image.isNull()||!c->brushReady()||!previewDisplayedReady(c,window))return;
            s->selectedBefore=sample(image,window,imagePoint(c,overlay,.5,.6));if(!s->selectedBefore.isValid())return;
            s->guideBaseInk=selectedGuideInk(image,window,c,overlay);s->history=cursor(c);
            if(!selectFirstStroke(window)){finish("Cannot select an earlier stored stroke through the native selector");return;}s->phase=23;s->settle=0;
        }else if(s->phase==23) {
            if(++s->settle<3)return;const auto image=grab();
            s->guideInk=selectedGuideInk(image,window,c,overlay)-s->guideBaseInk;if(s->guideInk<16)return;
            if(masks(c)!=s->edited||cursor(c)!=s->history||!save(image,reportPath+".selected-stroke.png")){finish("Selecting a stored stroke changed edit intent or cannot be captured");return;}
            auto layers=s->edited["layers"].toArray();auto layer=layers[0].toObject();auto strokes=layer["strokes"].toArray();strokes.removeFirst();layer["strokes"]=strokes;layers[0]=layer;
            s->strokeReduced=s->edited;s->strokeReduced["layers"]=layers;
            auto *button=child(window->contentItem(),"removeSelectedBrushStroke");
            if(!exposed(window,button)||!click(window,button)||masks(c)!=s->strokeReduced||cursor(c)!=s->history+1){finish("Selected stroke deletion changed another stroke or is not one Undo");return;}
            s->phase=24;s->settle=0;
        }else if(s->phase==24) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;const auto image=grab();
            if(image.isNull()||!c->brushReady()||!previewDisplayedReady(c,window))return;
            const auto color=sample(image,window,imagePoint(c,overlay,.5,.6));if(!color.isValid())return;
            s->selectedDelta=std::max({std::abs(color.red()-s->selectedBefore.red()),std::abs(color.green()-s->selectedBefore.green()),std::abs(color.blue()-s->selectedBefore.blue())});
            if(s->selectedDelta<3)return;
            auto *selector=child(window->contentItem(),"brushStrokeSelector"),*button=child(window->contentItem(),"removeSelectedBrushStroke");
            if(!selector||selector->property("currentIndex").toInt()!=0||!button||button->isEnabled()
                ||!click(window,child(window->contentItem(),"undoEditButton"))||masks(c)!=s->edited){finish("Deleted selection did not clear or native Undo changed intent");return;}
            s->phase=25;s->settle=0;
        }else if(s->phase==25) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;const auto image=grab();
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
            c->setLanguage("en_US");s->localeRequest=brushControls(window)["pending"].toBool();
            if(!s->localeRequest){finish("Language reflow did not request a new brush reveal after automatic positioning");return;}
            s->phase=19;s->settle=0;
        }else if(s->phase==19) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            if(brushControls(window)["pending"].toBool()){++s->revealWaitTicks;return;}
            auto *plot=child(window->contentItem(),"scopePlotImage");
            if(!plot||plot->property("status").toInt()!=1||plot->property("source").toUrl()!=QUrl(c->scopePlotUrl())){s->settle=0;return;}
            auto *hint=child(window->contentItem(),"maskToolHint");s->actualHint=hint?hint->property("text").toString():QString{};s->expectedHint=c->brushStatus();
            s->pointerReady=overlay->isEnabled()&&hint&&s->actualHint==s->expectedHint;
            if(!s->pointerReady){finish("Canvas brush availability did not refresh after completed processing");return;}
            const auto image=grab();
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
            if(!save(grab(),reportPath+".disabled.png")||!click(window,child(window->contentItem(),"brushLayerEnabled"))){finish("Cannot capture or re-enable the disabled English brush layer");return;}s->phase=21;s->settle=0;
        }else if(s->phase==21) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            auto *hint=child(window->contentItem(),"maskToolHint"),*pointer=child(window->contentItem(),"brushPointerArea");
            if(masks(c)!=s->reduced||!overlay->isEnabled()||!pointer||pointer->property("cursorShape").toInt()!=int(Qt::CrossCursor)
                ||!hint||hint->property("text").toString()!=c->brushStatus()){finish("Re-enabling the brush layer did not restore paint availability");return;}
            auto *plot=child(window->contentItem(),"scopePlotImage");
            if(!plot||plot->property("status").toInt()!=1||plot->property("source").toUrl()!=QUrl(c->scopePlotUrl())){s->settle=0;return;}
            const auto image=grab();s->scopeInk=scopeInkSamples(image,window,plot);
            if(!c->brushReady()||!c->scopePlotCurrent()||s->scopeInk<64){s->settle=0;return;}
            if(!save(image,reportPath+".layers.png")){finish("Cannot retain re-enabled English layer controls");return;}
            // Capacity cases inspect admission/feedback and preserve edits;
            // their repeated temporary documents do not need full-source plots.
            // Retain the full checks above and restore exact statistics below.
            const auto scopeMasks=masks(c);const auto scopeHistory=cursor(c);
            if(!c->exactScopes()||!click(window,child(window->contentItem(),"exactScopesCheck"))||c->exactScopes()) {finish("Cannot select preview statistics for capacity checks");return;}
            s->capacityPreviewStatistics=true;s->scopeControlsReadonly=masks(c)==scopeMasks&&cursor(c)==scopeHistory;
            if(!s->scopeControlsReadonly){finish("Statistics choice changed brush intent/history");return;}
            BrushMaskGeometry::Stroke stroke{{.002,.6,1},{}};
            stroke.dabs.resize(BrushMaskGeometry::MaxDabsPerStroke,{{.5,.5},1});
            BrushMaskDocument::Document full{{{"capacity_full",true,0,{}}}};
            for(std::size_t n=0;n<BrushMaskDocument::MaxTotalDabs/BrushMaskGeometry::MaxDabsPerStroke;++n)full.layers[0].strokes.push_back(stroke);
            QJsonObject document;
            if(!BrushMaskDocument::toJson(full,&document)||!c->executeEditCommand({{"command","mask.brush.replace"},{"document",document}})){finish("Cannot initialize a valid full-capacity owned document");return;}
            s->capacityFull=masks(c);s->phase=40;s->settle=0;
        }else if(s->phase==40) {
            if(!previewDisplayedReady(c,window)||++s->settle<3||brushControls(window)["pending"].toBool())return;
            s->capacity=capacityBlocked("dabs-en","Brush document full. Remove a stroke or layer to continue.",int(BrushMaskDocument::MaxTotalDabs),false,true);
            if(!s->capacity){finish("Full brush document still advertises or starts paint input");return;}
            c->setLanguage("zh_CN");s->phase=42;s->settle=0;
        }else if(s->phase==42) {
            if(!previewDisplayedReady(c,window)||++s->settle<3||brushControls(window)["pending"].toBool())return;
            s->capacity=s->capacity&&capacityBlocked("dabs-zh",QStringLiteral("笔刷文档已满，请删除笔触或图层后继续。"),int(BrushMaskDocument::MaxTotalDabs),false);
            if(!s->capacity||!selectFirstLayer(window)){finish("Chinese full-document hint or native layer selection failed");return;}
            s->phase=43;s->settle=0;
        }else if(s->phase==43) {
            if(++s->settle<3)return;s->capacityHistory=cursor(c);
            s->capacityRemoved=s->capacityFull;auto layers=s->capacityRemoved["layers"].toArray();auto layer=layers[0].toObject();auto strokes=layer["strokes"].toArray();
            strokes.removeLast();layer["strokes"]=strokes;layers[0]=layer;s->capacityRemoved["layers"]=layers;
            if(!click(window,child(window->contentItem(),"removeLastBrushStroke"))||masks(c)!=s->capacityRemoved||cursor(c)!=s->capacityHistory+1){finish("Native removal did not free exact document capacity");return;}
            s->phase=44;s->settle=0;
        }else if(s->phase==44) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            s->capacityRecovery=capacityReady("after-removal",true);
            if(!s->capacityRecovery||!click(window,child(window->contentItem(),"undoEditButton"))||masks(c)!=s->capacityFull||cursor(c)!=s->capacityHistory){finish("Capacity removal/Undo did not preserve its exact intent");return;}
            s->phase=45;s->settle=0;
        }else if(s->phase==45) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            if(!capacityBlocked("undo-full-zh",QStringLiteral("笔刷文档已满，请删除笔触或图层后继续。"),int(BrushMaskDocument::MaxTotalDabs),false)
                ||!click(window,child(window->contentItem(),"redoEditButton"))||masks(c)!=s->capacityRemoved||cursor(c)!=s->capacityHistory+1){finish("Undo did not restore the full-document input guard or Redo failed");return;}
            s->phase=46;s->settle=0;
        }else if(s->phase==46) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            s->capacityRecovery=s->capacityRecovery&&capacityReady("after-redo",true);
            if(!s->capacityRecovery){finish("Redo did not restore native paint availability");return;}
            const BrushMaskGeometry::Stroke one{{.002,.6,1},{{{.5,.5},1}}};BrushMaskDocument::Document full;
            for(std::size_t n=0;n<BrushMaskDocument::MaxLayers;++n)full.layers.push_back({QString("capacity_layer_%1").arg(n),true,0,{one}});
            QJsonObject document;if(!BrushMaskDocument::toJson(full,&document)||!c->executeEditCommand({{"command","mask.brush.replace"},{"document",document}})){finish("Cannot initialize max-layer owned intent");return;}
            s->phase=47;s->settle=0;
        }else if(s->phase==47) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            if(!capacityBlocked("layers-zh",QStringLiteral("已达笔刷图层上限，请删除图层后再新建。"),int(BrushMaskDocument::MaxLayers),false)){finish("Chinese new-layer capacity feedback is incorrect");return;}
            c->setLanguage("en_US");s->phase=48;s->settle=0;
        }else if(s->phase==48) {
            if(!previewDisplayedReady(c,window)||++s->settle<3||brushControls(window)["pending"].toBool())return;
            if(!capacityBlocked("layers-en","Brush layer limit reached. Remove a layer to create another.",int(BrushMaskDocument::MaxLayers),false)
                ||!selectFirstLayer(window)){finish("English layer capacity feedback or existing target selection failed");return;}
            s->phase=49;s->settle=0;
        }else if(s->phase==49) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            s->capacityExisting=capacityReady("existing-at-layer-cap",false);
            if(!s->capacityExisting){finish("Layer cap incorrectly blocks appending to an existing layer");return;}
            const BrushMaskGeometry::Stroke one{{.002,.6,1},{{{.5,.5},1}}};BrushMaskDocument::Document full{{{"capacity_strokes",true,0,{}}}};
            full.layers[0].strokes.resize(BrushMaskGeometry::MaxStrokes,one);QJsonObject document;
            if(!BrushMaskDocument::toJson(full,&document)||!c->executeEditCommand({{"command","mask.brush.replace"},{"document",document}})){finish("Cannot initialize max-stroke owned intent");return;}
            s->phase=50;s->settle=0;
        }else if(s->phase==50) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            if(!selectFirstLayer(window)){finish("Cannot select max-stroke target through the actual selector");return;}s->phase=51;s->settle=0;
        }else if(s->phase==51) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            if(!capacityBlocked("strokes-en","Stroke limit reached for this layer. Remove a stroke to continue.",int(BrushMaskGeometry::MaxStrokes),true)){finish("English selected-stroke capacity feedback is incorrect");return;}
            c->setLanguage("zh_CN");s->phase=52;s->settle=0;
        }else if(s->phase==52) {
            if(!previewDisplayedReady(c,window)||++s->settle<3||brushControls(window)["pending"].toBool())return;
            if(!capacityBlocked("strokes-zh",QStringLiteral("该图层已达笔触上限，请删除一笔后继续。"),int(BrushMaskGeometry::MaxStrokes),true)
                ||!click(window,child(window->contentItem(),"newBrushLayerCanvas"))){finish("Chinese stroke limit or native new-layer action is incorrect");return;}
            s->phase=53;s->settle=0;
        }else if(s->phase==53) {
            if(!previewDisplayedReady(c,window)||++s->settle<3)return;
            s->capacityTargets=s->capacityExisting&&capacityReady("new-at-stroke-cap",true);
            if(!s->capacityTargets){finish("A full selected layer incorrectly blocks a new layer");return;}
            if(!c->executeEditCommand({{"command","mask.brush.replace"},{"document",s->reduced}})){finish("Cannot restore owned brush intent after capacity checks");return;}
            c->setLanguage("en_US");s->phase=54;s->settle=0;
        }else if(s->phase==54) {
            if(!previewDisplayedReady(c,window)||++s->settle<3||brushControls(window)["pending"].toBool())return;
            const auto scopeMasks=masks(c);const auto scopeHistory=cursor(c);
            if(c->exactScopes()||!click(window,child(window->contentItem(),"exactScopesCheck"))||!c->exactScopes()){finish("Cannot restore full statistics after capacity checks");return;}
            s->capacityExactStatistics=true;s->scopeControlsReadonly=s->scopeControlsReadonly&&masks(c)==scopeMasks&&cursor(c)==scopeHistory;
            if(!s->scopeControlsReadonly){finish("Restoring full statistics changed brush intent/history");return;}
            s->phase=41;s->settle=0;
        }else if(s->phase==41) {
            if(!c->exactScopes()||!c->scopePlotCurrent()||c->scopePlotPixels()!=2659021||c->scopesPixelCount()!=2659021||!previewDisplayedReady(c,window)||++s->settle<3||brushControls(window)["pending"].toBool())return;
            auto *plot=child(window->contentItem(),"scopePlotImage");
            if(!plot||plot->property("status").toInt()!=1||plot->property("source").toUrl()!=QUrl(c->scopePlotUrl())){s->settle=0;return;}
            s->scopeInk=scopeInkSamples(grab(),window,plot);
            if(!c->brushReady()||!c->scopePlotCurrent()||s->scopeInk<64){s->settle=0;return;}
            if(masks(c)!=s->reduced||s->capacityCases.size()!=7||s->capacityReadyCases.size()!=4){finish("Capacity matrix changed original intent or left cases incomplete");return;}finish({});
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
