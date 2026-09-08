#pragma once
#include <QQuickRhiItem>
#include <QPointer>
#include <QTimer>
class PhotoController;
class GpuPreviewItem : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
public:
    explicit GpuPreviewItem(QQuickItem *parent=nullptr);
    QObject *controller() const;
    void setController(QObject *controller);
    QQuickRhiItemRenderer *createRenderer() override;
signals:
    void controllerChanged();
private:
    void checkBackend();
    QPointer<PhotoController> m_controller;
    QTimer m_histogramRefresh;
};
