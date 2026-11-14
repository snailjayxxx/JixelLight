#pragma once
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QQuickWindow>
#include <QRunnable>
#include <QScopedValueRollback>
#include <QSGRendererInterface>
#include <QThread>
#include <QTimer>
#include <rhi/qrhi.h>
#include <algorithm>
#include <cstring>
#include <memory>

// QA only. Read the actual window backbuffer after a post-request scene-graph
// synchronization, then wait for that normal frame's swap/end signals. This
// cannot attach the readback to an older frame already rendering. Unlike grabWindow(), this does
// not open another frame that ends with SkipPresent. Never substitute a cached
// image, offscreen item render, desktop screenshot, or another rendering API.
class PresentedWindowCapture final {
public:
    static constexpr qint64 BufferBudget = 64 * 1024 * 1024;
    struct Result {
        QImage image;
        QString error;
        qint64 bufferBytes=0;
        bool frameSwapped=false,synchronizedAfterRequest=false;
        bool ok() const { return error.isEmpty() && synchronizedAfterRequest && frameSwapped && !image.isNull(); }
    };
    bool busy() const { return busy_; }

    Result grab(QQuickWindow *window,int remainingMs,qint64 budget=BufferBudget) {
        const auto refused=[](const char *reason){Result r;r.error=QString::fromLatin1(reason);return r;};
        if(!window || QThread::currentThread()!=window->thread())return refused("capture-GUI-owner-required");
        if(busy_)return refused("capture-reentrant");
        if(remainingMs<=0)return refused("capture-deadline-expired");
        if(!window->isVisible() || !window->isExposed())return refused("capture-window-not-exposed");
        const qreal dpr=window->effectiveDevicePixelRatio();const QSize extent=window->size()*dpr;
        const qint64 limit=std::min(budget,BufferBudget);
        const qint64 pixels=qint64(extent.width())*extent.height();
        // Both the raw readback and its owned image must fit before recording.
        if(extent.isEmpty() || !(dpr>0) || limit<=0 || pixels>limit/8)return refused("capture-buffer-budget");
        QScopedValueRollback<bool> busy(busy_,true);
        const auto state=std::make_shared<Pending>();state->extent=extent;state->dpr=dpr;
        state->bytes=pixels*4;state->limit=limit;
        QElapsedTimer elapsed;elapsed.start();
        QEventLoop waiter;QPointer<QEventLoop> waiterGuard(&waiter);QPointer<QQuickWindow> windowGuard(window);
        // These direct callbacks only touch owned state. The stack event loop
        // is inspected on its GUI thread, after posting through the window.
        const auto swapped=QObject::connect(window,&QQuickWindow::frameSwapped,window,[state]{
            QMutexLocker lock(&state->mutex);if(state->attempted && state->error.isEmpty() && !state->cancelled)state->swapped=true;
        },Qt::DirectConnection);
        const auto ended=QObject::connect(window,&QQuickWindow::afterFrameEnd,window,[state,window,waiterGuard]{
            {QMutexLocker lock(&state->mutex);if(!state->attempted || state->cancelled)return;state->ended=true;}
            QMetaObject::invokeMethod(window,[waiterGuard]{if(waiterGuard)waiterGuard->quit();},Qt::QueuedConnection);
        },Qt::DirectConnection);
        const auto destroyed=QObject::connect(window,&QObject::destroyed,&waiter,&QEventLoop::quit);
        // An AfterRendering job posted directly from the GUI can join a frame
        // that already synchronized older properties. Register it only from
        // our own future AfterSynchronizing job. Qt releases its job-list lock
        // before executing these jobs; the second job belongs to this frame.
        window->scheduleRenderJob(QRunnable::create([window,state]{
            {QMutexLocker lock(&state->mutex);if(state->cancelled)return;state->synchronized=true;}
            window->scheduleRenderJob(QRunnable::create([window,state]{readFrame(window,state);}),QQuickWindow::AfterRenderingStage);
        }),QQuickWindow::AfterSynchronizingStage);
        QTimer deadline;deadline.setSingleShot(true);deadline.setTimerType(Qt::PreciseTimer);
        QObject::connect(&deadline,&QTimer::timeout,&waiter,&QEventLoop::quit);
        deadline.start(remainingMs);window->update();
        waiter.exec(QEventLoop::ExcludeUserInputEvents);
        QObject::disconnect(swapped);QObject::disconnect(ended);QObject::disconnect(destroyed);
        QMutexLocker lock(&state->mutex);state->cancelled=true;
        Result result;result.bufferBytes=state->bufferBytes;result.frameSwapped=state->swapped;result.synchronizedAfterRequest=state->synchronized;
        if(!windowGuard)result.error=QStringLiteral("capture-window-destroyed");
        else if(elapsed.elapsed()>=remainingMs)result.error=QStringLiteral("capture-deadline-expired");
        else if(!state->error.isEmpty())result.error=state->error;
        else if(!state->ended || !state->swapped)result.error=QStringLiteral("capture-normal-frame-not-ended");
        else if(windowGuard->size()*windowGuard->effectiveDevicePixelRatio()!=extent || windowGuard->effectiveDevicePixelRatio()!=dpr)
            result.error=QStringLiteral("capture-window-resized");
        else result.image=state->image;
        return result;
    }
private:
    struct Pending {
        QMutex mutex;QSize extent;qreal dpr=1;qint64 bytes=0,limit=0,bufferBytes=0;
        QImage image;QString error;bool synchronized=false,attempted=false,swapped=false,ended=false,cancelled=false;
    };
    static void readFrame(QQuickWindow *window,const std::shared_ptr<Pending> &state) {
        {QMutexLocker lock(&state->mutex);if(state->cancelled || !state->synchronized || state->attempted)return;state->attempted=true;}
        const auto fail=[&](const char *reason){QMutexLocker lock(&state->mutex);state->error=QString::fromLatin1(reason);};
        auto *renderer=window->rendererInterface();
        auto *rhi=static_cast<QRhi *>(renderer->getResource(window,QSGRendererInterface::RhiResource));
        auto *swapchain=static_cast<QRhiSwapChain *>(renderer->getResource(window,QSGRendererInterface::RhiSwapchainResource));
        if(!rhi || !swapchain || !rhi->isRecordingFrame() || !swapchain->flags().testFlag(QRhiSwapChain::UsedAsTransferSource)){
            fail("capture-normal-RHI-swapchain-required");return;
        }
        if(swapchain->currentPixelSize()!=state->extent){fail("capture-backbuffer-size-mismatch");return;}
        auto *cb=swapchain->currentFrameCommandBuffer();if(!cb){fail("capture-frame-command-buffer-missing");return;}
        QRhiReadbackResult readback;bool completed=false;readback.completed=[&]{completed=true;};
        auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription{},&readback);cb->resourceUpdate(updates);
        // Explicit QA-only synchronous readback, like Qt's own window grab.
        // finish guarantees stack readback ownership ends before this job ends;
        // the surrounding normal frame still proceeds to presentation.
        const auto finished=rhi->finish();
        if(finished!=QRhi::FrameOpSuccess || !completed || readback.pixelSize!=state->extent || readback.data.size()!=state->bytes){
            fail("capture-backbuffer-readback-invalid");return;
        }
        if(readback.format!=QRhiTexture::RGBA8 && readback.format!=QRhiTexture::BGRA8){fail("capture-backbuffer-format-unsupported");return;}
        QImage image(state->extent,QImage::Format_RGBA8888_Premultiplied);if(image.isNull()){fail("capture-image-allocation-failed");return;}
        const int width=state->extent.width(),height=state->extent.height();const bool flip=rhi->isYUpInFramebuffer();
        for(int y=0;y<height;++y){
            const auto *input=reinterpret_cast<const uchar *>(readback.data.constData())+qint64(flip?height-1-y:y)*width*4;
            auto *output=image.scanLine(y);
            if(readback.format==QRhiTexture::RGBA8)std::memcpy(output,input,size_t(width)*4);
            else for(int x=0;x<width;++x){output[x*4]=input[x*4+2];output[x*4+1]=input[x*4+1];output[x*4+2]=input[x*4];output[x*4+3]=input[x*4+3];}
        }
        image.setDevicePixelRatio(state->dpr);
        QMutexLocker lock(&state->mutex);state->bufferBytes=state->bytes+image.sizeInBytes();
        if(state->bufferBytes>state->limit)state->error=QStringLiteral("capture-buffer-budget");
        else if(!state->cancelled)state->image=std::move(image);
    }
    bool busy_=false;
};
