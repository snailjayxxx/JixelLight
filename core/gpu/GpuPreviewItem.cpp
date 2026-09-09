#include "core/gpu/GpuPreviewItem.h"
#include "core/gpu/GpuEngine.h"
#include "core/color/MonitorColorTransform.h"
#include "app/PhotoController.h"
#include <QQuickWindow>
#include <QPointer>
#include <QScreen>
#include "diagnostics/PerformanceRecorder.h"

namespace {
class Renderer final : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer *) override {
        if (m_rhi != rhi()) { m_engine.reset(); m_rhi=rhi(); m_failed=false; m_notified=0; }
        if (!m_engine) m_engine=std::make_unique<GpuEngine>(rhi());
    }
    void synchronize(QQuickRhiItem *item) override {
        const auto *view=static_cast<GpuPreviewItem *>(item);
        m_controller=qobject_cast<PhotoController *>(view->controller());
        m_source={};
        if (m_engine) m_engine->setDisplayColorLut(view->displayColorLut(), view->displayColorLutKey(), view->displayColorProfileName());
        if (m_controller && m_controller->gpuEnabled()) {
            if (m_failed && m_rhi) {
                m_engine=std::make_unique<GpuEngine>(m_rhi);m_failed=false;m_notified=0;
                m_engine->setDisplayColorLut(view->displayColorLut(), view->displayColorLutKey(), view->displayColorProfileName());
            }
            m_source=m_controller->gpuSource();
            m_plan=m_controller->gpuPlan();
            m_revision=m_controller->renderRevision();
        }
    }
    void render(QRhiCommandBuffer *cb) override {
        if (m_source.isNull() || !m_engine || m_failed) {
            cb->beginPass(renderTarget(), Qt::transparent, {1.0f,0}); cb->endPass();
            return;
        }
        if (qEnvironmentVariableIsSet("JIXELLIGHT_GPU_TIMESTAMPS")) {
            const double gpuSeconds=cb->lastCompletedGpuTime();
            if (gpuSeconds>0) PerformanceRecorder::sample("gpu_previous_frame_ms",gpuSeconds*1000.0,{{"scope",QStringLiteral("whole previous QRhi frame, not one node")}});
        }
        const auto controller=m_controller;
        const bool processed=m_engine->process(cb,m_source,m_plan,m_revision,
            [controller](quint64 revision,QByteArray counts,quint64 pixels) {
                if (controller) QMetaObject::invokeMethod(controller,[controller,revision,counts=std::move(counts),pixels] {
                    if (controller) controller->gpuScopes(revision,counts,pixels);
                },Qt::QueuedConnection);
            });
        if (!processed || !m_engine->draw(cb,renderTarget())) {
            m_failed=true;
            if (controller) QMetaObject::invokeMethod(controller,[controller,error=m_engine->error()] {
                if (controller) controller->gpuFailed(error);
            },Qt::QueuedConnection);
            return;
        }
        if (m_notified!=m_revision) {
            m_notified=m_revision;
            if (controller) QMetaObject::invokeMethod(controller,[controller,revision=m_revision,backend=m_engine->backendName()] {
                if (controller) controller->gpuPresented(revision,backend);
            },Qt::QueuedConnection);
        }
        // Drive the bounded number of frames needed to complete an asynchronous
        // readback; do not wait on the GUI thread or call finish() per frame.
        if (m_engine->hasPendingReadback()) update();
    }
