#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "app/BrushTabletInput.h"
#include <QApplication>
#include <QFile>
#include <QEventLoop>
#include <utility>
#include <QQuickWindow>
#include <QQuickItem>
#include <QPointer>
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
    bool capture(const QString &label) {
        if(!w)return false;const auto image=w->grabWindow();const auto path=report+"."+label+".png";
        if(image.isNull()||!image.save(path))return false;
        captures.append(QJsonObject{{"label",label},{"path",path},{"width",image.width()},{"height",image.height()},
            {"logical_width",w->width()},{"logical_height",w->height()},{"dpr",w->effectiveDevicePixelRatio()},
            {"sha256",QString::fromLatin1(hash(path))}});return true;
    }
    void finish(const QString &error) {
        if(done)return;done=true;timer->stop();const bool captured=capture(error.isEmpty()?"final":"failure");
        const bool passed=error.isEmpty()&&records.size()==14&&stable()&&captured&&captures.size()==5&&backendCaptionChecks==13&&backendCaptionsCurrent&&clock.elapsed()<60000;
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
            auto *input=tablet();if(!record("fresh-pressure-after-primary-lift-cancels-without-edit",!visible()&&!c->brushDrawing()&&input&&!input->pressed()&&stable())){finish("Fresh pressure cancellation changed edits/history");return;}finish({});
        }
    }
};
}
void startShortcutHelpSmokeRun(PhotoController *controller,QQuickWindow *window,const QString &reportPath) {
    new ShortcutHelpSmoke(controller,window,reportPath);
}
