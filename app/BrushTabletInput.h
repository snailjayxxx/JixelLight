#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointingDevice>
#include <QHash>

// Route tablet packets to the active brush canvas with one retained input owner.
class BrushTabletInput : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool blocked READ blocked WRITE setBlocked NOTIFY blockedChanged)
    Q_PROPERTY(bool pressed READ pressed NOTIFY pressedChanged)
public:
    explicit BrushTabletInput(QObject *parent=nullptr):QObject(parent){}
    ~BrushTabletInput() override;
    QQuickItem *target() const{return m_target;}
    void setTarget(QQuickItem *target);
    bool enabled() const{return m_enabled;}
    void setEnabled(bool value);
    bool blocked() const{return m_blocked;}
    void setBlocked(bool value){if(m_blocked==value)return;m_blocked=value;emit blockedChanged();}
    bool pressed() const{return m_owner!=nullptr;}
    Q_INVOKABLE void cancelGesture();
signals:
    void targetChanged();void enabledChanged();void blockedChanged();void pressedChanged();
    void began(qreal x,qreal y,qreal pressure,bool eraserTip);
    void moved(qreal x,qreal y,qreal pressure);
    void ended(qreal x,qreal y,qreal pressure);
    void canceled();
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    void attachWindow(QQuickWindow *window);
    bool usable() const;
    bool inside(QPointF scene) const;
    void clearOwner();
    void ignoreUntilRelease(const QPointingDevice *device);
    QPointer<QQuickItem> m_target;
    QPointer<QQuickWindow> m_window;
    // Identity only; never dereferenced. The single destruction connection
    // clears it before a device can disappear or be reused.
    const QPointingDevice *m_owner=nullptr;
    QMetaObject::Connection m_ownerDestroyed;
    QHash<const QPointingDevice *,QMetaObject::Connection> m_ignored;
    bool m_enabled=true,m_blocked=false,m_routing=false;
};
