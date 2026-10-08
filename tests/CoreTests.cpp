#include <QtTest>
#include <QColorSpace>
#include <QFile>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonDocument>
#include "core/project/ProjectDatabase.h"
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
#include "core/pipeline/StageGraph.h"
#include "core/export/PngExporter.h"
#include "core/commands/CommandRegistry.h"
#include "core/raw/RawDecoder.h"
#include "core/scopes/ScopesEngine.h"
#include "diagnostics/ZipStoreWriter.h"

namespace {
int channelSpread(const QColor &c) {
    const int hi = std::max({c.red(), c.green(), c.blue()});
    const int lo = std::min({c.red(), c.green(), c.blue()});
    return hi - lo;
}
QImage sceneGrayImage(double value) {
    QImage image(1, 1, QImage::Format_RGBA64);
    auto *px = reinterpret_cast<QRgba64 *>(image.scanLine(0));
    const quint16 code = static_cast<quint16>(std::lround(value * 65535.0));
    px[0] = QRgba64::fromRgba64(code, code, code, 65535);
    return image;
}
}

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void editHistoryBranchesAndPreservesSonyState() {
        EditHistory history;
        AdjustmentState original;
        original.look.mode = "as-shot";
        history.initialize(original);
        auto edited = original;
        edited.exposure = 1;
        QVERIFY(history.record(edited, "exposure", "exposure"));
        edited.exposure = 2;
        QVERIFY(history.record(edited, "exposure", "exposure"));
        QCOMPARE(history.entries().size(), 2);
        QCOMPARE(history.undo().toJson(), original.toJson());
        QCOMPARE(history.redo().exposure, 2.0);
        history.finish();
        edited.look.mode = "manual";
        edited.look.code = "FL";
        edited.look.parameters["contrast"] = -2;
        history.record(edited, "look");
        QCOMPARE(history.undo().look.mode, QStringLiteral("as-shot"));
        edited.saturation = 12;
        history.record(edited, "saturation");
        QVERIFY(!history.canRedo());
        for (int i = 0; i < 160; ++i) {
            edited.tint = i;
            history.record(edited, "tint");
        }
        QCOMPARE(history.entries().size(), 129);
        int undos = 0;
        while (history.canUndo()) { history.undo(); ++undos; }
        QCOMPARE(undos, 128);
    }

    void controllerUndoIsPerPhotoAndPersistsRestoredState() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const QString a = dir.filePath("a.png"), b = dir.filePath("b.png");
        QVERIFY(image.save(a)); QVERIFY(image.save(b));
        PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(a)));
        QVERIFY(controller.importFile(QUrl::fromLocalFile(b)));
        controller.selectPhoto(0);
        controller.setExposure(1); controller.setExposure(2);
        controller.finishInteraction();
        controller.setSaturation(30);
        controller.undo(); QCOMPARE(controller.saturation(), 0.0);
        controller.undo(); QCOMPARE(controller.exposure(), 0.0);
        controller.redo(); QCOMPARE(controller.exposure(), 2.0);
        controller.selectPhoto(1); QVERIFY(!controller.canUndo());
        controller.setExposure(-1); controller.finishInteraction();
        controller.selectPhoto(0); QCOMPARE(controller.exposure(), 2.0);
        controller.resetAdjustments(); QCOMPARE(controller.exposure(), 0.0);
        controller.undo(); QCOMPARE(controller.exposure(), 2.0);
        controller.copyAdjustments(); controller.selectPhoto(1);
        controller.pasteAdjustments(); QCOMPARE(controller.exposure(), 2.0);
        controller.undo(); QCOMPARE(controller.exposure(), -1.0);
        controller.rotatePhoto(1);
        QCOMPARE(controller.geometry()["quarterTurns"].toInt(),1);
        controller.undo(); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),0);
        controller.redo(); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),1);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()), "history"));
        QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr);
        QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        reopened.selectPhoto(1); QCOMPARE(reopened.exposure(), -1.0);
        QCOMPARE(reopened.geometry()["quarterTurns"].toInt(),1);
        QSignalSpy exported(&controller, &PhotoController::exportFinished);
        QVERIFY(controller.exportAll(QUrl::fromLocalFile(dir.path()), "display-p3",92,"png"));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(),1,10000);
        QCOMPARE(exported.first()[0].toInt(),2);
        QCOMPARE(QImage(dir.filePath("a_JixelLight.png")).depth(),64);
        QCOMPARE(QImage(dir.filePath("b_JixelLight.png")).depth(),64);
    }

    void stageDependenciesIsolateColorFromGeometry() {
        PrepareRequest request;
        request.image = QImage(256,128,QImage::Format_RGBA64);
        request.image.fill(Qt::gray);
        request.fullResolution = true;
        AdjustmentState state;
        const auto before = StageGraph::describe("source", request, state, true, 0);
        state.exposure = 1;
        const auto edited = StageGraph::describe("source", request, state, true, 0);
        QCOMPARE(before["prepare"], edited["prepare"]);
        QVERIFY(before["render"] != edited["render"]);
        QVERIFY(before["scopes"] != edited["scopes"]);
        request.zoom = 1;
        QVERIFY(before["prepare"] != StageGraph::describe("source",request,state,true,0)["prepare"]);
        const auto first = preparePreview(request, {});
        const auto second = preparePreview(request, {});
        QCOMPARE(first.normal, second.normal);
        QCOMPARE(first.gpu.cacheKey(), second.gpu.cacheKey());
        auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(preparePreview(request, cancel).normal.isNull());
        request.image.fill(Qt::red);
        const auto changed = preparePreview(request, {});
        QVERIFY(changed.normal != first.normal);
    }

    void pngExportPreserves16BitsAndIccAndCancellation() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage source(17,9,QImage::Format_RGBA64);
        source.fill(QColor::fromRgbF(.17,.23,.29));
        AdjustmentState state; state.exposure = .5;
        const auto space = ColorManagement::OutputSpace::ProPhotoRgb;
        QString error;
        const QString path = dir.filePath("export.png");
        QVERIFY2(exportPng16(source,state,path,space,{},&error),qPrintable(error));
        QImageReader reader(path);
        const auto reopened = reader.read();
        QCOMPARE(reopened.depth(), 64);
        QCOMPARE(reopened.colorSpace(), ColorManagement::colorSpace(space));
        const auto expected = ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,false,0));
        const auto actualPixel = reopened.pixelColor(4,4).rgba64(), expectedPixel = expected.pixelColor(4,4).rgba64();
        QCOMPARE(actualPixel, expectedPixel);
        const QString rawPath = dir.filePath("raw.png");
        QVERIFY2(exportPng16(source,state,rawPath,space,{},&error,true,.3f),qPrintable(error));
        const auto rawExpected = ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,true,.3f));
        QCOMPARE(QImage(rawPath).pixelColor(4,4).rgba64(), rawExpected.pixelColor(4,4).rgba64());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray before = file.readAll(); file.close();
        auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!exportPng16(source,state,path,space,cancel,&error));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),before);
    }

    void commandRegistryRejectsInvalidAndMatchesUiRanges() {
        AdjustmentState state;
        QString error;
        QVERIFY(CommandRegistry::execute(state,{{"command","develop.set"},{"parameter","exposure"},{"value",8}},&error));
        QCOMPARE(state.exposure,5.0);
        const auto before=state.toJson();
        QVERIFY(!CommandRegistry::execute(state,{{"command","develop.set"},{"parameter","unknown"},{"value",1}},&error));
        QVERIFY(!error.isEmpty()); QCOMPARE(state.toJson(),before);
        QVERIFY(!CommandRegistry::set(state,"exposure",std::numeric_limits<double>::infinity(),&error));
        QCOMPARE(state.toJson(),before);
        QVERIFY(!CommandRegistry::execute(state,{{"command","develop.set"},{"parameter","exposure"},{"value","2"}},&error));
        QCOMPARE(CommandRegistry::schema()["parameters"].toArray().size(),12);
    }

    void geometryPreservesPixelsAndRestoresOldProjectDefaults() {
        QImage source(4,2,QImage::Format_RGBA64);
        source.setColorSpace(ColorManagement::colorSpace(ColorManagement::OutputSpace::ProPhotoRgb));
        for(int y=0;y<2;++y)for(int x=0;x<4;++x) source.setPixelColor(x,y,QColor(20+x*30,40+y*60,80));
        const auto original=source.copy();
        GeometryState geometry;
        QCOMPARE(geometry.apply(source).cacheKey(),source.cacheKey());
        geometry.quarterTurns=1;
        const auto rotated=geometry.apply(source);
        QCOMPARE(rotated.size(),QSize(2,4));
        QCOMPARE(rotated.pixelColor(1,0),source.pixelColor(0,0));
        QCOMPARE(rotated.colorSpace(),source.colorSpace());
        geometry.quarterTurns=0; geometry.crop=QRectF(.25,0,.5,1);
        QCOMPARE(geometry.apply(source),source.copy(1,0,2,2));
        geometry.flipHorizontal=true;
        QCOMPARE(geometry.apply(source),source.copy(1,0,2,2).mirrored(true,false));
        QCOMPARE(source,original);
        AdjustmentState state; state.geometry=geometry;
        QCOMPARE(AdjustmentState::fromJson(state.toJson()).geometry.toJson(),geometry.toJson());
        QCOMPARE(AdjustmentState::fromJson({}).geometry.crop,QRectF(0,0,1,1));
        QVERIFY(!GeometryState::validCrop(-.1,0,.5,1));
        QVERIFY(!GeometryState::validCrop(0,0,0,1));
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString error;
        QVERIFY2(exportPng16(source,state,dir.filePath("cropped.png"),ColorManagement::OutputSpace::SRgb,{},&error),qPrintable(error));
        QCOMPARE(QImage(dir.filePath("cropped.png")).size(),QSize(2,2));
        PrepareRequest request; request.image=source; request.geometry=geometry;
        QCOMPARE(preparePreview(request,{}).normal,geometry.apply(source));
    }

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
        const QImage image = sceneGrayImage(ProcessingPlan::RawNeutralSceneGray);
        const QImage baseline = ImagePipeline::process(image, {}, ImagePipeline::InputEncoding::LinearProPhoto);
        AdjustmentState plusOne;
        plusOne.exposure = 1.0;
        const QImage brighter = ImagePipeline::process(image, plusOne, ImagePipeline::InputEncoding::LinearProPhoto);

        const int baseline8 = baseline.pixelColor(0,0).red();
        const int plusOne8 = brighter.pixelColor(0,0).red();
        QVERIFY2(baseline8 >= 114 && baseline8 <= 121, qPrintable(QString::number(baseline8)));
        QVERIFY2(plusOne8 >= 148 && plusOne8 <= 155, qPrintable(QString::number(plusOne8)));
        QVERIFY(plusOne8 > baseline8 + 28);
        QVERIFY(plusOne8 < baseline8 * 2);
    }

    void rawBaseRenderingIsSeparateFromLinearRepresentation() {
        const auto rawPlan = ProcessingPlan::compile({}, ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb, true, 0.0f);
        const auto nonRawLinearPlan = ProcessingPlan::compile({}, ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb, false, 0.0f);
        QVERIFY(rawPlan.rawSource);
        QVERIFY(!nonRawLinearPlan.rawSource);
        QCOMPARE(rawPlan.data[ProcessingPlan::Flags].w, 1.0f);
        QCOMPARE(nonRawLinearPlan.data[ProcessingPlan::Flags].w, 0.0f);

        const QImage linear = sceneGrayImage(0.18);
        const auto rawRendered=ImagePipeline::processWithPlan(linear,rawPlan);
        const auto nonRawRendered=ImagePipeline::processWithPlan(linear,nonRawLinearPlan);
        const int rawV=rawRendered.pixelColor(0,0).red();
        const int nonRawV=nonRawRendered.pixelColor(0,0).red();
        QVERIFY2(rawV >= 200 && rawV <= 210,qPrintable(QString::number(rawV)));
        QVERIFY2(nonRawV >= 115 && nonRawV <= 122,qPrintable(QString::number(nonRawV)));
        QVERIFY(rawV > nonRawV + 70);
        QCOMPARE(rawRendered.text("JixelLightPipeline"),QString::fromLatin1(ProcessingPlan::EngineVersion));
        QVERIFY(rawRendered.text("JixelLightBaseRendering").contains("Jixel Neutral v2"));
        QVERIFY(!rawRendered.text("JixelLightBaseRendering").contains("+2.5 EV"));
        QCOMPARE(nonRawRendered.text("JixelLightBaseRendering"),QStringLiteral("none"));
    }

    void rawCameraBaselineIsSeparateFromUserExposure() {
        const QImage image=sceneGrayImage(ProcessingPlan::RawNeutralSceneGray);
        const auto basePlusOne=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb,true,1.0f);
        const auto cameraRendered=ImagePipeline::processWithPlan(image,basePlusOne);
        AdjustmentState userPlusOne; userPlusOne.exposure=1.0;
        const auto userPlan=ProcessingPlan::compile(userPlusOne,ImagePipeline::InputEncoding::LinearProPhoto,
            ColorManagement::OutputSpace::SRgb,true,0.0f);
        const auto userRendered=ImagePipeline::processWithPlan(image,userPlan);
        QCOMPARE(basePlusOne.state.exposure,0.0);
        QVERIFY(std::abs(cameraRendered.pixelColor(0,0).red()-userRendered.pixelColor(0,0).red())<=1);
        QVERIFY(cameraRendered.text("JixelLightBaseRendering").contains("1.000 EV"));
    }

    void rawBaseRenderingKeepsOutOfSrgbHighlightHueOrdering() {
        // This ProPhoto fixture is already outside sRGB before any creative
        // adjustment (its linear-sRGB red channel is negative). The correct
        // display result may therefore land on the sRGB boundary; protect hue
        // ordering and continuity rather than alpha.10's arbitrary red code.
        QImage image(1,1,QImage::Format_RGBA64);
        auto *px=reinterpret_cast<QRgba64 *>(image.scanLine(0));
        px[0]=QRgba64::fromRgba64(23559,59073,40796,65535);
        AdjustmentState state;state.hue=-23;state.saturation=18;state.vibrance=22;
        state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8;
        const QColor out=ImagePipeline::process(image,state,ImagePipeline::InputEncoding::LinearProPhoto).pixelColor(0,0);
        QVERIFY2(out.green()>240,qPrintable(QString("red=%1 green=%2 blue=%3").arg(out.red()).arg(out.green()).arg(out.blue())));
        QVERIFY(out.blue()>100);
        QVERIFY(out.green()>out.blue());
        QVERIFY(out.blue()>out.red());
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
        QVERIFY(!metadata.highlightBlendEnabled);
        QCOMPARE(metadata.highlightMode,1);
        QVERIFY(std::abs(metadata.adjustMaximumThreshold-0.75f)<1.0e-6f);
        QCOMPARE(image.text(QStringLiteral("JixelLightLibRawHighlightMode")),QStringLiteral("1 / unclip"));
        QCOMPARE(image.text(QStringLiteral("JixelLightAdjustMaximumThreshold")),QStringLiteral("0.75"));
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
        QVERIFY(controller.currentMetadata().contains(QStringLiteral("rawBaseExposureStops")));
        QCOMPARE(controller.currentMetadata().value(QStringLiteral("libRawHighlightMode")).toInt(),1);

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

    void projectCanOpenAnExistingCatalogWithEdits() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectDatabase created;
        QVERIFY(created.create(dir.path(), QStringLiteral("Resume")));
        AdjustmentState state;
        state.exposure = 1.25;
        state.highlights = -40;
        state.hslHue[2] = 8;
        const QString original = QDir(dir.path()).filePath(QStringLiteral("sample.ARW"));
        QVERIFY(created.addOrUpdatePhoto(original, state));
        QVERIFY(created.flush());
        ProjectDatabase::PhotoCuration picked {4, QStringLiteral("pick")};
        QVERIFY(created.updateCurationBatch({{original, picked}}));
        QVERIFY(created.flush());

        ProjectDatabase reopened;
        QVector<ProjectDatabase::SavedPhoto> loaded;
        QVERIFY2(reopened.open(QDir(dir.path()).filePath(QStringLiteral("Resume.jlp")), &loaded),
                 qPrintable(reopened.lastError()));
        QCOMPARE(reopened.projectName(), QStringLiteral("Resume"));
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().path, original);
        QCOMPARE(loaded.first().adjustments.exposure, 1.25);
        QCOMPARE(loaded.first().adjustments.highlights, -40.0);
        QCOMPARE(loaded.first().adjustments.hslHue[2], 8.0);
        QCOMPARE(loaded.first().rating, 4);
        QCOMPARE(loaded.first().flag, QStringLiteral("pick"));
        AdjustmentState changed = loaded.first().adjustments;
        changed.exposure = -0.75;
        QVERIFY(reopened.updateAdjustment(original, changed));
        QVERIFY(reopened.flush());
        QVERIFY2(reopened.open(QDir(dir.path()).filePath(QStringLiteral("Resume.jlp")), &loaded),
                 qPrintable(reopened.lastError()));
        QCOMPARE(loaded.first().adjustments.exposure, -0.75);
    }

    void legacyProjectCanReadWithoutCurationAndUpgradeOnFirstRating() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString folder = QDir(dir.path()).filePath(QStringLiteral("Legacy.jlp"));
        QVERIFY(QDir().mkpath(folder));
        const QString dbPath = QDir(folder).filePath(QStringLiteral("Project.db"));
        const QString conn = QStringLiteral("jixellight-legacy-fixture");
        const QString photoPath = QDir(folder).filePath(QStringLiteral("older.ARW"));
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            database.setDatabaseName(dbPath);
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT)"));
            QVERIFY(query.exec("INSERT INTO meta(key,value) VALUES('project_name','Legacy')"));
            QVERIFY(query.exec("CREATE TABLE photos(path TEXT PRIMARY KEY, imported_at TEXT DEFAULT CURRENT_TIMESTAMP, adjustment_json TEXT NOT NULL DEFAULT '{}')"));
            query.prepare("INSERT INTO photos(path, adjustment_json) VALUES(?, ?)");
            query.addBindValue(photoPath);
            AdjustmentState fromOld;
            fromOld.exposure = 0.75;
            query.addBindValue(QString::fromUtf8(QJsonDocument(fromOld.toJson()).toJson(QJsonDocument::Compact)));
            QVERIFY(query.exec());
            database.close();
        }
        QSqlDatabase::removeDatabase(conn);
        ProjectDatabase restored;
        QVector<ProjectDatabase::SavedPhoto> loaded;
        QVERIFY2(restored.open(folder, &loaded), qPrintable(restored.lastError()));
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].adjustments.exposure, 0.75);
        QCOMPARE(loaded[0].rating, 0);
        QCOMPARE(loaded[0].flag, QStringLiteral("none"));
        QVERIFY(restored.updateCurationBatch({{photoPath, {5, QStringLiteral("reject")}}}));
        QVERIFY(restored.flush());
        QVERIFY2(restored.open(folder, &loaded), qPrintable(restored.lastError()));
        QCOMPARE(loaded[0].rating, 5);
        QCOMPARE(loaded[0].flag, QStringLiteral("reject"));
        QCOMPARE(loaded[0].adjustments.exposure, 0.75);
    }

    void projectRejectsNonexistentDatabaseWithoutCreatingOne() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectDatabase db;
        QVector<ProjectDatabase::SavedPhoto> records;
        const QString invalid = QDir(dir.path()).filePath(QStringLiteral("Absent.jlp"));
        QVERIFY(!db.open(invalid, &records));
        QVERIFY(!QFileInfo::exists(QDir(invalid).filePath(QStringLiteral("Project.db"))));
        QVERIFY(records.isEmpty());
    }

    void failedProjectOpenMustNotPoisonCurrentProject() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectDatabase db;
        QVERIFY(db.create(dir.path(), QStringLiteral("Working")));
        AdjustmentState before;
        before.exposure = 0.3;
        const QString photo = QDir(dir.path()).filePath(QStringLiteral("existing.ARW"));
        QVERIFY(db.updateAdjustment(photo, before));
        QVERIFY(db.flush());

        QVector<ProjectDatabase::SavedPhoto> photos;
        const QString bad = QDir(dir.path()).filePath(QStringLiteral("DoesNotExist.jlp"));
        QVERIFY(!db.open(bad, &photos));
        QVERIFY(!db.lastError().isEmpty());
        // A bad destination must not switch projects or block shutdown saves.
        QCOMPARE(db.projectName(), QStringLiteral("Working"));
        QVERIFY(db.flush());
        AdjustmentState after = before;
        after.exposure = 1.3;
        QVERIFY(db.updateAdjustment(photo, after));
        QVERIFY(db.flush());
        QVERIFY2(db.open(QDir(dir.path()).filePath(QStringLiteral("Working.jlp")), &photos),
                 qPrintable(db.lastError()));
        QCOMPARE(photos.size(), 1);
        QCOMPARE(photos[0].adjustments.exposure, 1.3);
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
