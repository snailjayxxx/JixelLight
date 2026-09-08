#include <QtTest>
#include <QThread>
#include "core/raw/RawDecoder.h"

class RawWorkerTests : public QObject {
    Q_OBJECT
private slots:
    void decodeOn512KiBStack() {
        const QString path = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (path.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");
        struct Worker final : QThread {
            QString path, error;
            QImage thumbnail, image;
            CancelToken token = std::make_shared<std::atomic_bool>(false);
            ~Worker() override { token->store(true); wait(); }
            void run() override {
                thumbnail = RawDecoder::thumbnail(path, token);
                image = RawDecoder::decode(path, &error, nullptr, token);
            }
        } worker;
        worker.path = path;
        worker.setStackSize(512 * 1024);
        worker.start();
        QVERIFY2(worker.wait(60000), "Small-stack RAW worker did not finish");
        QVERIFY2(!worker.image.isNull(), qPrintable(worker.error));
        QCOMPARE(worker.image.format(), QImage::Format_RGBA64);
        // Embedded thumbnails are optional in supported RAW formats.
        QVERIFY(worker.thumbnail.isNull() || worker.thumbnail.width() <= 2048);
    }
};
QTEST_GUILESS_MAIN(RawWorkerTests)
#include "RawWorkerTests.moc"
