#pragma once
#include <QQuickRhiItem>
#include <QPointer>
#include <QTimer>
#include <QImage>
#include <QString>
class PhotoController;
class GpuPreviewItem : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
public:
    explicit GpuPreviewItem(QQuickItem *parent=nullptr);
    QObject *controller() const;
    void setController(QObject *controller);
    QQuickRhiItemRenderer *createRenderer() override;
    QImage displayColorLut() const { return m_displayColorLut; }
    QString displayColorLutKey() const { return m_displayColorLutKey; }
    QString displayColorProfileName() const { return m_displayColorProfileName; }
signals:
    void controllerChanged();
private:
    void checkBackend();
    void refreshDisplayColorManagement();
    QPointer<PhotoController> m_controller;
    QTimer m_histogramRefresh;
    QImage m_displayColorLut;
    QString m_displayColorLutKey;
    QString m_displayColorProfileName;
};
