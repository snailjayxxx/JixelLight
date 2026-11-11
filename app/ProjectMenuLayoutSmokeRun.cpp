#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>
#include <atomic>
#include <cmath>
#include <memory>

namespace {
QString backendName(QSGRendererInterface::GraphicsApi api) {
    switch(api) {
    case QSGRendererInterface::OpenGL:return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11:return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Metal:return QStringLiteral("Metal");
    default:return QStringLiteral("unsupported");
    }
}
QQuickItem *findAction(QQuickItem *root,const QString &text) {
    if(!root)return nullptr;
    if(root->property("text").toString()==text&&root->metaObject()->indexOfSignal("triggered()")>=0)return root;
    for(auto *child:root->childItems())if(auto *hit=findAction(child,text))return hit;
    return nullptr;
}
QPointF globalPoint(QQuickItem *item,const QPointF &p={}) {
    const auto scene=item->mapToScene(p);return item->window()->mapToGlobal(scene.toPoint());
}
bool clickButton(QQuickWindow *window,QQuickItem *button,qreal fraction) {
    if(!button||!button->isVisible()||!button->isEnabled())return false;
    const auto p=button->mapToScene({button->width()*fraction,button->height()/2});
    const auto g=window->mapToGlobal(p.toPoint());
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(window,p,g,Qt::NoButton,Qt::NoButton,QEvent::MouseMove);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(window,p,g,Qt::LeftButton,Qt::LeftButton,QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(window,p,g,Qt::NoButton,Qt::LeftButton,QEvent::MouseButtonRelease);
    return true;
}
struct State {
    QElapsedTimer clock,phaseClock;
    int index=0,phase=0;
    bool finished=false;
    quint64 previousFrame=0;
    QJsonArray rows;
    QJsonObject first;
    std::atomic<quint64> frames{0};
    std::atomic<quintptr> renderThread{0};
};
}

void startProjectMenuLayoutSmokeRun(PhotoController *controller,QQuickWindow *window,const QString &reportPath) {
    auto s=std::make_shared<State>();s->clock.start();
    const auto gui=reinterpret_cast<quintptr>(QThread::currentThreadId());
    auto *timer=new QTimer(controller);timer->setInterval(10);
    QObject::connect(window,&QQuickWindow::afterFrameEnd,controller,[s] {
        s->renderThread.store(reinterpret_cast<quintptr>(QThread::currentThreadId()),std::memory_order_release);
        s->frames.fetch_add(1,std::memory_order_acq_rel);
    },Qt::DirectConnection);
    const auto finish=[=](const QString &error) {
        if(s->finished)return;s->finished=true;timer->stop();
        const bool passed=error.isEmpty()&&s->rows.size()==4&&s->frames.load()>0&&s->renderThread.load()!=gui
            &&qApp->property("jixellightSmokeSettingsIsolated").toBool()&&!controller->hasImage();
        const QJsonObject result{{"schema",2},{"mode","project-menu-layout-only-v2"},{"passed",passed},{"error",error},
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"qt_version",QString::fromLatin1(qVersion())},
            {"GUI_elapsed_ms",s->clock.elapsed()},{"GUI_deadline_ms",60000},{"actual_qpa",QGuiApplication::platformName()},
            {"actual_backend",int(window->rendererInterface()->graphicsApi())},{"actual_DPR",window->effectiveDevicePixelRatio()},
            {"actual_backend_name",backendName(window->rendererInterface()->graphicsApi())},
            {"GUI_thread",QString::number(gui,16)},{"actual_RHI_thread",QString::number(s->renderThread.load(),16)},
            {"actual_afterFrameEnd_count",qint64(s->frames.load())},{"settings_isolated",qApp->property("jixellightSmokeSettingsIsolated").toBool()},
            {"cases",s->rows},{"actual_window_system_pointer",true},{"menu_close_is_controlled_cleanup",true},
            {"image_import_or_XMP_merge_exercised",false},{"physical_input_or_monitor_calibration_claim",false},
            {"Root_or_CI_or_F0_F6_acceptance_claim",false}};
        QFile f(reportPath);const auto b=QJsonDocument(result).toJson(QJsonDocument::Indented);
        const bool saved=f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(b)==b.size()&&f.flush();
        QTimer::singleShot(0,qApp,[code=passed&&saved?0:1]{QCoreApplication::exit(code);});
    };
    QObject::connect(timer,&QTimer::timeout,controller,[=] {
        if(s->finished)return;
        if(s->clock.elapsed()>=60000){finish("Menu layout GUI deadline");return;}
        if(s->index==4){finish({});return;}
        const QString language=(s->index%2==0)?"en_US":"zh_CN";
        const int width=s->index<2?1180:1540,height=s->index<2?720:920;
        const QString text=language=="en_US"?"Import XMP into current version…":"导入 XMP 到当前版本…";
        auto *button=window->findChild<QQuickItem*>("projectMenuButton");
        auto *menu=window->findChild<QObject*>("projectActionsMenu");
        if(!button||!menu){if(s->clock.elapsed()>5000)finish("Real project menu objects missing");return;}
        const bool visible=menu->property("visible").toBool();
        if(s->phase==0) {
            controller->setLanguage(language);window->resize(width,height);s->previousFrame=s->frames.load();
            s->phase=1;s->phaseClock.restart();window->requestUpdate();return;
        }
        if(s->phase==1) {
            if(s->frames.load()<=s->previousFrame)return;
            if(!clickButton(window,button,.25)){finish("First actual project button pointer refused");return;}
            s->phase=2;s->phaseClock.restart();return;
        }
        if(s->phase==3) {
            if(visible){if(s->phaseClock.elapsed()>2000)finish("Controlled menu cleanup did not close");return;}
            if(!clickButton(window,button,.75)){finish("Second actual project button pointer refused");return;}
            s->phase=4;s->phaseClock.restart();return;
        }
        if((s->phase!=2&&s->phase!=4)||!visible)return;
        auto *content=menu->property("contentItem").value<QQuickItem*>();
        auto *action=content?findAction(content,text):nullptr;
        auto *label=action?action->property("contentItem").value<QQuickItem*>():nullptr;
        if(!content||!action||!label||!action->isVisible()||label->implicitWidth()<=0) {
            if(s->phaseClock.elapsed()>2000)finish("Actual localized menu label missing");return;
        }
        const bool truncated=label->property("truncated").toBool();
        const qreal textWidth=QFontMetricsF(label->property("font").value<QFont>()).horizontalAdvance(text);
        if(truncated||label->width()+1<label->implicitWidth()||label->width()+1<textWidth) {
            if(s->phaseClock.elapsed()>1000)finish("Localized project-menu label remains truncated");return;
        }
        const auto menuOrigin=globalPoint(content,{-menu->property("leftPadding").toReal(),-menu->property("topPadding").toReal()});
        const auto anchor=globalPoint(button,{0,button->height()});
        const bool anchorCorrect=std::abs(menuOrigin.x()-anchor.x())<=2&&std::abs(menuOrigin.y()-anchor.y())<=2;
        const qreal menuWidth=menu->property("width").toReal();
        if(!anchorCorrect||menuWidth>width-24||menuWidth<200||menu->property("parent").value<QQuickItem*>()!=button) {
            finish("Real menu did not remain anchored and bounded");return;
        }
        QJsonObject row{{"language",language},{"window_width",width},{"window_height",height},{"menu_text",text},
            {"actual_menu_width",menuWidth},{"label_width",label->width()},{"label_implicit_width",label->implicitWidth()},
            {"actual_font_text_width",textWidth},{"actual_truncated",truncated},{"menu_global_x",menuOrigin.x()},
            {"menu_global_y",menuOrigin.y()},{"button_bottom_global_x",anchor.x()},{"button_bottom_global_y",anchor.y()},
            {"actual_button_parent",true},{"anchor_error_at_most2",anchorCorrect}};
        if(s->phase==2) {
            s->first=row;
            if(!QMetaObject::invokeMethod(menu,"close",Qt::DirectConnection)){finish("Menu cleanup unavailable");return;}
            s->phase=3;s->phaseClock.restart();return;
        }
        const bool stable=s->first.value("menu_global_x")==row.value("menu_global_x")&&s->first.value("menu_global_y")==row.value("menu_global_y");
        if(!stable){finish("Menu origin followed the different pointer point");return;}
        row.insert("first_menu_global_x",s->first.value("menu_global_x"));
        row.insert("first_menu_global_y",s->first.value("menu_global_y"));
        row.insert("first_button_bottom_global_x",s->first.value("button_bottom_global_x"));
        row.insert("first_button_bottom_global_y",s->first.value("button_bottom_global_y"));
        auto *captureWindow=content->window();const QImage image=captureWindow->grabWindow();
        const QString capture=reportPath+".case-"+QString::number(s->index)+".png";QFile f(capture);
        if(image.isNull()||!f.open(QIODevice::WriteOnly|QIODevice::NewOnly)||!image.save(&f,"PNG")||!f.flush()) {finish("Fresh owned menu capture failed");return;}
        f.close();QFile read(capture);if(!read.open(QIODevice::ReadOnly)){finish("Capture hash read failed");return;}
        QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&read)){finish("Capture hash failed");return;}
        row.insert("two_distinct_button_pointer_origins_stable",stable);row.insert("first_pointer_fraction",.25);row.insert("second_pointer_fraction",.75);
        row.insert("capture",QJsonObject{{"path",capture},{"width",image.width()},{"height",image.height()},{"sha256",QString::fromLatin1(hash.result().toHex())}});
        s->rows.append(row);QMetaObject::invokeMethod(menu,"close",Qt::DirectConnection);++s->index;s->phase=0;
    });
    QTimer::singleShot(0,controller,[=]{window->show();window->requestUpdate();timer->start();});
}
