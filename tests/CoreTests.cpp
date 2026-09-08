#include <QtTest>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QRgba64>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include "app/PhotoController.h"
#include "core/color/ColorManagement.h"
#include "core/metadata/MetadataReader.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/raw/RawDecoder.h"
#include "core/scopes/ScopesEngine.h"
#include "diagnostics/ZipStoreWriter.h"

namespace {
int channelSpread(const QColor &c) {
    const int hi = std::max({c.red(), c.green(), c.blue()});
    const int lo = std::min({c.red(), c.green(), c.blue()});
    return hi - lo;
}
}

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void identityPipelinePreservesDisplayPixel() {
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.fill(QColor(64, 128, 192, 255));
        const QImage out = ImagePipeline::process(image, {});
        const QColor pixel = out.pixelColor(0,0);
        QVERIFY(std::abs(pixel.red() - 64) <= 2);
        QVERIFY(std::abs(pixel.green() - 128) <= 2);
        QVERIFY(std::abs(pixel.blue() - 192) <= 2);
        QCOMPARE(out.format(), QImage::Format_RGBA64);
        QCOMPARE(out.colorSpace(), QColorSpace(QColorSpace::SRgb));
    }

    void srgbExposureHappensInLinearLight() {
        QImage image(1, 1, QImage::Format_RGBA64);
        image.fill(QColor(50,50,50));
        AdjustmentState state;
        state.exposure = 1.0;
        const auto out = ImagePipeline::process(image, state);
        const int value = out.pixelColor(0,0).red();
        QVERIFY(value > 65);
        QVERIFY(value < 80);
    }

    void rawExposureUsesLinearProPhotoStops() {
        // With camera headroom, a normal scene middle gray is commonly well
        // below 18% of sensor saturation. Jixel Neutral v1 places 3% scene
        // linear near display middle gray while keeping user Exposure=0.
        QImage image(1, 1, QImage::Format_RGBA64);
        auto *px = reinterpret_cast<QRgba64 *>(image.scanLine(0));
        const quint16 sceneGray = static_cast<quint16>(std::lround(0.03 * 65535.0));
        px[0] = QRgba64::fromRgba64(sceneGray, sceneGray, sceneGray, 65535);

        const QImage baseline = ImagePipeline::process(image, {}, ImagePipeline::InputEncoding::LinearProPhoto);
        AdjustmentState plusOne;
        plusOne.exposure = 1.0;
        const QImage brighter = ImagePipeline::process(image, plusOne, ImagePipeline::InputEncoding::LinearProPhoto);

        const int baseline8 = baseline.pixelColor(0,0).red();
        const int plusOne8 = brighter.pixelColor(0,0).red();
        QVERIFY2(baseline8 >= 110 && baseline8 <= 120, qPrintable(QString::number(baseline8)));
        QVERIFY2(plusOne8 >= 153 && plusOne8 <= 164, qPrintable(QString::number(plusOne8)));
        QVERIFY(plusOne8 > baseline8 + 35);
        QVERIFY(plusOne8 < baseline8 * 2);
    }

    void rawBaseRenderingIsSeparateFromDisplayInputs() {
        const auto rawPlan = ProcessingPlan::compile({}, ImagePipeline::InputEncoding::LinearProPhoto);
        const auto displayPlan = ProcessingPlan::compile({}, ImagePipeline::InputEncoding::SRgb);
        QVERIFY(std::abs(rawPlan.data[ProcessingPlan::Flags].w - ProcessingPlan::RawBaseGain) < 1.0e-6f);
        QCOMPARE(displayPlan.data[ProcessingPlan::Flags].w, 1.0f);

        QImage raw(1,1,QImage::Format_RGBA64);
        auto *p = reinterpret_cast<QRgba64 *>(raw.scanLine(0));
        const quint16 headroomWhite = static_cast<quint16>(std::lround(0.18 * 65535.0));
        p[0] = QRgba64::fromRgba64(headroomWhite,headroomWhite,headroomWhite,65535);
        const auto rendered=ImagePipeline::process(raw,{},ImagePipeline::InputEncoding::LinearProPhoto);
        const int v=rendered.pixelColor(0,0).red();
        QVERIFY2(v >= 247 && v <= 254,qPrintable(QString::number(v)));
        QCOMPARE(rendered.text("JixelLightPipeline"),QString::fromLatin1(ProcessingPlan::EngineVersion));
        QVERIFY(rendered.text("JixelLightBaseRendering").contains("+2.5 EV"));
    }

    void rawBaseRenderingDoesNotCrushBrightHslChromaticity() {
        // Regression from the Windows/WARP dense fixture after introducing
        // Jixel Neutral. The old fixed Oklab-L=1.5 ceiling turned this bright
        // cyan/green highlight's red channel almost black after a hue edit.
        QImage image(1,1,QImage::Format_RGBA64);
        auto *px=reinterpret_cast<QRgba64 *>(image.scanLine(0));
        px[0]=QRgba64::fromRgba64(23559,59073,40796,65535);
        AdjustmentState state;state.hue=-23;state.saturation=18;state.vibrance=22;
        state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8;
        const QColor out=ImagePipeline::process(image,state,ImagePipeline::InputEncoding::LinearProPhoto).pixelColor(0,0);
        QVERIFY2(out.red()>80,qPrintable(QString("red=%1 green=%2 blue=%3").arg(out.red()).arg(out.green()).arg(out.blue())));
        QVERIFY(out.green()>240);
        QVERIFY(out.blue()>240);
    }

    void saturationRunsInsideWorkingPipeline() {
        QImage image(1, 1, QImage::Format_RGBA64);
        image.fill(QColor(180, 105, 80));
        const QColor baseline = ImagePipeline::process(image, {}).pixelColor(0,0);
        AdjustmentState state;
        state.saturation = 70.0;
        const QColor saturated = ImagePipeline::process(image, state).pixelColor(0,0);
        QVERIFY(channelSpread(saturated) > channelSpread(baseline));
    }

    void masterCurveChangesRenderedMidtones() {
        QImage image(1, 1, QImage::Format_RGBA64);
        image.fill(QColor(128,128,128));
        const int baseline = ImagePipeline::process(image, {}).pixelColor(0,0).red();
        AdjustmentState state;
        state.masterCurve[1] = 0.10;
        const int darker = ImagePipeline::process(image, state).pixelColor(0,0).red();
        QVERIFY(darker < baseline - 10);
    }

    void colorAndCurveStateRoundTripsThroughJson() {
        AdjustmentState state;
        state.hue = 15.0;
        state.saturation = 22.0;
        state.vibrance = 31.0;
        state.highlightRecovery = 45.0;
        state.hslHue[0] = -12.0;
        state.hslSaturation[5] = 38.0;
        state.hslLuminance[1] = 19.0;
        state.masterCurve[2] = 0.61;
        state.redCurve[3] = 0.82;
        const AdjustmentState restored = AdjustmentState::fromJson(state.toJson());
        QCOMPARE(restored.hue, state.hue);
        QCOMPARE(restored.saturation, state.saturation);
        QCOMPARE(restored.vibrance, state.vibrance);
        QCOMPARE(restored.highlightRecovery, state.highlightRecovery);
        QCOMPARE(restored.hslHue[0], state.hslHue[0]);
        QCOMPARE(restored.hslSaturation[5], state.hslSaturation[5]);
        QCOMPARE(restored.hslLuminance[1], state.hslLuminance[1]);
        QCOMPARE(restored.masterCurve[2], state.masterCurve[2]);
        QCOMPARE(restored.redCurve[3], state.redCurve[3]);
    }

    void histogramUses1024BinsAndCountsPixels() {
        QImage image(10, 10, QImage::Format_RGBA64);
        image.fill(QColor(128,64,32));
        const auto scopes = ScopesEngine::analyze(image, 1024);
        QCOMPARE(scopes.red.size(), 1024);
        qulonglong total = 0;
        for (const auto &v : scopes.red) total += v.toULongLong();
        QCOMPARE(total, qulonglong(100));
    }

    void namedOutputProfilesAreValidRgbIcc() {
        for (const QString &key : ColorManagement::keys()) {
            const auto space = ColorManagement::fromKey(key);
            const QByteArray profile = ColorManagement::iccProfile(space);
            QVERIFY2(profile.size() > 100, qPrintable(QStringLiteral("ICC profile missing for %1").arg(key)));
            QString description;
            QVERIFY2(ColorManagement::validateIcc(profile, &description), qPrintable(QStringLiteral("LittleCMS rejected %1").arg(key)));
        }
    }

    void colorManagedConversionAssignsDestinationProfile() {
        QImage image(4, 4, QImage::Format_RGBA64);
        image.fill(QColor(220, 80, 55));
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));

        const auto target = ColorManagement::OutputSpace::DisplayP3;
        const QImage converted = ColorManagement::convertFromSrgb(image, target);
        QVERIFY(!converted.isNull());
        QCOMPARE(converted.format(), QImage::Format_RGBA64);
        QCOMPARE(converted.colorSpace(), ColorManagement::colorSpace(target));
        QCOMPARE(converted.text(QStringLiteral("JixelLightICCManaged")), QStringLiteral("true"));
        QVERIFY(ColorManagement::validateIcc(converted.colorSpace().iccProfile()));
    }

    void rawExtensionsAreRecognized() {
        QVERIFY(RawDecoder::isRawFile("DSC00001.ARW"));
        QVERIFY(RawDecoder::isRawFile("IMG_0001.CR3"));
        QVERIFY(RawDecoder::isRawFile("DSC_0001.NEF"));
        QVERIFY(RawDecoder::isRawFile("FUJI0001.RAF"));
        QVERIFY(RawDecoder::isRawFile("photo.DNG"));
        QVERIFY(!RawDecoder::isRawFile("photo.jpg"));
    }

    void realRawMetadataSmoke() {
        const QString rawPath = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (rawPath.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");

        QString error;
        const QVariantMap metadata = MetadataReader::read(rawPath, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!metadata.isEmpty());
        QVERIFY(metadata.contains(QStringLiteral("make")));
        QVERIFY(metadata.contains(QStringLiteral("model")));
        QVERIFY(!metadata.value(QStringLiteral("make")).toString().isEmpty());
        QVERIFY(!metadata.value(QStringLiteral("model")).toString().isEmpty());
        QVERIFY(metadata.value(QStringLiteral("pixelWidth")).toULongLong() > 1000);
        QVERIFY(metadata.value(QStringLiteral("pixelHeight")).toULongLong() > 1000);
    }

    void realRawDecodeSmoke() {
        const QString rawPath = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (rawPath.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");

        QString error;
        RawMetadata metadata;
        const QImage image = RawDecoder::decode(rawPath, &error, &metadata);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(image.format(), QImage::Format_RGBA64);
        QCOMPARE(image.text(QStringLiteral("JixelLightWorkingSpace")), QStringLiteral("Linear ProPhoto RGB"));
        QVERIFY(image.width() > 1000);
        QVERIFY(image.height() > 1000);
        QCOMPARE(metadata.bitsPerChannel, 16);
        QCOMPARE(metadata.workingSpace, QStringLiteral("Linear ProPhoto RGB"));
        QVERIFY(metadata.cameraMatrixEnabled);
        QVERIFY(metadata.cameraWhiteBalanceEnabled);
        QVERIFY(metadata.highlightBlendEnabled);
        QVERIFY(!metadata.make.isEmpty());
        QVERIFY(!metadata.model.isEmpty());
    }

    void controllerImportsRealRawIntoWideGamutPipeline() {
        const QString rawPath = qEnvironmentVariable("JIXELLIGHT_TEST_RAW");
        if (rawPath.isEmpty()) QSKIP("JIXELLIGHT_TEST_RAW is not set");

        PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(rawPath)));
        QCOMPARE(controller.library().size(), 1);
        QVERIFY(controller.hasImage());
        QVERIFY(controller.currentIsRaw());
        QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady() && !controller.previewUrl().isEmpty(), 30000);
        QVERIFY(controller.pipelineDescription().contains(QStringLiteral("Linear ProPhoto RGB")));
        QVERIFY(controller.pipelineDescription().contains(QStringLiteral("ICC sRGB Preview")));
        QVERIFY(controller.currentMetadata().contains(QStringLiteral("make")));
        QVERIFY(controller.currentMetadata().contains(QStringLiteral("model")));
        QCOMPARE(controller.currentMetadata().value(QStringLiteral("workingSpace")).toString(), QStringLiteral("Linear ProPhoto RGB"));
        QCOMPARE(controller.currentMetadata().value(QStringLiteral("bitDepth")).toInt(), 16);

        controller.setSaturation(25.0);
        controller.setColorMix(5, 1, 30.0);
        controller.setCurvePoint(0, 2, 0.58);
        QCOMPARE(controller.saturation(), 25.0);
        QCOMPARE(controller.hslSaturation().at(5).toDouble(), 30.0);
        QCOMPARE(controller.masterCurve().at(2).toDouble(), 0.58);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString exportPath = dir.filePath(QStringLiteral("p3-export.jpg"));
        QSignalSpy exportedSignal(&controller, &PhotoController::exportFinished);
        QVERIFY(controller.exportCurrent(QUrl::fromLocalFile(exportPath), QStringLiteral("display-p3"), 91));
        QTRY_COMPARE_WITH_TIMEOUT(exportedSignal.size(), 1, 60000);
        QCOMPARE(exportedSignal.first().at(0).toInt(), 1);
        QCOMPARE(exportedSignal.first().at(1).toInt(), 0);
        QVERIFY(QFile::exists(exportPath));

        QImageReader reader(exportPath);
        const QImage exported = reader.read();
        QVERIFY2(!exported.isNull(), qPrintable(reader.errorString()));
        QVERIFY(exported.colorSpace().isValid());
        QCOMPARE(exported.colorSpace(), ColorManagement::colorSpace(ColorManagement::OutputSpace::DisplayP3));
        QVERIFY(ColorManagement::validateIcc(exported.colorSpace().iccProfile()));
    }

    void zipWriterCreatesZipSignature() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("test.zip");
        ZipStoreWriter zip(path);
        QVERIFY(zip.open());
        QVERIFY(zip.addFile("hello.txt", "hello"));
        QVERIFY(zip.close());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.read(4), QByteArray("PK\x03\x04",4));
    }
};

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
