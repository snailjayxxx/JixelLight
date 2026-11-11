#pragma once
#include <QQuickRhiItem>
#include <QPointer>
#include <QTimer>
#include <QImage>
#include <QString>
#include "core/gpu/brush/NativeBrushUpload.h"
#include <functional>
class PhotoController;
class QQuickWindow;
class GpuPreviewItem : public QQuickRhiItem {
    friend void startNativeBrushMonitorSmokeRun(PhotoController *,QQuickWindow *,const QString &);
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
public:
    explicit GpuPreviewItem(QQuickItem *parent=nullptr);
    ~GpuPreviewItem() override;
    void bindNativeRetirement(NativeBrush::DomainRetirement,std::function<void()>);
    void retireNativeResources();
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
    void releaseResources() override;
    NativeBrush::DomainRetirement m_nativeRetirement;
    std::function<void()> m_nativeReclaim;
    QPointer<QQuickWindow> m_nativeWindow;
    QPointer<PhotoController> m_controller;
    QTimer m_histogramRefresh;
    QImage m_displayColorLut;
    QString m_displayColorLutKey;
    QString m_displayColorProfileName;
};
