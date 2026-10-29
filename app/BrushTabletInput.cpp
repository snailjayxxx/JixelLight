#include "BrushTabletInput.h"
#include <QTabletEvent>
#include <QMouseEvent>
#include <cmath>

BrushTabletInput::~BrushTabletInput(){if(m_window)m_window->removeEventFilter(this);}
void BrushTabletInput::setTarget(QQuickItem *value) {
    if(m_target==value)return;cancelGesture();clearOwner();
    if(m_target)disconnect(m_target,nullptr,this,nullptr);
    m_target=value;
    if(value) {
        connect(value,&QQuickItem::windowChanged,this,&BrushTabletInput::attachWindow);
        connect(value,&QQuickItem::visibleChanged,this,[this]{if(!usable())cancelGesture();});
        connect(value,&QQuickItem::enabledChanged,this,[this]{if(!usable())cancelGesture();});
        connect(value,&QObject::destroyed,this,[this]{m_routing=false;clearOwner();attachWindow(nullptr);});
    }
    attachWindow(value?value->window():nullptr);emit targetChanged();
}
void BrushTabletInput::attachWindow(QQuickWindow *value) {
    if(m_window==value)return;cancelGesture();clearOwner();
    if(m_window)m_window->removeEventFilter(this);
    for(const auto &connection:m_ignored)disconnect(connection);m_ignored.clear();
    m_window=value;if(value)value->installEventFilter(this);
}
void BrushTabletInput::setEnabled(bool value) {
    if(m_enabled==value)return;m_enabled=value;if(!value)cancelGesture();emit enabledChanged();
}
bool BrushTabletInput::usable() const {
    return m_enabled&&m_target&&m_window&&m_target->isEnabled()&&m_target->isVisible()
        &&m_target->width()>0&&m_target->height()>0;
}
bool BrushTabletInput::inside(QPointF scene) const {
    if(!usable()||!std::isfinite(scene.x())||!std::isfinite(scene.y()))return false;
    if(!QRectF(0,0,m_window->width(),m_window->height()).contains(scene))return false;
    if(!m_target->contains(m_target->mapFromScene(scene)))return false;
    for(auto *p=m_target->parentItem();p;p=p->parentItem())
        if(p->clip()&&!p->contains(p->mapFromScene(scene)))return false;
    return true;
}
void BrushTabletInput::cancelGesture(){if(m_routing){m_routing=false;emit canceled();}}
void BrushTabletInput::clearOwner(){const bool held=pressed();disconnect(m_ownerDestroyed);m_owner=nullptr;m_routing=false;if(held)emit pressedChanged();}
void BrushTabletInput::ignoreUntilRelease(const QPointingDevice *device) {
    if(m_ignored.contains(device))return;
    m_ignored.insert(device,connect(device,&QObject::destroyed,this,[this,device]{m_ignored.remove(device);}));
}
bool BrushTabletInput::eventFilter(QObject *watched,QEvent *event) {
    if(watched!=m_window)return false;
    const auto type=event->type();
    if(type==QEvent::WindowDeactivate||type==QEvent::Hide||type==QEvent::Close){cancelGesture();return false;}
    if(pressed()&&(type==QEvent::MouseButtonPress||type==QEvent::MouseMove||type==QEvent::MouseButtonRelease)) {
        // Freeze one input owner until its lift, including after cancellation.
        event->accept();return true;
    }
    if(type!=QEvent::TabletPress&&type!=QEvent::TabletMove&&type!=QEvent::TabletRelease)return false;
    auto *pen=static_cast<QTabletEvent *>(event);const auto *device=pen->pointingDevice();
    if(!device)return false;
    const auto handled=[&]{event->accept();return true;};
    if(m_ignored.contains(device)){if(type==QEvent::TabletRelease&&!(pen->buttons()&Qt::LeftButton)){disconnect(m_ignored.take(device));}return handled();}
    const auto pointer=device->pointerType();
    if(pointer!=QPointingDevice::PointerType::Pen&&pointer!=QPointingDevice::PointerType::Eraser)return false;
    if(pressed()&&device!=m_owner) {
        if(type==QEvent::TabletPress&&inside(pen->position())){ignoreUntilRelease(device);return handled();}
        return false;
    }
    const bool pressureCapable=device->capabilities().testFlag(QInputDevice::Capability::Pressure);
    const qreal pressure=pressureCapable?pen->pressure():1;
    const bool valid=std::isfinite(pressure)&&pressure>=0&&pressure<=1
        &&std::isfinite(pen->position().x())&&std::isfinite(pen->position().y());
    if(type==QEvent::TabletPress) {
        // A cancelled owner stays captured until its primary tip lifts.
        // A duplicate press or barrel-button edge cannot restart its stroke.
        if(pressed())return handled();
        if(pen->button()!=Qt::LeftButton||!(pen->buttons()&Qt::LeftButton)||!inside(pen->position()))return false;
        if(m_blocked||!valid){ignoreUntilRelease(device);return handled();}
        m_owner=device;m_routing=true;emit pressedChanged();
        m_ownerDestroyed=connect(device,&QObject::destroyed,this,[this,device]{
            if(m_owner!=device)return;const bool send=m_routing;clearOwner();if(send)emit canceled();
        });
        const auto local=m_target->mapFromScene(pen->position());emit began(local.x(),local.y(),pressure,pointer==QPointingDevice::PointerType::Eraser);return handled();
    }
    if(!pressed())return false;
    if(!valid||!usable())cancelGesture();
    if(type==QEvent::TabletRelease) {
        // Releasing a side button while the tip remains down is not a lift.
        if(pen->buttons()&Qt::LeftButton)return handled();
        const bool send=m_routing&&valid&&usable();const auto local=m_target?m_target->mapFromScene(pen->position()):QPointF{};
        clearOwner();if(send)emit ended(local.x(),local.y(),pressure);return handled();
    }
    if(m_routing){const auto local=m_target->mapFromScene(pen->position());emit moved(local.x(),local.y(),pressure);}
    return handled();
}