private:
    QRhi *m_rhi=nullptr;
    std::unique_ptr<GpuEngine> m_engine;
    QPointer<PhotoController> m_controller;
    QImage m_source;
    ProcessingPlan m_plan;
    quint64 m_revision=0,m_notified=0;
    bool m_failed=false;
};
}
GpuPreviewItem::GpuPreviewItem(QQuickItem *parent):QQuickRhiItem(parent) {
    setAlphaBlending(false);
    m_histogramRefresh.setInterval(90); m_histogramRefresh.setSingleShot(true);
    connect(&m_histogramRefresh,&QTimer::timeout,this,[this] { update(); });
    connect(this,&QQuickItem::windowChanged,this,[this](QQuickWindow *window) {
        if (!window) return;
        connect(window,&QQuickWindow::sceneGraphInitialized,this,&GpuPreviewItem::checkBackend,Qt::QueuedConnection);
        connect(window,&QWindow::screenChanged,this,[this](QScreen *) { refreshDisplayColorManagement(); });
        connect(window,&QWindow::activeChanged,this,[this] {
            if (this->window() && this->window()->isActive()) refreshDisplayColorManagement();
        });
        QTimer::singleShot(0,this,&GpuPreviewItem::checkBackend);
        QTimer::singleShot(0,this,&GpuPreviewItem::refreshDisplayColorManagement);
    });
}
QObject *GpuPreviewItem::controller() const { return m_controller; }
void GpuPreviewItem::setController(QObject *object) {
    auto *controller=qobject_cast<PhotoController *>(object);
    if (controller==m_controller) return;
    if (m_controller) disconnect(m_controller,nullptr,this,nullptr);
    m_controller=controller;
    if (controller) {
        connect(controller,&PhotoController::gpuFrameChanged,this,[this] { update(); m_histogramRefresh.start(); });
        connect(controller,&PhotoController::backendChanged,this,[this] { update(); });
    }
    emit controllerChanged(); update();
    QTimer::singleShot(0,this,&GpuPreviewItem::checkBackend);
    QTimer::singleShot(0,this,&GpuPreviewItem::refreshDisplayColorManagement);
}
QQuickRhiItemRenderer *GpuPreviewItem::createRenderer() { return new Renderer; }

void GpuPreviewItem::checkBackend() {
    if (window() && m_controller && window()->rendererInterface()->graphicsApi()==QSGRendererInterface::Software)
        m_controller->gpuFailed(QStringLiteral("Qt Quick software scene graph"));
}

void GpuPreviewItem::refreshDisplayColorManagement() {
    constexpr int LutSize = 33;
    const MonitorColorProfile profile=MonitorColorTransform::profileForScreen(window()?window()->screen():nullptr);
    const QString requestedKey=profile.valid ? QStringLiteral("icc:")+profile.key : QStringLiteral("identity-srgb");
    if (requestedKey==m_displayColorLutKey && !m_displayColorLut.isNull()) {
        if(m_controller)m_controller->setDisplayColorLut(m_displayColorLut,m_displayColorLutKey);
        return;
    }

    QString error;
    QImage lut=profile.valid ? MonitorColorTransform::srgbToMonitorLut(profile.icc,LutSize,&error)
                             : MonitorColorTransform::identityLut(LutSize);
    QString effectiveKey=requestedKey;
    QString name=profile.valid ? profile.description : QStringLiteral("sRGB identity fallback");
    if (lut.isNull()) {
        lut=MonitorColorTransform::identityLut(LutSize);
        effectiveKey=QStringLiteral("identity-after-icc-error:")+profile.key;
        name=QStringLiteral("sRGB identity fallback (%1)").arg(error);
        PerformanceRecorder::value("display_monitor_icc_error",error);
    }
    if (lut.isNull()) return;

    m_displayColorLut=std::move(lut);
    m_displayColorLutKey=effectiveKey;
    m_displayColorProfileName=name;
    if(m_controller)m_controller->setDisplayColorLut(m_displayColorLut,m_displayColorLutKey);
    PerformanceRecorder::value("display_color_management",
        profile.valid && error.isEmpty()
            ? QStringLiteral("shared sRGB -> monitor ICC 33^3 LUT for GPU and QML image-provider display")
            : QStringLiteral("shared identity-sRGB display LUT"));
    PerformanceRecorder::value("display_monitor_profile",name);
    if (profile.valid) PerformanceRecorder::value("display_monitor_profile_path",profile.sourcePath);
    update();
}
