#include <QtTest>
#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QQuickItem>
#include <QSGSimpleRectNode>
#include <QSemaphore>
#include <QCoreApplication>
#include <QEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QFileInfo>
#include <QDir>
#include <QFont>
#include <atomic>
#include <functional>
#include <utility>
#include <QScopeGuard>
#include "app/PresentedWindowCapture.h"
namespace {
class CaptureGateRelease final:public QObject {
public:
    CaptureGateRelease(QObject *parent,std::shared_ptr<QSemaphore> gate):QObject(parent),gate_(std::move(gate)){}
protected:
    bool event(QEvent *event) override {
        if(event->type()==QEvent::User){gate_->release();return true;}
        return QObject::event(event);
    }
private:
    std::shared_ptr<QSemaphore> gate_;
};
class CaptureGuiRequest final:public QObject {
public:
    static constexpr QEvent::Type RequestEvent=QEvent::Type(QEvent::User+1);
    CaptureGuiRequest(QObject *parent,std::function<void()> request):QObject(parent),request_(std::move(request)){}
protected:
    bool event(QEvent *event) override {
        if(event->type()==RequestEvent){request_();return true;}
        return QObject::event(event);
    }
private:
    std::function<void()> request_;
};
struct InFlightCaptureProbe {
    std::atomic_bool done=false,threaded=false,gateReleased=false;
    std::atomic_bool requestQueuedAfterSync=false,gateWaiting=false,gateEnteredBeforeRequest=false,requestWhileGateHeld=false;
    PresentedWindowCapture::Result result;
};
class CaptureRect final:public QQuickItem {
public:
    explicit CaptureRect(QQuickItem *parent):QQuickItem(parent){setFlag(ItemHasContents,true);}
    void color(QColor value){color_=value;update();}
protected:
    QSGNode *updatePaintNode(QSGNode *old,UpdatePaintNodeData *) override {
        auto *node=static_cast<QSGSimpleRectNode *>(old);if(!node)node=new QSGSimpleRectNode;
        node->setRect(boundingRect());node->setColor(color_);return node;
    }
private:
    QColor color_;
};
QByteArray digest(const QByteArray &b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();}
}
class PresentedWindowCaptureTests final:public QObject {
    Q_OBJECT
    QJsonArray records;
private slots:
    void initTestCase() {
#ifdef Q_OS_MACOS
        QCOMPARE(QGuiApplication::platformName(),QStringLiteral("cocoa"));
#elif defined(Q_OS_WIN)
        QCOMPARE(QGuiApplication::platformName(),QStringLiteral("windows"));
#else
        QCOMPARE(QGuiApplication::platformName(),QStringLiteral("xcb"));
#endif
    }
    void presentedWindowCaptureTracksActualFramePixels() {
        QQuickWindow window;window.resize(192,128);window.setColor(QColor(13,37,61));
        auto *rect=new CaptureRect(window.contentItem());rect->setPosition({24,16});rect->setSize({80,56});
        auto *overlay=new CaptureRect(rect);overlay->setPosition({40,24});overlay->setSize({24,16});overlay->color(QColor(224,136,40,128));
        window.show();QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(),5000);
        PresentedWindowCapture capture;QByteArray previous;QJsonArray frames;
        for(const auto foreground:{QColor(141,53,197),QColor(49,163,83)}) {
            rect->color(foreground);const auto result=capture.grab(&window,5000);
            QVERIFY2(result.ok(),qPrintable(result.error));QVERIFY(result.frameSwapped);QVERIFY(!capture.busy());
            const auto image=result.image.convertToFormat(QImage::Format_RGBA8888);const qreal dpr=window.effectiveDevicePixelRatio();
            QCOMPARE(image.size(),window.size()*dpr);QCOMPARE(image.devicePixelRatio(),dpr);
            QCOMPARE(image.pixelColor(qRound(170*dpr),qRound(110*dpr)),QColor(13,37,61));
            QCOMPARE(image.pixelColor(qRound(40*dpr),qRound(30*dpr)),foreground);
            const auto blend=image.pixelColor(qRound(70*dpr),qRound(45*dpr));
            const auto channel=[](int a,int b){return (a*128+b*127+127)/255;};
            QVERIFY(std::abs(blend.red()-channel(224,foreground.red()))<=1);
            QVERIFY(std::abs(blend.green()-channel(136,foreground.green()))<=1);
            QVERIFY(std::abs(blend.blue()-channel(40,foreground.blue()))<=1);QCOMPARE(blend.alpha(),255);
            // Independent existing Qt grab: all pixels, not just probes, must
            // agree after normal presentation. No cached-frame substitution.
            const auto control=window.grabWindow().convertToFormat(QImage::Format_RGBA8888);window.update();
            QCOMPARE(image,control);QCOMPARE(result.bufferBytes,qint64(image.width())*image.height()*8);
            QVERIFY(result.bufferBytes<=PresentedWindowCapture::BufferBudget);
            const auto hash=digest(QByteArray(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()));
            QVERIFY(!hash.isEmpty()&&hash!=previous);previous=hash;
            frames.append(QJsonObject{{"pixel_sha256",QString::fromLatin1(hash)},{"DPR",dpr},{"normal_frame_swapped",true},
                {"full_image_matches_Qt_grab",true},{"owned_buffer_bytes",result.bufferBytes}});
        }
        records.append(QJsonObject{{"name","presentedWindowCaptureTracksActualFramePixels"},{"passed",true},{"frames",frames},
            {"capture_source","actual-normal-window-backbuffer"},{"offscreen_item_or_desktop_capture",false}});
    }
    void presentedWindowCaptureBoundsReentryAndPendingWindowDestruction() {
        PresentedWindowCapture capture;QQuickWindow hidden;hidden.resize(96,64);
        QCOMPARE(capture.grab(&hidden,0).error,QStringLiteral("capture-deadline-expired"));
        QCOMPARE(capture.grab(&hidden,100).error,QStringLiteral("capture-window-not-exposed"));
        auto window=std::make_unique<QQuickWindow>();window->resize(96,64);window->setColor(QColor(19,71,139));window->show();
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(),5000);
        QCOMPARE(capture.grab(window.get(),100,1).error,QStringLiteral("capture-buffer-budget"));
        PresentedWindowCapture::Result nested;
        QTimer::singleShot(0,window.get(),[&]{nested=capture.grab(window.get(),1000);});
        const auto outer=capture.grab(window.get(),5000);QVERIFY2(outer.ok(),qPrintable(outer.error));
        QCOMPARE(nested.error,QStringLiteral("capture-reentrant"));QVERIFY(nested.image.isNull());QVERIFY(!capture.busy());
        auto gate=std::make_shared<QSemaphore>();auto entered=std::make_shared<std::atomic_bool>(false);
        window->scheduleRenderJob(QRunnable::create([gate,entered]{entered->store(true);gate->tryAcquire(1,250);}),QQuickWindow::BeforeRenderingStage);
        const auto expired=capture.grab(window.get(),25);QCOMPARE(expired.error,QStringLiteral("capture-deadline-expired"));
        QVERIFY(expired.image.isNull());QVERIFY(!capture.busy());window.reset();QVERIFY(entered->load());
        QQuickWindow healthy;healthy.resize(96,64);healthy.setColor(QColor(19,71,139));healthy.show();
        QTRY_VERIFY_WITH_TIMEOUT(healthy.isExposed(),5000);const auto recovered=capture.grab(&healthy,5000);
        QVERIFY2(recovered.ok(),qPrintable(recovered.error));QCOMPARE(recovered.image.pixelColor(8,8),QColor(19,71,139));
        records.append(QJsonObject{{"name","presentedWindowCaptureBoundsReentryAndPendingWindowDestruction"},{"passed",true},
            {"hidden_expired_overbudget_and_reentrant_refused",true},{"pending_window_destroyed",true},{"healthy_next_window",true}});
    }
    void captureWaitsForRequestSynchronizationWhileOlderFrameIsRendering() {
        QQuickWindow window;window.resize(96,64);window.setColor(QColor(13,37,61));
        auto *rect=new CaptureRect(window.contentItem());rect->setPosition({8,8});rect->setSize({64,40});rect->color(QColor(120,30,60));
        window.show();QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(),5000);
        auto capture=std::make_shared<PresentedWindowCapture>();auto gate=std::make_shared<QSemaphore>();auto probe=std::make_shared<InFlightCaptureProbe>();
        // Exposure alone does not mean the first frame ended. Qt keeps the
        // GUI blocked through submission of an expose frame, so a render gate
        // in that frame cannot be released by a queued GUI capture request.
        // Complete an actual normal capture before arming the concurrent
        // regression; its older frame must be an ordinary update frame.
        const auto warmup=capture->grab(&window,5000);
        QVERIFY2(warmup.ok(),qPrintable(warmup.error));QVERIFY(warmup.frameSwapped);
        const qreal dpr=window.effectiveDevicePixelRatio();
        QCOMPARE(warmup.image.pixelColor(qRound(20*dpr),qRound(20*dpr)),QColor(120,30,60));
        auto entered=std::make_shared<QSemaphore>();auto active=std::make_shared<std::atomic_bool>(true);
        // Retire posted requests before window teardown, including assertion
        // failure paths. Render jobs keep their shared state alive and bounded.
        const auto retire=qScopeGuard([active,gate]{active->store(false);gate->release();});
        auto *request=new CaptureGuiRequest(&window,[&window,rect,capture,gate,entered,probe,active]{
            if(!active->load())return;
            const bool oldFrameEntered=entered->tryAcquire(1,1000);
            probe->gateEnteredBeforeRequest.store(oldFrameEntered);
            probe->requestWhileGateHeld.store(oldFrameEntered&&probe->gateWaiting.load());
            if(!oldFrameEntered){gate->release();probe->done.store(true,std::memory_order_release);return;}
            rect->color(QColor(30,160,210));
            // Delivery waits until grab has installed its future-sync jobs
            // and entered its GUI event loop, preserving the old-pixel probe.
            QCoreApplication::postEvent(new CaptureGateRelease(&window,gate),new QEvent(QEvent::User),Qt::HighEventPriority);
            probe->result=capture->grab(&window,5000);probe->done.store(true,std::memory_order_release);
        });
        window.scheduleRenderJob(QRunnable::create([&window,request,probe,active]{
            if(!active->load())return;
            probe->requestQueuedAfterSync.store(true);
            // Force the competing ordinary update that can otherwise precede
            // a normal-priority GUI invocation and block on the held renderer.
            QCoreApplication::postEvent(&window,new QEvent(QEvent::UpdateRequest),Qt::NormalEventPriority);
            QCoreApplication::postEvent(request,new QEvent(CaptureGuiRequest::RequestEvent),Qt::HighEventPriority);
        }),QQuickWindow::AfterSynchronizingStage);
        window.scheduleRenderJob(QRunnable::create([&window,gate,entered,probe,active]{
            if(!active->load())return;
            probe->threaded.store(QThread::currentThread()!=window.thread());
            probe->gateWaiting.store(true);entered->release();
            probe->gateReleased.store(gate->tryAcquire(1,1000));probe->gateWaiting.store(false);
        }),QQuickWindow::BeforeRenderingStage);
        // Drive an ordinary update directly, so late native expose events
        // cannot turn the held frame into Qt's GUI-blocking expose path.
        // Process only the already-posted capture request before unrelated
        // pending updates. The capture itself still uses a normal RHI frame.
        QElapsedTimer handshake;handshake.start();window.update();
        QEvent update(QEvent::UpdateRequest);QCoreApplication::sendEvent(&window,&update);
        QVERIFY(probe->requestQueuedAfterSync.load());
        QCoreApplication::sendPostedEvents(request,CaptureGuiRequest::RequestEvent);
        QVERIFY(handshake.elapsed()<6000);
        const int remaining=6000-int(handshake.elapsed());
        QTRY_VERIFY_WITH_TIMEOUT(probe->done.load(std::memory_order_acquire),remaining);
        const auto handshakeMs=handshake.elapsed();QVERIFY(handshakeMs<6000);QVERIFY(probe->requestQueuedAfterSync.load());
        QVERIFY(probe->threaded.load());QVERIFY(probe->gateEnteredBeforeRequest.load());
        QVERIFY(probe->requestWhileGateHeld.load());QVERIFY(probe->gateReleased.load());
        QVERIFY2(probe->result.ok(),qPrintable(probe->result.error));QVERIFY(probe->result.synchronizedAfterRequest);
        QCOMPARE(probe->result.image.pixelColor(qRound(20*dpr),qRound(20*dpr)),QColor(30,160,210));
        const auto control=window.grabWindow().convertToFormat(QImage::Format_RGBA8888);window.update();
        QCOMPARE(probe->result.image.convertToFormat(QImage::Format_RGBA8888),control);
        records.append(QJsonObject{{"name","captureWaitsForRequestSynchronizationWhileOlderFrameIsRendering"},{"passed",true},
            {"warmup_normal_frame_swapped",true},{"warmup_old_revision_matches",true},
            {"capture_request_posted_from_old_after_sync",true},{"gate_entered_before_GUI_request",true},
            {"competing_normal_update_queued",true},{"normal_update_explicitly_driven",true},{"whole_handshake_deadline_ms",6000},{"whole_handshake_elapsed_ms",handshakeMs},
            {"older_frame_held_after_sync",true},{"request_during_in_flight_render",true},{"post_request_synchronization_observed",true},
            {"new_revision_pixels_match_Qt_control",true},{"DPR",dpr}});
    }
    void stableSeedObservesChangingGuiFramesBeforeExactWindowControl() {
        QQuickWindow window;window.resize(96,64);window.setColor(QColor(13,37,61));
        auto *rect=new CaptureRect(window.contentItem());rect->setPosition({8,8});rect->setSize({64,40});rect->color(QColor(120,30,60));
        window.show();QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(),5000);PresentedWindowCapture capture;
        const auto first=capture.grab(&window,5000);QVERIFY2(first.ok(),qPrintable(first.error));
        auto changes=std::make_shared<std::atomic_int>(0);
        const auto connection=QObject::connect(&window,&QQuickWindow::beforeSynchronizing,&window,[&window,rect,changes]{
            const int index=changes->fetch_add(1);
            if(index<2)QMetaObject::invokeMethod(&window,[rect,index]{rect->color(index==0?QColor(50,170,90):QColor(30,160,210));},Qt::QueuedConnection);
        },Qt::DirectConnection);
        const auto stable=capture.grabStable(&window,5000);QObject::disconnect(connection);
        QVERIFY2(stable.ok(),qPrintable(stable.frame.error));QVERIFY(!capture.busy());
        QVERIFY(stable.observations.size()>=4&&stable.observations.size()<=PresentedWindowCapture::MaxSeedFrames);
        QCOMPARE(stable.consecutiveFrames,PresentedWindowCapture::RequiredStableFrames);
        QVERIFY(stable.observations.first().pixelSHA256!=stable.observations.last().pixelSHA256);
        for(int i=stable.observations.size()-3;i<stable.observations.size();++i)QCOMPARE(stable.observations[i].pixelSHA256,stable.observations.last().pixelSHA256);
        for(const auto &row:stable.observations)QVERIFY(row.synchronizedAfterRequest);
        const qreal dpr=window.effectiveDevicePixelRatio();QCOMPARE(stable.frame.image.pixelColor(qRound(20*dpr),qRound(20*dpr)),QColor(30,160,210));
        const auto control=window.grabWindow().convertToFormat(QImage::Format_RGBA8888);window.update();
        QCOMPARE(stable.frame.image.convertToFormat(QImage::Format_RGBA8888),control);
        QVERIFY(stable.peakBufferBytes<=PresentedWindowCapture::BufferBudget);
        records.append(QJsonObject{{"name","stableSeedObservesChangingGuiFramesBeforeExactWindowControl"},{"passed",true},
            {"normal_frames",stable.observations.size()},{"required_matching_frames",3},{"consecutive_matching_frames",stable.consecutiveFrames},
            {"earlier_different_actual_frame_observed",true},{"final_full_image_matches_Qt_control",true},{"DPR",dpr}});
    }
    void bilingualCaptionKeepsPhotoViewportStable() {
        QQmlEngine engine;
        const auto path=QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("../ui/BilingualCaption.qml");
        QQmlComponent component(&engine,QUrl::fromLocalFile(path));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QQuickWindow window;window.resize(400,240);window.setColor(QColor(13,37,61));
        std::unique_ptr<QObject> owned(component.createWithInitialProperties({
            {"chineseText",QStringLiteral("当前 RAW / 编辑结果")},
            {"englishText",QStringLiteral("Current RAW / edit result")}}));
        QVERIFY2(owned,qPrintable(component.errorString()));
        QQmlEngine::setObjectOwnership(owned.get(),QQmlEngine::CppOwnership);
        auto *caption=qobject_cast<QQuickItem *>(owned.get());QVERIFY(caption);
        caption->setParentItem(window.contentItem());
        auto *canvas=new CaptureRect(window.contentItem());canvas->color(QColor(49,83,113));
        const auto layout=[&]{canvas->setPosition({0,caption->height()+6});canvas->setHeight(window.height()-caption->height()-6);};
        QObject::connect(caption,&QQuickItem::heightChanged,canvas,layout);
        layout();
        window.show();QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(),5000);
        const QFont initial=caption->property("font").value<QFont>();
        PresentedWindowCapture capture;QJsonArray rows;
        for(const QString &family:QStringList{QString{},QStringLiteral("Arial"),QStringLiteral("Helvetica"),QStringLiteral("Times New Roman"),QStringLiteral("Courier New"),QStringLiteral("PingFang SC")}) {
            QFont font=initial;if(!family.isEmpty())font.setFamily(family);
            caption->setProperty("font",font);
            for(const int width:{400,176,100}) {
                caption->setWidth(width);canvas->setWidth(width);
                caption->setProperty("chinese",false);layout();
                const auto english=capture.grab(&window,5000);QVERIFY2(english.ok(),qPrintable(english.error));
                const qreal captionHeight=caption->height(),canvasHeight=canvas->height();
                QVERIFY(captionHeight>0&&canvasHeight>0);
                QCOMPARE(caption->property("text").toString(),QStringLiteral("Current RAW / edit result"));
                const qreal padding=caption->property("topPadding").toReal()+caption->property("bottomPadding").toReal();
                QVERIFY(caption->property("contentHeight").toReal()+padding<=captionHeight+.01);
                caption->setProperty("chinese",true);
                const auto chinese=capture.grab(&window,5000);QVERIFY2(chinese.ok(),qPrintable(chinese.error));
                QCOMPARE(caption->height(),captionHeight);QCOMPARE(canvas->height(),canvasHeight);
                QCOMPARE(caption->property("text").toString(),QStringLiteral("当前 RAW / 编辑结果"));
                QVERIFY(caption->property("contentHeight").toReal()+padding<=captionHeight+.01);
                QVERIFY(english.image!=chinese.image);
                const auto control=window.grabWindow().convertToFormat(QImage::Format_RGBA8888);window.update();
                QCOMPARE(chinese.image.convertToFormat(QImage::Format_RGBA8888),control);
                rows.append(QJsonObject{{"family",family.isEmpty()?initial.family():family},{"width",width},
                    {"caption_height",captionHeight},{"canvas_height",canvasHeight},{"both_languages_unclipped",true},
                    {"canvas_height_unchanged",true},{"different_actual_language_frames",true},{"full_image_matches_Qt_control",true}});
            }
        }
        QCOMPARE(rows.size(),18);
        records.append(QJsonObject{{"name","bilingualCaptionKeepsPhotoViewportStable"},{"passed",true},
            {"rows",rows},{"DPR",window.effectiveDevicePixelRatio()},{"actual_frozen_QML_component",true}});
    }
    void cleanupTestCase() {
        const QJsonObject report{{"passed",records.size()==5},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"actual_qpa",QGuiApplication::platformName()},{"cases",records},{"QA_only",true},{"full_App_ASAN_or_physical_OS_presentation_claim",false}};
        const auto path=qEnvironmentVariable("JIXELLIGHT_PRESENTED_CAPTURE_REPORT");
        if(!path.isEmpty()){QSaveFile f(path);const auto bytes=QJsonDocument(report).toJson();QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),bytes.size());QVERIFY(f.commit());}
        qInfo().noquote()<<"PRESENTED_WINDOW_CAPTURE_EVIDENCE"<<QJsonDocument(report).toJson(QJsonDocument::Compact);
    }
};
int main(int argc,char **argv){
    QSurfaceFormat f;f.setVersion(4,3);f.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(f);
    QGuiApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);PresentedWindowCaptureTests test;return QTest::qExec(&test,argc,argv);
}
#include "PresentedWindowCaptureTests.moc"
