#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QUrl>

#include "app/PhotoController.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/raw/RawDecoder.h"
#include "core/scopes/ScopesEngine.h"
#include "diagnostics/ZipStoreWriter.h"

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void identityPipelinePreservesPixel() {
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.fill(QColor(64, 128, 192, 255));
        const QImage out = ImagePipeline::process(image, {});
        QCOMPARE(out.pixelColor(0,0), QColor(64,128,192,255));
        QCOMPARE(out.format(), QImage::Format_RGBA64);
    }

    void exposureBrightens() {
        QImage image(1, 1, QImage::Format_RGBA64); image.fill(QColor(50,50,50));
        AdjustmentState s; s.exposure = 1.0;
        const auto out = ImagePipeline::process(image, s);
        QVERIFY(out.pixelColor(0,0).red() > 90);
    }

    void histogramUses1024BinsAndCountsPixels() {
        QImage image(10, 10, QImage::Format_RGBA64); image.fill(QColor(128,64,32));
        const auto scopes = ScopesEngine::analyze(image, 1024);
        QCOMPARE(scopes.red.size(), 1024);
        qulonglong total = 0; for (const auto &v : scopes.red) total += v.toULongLong();
        QCOMPARE(total, qulonglong(100));
    }

    void rawExtensionsAreRecognized() {
        QVERIFY(RawDecoder::isRawFile("DSC00001.ARW"));
        QVERIFY(RawDecoder::isRawFile("IMG_0001.CR3"));
        QVERIFY(RawDecoder::isRawFile("DSC_0001.NEF"));
        QVERIFY(RawDecoder::isRawFile("FUJI0001.RAF"));
        QVERIFY(RawDecoder::isRawFile("photo.DNG"));
        QVERIFY(!RawDecoder::isRawFile("photo.jpg"));
    }

    void realRawDecodeSmoke() {
        const QString rawPath = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (rawPath.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");

        QString error;
        RawMetadata metadata;
        const QImage image = RawDecoder::decode(rawPath, &error, &metadata);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(image.format(), QImage::Format_RGBA64);
        QVERIFY(image.width() > 1000);
        QVERIFY(image.height() > 1000);
        QCOMPARE(metadata.bitsPerChannel, 16);
        QVERIFY(!metadata.make.isEmpty());
        QVERIFY(!metadata.model.isEmpty());
    }

    void controllerImportsRealRaw() {
        const QString rawPath = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (rawPath.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");

        PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(rawPath)));
        QCOMPARE(controller.library().size(), 1);
        QVERIFY(controller.hasImage());
        QVERIFY(controller.currentIsRaw());
        QVERIFY(!controller.previewUrl().isEmpty());
        QVERIFY(controller.statusMessage().contains(QStringLiteral("RAW")) || controller.statusMessage().contains(QStringLiteral("导入")));
    }

    void zipWriterCreatesZipSignature() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("test.zip");
        ZipStoreWriter zip(path); QVERIFY(zip.open()); QVERIFY(zip.addFile("hello.txt", "hello")); QVERIFY(zip.close());
        QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.read(4), QByteArray("PK\x03\x04",4));
    }
};

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
