#include <QtTest>
#include <QThread>
#include <QJsonArray>
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
            RawMetadata metadata;
            CancelToken token = std::make_shared<std::atomic_bool>(false);
            ~Worker() override { token->store(true); wait(); }
            void run() override {
                thumbnail = RawDecoder::thumbnail(path, token);
                metadata.captureSensorAudit=true;
                image = RawDecoder::decode(path, &error, &metadata, token);
            }
        } worker;
        worker.path = path;
        worker.setStackSize(512 * 1024);
        worker.start();
        QVERIFY2(worker.wait(60000), "Small-stack RAW worker did not finish");
        QVERIFY2(!worker.image.isNull(), qPrintable(worker.error));
        QCOMPARE(worker.image.format(), QImage::Format_RGBA64);
        QCOMPARE(worker.metadata.width,worker.image.width());
        QCOMPARE(worker.metadata.height,worker.image.height());
        const auto audit=worker.metadata.sensorAudit;
        QCOMPARE(audit["schema"].toInt(),1);
        QVERIFY(audit.contains("available"));
        QVERIFY(audit.contains("black_channel_corrections"));
        QVERIFY(audit.contains("linear_white_metadata_not_black_subtracted"));
        if(audit["available"].toBool()) {
            QVERIFY(audit["sample_count"].toInteger()>0);
            QCOMPARE(audit["pixel_sha256"].toString().size(),64);
            const auto parity=audit["parity_2x2_raw_grid_not_cfa_color"].toArray();
            QCOMPARE(parity.size(),4);
            qint64 pixels=0;
            for(const auto &item:parity) pixels+=item.toObject()["pixels"].toInteger();
            QCOMPARE(pixels,audit["sample_count"].toInteger());
        } else QVERIFY(!audit["reason"].toString().isEmpty());
        // Embedded thumbnails are optional in supported RAW formats.
        QVERIFY(worker.thumbnail.isNull() || worker.thumbnail.width() <= 2048);
    }
};
QTEST_GUILESS_MAIN(RawWorkerTests)
#include "RawWorkerTests.moc"
