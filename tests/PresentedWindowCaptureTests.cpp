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
#include <atomic>
#include "app/PresentedWindowCapture.h"
namespace {
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
    void cleanupTestCase() {
        const QJsonObject report{{"passed",records.size()==2},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
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
