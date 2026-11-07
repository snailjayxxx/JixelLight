#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QPointingDevice>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QJSValue>
#include <QQmlEngine>
#include <QMouseEvent>
#include "app/BrushTabletInput.h"
#ifndef JIXELLIGHT_GIT_COMMIT
#define JIXELLIGHT_GIT_COMMIT "unknown"
#endif
#include <qpa/qwindowsysteminterface.h>
#include <private/qhighdpiscaling_p.h>
#include <iostream>
#include <limits>
#include <memory>

int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    const auto arguments=app.arguments();
    if(arguments.size()==2&&arguments[1]=="--dpi-probe") {
        QQuickView probe;probe.resize(320,200);probe.show();
        QTimer::singleShot(200,&app,[&]{std::cout<<QJsonDocument(QJsonObject{{"dpr",probe.devicePixelRatio()},{"Qt_runtime",qVersion()}}).toJson().toStdString();app.quit();});
        return app.exec();
    }
    if(arguments.size()<3||arguments.size()>4)return 2;
    const QString scenario=arguments.size()==4?arguments[3]:QStringLiteral("tablet");
    qmlRegisterType<BrushTabletInput>("Private.Input",1,0,"TabletInput");
    QQuickView view;view.setSource(QUrl::fromLocalFile(arguments[1]));
    if(view.status()!=QQuickView::Ready)return 2;
    view.show();view.requestActivate();
    QInputDevice::Capabilities capabilities=QInputDevice::Capability::Position;
    if(scenario!="no-pressure")capabilities|=QInputDevice::Capability::Pressure;
    QPointingDevice pen("Controlled tablet pen",987654321,QInputDevice::DeviceType::Stylus,
        QPointingDevice::PointerType::Pen,capabilities,1,3);
    QWindowSystemInterface::registerInputDevice(&pen);
    QPointingDevice other("Controlled second pen",987654322,QInputDevice::DeviceType::Stylus,
        QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
    QWindowSystemInterface::registerInputDevice(&other);
    std::unique_ptr<QPointingDevice> dying;
    if(scenario=="owner-destroy") {
        dying=std::make_unique<QPointingDevice>("Controlled removable pen",987654323,QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen,capabilities,1,3);
        QWindowSystemInterface::registerInputDevice(dying.get());
    }
    QTimer::singleShot(250,&app,[&]{
        ulong stamp=1000;
        const auto tablet=[&](QPointF local,qreal pressure,Qt::MouseButtons buttons,const QPointingDevice *device=nullptr) {
            const QPointF global=view.mapToGlobal(local);
            QWindowSystemInterface::handleTabletEvent(&view,stamp+=20,device?device:&pen,
                QHighDpi::toNativePixels(local,&view),QHighDpi::toNativePixels(global,&view),
                buttons,pressure,0,0,0,0,0);
            QWindowSystemInterface::flushWindowSystemEvents();QCoreApplication::processEvents();
        };
        const auto mouse=[&](QEvent::Type type,QPointF local,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,local,local,view.mapToGlobal(local),button,buttons,Qt::NoModifier);
            QCoreApplication::sendEvent(&view,&event);QCoreApplication::processEvents();
        };
        if(scenario=="barrel-held"||scenario=="cancel-barrel") {
            tablet({50,100},.2,Qt::LeftButton);
            if(scenario=="cancel-barrel")view.rootObject()->findChild<BrushTabletInput *>()->cancelGesture();
            tablet({90,100},.5,Qt::LeftButton|Qt::RightButton);
            tablet({110,100},.7,Qt::LeftButton);
            tablet({150,100},.8,Qt::LeftButton);tablet({150,100},0,Qt::NoButton);
            tablet({70,100},.3,Qt::LeftButton);tablet({140,100},.7,Qt::LeftButton);tablet({140,100},0,Qt::NoButton);
        }else if(scenario.endsWith("-duplicate")) {
            tablet({50,100},.2,Qt::LeftButton);
            auto *router=view.rootObject()->findChild<BrushTabletInput *>();
            if(!router){app.exit(3);return;}
            if(scenario=="cancel-duplicate")router->cancelGesture();
            if(scenario=="disable-duplicate"){
                view.rootObject()->setProperty("routeEnabled",false);
                view.rootObject()->setProperty("routeEnabled",true);
            }
            if(scenario=="deactivate-duplicate"){
                QEvent deactivate(QEvent::WindowDeactivate),activate(QEvent::WindowActivate);
                QCoreApplication::sendEvent(&view,&deactivate);
                QCoreApplication::sendEvent(&view,&activate);
            }
            // Deliberately malformed duplicate press bypasses QPA button-edge
            // normalization. It must not resurrect a cancelled held stroke.
            QTabletEvent duplicate(QEvent::TabletPress,&pen,{60,100},view.mapToGlobal(QPointF(60,100)),
                .6,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);
            QCoreApplication::sendEvent(&view,&duplicate);
            tablet({110,100},.8,Qt::LeftButton);tablet({110,100},0,Qt::NoButton);
            // A genuine lift permits the next ordinary QPA gesture.
            tablet({70,100},.3,Qt::LeftButton);tablet({140,100},.7,Qt::LeftButton);tablet({140,100},0,Qt::NoButton);
        }else if(scenario=="outside-window") {
            tablet({50,100},.2,Qt::LeftButton);
            for(const auto &sample:{QPair<qreal,Qt::MouseButtons>(.7,Qt::LeftButton),QPair<qreal,Qt::MouseButtons>(0,Qt::NoButton)}) {
                const QPointF local(500,300),global=view.mapToGlobal(local);
                QWindowSystemInterface::handleTabletEvent(nullptr,stamp+=20,&pen,
                    QHighDpi::toNativePixels(local,&view),QHighDpi::toNativePixels(global,&view),sample.second,sample.first,0,0,0,0,0);
                QWindowSystemInterface::flushWindowSystemEvents();QCoreApplication::processEvents();
            }
        }else if(scenario=="initial-zero") {
            tablet({50,100},0,Qt::LeftButton);tablet({100,100},.5,Qt::LeftButton);tablet({100,100},0,Qt::NoButton);
        }else if(scenario=="barrel-only") {
            tablet({50,100},.2,Qt::RightButton);tablet({100,100},.5,Qt::RightButton);tablet({100,100},0,Qt::NoButton);
        }else if(scenario=="mouse") {
            mouse(QEvent::MouseButtonPress,{50,100},Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseMove,{100,100},Qt::NoButton,Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease,{100,100},Qt::LeftButton,Qt::NoButton);
        }else if(scenario=="clipped") {
            tablet({250,100},.2,Qt::LeftButton);tablet({255,100},.7,Qt::LeftButton);tablet({255,100},0,Qt::NoButton);
        }else if(scenario=="owner-destroy") {
            tablet({50,100},.2,Qt::LeftButton,dying.get());dying.reset();QCoreApplication::processEvents();
            mouse(QEvent::MouseButtonPress,{100,100},Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease,{100,100},Qt::LeftButton,Qt::NoButton);
        }else {
            if(scenario=="mouse-owner")mouse(QEvent::MouseButtonPress,{70,100},Qt::LeftButton,Qt::LeftButton);
            tablet({50,100},.2,Qt::LeftButton);
            if(scenario=="disable"||scenario=="recover")view.rootObject()->setProperty("routeEnabled",false);
            if(scenario=="deactivate"){QEvent event(QEvent::WindowDeactivate);QCoreApplication::sendEvent(&view,&event);}
            if(scenario=="foreign") {tablet({80,110},.9,Qt::LeftButton,&other);tablet({100,110},0,Qt::NoButton,&other);}
            if(scenario=="mouse-during-pen") {
                mouse(QEvent::MouseButtonPress,{100,110},Qt::LeftButton,Qt::LeftButton);
                mouse(QEvent::MouseMove,{120,110},Qt::NoButton,Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease,{120,110},Qt::LeftButton,Qt::NoButton);
            }
            if(scenario=="invalid") {
                QTabletEvent invalid(QEvent::TabletMove,&pen,{80,100},view.mapToGlobal(QPointF(80,100)),
                    std::numeric_limits<qreal>::quiet_NaN(),0,0,0,0,0,Qt::NoModifier,Qt::NoButton,Qt::LeftButton);
                QCoreApplication::sendEvent(&view,&invalid);
            }
            tablet({100,100},.5,Qt::LeftButton);
            tablet({150,100},.8,Qt::LeftButton);tablet({150,100},0,Qt::NoButton);
            if(scenario=="mouse-owner")mouse(QEvent::MouseButtonRelease,{70,100},Qt::LeftButton,Qt::NoButton);
            if(scenario=="recover") {
                view.rootObject()->setProperty("routeEnabled",true);
                tablet({60,100},.3,Qt::LeftButton);tablet({130,100},.7,Qt::LeftButton);tablet({130,100},0,Qt::NoButton);
            }
        }
        QTimer::singleShot(100,&app,[&]{
            const QVariant raw=view.rootObject()->property("records");
            const QVariant value=raw.canConvert<QJSValue>()?raw.value<QJSValue>().toVariant():raw;
            const QJsonObject out{{"test_fixture",true},{"native_ui_acceptance",false},{"physical_tablet_verified",false},{"source_commit",JIXELLIGHT_GIT_COMMIT},{"Qt_platform",QGuiApplication::platformName()},
                {"product_UI_acceptance",false},{"Qt_runtime",qVersion()},{"actual_dpr",view.devicePixelRatio()},
                {"mouse_synthesis_enabled",QCoreApplication::testAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents)},
                {"scenario",scenario},{"router_pressed_after",view.rootObject()->findChild<BrushTabletInput *>()?view.rootObject()->findChild<BrushTabletInput *>()->pressed():false},
                {"records",QJsonArray::fromVariantList(value.toList())}};
            const QByteArray data=QJsonDocument(out).toJson(QJsonDocument::Indented);
            QSaveFile file(arguments[2]);const bool saved=file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();
            std::cout<<data.toStdString();app.exit(saved?0:1);
        });
    });
    return app.exec();
}
