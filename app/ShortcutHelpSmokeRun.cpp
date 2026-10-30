#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "app/BrushTabletInput.h"
#include <QApplication>
#include <QFile>
#include <QEventLoop>
#include <utility>
#include <QQuickWindow>
#include <QQuickItem>
#include <QScreen>
#include <QQmlProperty>
#include <QQmlEngine>
#include <QPointer>
#include <QJSValue>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QKeySequence>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QPointingDevice>
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
#include <cmath>

namespace {
QQuickItem *item(QQuickItem *root,const QString &name) {
    if(!root)return nullptr;
    if(root->objectName()==name)return root;
    for(auto *child:root->childItems())if(auto *found=item(child,name))return found;
    return nullptr;
}
QByteArray hash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash sum(QCryptographicHash::Sha256);sum.addData(&file);return sum.result().toHex();
}
void labels(QQuickItem *root,QStringList &text,QJsonArray &bounds) {
    if(!root)return;
    if(root->isVisible()&&root->metaObject()->indexOfProperty("text")>=0&&root->metaObject()->indexOfProperty("wrapMode")>=0) {
        const auto value=root->property("text").toString();
        if(!value.isEmpty()) {
            text.append(value);bounds.append(QJsonObject{{"text",value},{"width",root->width()},{"height",root->height()},
                {"painted_width",root->property("paintedWidth").toDouble()},{"painted_height",root->property("paintedHeight").toDouble()}});
        }
    }
    for(auto *child:root->childItems())labels(child,text,bounds);
}
void headerControls(QQuickItem *root,QJsonArray &rows,bool &valid,int width) {
    if(!root)return;
    if(root->isVisible()&&root->metaObject()->indexOfProperty("down")>=0&&root->width()>5&&root->height()>5) {
        const auto point=root->mapToScene({0,0});
        if(point.y()>=0&&point.y()<56) {
            const bool contained=point.x()>=-.5&&point.x()+root->width()<=width+.5;valid=valid&&contained;
            rows.append(QJsonObject{{"name",root->objectName()},{"text",root->property("text").toString()},
                {"x",point.x()},{"y",point.y()},{"width",root->width()},{"contained",contained}});
        }
    }
    for(auto *child:root->childItems())headerControls(child,rows,valid,width);
}
class ShortcutHelpSmoke final:public QObject {
public:
    ShortcutHelpSmoke(PhotoController *controller,QQuickWindow *window,const QString &reportPath)
        :QObject(controller),c(controller),w(window),report(reportPath),pen("Controlled modal-help pressure pen",987654331,
         QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
         QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3) {
        clock.start();timer=new QTimer(this);timer->setInterval(40);
        dialog=w->findChild<QObject*>("keyboardShortcutsDialog");
        source=files.filePath("owned-shortcut-help.png");
        QImage image(1024,768,QImage::Format_RGBA64);image.fill(QColor(50,90,120));image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        if(!files.isValid()||!dialog||!QFileInfo(report).absoluteDir().mkpath(".")||!image.save(source)) {
            QTimer::singleShot(0,this,[this]{finish("Shortcut help fixture/UI setup failed");});return;
        }
        sourceHash=hash(source);QWindowSystemInterface::registerInputDevice(&pen);
        w->setScreen(QGuiApplication::primaryScreen());
        w->setPosition(QGuiApplication::primaryScreen()->availableGeometry().topLeft()+QPoint(20,20));
        w->resize(1180,720);w->setProperty("workspaceIndex",0);c->setLanguage("en_US");c->setExactScopes(false);
        initialApplicationState=qGuiApp->applicationState();
        connect(timer,&QTimer::timeout,this,[this]{tick();});timer->start();
    }
private:
    PhotoController *c;QPointer<QQuickWindow> w;QPointer<QObject> dialog;QString report,source;
    QTemporaryDir files;QPointingDevice pen;QTimer *timer=nullptr;QElapsedTimer clock;
    QJsonObject before,emptyLayout;QVariantList history;QJsonArray records,captures,keys;
    QByteArray sourceHash;int phase=-2,settle=0,focusBootstraps=0,applicationBootstraps=0;
    int backendCaptionChecks=0;bool backendCaptionsCurrent=true;
    QJsonArray compactRecords,compactCaptures;
    bool compactOvalPointer=false,compactCancelPointer=false;
    QJsonArray exportModalButtons;QJsonObject exportNavigation;QVariantList exportSelection;
    int exportCurrentIndex=-1;bool exportTextInputWorks=false;
    QString transferTargetSource;QByteArray transferTargetHash;QJsonObject transferTargetBefore;QVariantList transferTargetHistory;
    QVariantList transferSelection;QStringList transferGroups;QJsonArray transferChecks,transferClosedChecks;
    int transferMode=0,transferFocus=0;bool transferSyncOpened=false,transferPasteOpened=false;
    bool transferSyncCancelled=false,transferPasteCancelled=false,transferTargetUnchanged=false,transferSourceRestored=false;
    QVariantList copyImportSelection;QJsonArray copyImportChecks;QJsonObject copyImportClosed;int copyImportFocus=0;bool copyImportOpened=false,copyImportToggled=false,copyImportTextInput=false,copyImportCancelled=false;
    Qt::ApplicationState initialApplicationState=Qt::ApplicationInactive;
    ulong stamp=1000,tabletStamp=10000;bool snapshot=false,done=false;
    QQuickItem *find(const QString &name)const{return w?item(w->contentItem(),name):nullptr;}
    bool visible()const{return dialog&&dialog->property("visible").toBool();}
    bool stable()const{return snapshot&&c->gpuPlan(true).state.toJson()==before&&c->editHistory()==history&&hash(source)==sourceHash;}
    void advance(int next){phase=next;settle=0;}
    bool click(QQuickItem *target) {
        if(!w||!target||!target->isVisible()||!target->isEnabled())return false;
        const auto point=target->mapToScene({target->width()/2,target->height()/2});
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,point,point,w->mapToGlobal(point),Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(w,&event);
        }
        return true;
    }
    void down(int code,const QString &text={},Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        const bool consumed=QWindowSystemInterface::handleShortcutEvent(w,++stamp,code,modifiers,0,0,0,text);
        keys.append(QJsonObject{{"phase",phase},{"key",code},{"text",text},{"modifiers",int(modifiers)},
            {"shortcut_consumed",consumed},{"key_press_delivered",!consumed}});
        if(!consumed)QWindowSystemInterface::handleKeyEvent(w,++stamp,QEvent::KeyPress,code,modifiers,text);
        QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
    }
    void up(int code,const QString &text={}) {
        QWindowSystemInterface::handleKeyEvent(w,++stamp,QEvent::KeyRelease,code,Qt::NoModifier,text);
        QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
    }
    void key(int code,const QString &text={},Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        down(code,text,modifiers);QWindowSystemInterface::handleKeyEvent(w,++stamp,QEvent::KeyRelease,code,modifiers,text);
        QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
    }
    void standardKeys() {
        for(auto standard:{QKeySequence::Open,QKeySequence::Undo,QKeySequence::Redo})
            for(const auto &sequence:QKeySequence::keyBindings(standard))key(sequence[0].key(),{},sequence[0].keyboardModifiers());
    }
    bool open(){return click(find("keyboardShortcutsButton"));}
    BrushTabletInput *tablet()const {
        auto *overlay=find("brushCanvasOverlay");return overlay?overlay->findChild<BrushTabletInput*>("brushTabletInput"):nullptr;
    }
    bool packet(Qt::MouseButtons held) {
        auto *overlay=find("brushCanvasOverlay");if(!overlay||!w)return false;
        const auto point=overlay->mapToScene({overlay->width()*.45,overlay->height()*.55}),global=w->mapToGlobal(point);
        QWindowSystemInterface::handleTabletEvent(w,tabletStamp+=20,&pen,QHighDpi::toNativePixels(point,w.data()),
            QHighDpi::toNativePixels(global,w.data()),held,.6,0,0,0,0,0);QWindowSystemInterface::flushWindowSystemEvents();return true;
    }
    bool record(const QString &name,bool passed,QJsonObject details={}) {
        auto *canvas=find("photoCanvas"),*overlay=find("brushCanvasOverlay");
        const bool captionApplicable=c->hasImage()&&w&&w->property("workspaceIndex").toInt()==1;
        auto *backend=find("processingBackendLabel");
        const auto actual=backend?backend->property("text").toString():QString{};
        const bool captionCurrent=!captionApplicable||(backend&&backend->isVisible()&&actual==c->processingBackend());
        if(captionApplicable)++backendCaptionChecks;
        backendCaptionsCurrent=backendCaptionsCurrent&&captionCurrent;passed=passed&&captionCurrent;
        details.insert("backend_caption_applicable",captionApplicable);
        details.insert("backend_caption_current",captionCurrent);
        details.insert("backend_caption_language",c->language());
        details.insert("backend_caption_text",actual);
        details.insert("backend_caption_expected",c->processingBackend());
        details.insert("case",name);details.insert("passed",passed);details.insert("document_history_source_unchanged",stable());
        details.insert("dialog_visible",visible());details.insert("crop_editing",c->cropEditing());
        details.insert("workspace",w?w->property("workspaceIndex").toInt():-1);
        details.insert("mask_tool",canvas?canvas->property("maskTool").toString():QString{});
        details.insert("navigation_active",overlay&&overlay->property("navigationActive").toBool());
        details.insert("focus_class",w&&w->activeFocusItem()?QString::fromLatin1(w->activeFocusItem()->metaObject()->className()):QString{});
        details.insert("focus_object_name",w&&w->activeFocusItem()?w->activeFocusItem()->objectName():QString{});
        details.insert("brush_active_focus",overlay&&overlay->hasActiveFocus());
        auto *help=find("keyboardShortcutsButton");
        details.insert("help_button_focus_policy",help?help->property("focusPolicy").toInt():-1);
        details.insert("help_button_active_focus",help&&help->hasActiveFocus());
        records.append(details);return passed;
    }
    std::pair<bool,QJsonObject> layout(const QString &language)const {
        auto *guide=find("keyboardShortcutsGuide");QStringList text;QJsonArray bounds;
        labels(guide,text,bounds);bool good=w&&guide&&visible()&&stable()&&c->language()==language;
        const QStringList headings=language=="zh_CN"?QStringList{"页面与导入","编辑历史","笔刷画布","裁剪画布"}
            :QStringList{"Workspace and import","Edit history","Brush canvas","Crop canvas"};
        for(const auto &heading:headings)good=good&&text.contains(heading);
        QJsonArray native;
        for(auto standard:{QKeySequence::Open,QKeySequence::Undo,QKeySequence::Redo}) {
            QStringList sequences;for(const auto &sequence:QKeySequence::keyBindings(standard)) {
                const auto value=sequence.toString(QKeySequence::NativeText);
                if(!value.isEmpty()&&!sequences.contains(value))sequences.append(value);
                if(standard==QKeySequence::Open)break;
            }
            const auto value=sequences.join(" / ");native.append(value);good=good&&!value.isEmpty()&&text.contains(value);
        }
        for(const auto &entry:bounds) {
            const auto row=entry.toObject();good=good&&row["width"].toDouble()>0&&row["height"].toDouble()>0
                &&row["painted_width"].toDouble()<=row["width"].toDouble()+1;
        }
        const double x=dialog?dialog->property("x").toDouble():0,y=dialog?dialog->property("y").toDouble():0;
        const double width=dialog?dialog->property("width").toDouble():0,height=dialog?dialog->property("height").toDouble():0;
        good=good&&x>=0&&y>=0&&x+width<=w->width()+1&&y+height<=w->height()+1
            &&std::abs(x+width/2-w->width()/2.)<1&&std::abs(y+height/2-w->height()/2.)<1;
        bool headerGood=true;QJsonArray controls;headerControls(w->contentItem(),controls,headerGood,w->width());good=good&&headerGood;
        return {good,QJsonObject{{"language",language},{"labels",QJsonArray::fromStringList(text)},
            {"label_bounds",bounds},{"native_shortcuts",native},{"header_controls",controls},
            {"window_width",w->width()},{"window_height",w->height()},{"dialog_x",x},{"dialog_y",y},
            {"dialog_width",width},{"dialog_height",height}}};
    }
    bool canvasBounded(QQuickItem *target) const {
        auto *canvas=find("photoCanvas");if(!canvas||!target||!target->isVisible()||target->width()<1||target->height()<1)return false;
        const auto point=target->mapToItem(canvas,{0,0});
        return point.x()>=-.5&&point.y()>=-.5&&point.x()+target->width()<=canvas->width()+.5&&point.y()+target->height()<=canvas->height()+.5;
    }
    void canvasControls(QQuickItem *root,QJsonArray &rows,bool &valid) const {
        if(!root)return;
        if(root->isVisible()&&root->metaObject()->indexOfProperty("down")>=0) {
            const auto point=root->mapToItem(find("photoCanvas"),{0,0});const bool bounded=canvasBounded(root);valid=valid&&bounded;
            rows.append(QJsonObject{{"name",root->objectName()},{"text",root->property("text").toString()},{"x",point.x()},{"y",point.y()},{"width",root->width()},{"height",root->height()},{"contained",bounded}});
        }
        for(auto *child:root->childItems())canvasControls(child,rows,valid);
    }
    QQuickItem *canvasButton(QQuickItem *root,const QString &text) const {
        if(!root)return nullptr;
        if(root->isVisible()&&root->metaObject()->indexOfProperty("down")>=0&&root->property("text").toString()==text)return root;
        for(auto *child:root->childItems())if(auto *found=canvasButton(child,text))return found;return nullptr;
    }
    bool splitWidth(const QString &name,qreal value) {
        auto *target=find(name);if(!target)return false;
        QQmlProperty property(target,"SplitView.preferredWidth",qmlContext(target));return property.isValid()&&property.write(value);
    }
    bool compactRecord(const QString &name) {
        auto *canvas=find("photoCanvas"),*hint=find("maskToolHint"),*backend=find("processingBackendLabel");
        QJsonArray bounds;bool okay=canvas&&stable();canvasControls(canvas,bounds,okay);
        const bool hintVisible=hint&&hint->isVisible();
        const bool hintBounded=!hintVisible||(canvasBounded(hint)&&hint->property("paintedWidth").toDouble()<=hint->width()+1&&hint->property("paintedHeight").toDouble()<=hint->height()+1);
        const auto text=backend?backend->property("text").toString():QString{};
        okay=okay&&hintBounded&&backend&&backend->isVisible()&&text==c->processingBackend();
        compactRecords.append(QJsonObject{{"case",name},{"passed",okay},{"language",c->language()},{"canvas_width",canvas?canvas->width():0},{"canvas_height",canvas?canvas->height():0},{"window_width",w->width()},{"window_height",w->height()},
            {"reference_visible",c->showCameraReference()},{"mask_tool",canvas?canvas->property("maskTool").toString():QString{}},{"crop_editing",c->cropEditing()},{"controls",bounds},{"hint_visible",hintVisible},{"hint_bounded",hintBounded},{"hint_width",hint?hint->width():0},{"hint_height",hint?hint->height():0},{"hint_painted_width",hint?hint->property("paintedWidth").toDouble():0},{"hint_painted_height",hint?hint->property("paintedHeight").toDouble():0},{"backend_caption",text},{"backend_getter",c->processingBackend()},{"document_history_source_unchanged",stable()}});
        return okay&&capture(name,&compactCaptures);
    }
    bool capture(const QString &label,QJsonArray *destination=nullptr) {
        if(!w)return false;const auto image=w->grabWindow();const auto path=report+"."+label+".png";
        if(image.isNull()||!image.save(path))return false;
        (destination?*destination:captures).append(QJsonObject{{"label",label},{"path",path},{"width",image.width()},{"height",image.height()},
            {"logical_width",w->width()},{"logical_height",w->height()},{"dpr",w->effectiveDevicePixelRatio()},
            {"sha256",QString::fromLatin1(hash(path))}});return true;
    }
    void finish(const QString &error) {
        if(done)return;done=true;timer->stop();const bool captured=(error.isEmpty()&&captures.size()==5)||capture(error.isEmpty()?"final":"failure");
        const bool passed=error.isEmpty()&&records.size()==17&&stable()&&captured&&captures.size()==5&&backendCaptionChecks==16&&backendCaptionsCurrent&&compactRecords.size()==4&&compactCaptures.size()==4&&compactOvalPointer&&compactCancelPointer&&clock.elapsed()<60000;
        const QJsonObject data{{"backend_caption_checks",backendCaptionChecks},{"backend_captions_current",backendCaptionsCurrent},{"protocol_version",2},{"passed",passed},{"error",error},{"phase",phase},
            {"elapsed_ms",clock.elapsed()},{"deadline_ms",60000},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"build_version",QStringLiteral(JIXELLIGHT_VERSION)},{"test_fixture",true},{"product_UI_acceptance",false},
            {"actual_photo_app_UI",true},{"source_kind","owned-rgba64-png"},{"source_pixels",1024*768},
            {"source_sha256",QString::fromLatin1(sourceHash)},{"source_unchanged",hash(source)==sourceHash},
            {"document_history_unchanged",stable()},{"settings_isolated",qApp->property("jixellightSmokeSettingsIsolated").toBool()},
            {"keyboard_injection","Qt-QPA-shortcut-override-map-then-key-events"},{"keyboard_packets",keys},
            {"pressure_injection","Qt-QPA-controlled-stylus"},{"window_focus_injection","Qt-QPA-controlled-window-focus"},
            {"focus_bootstrap_count",focusBootstraps},{"application_bootstrap_count",applicationBootstraps},
            {"initial_application_state",int(initialApplicationState)},{"Qt_platform",QGuiApplication::platformName()},
            {"physical_OS_activation_verified",false},{"physical_keyboard_driver_verified",false},{"physical_tablet_verified",false},
            {"native_gpu_brush",false},{"native_float_raw_decoder",false},{"gpu_active",c->gpuActive()},
            {"crop_context_setup_internal_beginCrop",true},{"help_button_focus_setup_internal",true},
            {"compact_canvas_protocol_version",1},{"compact_canvas_records",compactRecords},{"compact_canvas_captures",compactCaptures},{"compact_oval_pointer_activated",compactOvalPointer},{"compact_crop_cancel_pointer_activated",compactCancelPointer},{"compact_pose_setup_internal_splitter_reference",true},
            {"export_modal_button_checks",exportModalButtons},{"export_text_input_works",exportTextInputWorks},{"export_navigation_after_close",exportNavigation},
            {"transfer_modal_checks",transferChecks},{"transfer_closed_checks",transferClosedChecks},
            {"transfer_actual_sync_clicked",transferSyncOpened},{"transfer_actual_paste_clicked",transferPasteOpened},
            {"transfer_actual_sync_cancelled",transferSyncCancelled},{"transfer_actual_paste_cancelled",transferPasteCancelled},
            {"transfer_setup_internal_selection_and_copy",true},{"transfer_source_restored",transferSourceRestored},
            {"transfer_target_unchanged",transferTargetUnchanged},{"transfer_target_source_sha256",QString::fromLatin1(transferTargetHash)},
            {"transfer_target_source_unchanged",!transferTargetHash.isEmpty()&&hash(transferTargetSource)==transferTargetHash},
            {"copy_import_modal_checks",copyImportChecks},{"copy_import_navigation_after_close",copyImportClosed},
            {"copy_import_picker_result_simulated",true},{"copy_import_folder_result_simulated",true},{"copy_import_opened",copyImportOpened},{"copy_import_custom_names_clicked",copyImportToggled},
            {"copy_import_text_input",copyImportTextInput},{"copy_import_cancel_clicked",copyImportCancelled},{"copy_import_source_unchanged",hash(source)==sourceHash},
            {"dpr",w?w->effectiveDevicePixelRatio():0},{"records",records},{"captures",captures}};
        QSaveFile file(report);const auto bytes=QJsonDocument(data).toJson();const bool saved=file.open(QIODevice::WriteOnly)
            &&file.write(bytes)==bytes.size()&&file.commit();QCoreApplication::exit(passed&&saved?0:1);
    }
    void tick() {
        if(!w||!dialog){finish("Shortcut help window/dialog disappeared");return;}
        if(clock.elapsed()>=60000){finish("Shortcut help GUI deadline exceeded");return;}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive) {
            QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);
            QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);++applicationBootstraps;return;
        }
        if(!w->isActive()) {
            QWindowSystemInterface::handleFocusWindowChanged(w);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);++focusBootstraps;return;
        }
        if(phase==-2) {
            before=c->gpuPlan(true).state.toJson();history=c->editHistory();snapshot=true;
            if(!open()){finish("Empty-library help control unavailable");return;}advance(-1);return;
        }
        if(phase==-1) {
            if(!visible()||++settle<3)return;const auto [good,data]=layout("en_US");
            if(!good||c->hasImage()||c->canUndo()||c->canRedo()||!capture("empty-library")){finish("Empty-library help/native captions unavailable");return;}
            auto *library=find("libraryWorkspace"),*grid=find("libraryPhotoGrid");
            const bool libraryVisible=library&&library->isVisible()&&library->width()>0&&library->height()>0;
            const bool gridVisible=grid&&grid->isVisible()&&grid->width()>0&&grid->height()>0;
            if(w->property("workspaceIndex").toInt()!=0||!libraryVisible||!gridVisible||grid->property("count").toInt()!=0) {
                finish("Direct empty Library workspace/grid was not actually visible");return;
            }
            emptyLayout=data;emptyLayout.insert("library_workspace_zero_checked",true);
            emptyLayout.insert("library_workspace_visible",libraryVisible);emptyLayout.insert("library_grid_visible",gridVisible);
            emptyLayout.insert("library_grid_count",grid->property("count").toInt());
            key(Qt::Key_G,"g");if(w->property("workspaceIndex").toInt()!=0){finish("Guide leaked G in Library");return;}
            key(Qt::Key_D,"d");if(w->property("workspaceIndex").toInt()!=0){finish("Guide leaked D in Library");return;}
            standardKeys();key(Qt::Key_Escape);advance(-3);return;
        }
        if(phase==-3) {
            if(!record("empty-library-guide-and-keys-read-only",!visible()&&!c->hasImage()&&!c->canUndo()&&!c->canRedo()&&stable(),emptyLayout)) {
                finish("Guide leaked Open or changed the empty library");return;
            }
            advance(0);return;
        }
        if(phase==0) {
            w->setProperty("workspaceIndex",1);
            if(!c->importFile(QUrl::fromLocalFile(source))){finish("Owned help fixture import failed");return;}advance(1);return;
        }
        if(!c->previewReady()||c->loading()||c->rendering()||++settle<3)return;
        if(phase==1){c->setExposure(.35);c->finishInteraction();advance(2);return;}
        if(phase==2) {
            before=c->gpuPlan(true).state.toJson();history=c->editHistory();snapshot=true;
            if(!open()){finish("Actual help toolbar control unavailable");return;}advance(3);return;
        }
        if(phase==3) {
            if(!visible())return;const auto [good,data]=layout("en_US");
            if(!record("english-compact-native-key-layout",good,data)||!capture("english-compact")){finish("English compact/native shortcut layout failed");return;}
            key(Qt::Key_G,"g");key(Qt::Key_D,"d");standardKeys();key(Qt::Key_BracketLeft,"[");key(Qt::Key_BracketRight,"]");key(Qt::Key_Space," ");key(Qt::Key_Return);advance(4);return;
        }
        if(phase==4) {
            if(!record("guide-owns-navigation-edit-and-canvas-keys",visible()&&w->property("workspaceIndex").toInt()==1&&stable())){finish("Guide allowed background shortcuts");return;}
            auto *close=find("keyboardShortcutsClose");for(int n=0;close&&!close->hasActiveFocus()&&n<12;++n)key(Qt::Key_Tab,"\t");
            if(!close||!close->hasActiveFocus()){finish("Close control not keyboard reachable");return;}key(Qt::Key_Escape);advance(5);return;
        }
        if(phase==5) {
            if(!record("tabbed-close-escape-dismisses-guide",!visible()&&stable())){finish("Close-focus Escape did not dismiss help");return;}
            if(!click(find("editBrushMaskCanvas")))return;auto *canvas=find("photoCanvas");if(!canvas){finish("Develop canvas disappeared");return;}canvas->setProperty("zoom",2.0);advance(6);return;
        }
        if(phase==6) {
            auto *overlay=find("brushCanvasOverlay");if(!overlay||!overlay->isEnabled()||!overlay->hasActiveFocus()||!c->brushReady())return;
            down(Qt::Key_Space," ");if(!overlay->property("navigationActive").toBool()||!open()){finish("Brush navigation/help setup failed");return;}advance(7);return;
        }
        if(phase==7) {
            auto *canvas=find("photoCanvas"),*overlay=find("brushCanvasOverlay");
            if(!record("open-guide-cancels-pan-retains-brush",visible()&&canvas&&overlay&&!overlay->property("navigationActive").toBool()
                &&canvas->property("maskTool").toString()=="brush"&&!c->brushDrawing()&&stable())){finish("Help changed brush/document ownership");return;}
            up(Qt::Key_Space," ");key(Qt::Key_Escape);advance(8);return;
        }
        if(phase==8) {
            auto *canvas=find("photoCanvas");if(!record("content-escape-retains-brush",!visible()&&canvas&&canvas->property("maskTool").toString()=="brush"&&!c->brushDrawing()&&stable())){finish("Help Escape also exited brush");return;}
            down(Qt::Key_Space," ");advance(9);return;
        }
        if(phase==9) {
            auto *overlay=find("brushCanvasOverlay");if(!record("closing-guide-restores-brush-key-focus",overlay&&overlay->property("navigationActive").toBool()&&stable())){finish("Help lost brush keyboard focus");return;}
            up(Qt::Key_Space," ");key(Qt::Key_Escape);advance(10);return;
        }
        if(phase==10){if(!c->beginCrop()||!open()){finish("Crop/help context unavailable");return;}advance(11);return;}
        if(phase==11){if(!visible())return;key(Qt::Key_Return);key(Qt::Key_Escape);advance(12);return;}
        if(phase==12) {
            if(!record("help-return-escape-preserve-crop",!visible()&&c->cropEditing()&&stable())){finish("Help leaked Return/Escape into crop");return;}
            key(Qt::Key_Escape);advance(13);return;
        }
        if(phase==13) {
            if(!record("canvas-escape-still-cancels-crop",!c->cropEditing()&&stable())){finish("Crop Escape unavailable after help");return;}
            c->setLanguage("zh_CN");if(!open()){finish("Chinese help inaccessible");return;}advance(14);return;
        }
        if(phase==14) {
            if(!visible())return;const auto [good,data]=layout("zh_CN");
            if(!record("chinese-compact-native-key-layout",good,data)||!capture("chinese-compact")){finish("Chinese compact/native shortcut layout failed");return;}
            w->resize(1540,920);advance(15);return;
        }
        if(phase==15) {
            const auto [good,data]=layout("zh_CN");if(!record("open-guide-resize-stays-centered",good,data)||!capture("resized")||!click(find("keyboardShortcutsClose"))){finish("Help resize/Close control failed");return;}advance(16);return;
        }
        if(phase==16){if(visible()){finish("Actual Close did not dismiss help");return;}if(!click(find("editBrushMaskCanvas")))return;advance(17);return;}
        if(phase==17) {
            auto *overlay=find("brushCanvasOverlay");if(!overlay||!overlay->isEnabled()||!c->brushReady())return;
            if(!packet(Qt::LeftButton)||!c->brushDrawing()){finish("Controlled held-pressure setup failed");return;}
            auto *button=find("keyboardShortcutsButton");if(!button){finish("Help control missing during pressure");return;}button->forceActiveFocus();key(Qt::Key_Space," ");advance(18);return;
        }
        if(phase==18) {
            if(!visible())return;auto *input=tablet();const bool good=!c->brushDrawing()&&input&&input->pressed()&&stable();
            if(!record("keyboard-open-guide-cancels-held-pressure",good,QJsonObject{{"pressure_owner_held",input&&input->pressed()},
                {"brush_drawing",c->brushDrawing()},{"help_button_focus_setup_internal",true}})){finish("Held pressure remained live behind help");return;}
            packet(Qt::LeftButton|Qt::RightButton);packet(Qt::LeftButton);if(c->brushDrawing()||!stable()){finish("Held pressure restarted under help");return;}key(Qt::Key_Escape);advance(19);return;
        }
        if(phase==19) {
            packet(Qt::LeftButton);auto *input=tablet();if(!record("modal-close-keeps-cancelled-owner-until-primary-lift",!visible()&&!c->brushDrawing()&&input&&input->pressed()&&stable())){finish("Cancelled pressure restarted after help");return;}
            packet(Qt::NoButton);advance(20);return;
        }
        if(phase==20) {
            auto *overlay=find("brushCanvasOverlay");auto *input=tablet();if(!overlay||!overlay->isEnabled()||!c->brushReady())return;
            if(!input||input->pressed()||!packet(Qt::LeftButton)||!c->brushDrawing()){finish("Fresh primary press unavailable after help");return;}
            key(Qt::Key_Escape);packet(Qt::NoButton);advance(21);return;
        }
        if(phase==21) {
            auto *input=tablet();if(!record("fresh-pressure-after-primary-lift-cancels-without-edit",!visible()&&!c->brushDrawing()&&input&&!input->pressed()&&stable())||!capture("final")){finish("Fresh pressure cancellation changed edits/history");return;}
            c->setLanguage("en_US");w->resize(1180,720);c->setShowCameraReference(true);
            if(!splitWidth("librarySidebar",390)||!splitWidth("developRightPanel",390)){finish("Compact splitter setup unavailable");return;}advance(22);return;
        }
        if(phase>=22&&phase<=25) {
            if(c->loading()||c->rendering()||!c->previewReady()||(!c->cropEditing()&&!c->brushReady()))return;
            if(++settle<3)return;
        }
        if(phase==22) {
            if(!compactRecord("compare-brush-en")||!click(find("editRadialMaskCanvas"))){finish("Compact English canvas controls unavailable");return;}
            compactOvalPointer=find("photoCanvas")->property("maskTool").toString()=="radial";
            if(!compactOvalPointer){finish("Compact Oval pointer did not select radial handles");return;}c->setLanguage("zh_CN");advance(23);return;
        }
        if(phase==23) {
            if(!compactRecord("compare-oval-zh")){finish("Compact Chinese canvas controls unavailable");return;}
            c->setLanguage("en_US");if(!c->beginCrop()){finish("Compact crop setup unavailable");return;}advance(24);return;
        }
        if(phase==24) {
            if(!compactRecord("compare-crop-en")||!click(canvasButton(find("photoCanvas"),"Cancel"))){finish("Compact crop controls unavailable");return;}
            compactCancelPointer=!c->cropEditing()&&stable();if(!compactCancelPointer){finish("Compact crop Cancel changed edits/history");return;}
            c->setShowCameraReference(false);w->resize(1540,920);
            if(!splitWidth("librarySidebar",228)||!splitWidth("developRightPanel",390)){finish("Restored splitter setup unavailable");return;}advance(25);return;
        }
        if(phase==25) {
            if(!compactRecord("restored-wide-en")){finish("Restored wide canvas controls unavailable");return;}
            auto *menu=w->findChild<QObject*>("exportActionsMenu");
            if(!menu||!QMetaObject::invokeMethod(menu,"open")){finish("Export menu unavailable");return;}advance(26);return;
        }
        if(phase==26) {
            auto *menu=w->findChild<QObject*>("exportActionsMenu");
            if(!menu||!menu->property("visible").toBool())return;
            if(!click(w->findChild<QQuickItem*>("batchExportAction"))){finish("Batch export action unavailable");return;}
            QMetaObject::invokeMethod(menu,"close");advance(27);return;
        }
        if(phase==27) {
            auto *exportDialog=w->findChild<QObject*>("exportSettingsDialog");
            if(!exportDialog||!exportDialog->property("visible").toBool())return;
            if(!exportDialog->property("batchMode").toBool()){finish("Batch export dialog unavailable");return;}
            exportSelection=c->selectedIndices();exportCurrentIndex=c->currentIndex();
            const auto unchanged=[&]{return stable()&&c->selectedIndices()==exportSelection&&c->currentIndex()==exportCurrentIndex
                &&!c->exportBusy()&&exportDialog->property("visible").toBool();};
            bool all=true;
            for(const auto *name:{"exportSettingsContinue","exportSettingsCancel"}) {
                auto *control=find(QString::fromLatin1(name));
                if(!control||!control->isVisible()||!control->isEnabled()){finish("Export dialog button unavailable");return;}
                w->setProperty("workspaceIndex",1);control->forceActiveFocus();const bool focusG=control->hasActiveFocus();key(Qt::Key_G,"g");
                const bool g=focusG&&w->property("workspaceIndex").toInt()==1&&unchanged();
                w->setProperty("workspaceIndex",0);control->forceActiveFocus();const bool focusD=control->hasActiveFocus();key(Qt::Key_D,"d");
                const bool d=focusD&&w->property("workspaceIndex").toInt()==0&&unchanged();
                exportModalButtons.append(QJsonObject{{"focus",QString::fromLatin1(name)},{"g_isolated",g},{"d_isolated",d},
                    {"dialog_visible",exportDialog->property("visible").toBool()},{"photo_history_selection_unchanged",unchanged()}});all=all&&g&&d;
            }
            w->setProperty("workspaceIndex",1);const auto savedTemplate=exportDialog->property("nameTemplate").toString();
            exportDialog->setProperty("nameTemplate",QString{});auto *field=find("batchExportTemplate");
            if(!field||!field->isVisible()||!field->isEnabled()){finish("Export template field unavailable");return;}
            field->forceActiveFocus();const bool focused=field->hasActiveFocus();key(Qt::Key_G,"g");key(Qt::Key_D,"d");
            exportTextInputWorks=focused&&field->property("text").toString()=="gd"&&w->property("workspaceIndex").toInt()==1&&unchanged();
            exportDialog->setProperty("nameTemplate",savedTemplate);
            if(!all||!exportTextInputWorks){finish("Export modal leaked navigation or blocked template input");return;}
            if(!click(find("exportSettingsCancel"))){finish("Export cancel unavailable");return;}advance(28);return;
        }
        if(phase==28) {
            auto *exportDialog=w->findChild<QObject*>("exportSettingsDialog");
            if(!exportDialog||exportDialog->property("visible").toBool())return;
            key(Qt::Key_G,"g");const bool g=w->property("workspaceIndex").toInt()==0;
            key(Qt::Key_D,"d");const bool d=w->property("workspaceIndex").toInt()==1;
            exportNavigation=QJsonObject{{"g_to_library",g},{"d_to_develop",d}};advance(29);return;
        }
        if(phase==29) {
            const bool selection=c->selectedIndices()==exportSelection&&c->currentIndex()==exportCurrentIndex;
            const bool noExport=!c->exportBusy();
            const bool okay=exportModalButtons.size()==2&&exportTextInputWorks&&exportNavigation.value("g_to_library").toBool()
                &&exportNavigation.value("d_to_develop").toBool()&&selection&&noExport&&stable();
            if(!record("export-modal-focus-and-navigation-isolated",okay,{{"modal_button_checks",exportModalButtons},
                {"text_input_works",exportTextInputWorks},{"navigation_after_close",exportNavigation},
                {"selection_preserved",selection},{"no_export_queued",noExport}})){finish("Export modal keyboard isolation failed");return;}
            transferTargetSource=files.filePath("owned-transfer-target.png");
            QImage target(512,320,QImage::Format_RGBA64);target.fill(QColor(170,70,40));target.setColorSpace(QColorSpace(QColorSpace::SRgb));
            if(!target.save(transferTargetSource)){finish("Transfer target fixture failed");return;}
            transferTargetHash=hash(transferTargetSource);
            if(!c->importFile(QUrl::fromLocalFile(transferTargetSource))||c->library().size()!=2){finish("Transfer target import failed");return;}
            c->selectPhoto(1);advance(30);return;
        }
        if(phase==30) {
            transferTargetBefore=c->gpuPlan(true).state.toJson();transferTargetHistory=c->editHistory();
            c->selectPhoto(0);advance(31);return;
        }
        if(phase==31) {
            if(!stable()||!c->setPhotoSelection({0,1})){finish("Transfer selection setup failed");return;}
            transferSelection=c->selectedIndices();c->copyAdjustments();
            if(!c->hasAdjustmentClipboard()){finish("Transfer copy setup failed");return;}
            transferSyncOpened=click(find("openTransferSync"));
            if(!transferSyncOpened){finish("Transfer sync action unavailable");return;}advance(32);return;
        }
        if(phase==32) {
            auto *transfer=w->findChild<QObject*>("adjustmentTransferDialog");
            if(!transfer||!transfer->property("visible").toBool())return;
            const auto value=transfer->property("groups");
            transferGroups=value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toStringList():value.toStringList();
            if(transfer->property("syncMode").toBool()!=(transferMode==0)||transfer->property("targetCount").toInt()!=1||transferGroups.isEmpty()){
                finish("Transfer mode, groups or target count unavailable");return;
            }
            transferFocus=0;advance(33);return;
        }
        if(phase==33) {
            auto *transfer=w->findChild<QObject*>("adjustmentTransferDialog");
            if(!transfer||!transfer->property("visible").toBool()){finish("Transfer dialog disappeared");return;}
            const auto unchanged=[&]{const auto value=transfer->property("groups");
                const auto groups=value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toStringList():value.toStringList();
                return transfer->property("visible").toBool()&&stable()&&c->currentIndex()==0&&c->selectedIndices()==transferSelection
                    &&groups==transferGroups&&!c->exportBusy()&&hash(transferTargetSource)==transferTargetHash;};
            const QStringList names{"transferApplyButton","transferSelectNone","transferGroup_masks","Cancel"};
            if(transferFocus>=names.size()) {
                const bool cancelled=click(canvasButton(w->contentItem(),"Cancel"));
                if(transferMode==0)transferSyncCancelled=cancelled;else transferPasteCancelled=cancelled;
                if(!cancelled){finish("Transfer cancel unavailable");return;}advance(34);return;
            }
            const auto name=names[transferFocus];auto *control=name=="Cancel"?canvasButton(w->contentItem(),name):find(name);
            if(!control||!control->isVisible()||!control->isEnabled()){finish("Transfer focus unavailable: "+name);return;}
            for(const auto &pair:{std::pair<int,QString>{Qt::Key_G,"g"},std::pair<int,QString>{Qt::Key_D,"d"}}) {
                const int workspace=pair.first==Qt::Key_G?1:0;w->setProperty("workspaceIndex",workspace);
                control->forceActiveFocus();const bool focused=control->hasActiveFocus();key(pair.first,pair.second);
                const bool consumed=keys.last().toObject().value("shortcut_consumed").toBool();
                transferChecks.append(QJsonObject{{"mode",transferMode==0?"sync":"paste"},{"focus",name},
                    {"shortcut",pair.second.toUpper()},{"focused",focused},{"shortcut_consumed",consumed},
                    {"workspace_unchanged",w->property("workspaceIndex").toInt()==workspace},{"state_unchanged",unchanged()},
                    {"expected_isolation",focused&&!consumed&&w->property("workspaceIndex").toInt()==workspace&&unchanged()}});
            }
            w->setProperty("workspaceIndex",1);++transferFocus;settle=0;return;
        }
        if(phase==34) {
            auto *transfer=w->findChild<QObject*>("adjustmentTransferDialog");if(!transfer||transfer->property("visible").toBool())return;
            w->setProperty("workspaceIndex",1);key(Qt::Key_G,"g");const bool g=w->property("workspaceIndex").toInt()==0;
            const bool gConsumed=keys.last().toObject().value("shortcut_consumed").toBool();
            key(Qt::Key_D,"d");const bool d=w->property("workspaceIndex").toInt()==1;
            const bool dConsumed=keys.last().toObject().value("shortcut_consumed").toBool();
            transferClosedChecks.append(QJsonObject{{"mode",transferMode==0?"sync":"paste"},{"g_to_library",g},{"d_to_develop",d},
                {"g_consumed",gConsumed},{"d_consumed",dConsumed},{"state_unchanged",stable()&&c->selectedIndices()==transferSelection}});
            if(transferMode==0) {
                transferMode=1;transferPasteOpened=click(canvasButton(w->contentItem(),"Paste…"));
                if(!transferPasteOpened){finish("Transfer paste action unavailable");return;}advance(32);return;
            }
            c->selectPhoto(1);advance(35);return;
        }
        if(phase==35) {
            transferTargetUnchanged=c->currentIndex()==1&&c->gpuPlan(true).state.toJson()==transferTargetBefore
                &&c->editHistory()==transferTargetHistory&&hash(transferTargetSource)==transferTargetHash;
            c->selectPhoto(0);advance(36);return;
        }
        if(phase==36) {
            transferSourceRestored=c->currentIndex()==0&&stable()&&c->setPhotoSelection(transferSelection)&&c->selectedIndices()==transferSelection;
            bool checks=transferChecks.size()==16&&transferClosedChecks.size()==2;
            for(const auto &v:transferChecks)checks=checks&&v.toObject().value("expected_isolation").toBool();
            for(const auto &v:transferClosedChecks) {const auto row=v.toObject();checks=checks&&row.value("g_to_library").toBool()
                &&row.value("d_to_develop").toBool()&&row.value("g_consumed").toBool()&&row.value("d_consumed").toBool()&&row.value("state_unchanged").toBool();}
            const bool okay=checks&&transferSyncOpened&&transferPasteOpened&&transferSyncCancelled&&transferPasteCancelled
                &&transferTargetUnchanged&&transferSourceRestored&&stable();
            if(!record("transfer-modal-focus-and-navigation-isolated",okay,{{"checks",transferChecks},{"closed_checks",transferClosedChecks},
                {"sync_clicked",transferSyncOpened},{"paste_clicked",transferPasteOpened},{"sync_cancelled",transferSyncCancelled},
                {"paste_cancelled",transferPasteCancelled},{"source_restored",transferSourceRestored},{"target_unchanged",transferTargetUnchanged}})){
                finish("Adjustment transfer modal keyboard isolation failed");return;
            }
            copyImportSelection=c->selectedIndices();c->copyImportRequested(QVariantList{QUrl::fromLocalFile(source)});advance(37);return;
        }
        if(phase==37) {
            auto *copyDialog=w->findChild<QObject*>("copyImportDialog");if(!copyDialog||!copyDialog->property("visible").toBool())return;
            const auto destination=files.filePath("owned-copy-import-destination");if(!QDir().mkpath(destination)||!copyDialog->setProperty("destination",QUrl::fromLocalFile(destination))){finish("Copy import destination setup failed");return;}copyImportOpened=true;copyImportFocus=0;advance(38);return;
        }
        if(phase==38) {
            auto *copyDialog=w->findChild<QObject*>("copyImportDialog");if(!copyDialog||!copyDialog->property("visible").toBool()){finish("Copy import dialog disappeared");return;}
            const auto unchanged=[&]{return copyDialog->property("visible").toBool()&&stable()&&c->currentIndex()==0&&c->selectedIndices()==copyImportSelection&&!c->copyImportBusy()&&!c->exportBusy();};
            const QStringList names{"copyImportCustomNames","Choose…","copyImportApply","Cancel"};
            if(copyImportFocus>=names.size()) {copyImportToggled=click(find("copyImportCustomNames"));if(!copyImportToggled){finish("Copy import custom names unavailable");return;}advance(39);return;}
            const auto name=names[copyImportFocus];auto *control=(copyImportFocus==0||copyImportFocus==2)?find(name):canvasButton(w->contentItem(),name);
            if(name=="copyImportApply"&&control&&!control->isEnabled())return;
            if(!control||!control->isVisible()||!control->isEnabled()){finish("Copy import focus unavailable: "+name);return;}
            for(const auto &pair:{std::pair<int,QString>{Qt::Key_G,"g"},std::pair<int,QString>{Qt::Key_D,"d"}}) {
                const int workspace=pair.first==Qt::Key_G?1:0;w->setProperty("workspaceIndex",workspace);control->forceActiveFocus();const bool focused=control->hasActiveFocus();key(pair.first,pair.second);
                const bool consumed=keys.last().toObject().value("shortcut_consumed").toBool();
                copyImportChecks.append(QJsonObject{{"focus",name},{"shortcut",pair.second.toUpper()},{"focused",focused},{"shortcut_consumed",consumed},{"workspace_unchanged",w->property("workspaceIndex").toInt()==workspace},{"state_unchanged",unchanged()},{"expected_isolation",focused&&!consumed&&w->property("workspaceIndex").toInt()==workspace&&unchanged()}});
            }
            w->setProperty("workspaceIndex",1);++copyImportFocus;settle=0;return;
        }
        if(phase==39) {
            auto *copyDialog=w->findChild<QObject*>("copyImportDialog");auto *field=find("copyImportPattern");
            if(!copyDialog||!copyDialog->property("visible").toBool()||!field||!field->isVisible()||!field->isEnabled()){finish("Copy import pattern unavailable");return;}
            w->setProperty("workspaceIndex",1);field->forceActiveFocus();const bool focused=field->hasActiveFocus();const auto prior=field->property("text").toString();field->setProperty("cursorPosition",prior.size());key(Qt::Key_G,"g");key(Qt::Key_D,"d");
            copyImportTextInput=focused&&field->property("text").toString()==prior+"gd"&&w->property("workspaceIndex").toInt()==1&&stable()&&c->selectedIndices()==copyImportSelection&&!c->copyImportBusy();
            copyImportCancelled=click(canvasButton(w->contentItem(),"Cancel"));if(!copyImportCancelled){finish("Copy import cancel unavailable");return;}advance(40);return;
        }
        if(phase==40) {
            auto *copyDialog=w->findChild<QObject*>("copyImportDialog");if(!copyDialog||copyDialog->property("visible").toBool())return;
            w->setProperty("workspaceIndex",1);key(Qt::Key_G,"g");const bool g=w->property("workspaceIndex").toInt()==0;const bool gConsumed=keys.last().toObject().value("shortcut_consumed").toBool();
            key(Qt::Key_D,"d");const bool d=w->property("workspaceIndex").toInt()==1;const bool dConsumed=keys.last().toObject().value("shortcut_consumed").toBool();
            const bool unchanged=stable()&&c->currentIndex()==0&&c->selectedIndices()==copyImportSelection&&!c->copyImportBusy()&&!c->exportBusy();
            copyImportClosed=QJsonObject{{"g_to_library",g},{"d_to_develop",d},{"g_consumed",gConsumed},{"d_consumed",dConsumed},{"state_unchanged",unchanged}};
            bool checks=copyImportChecks.size()==8;for(const auto &v:copyImportChecks)checks=checks&&v.toObject().value("expected_isolation").toBool();
            const bool okay=checks&&copyImportOpened&&copyImportToggled&&copyImportTextInput&&copyImportCancelled&&g&&d&&gConsumed&&dConsumed&&unchanged;
            if(!record("copy-import-modal-focus-and-navigation-isolated",okay,{{"checks",copyImportChecks},{"navigation_after_close",copyImportClosed},{"picker_result_simulated",true},{"folder_result_simulated",true},{"dialog_opened",copyImportOpened},{"custom_names_clicked",copyImportToggled},{"text_input_works",copyImportTextInput},{"cancel_clicked",copyImportCancelled},{"no_copy_queued",!c->copyImportBusy()}})){finish("Copy import modal keyboard isolation failed");return;}
            finish({});
        }
    }
};
}
void startShortcutHelpSmokeRun(PhotoController *controller,QQuickWindow *window,const QString &reportPath) {
    new ShortcutHelpSmoke(controller,window,reportPath);
}
