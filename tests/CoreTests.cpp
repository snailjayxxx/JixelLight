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
#include <QStandardPaths>
#include <QScopeGuard>
#include <QUuid>
#include <QUrl>
#include <exiv2/exiv2.hpp>
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
#include "core/commands/NamedPresets.h"
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
        QVERIFY(reopened.canUndo());
        reopened.undo(); QCOMPARE(reopened.geometry()["quarterTurns"].toInt(),0);
        QVERIFY(reopened.canRedo());
        QVERIFY(reopened.flushEdits());
        PhotoController resumed(nullptr);
        QVERIFY(resumed.openProject(QUrl::fromLocalFile(controller.projectPath())));
        resumed.selectPhoto(1);
        QVERIFY(resumed.canRedo());
        resumed.redo(); QCOMPARE(resumed.geometry()["quarterTurns"].toInt(),1);
        QSignalSpy exported(&controller, &PhotoController::exportFinished);
        QVERIFY(controller.exportAll(QUrl::fromLocalFile(dir.path()), "display-p3",92,"png"));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(),1,10000);
        QCOMPARE(exported.first()[0].toInt(),2);
        QCOMPARE(QImage(dir.filePath("a_JixelLight.png")).depth(),64);
        QCOMPARE(QImage(dir.filePath("b_JixelLight.png")).depth(),64);
    }

    void historyPersistenceRejectsUnsupportedOrInconsistentSnapshots() {
        AdjustmentState original, edited; edited.exposure = 1;
        EditHistory history; history.initialize(original); history.record(edited, "exposure");
        history.undo();
        EditHistory restored;
        QVERIFY(restored.restore(history.toJson(), original));
        QVERIFY(restored.canRedo()); QCOMPARE(restored.redo().exposure, 1.0);
        auto invalid = history.toJson(); invalid.insert("schema", 2);
        QVERIFY(!restored.restore(invalid, original));
        QVERIFY(!restored.restore(history.toJson(), edited));
        invalid = history.toJson(); invalid.insert("cursor", 0.5);
        QVERIFY(!restored.restore(invalid, original));
        QCOMPARE(restored.cursor(), 1);
    }

    void persistentHistorySharesAndValidatesLuts() {
        AdjustmentState state; state.look.mode = "calibrated"; state.look.lut = LookLut::identity();
        EditHistory history; history.initialize(state);
        state.exposure = 1; history.record(state, "exposure");
        const auto json = history.toJson(); QCOMPARE(json.value("luts").toArray().size(), 1);
        EditHistory restored; QVERIFY(restored.restore(json, state));
        QCOMPARE(restored.entries()[0].state.look.lut, restored.entries()[1].state.look.lut);
        QCOMPARE(restored.undo().look.lut->digest, state.look.lut->digest);
        auto invalid = json; auto luts = json.value("luts").toArray();
        auto lut = luts[0].toObject(); lut.insert("sha256", "invalid"); luts[0] = lut; invalid.insert("luts", luts);
        QVERIFY(!restored.restore(invalid, state));
    }

    void persistentHistoryAndPresetsRejectDifferentSonyFitEngine() {
        auto lut = std::make_shared<LookLut>(*LookLut::identity());
        lut->evidence = {{"kind", "multi-scene-empirical-fit"}, {"engineVersion", "older-engine"}};
        AdjustmentState state; state.look.mode = "calibrated"; state.look.lut = lut;
        EditHistory history; history.initialize(state); EditHistory restored;
        QVERIFY(!restored.restore(history.toJson(), state));
        QTemporaryDir dir; QVERIFY(dir.isValid()); QString error;
        NamedPresets presets(dir.filePath("presets.json")); QVERIFY(presets.load(&error));
        QVERIFY(!presets.save("Incompatible", state, &error));
        QVERIFY(!QFileInfo::exists(dir.filePath("presets.json")));
        const auto json = QJsonDocument(QJsonObject{{"schema", 1}, {"presets", QJsonObject{{"Incompatible", state.toJson()}}}}).toJson();
        QFile file(dir.filePath("presets.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(json), json.size()); file.close();
        QVERIFY(!presets.load(&error));
        QVERIFY(!presets.remove("Incompatible", &error));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), json);
    }

    void namedPresetsRoundTripWithoutGeometryAndProtectUnreadableFiles() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = dir.filePath("presets.json"); QString error;
        NamedPresets presets(file); QVERIFY(presets.load(&error));
        AdjustmentState state; state.exposure = 1.25; state.look.mode = "as-shot";
        state.geometry.quarterTurns = 1;
        QVERIFY(presets.save("Portrait", state, &error));
        QVERIFY(!presets.save("Portrait", state, &error)); // replacement uses a separate explicit operation
        NamedPresets reopened(file); QVERIFY(reopened.load(&error));
        AdjustmentState loaded; QVERIFY(reopened.get("Portrait", &loaded));
        QCOMPARE(loaded.exposure, 1.25); QCOMPARE(loaded.look.mode, QStringLiteral("as-shot"));
        QCOMPARE(loaded.geometry.quarterTurns, 0);
        QVERIFY(reopened.remove("Portrait", &error));
        QVERIFY(!reopened.get("Portrait", &loaded));
        QFile corrupt(file); QVERIFY(corrupt.open(QIODevice::WriteOnly));
        const QByteArray unknown = R"({"schema":99,"presets":{}})";
        QCOMPARE(corrupt.write(unknown), unknown.size()); corrupt.close();
        QVERIFY(!reopened.load(&error)); QVERIFY(!reopened.save("New", state, &error));
        QVERIFY(corrupt.open(QIODevice::ReadOnly)); QCOMPARE(corrupt.readAll(), unknown);
    }

    void namedPresetManagementAndPortableFilesPreserveIntent() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QString error; NamedPresets presets(dir.filePath("presets.json")); QVERIFY(presets.load(&error));
        AdjustmentState state; state.exposure=.5; state.look.mode="as-shot"; state.geometry.quarterTurns=1;
        QVERIFY(presets.save("First",state,&error)); QVERIFY(presets.save("Other",state,&error));
        QVERIFY(!presets.rename("First","Other",&error)); QVERIFY(presets.rename("First","Renamed",&error));
        state.exposure=1.5; QVERIFY(presets.replace("Renamed",state,&error)); QVERIFY(!presets.replace("Missing",state,&error));
        AdjustmentState loaded; QVERIFY(presets.get("Renamed",&loaded)); QCOMPARE(loaded.exposure,1.5); QCOMPARE(loaded.geometry.quarterTurns,0);
        const auto path=dir.filePath("portable.jixelpreset.json"); QVERIFY(presets.exportFile("Renamed",path,&error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll(); file.close();
        QVERIFY(!presets.exportFile("Renamed",path,&error)); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes); file.close();
        NamedPresets imported(dir.filePath("imported.json")); QVERIFY(imported.load(&error)); QVERIFY(imported.importFile(path,{},&error));
        QVERIFY(imported.get("Renamed",&loaded)); QCOMPARE(loaded.exposure,1.5); QCOMPARE(loaded.look.mode,QStringLiteral("as-shot"));
        QVERIFY(loaded.look.code.isEmpty()); QVERIFY(!imported.importFile(path,{},&error)); QVERIFY(imported.importFile(path,"Imported copy",&error));
        NamedPresets reopened(dir.filePath("imported.json")); QVERIFY(reopened.load(&error)); QCOMPARE(reopened.names().size(),2);
        auto invalid=QJsonDocument::fromJson(bytes).object(); invalid.insert("engine","future-engine");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(invalid).toJson()); file.close();
        QVERIFY(!reopened.importFile(path,"Invalid",&error)); QCOMPARE(reopened.names().size(),2);
        QFile protectedFile(dir.filePath("imported.json")); QVERIFY(protectedFile.open(QIODevice::ReadOnly)); const auto before=protectedFile.readAll(); protectedFile.close();
        state.look.error="invalid"; QVERIFY(!reopened.replace("Renamed",state,&error));
        QVERIFY(protectedFile.open(QIODevice::ReadOnly)); QCOMPARE(protectedFile.readAll(),before);
    }

    void controllerPresetManageImportExportLeavesDevelopAndOriginalUntouched() {
        const auto oldName=QCoreApplication::applicationName(); QCoreApplication::setApplicationName("JixelLightPresetManage-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto data=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto restore=qScopeGuard([&] { QDir(data).removeRecursively(); QCoreApplication::setApplicationName(oldName); });
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path=dir.filePath("source.png"); QVERIFY(image.save(path)); QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly)); const auto bytes=source.readAll(); source.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(path))); controller.setExposure(.5); controller.finishInteraction();
        QVERIFY(controller.saveNamedPreset("First")); QVERIFY(controller.renameNamedPreset("First","Renamed"));
        controller.setExposure(1.5); controller.finishInteraction(); controller.rotatePhoto(1); controller.setRating(4);
        const auto steps=controller.editHistory().size(); QVERIFY(controller.replaceNamedPreset("Renamed"));
        QCOMPARE(controller.editHistory().size(),steps); QCOMPARE(controller.exposure(),1.5); QCOMPARE(controller.currentRating(),4);
        const auto exported=QUrl::fromLocalFile(dir.filePath("preset.jixelpreset.json")); QVERIFY(controller.exportNamedPreset("Renamed",exported));
        QVERIFY(!controller.exportNamedPreset("Renamed",QUrl::fromLocalFile(path))); QVERIFY(!controller.exportNamedPreset("Renamed",exported));
        QVERIFY(controller.importNamedPreset(exported,"Imported")); QCOMPARE(controller.editHistory().size(),steps);
        controller.setExposure(-.5); controller.finishInteraction(); QVERIFY(controller.applyNamedPreset("Imported"));
        QCOMPARE(controller.exposure(),1.5); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),1); QCOMPARE(controller.currentRating(),4);
        controller.undo(); QCOMPARE(controller.exposure(),-.5); QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),bytes);
    }

    void controllerNamedPresetPreservesGeometryCurationAndUndo() {
        const auto oldName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("JixelLightPresetTest-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto restoreName = qScopeGuard([&] { QDir(data).removeRecursively(); QCoreApplication::setApplicationName(oldName); });
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("photo.png"); QVERIFY(image.save(path));
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        controller.setExposure(1.5); controller.finishInteraction();
        QVERIFY(controller.saveNamedPreset("Portrait"));
        controller.setExposure(-1); controller.finishInteraction(); controller.rotatePhoto(1);
        controller.setRating(4); controller.setFlag("pick");
        QVERIFY(controller.applyNamedPreset("Portrait")); QCOMPARE(controller.exposure(), 1.5);
        QCOMPARE(controller.currentRating(), 4); QCOMPARE(controller.currentFlag(), QStringLiteral("pick"));
        QCOMPARE(controller.geometry()["quarterTurns"].toInt(), 1);
        controller.undo(); QCOMPARE(controller.exposure(), -1.0);
        QCOMPARE(controller.geometry()["quarterTurns"].toInt(), 1);
        PhotoController reopened(nullptr); QCOMPARE(reopened.presetNames(), QStringList{"Portrait"});
        QVERIFY(reopened.removeNamedPreset("Portrait")); QVERIFY(reopened.presetNames().isEmpty());
    }

    void projectHistoryRejectsUnknownSchemaWithoutSwitchingWriter() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ProjectDatabase candidate; QVERIFY(candidate.create(dir.path(), "Future"));
        AdjustmentState state; EditHistory history; history.initialize(state);
        QVERIFY(candidate.updateBatch({{"test.ARW", state}}, {{"test.ARW", history}}));
        QVERIFY(candidate.flush());
        const QString connection = "future-history-fixture";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(QDir(candidate.projectPath()).filePath("Project.db")); QVERIFY(db.open());
            QSqlQuery query(db); auto json = state.toJson(); auto invalid = history.toJson();
            invalid.insert("schema", 99); json.insert("_history", invalid);
            query.prepare("UPDATE photos SET adjustment_json=?");
            query.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
            QVERIFY(query.exec()); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        ProjectDatabase active; QVERIFY(active.create(dir.path(), "Active"));
        QVector<ProjectDatabase::SavedPhoto> photos;
        QVERIFY(!active.open(candidate.projectPath(), &photos));
        QCOMPARE(active.projectName(), QStringLiteral("Active"));
        QVERIFY(active.updateAdjustment("active.ARW", state)); QVERIFY(active.flush());
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

    void projectCreationNeverOverwritesOrDropsCurrentWriter() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ProjectDatabase db; QVERIFY(db.create(dir.path(), "Existing"));
        AdjustmentState state; state.exposure = 0.75;
        QVERIFY(db.updateAdjustment("original.ARW", state)); QVERIFY(db.flush());
        const auto folder = db.projectPath();
        QVERIFY(!db.create(dir.path(), "Existing")); QCOMPARE(db.projectPath(), folder);
        QVERIFY(db.flush());
        QFile blocker(dir.filePath("file")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QVERIFY(!db.create(blocker.fileName(), "Unavailable")); QCOMPARE(db.projectPath(), folder);
        state.exposure = 1.75; QVERIFY(db.updateAdjustment("original.ARW", state)); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(folder, &photos));
        QCOMPARE(photos.size(), 1); QCOMPARE(photos[0].adjustments.exposure, 1.75);
    }

    void catalogBatchAnnotationsAndSelectionSurviveReopenWithoutDevelopChanges() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        PhotoController controller(nullptr);
        for (int i = 0; i < 3; ++i) { const auto path = dir.filePath(QString::number(i)+".png"); QVERIFY(image.save(path)); QVERIFY(controller.importFile(QUrl::fromLocalFile(path))); }
        controller.setExposure(1.25); controller.finishInteraction();
        QVERIFY(controller.setPhotoSelection({0,2,2})); QCOMPARE(controller.selectedIndices().size(),2);
        QVERIFY(!controller.setPhotoSelection({0,3})); QCOMPARE(controller.selectedIndices().size(),2);
        QVERIFY(!controller.setPhotoSelection({0,1.5}));
        QVERIFY(controller.setSelectionKeywords("travel, Sony, travel"));
        QVERIFY(controller.addSelectionToAlbum(" Japan "));
        QVERIFY(controller.setSelectionLabel("blue"));
        QVERIFY(controller.setSelectionRating(4)); QVERIFY(controller.setSelectionFlag("pick"));
        QVERIFY(!controller.setSelectionLabel("unknown"));
        QVERIFY(!controller.setSelectionKeywords(QString(81,'a')));
        QCOMPARE(controller.exposure(),1.25); QVERIFY(controller.canUndo());
        const auto untouched = controller.library()[1].toMap();
        QCOMPARE(untouched["rating"].toInt(),0); QVERIFY(untouched["keywords"].toStringList().isEmpty());
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Tags")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.albumNames(),QStringList{"Japan"});
        reopened.selectPhoto(2); QCOMPARE(reopened.currentKeywords(),(QStringList{"Sony","travel"}));
        QCOMPARE(reopened.currentColorLabel(),QStringLiteral("blue")); QCOMPARE(reopened.currentRating(),4);
        QCOMPARE(reopened.exposure(),0.0); // original edit belongs to photo 0
        QVERIFY(reopened.setPhotoSelection({0,2})); QVERIFY(reopened.removeSelectionFromAlbum("Japan"));
        QVERIFY(reopened.albumNames().isEmpty()); QVERIFY(reopened.flushEdits());
        QVERIFY(reopened.setPhotoSelection({})); QVERIFY(!reopened.setSelectionRating(3));
    }

    void legacyCatalogTagsMigrationHasWalConsistentBackupAndRecoversFromFailure() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ProjectDatabase db; QVERIFY(db.create(dir.path(),"Migration"));
        AdjustmentState state; state.exposure = 1.5;
        QVERIFY(db.updateAdjustment("source.ARW",state)); QVERIFY(db.flush());
        const auto folder = db.projectPath(); const QString connection = "tag-migration-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); QVERIFY(query.exec("DROP TABLE catalog_tags")); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(QDir(QDir(folder).filePath("backups")).removeRecursively());
        QFile blocked(QDir(folder).filePath("backups")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        CatalogTags tags; tags.keywords = {"Sony"}; tags.albums = {"Japan"};
        QVERIFY(db.updateTagsBatch({{"source.ARW",tags}})); QVERIFY(!db.flush());
        QVERIFY(blocked.remove());
        QVERIFY(db.updateTagsBatch({{"source.ARW",tags}})); QVERIFY2(db.flush(),qPrintable(db.lastError()));
        const auto backups = QDir(QDir(folder).filePath("backups")).entryList({"*.db"},QDir::Files);
        QCOMPARE(backups.size(),1);
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("backups/"+backups[0]));
            sql.setConnectOptions("QSQLITE_OPEN_READONLY"); QVERIFY(sql.open()); QSqlQuery query(sql);
            QVERIFY(query.exec("SELECT adjustment_json FROM photos")); QVERIFY(query.next());
            QCOMPARE(AdjustmentState::fromJson(QJsonDocument::fromJson(query.value(0).toByteArray()).object()).exposure,1.5);
            QVERIFY(query.exec("SELECT name FROM sqlite_master WHERE name='catalog_tags'")); QVERIFY(!query.next()); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(folder,&photos));
        QCOMPARE(photos[0].tags.keywords,QStringList{"Sony"}); QCOMPARE(photos[0].adjustments.exposure,1.5);
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); auto invalid = tags.toJson(); invalid.insert("schema",99);
            query.prepare("UPDATE catalog_tags SET json=?"); query.addBindValue(QString::fromUtf8(QJsonDocument(invalid).toJson())); QVERIFY(query.exec()); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        ProjectDatabase active; QVERIFY(active.create(dir.path(),"Active"));
        QVERIFY(!active.open(folder,&photos)); QCOMPARE(active.projectName(),QStringLiteral("Active")); QVERIFY(active.flush());
    }

    void virtualCopiesHaveIndependentStateHistoryCatalogAndExports() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto originalBytes = file.readAll(); file.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        controller.setExposure(1); controller.finishInteraction();
        QVERIFY(controller.createVirtualCopy("Warm")); QCOMPARE(controller.library().size(),2);
        QCOMPARE(controller.currentFile(),path); QCOMPARE(controller.exposure(),1.0); QVERIFY(!controller.canUndo());
        controller.setExposure(-1); controller.finishInteraction(); controller.rotatePhoto(1);
        controller.setRating(5); controller.setFlag("reject");
        QVERIFY(controller.setSelectionKeywords("warm")); QVERIFY(controller.addSelectionToAlbum("Versions"));
        controller.selectPhoto(0); QCOMPARE(controller.exposure(),1.0); QCOMPARE(controller.currentRating(),0);
        QCOMPARE(controller.geometry()["quarterTurns"].toInt(),0); QVERIFY(controller.currentKeywords().isEmpty());
        controller.selectPhoto(1); QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Copies")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY2(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())),qPrintable(controller.projectPath()));
        QCOMPARE(reopened.library().size(),2); reopened.selectPhoto(1);
        QVERIFY(reopened.library()[1].toMap()["virtual"].toBool()); QCOMPARE(reopened.currentFile(),path);
        QCOMPARE(reopened.exposure(),-1.0); QCOMPARE(reopened.currentRating(),5); QCOMPARE(reopened.currentFlag(),QStringLiteral("reject"));
        QCOMPARE(reopened.currentKeywords(),QStringList{"warm"}); QVERIFY(reopened.canUndo());
        reopened.undo(); QCOMPARE(reopened.geometry()["quarterTurns"].toInt(),0);
        QVERIFY(reopened.flushEdits());
        QVERIFY(reopened.createVirtualCopy("Cool")); QCOMPARE(reopened.library().size(),3);
        reopened.setExposure(-2); reopened.finishInteraction(); QVERIFY(reopened.flushEdits());
        PhotoController final(nullptr); QVERIFY(final.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(final.library().size(),3);
        int warm = -1, cool = -1;
        for (int i = 0; i < final.library().size(); ++i) {
            const auto row = final.library()[i].toMap();
            if (row["versionName"].toString() == "Warm") warm = i;
            if (row["versionName"].toString() == "Cool") cool = i;
        }
        QVERIFY(warm >= 0 && cool >= 0); final.selectPhoto(warm); QCOMPARE(final.exposure(),-1.0); QVERIFY(final.canRedo());
        final.selectPhoto(cool); QCOMPARE(final.exposure(),-2.0); final.selectPhoto(0); QCOMPARE(final.exposure(),1.0);
        QSignalSpy exported(&final,&PhotoController::exportFinished);
        QVERIFY(final.exportAll(QUrl::fromLocalFile(dir.path()),"srgb",92,"png")); QTRY_COMPARE_WITH_TIMEOUT(exported.size(),1,10000);
        QCOMPARE(exported.first()[0].toInt(),3); QCOMPARE(exported.first()[1].toInt(),0);
        const auto outputs = QDir(dir.path()).entryList({"source_JixelLight*.png"},QDir::Files); QCOMPARE(outputs.size(),3);
        QVERIFY(QImage(dir.filePath(outputs[0])) != QImage(dir.filePath(outputs[1])));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),originalBytes);
    }

    void virtualCopyCreationRollsBackAndUnknownMappingPreservesActiveWriter() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"Copies"));
        AdjustmentState state; EditHistory history; history.initialize(state); CatalogTags tags;
        const auto key = "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(db.addVirtualCopy(key,"source.ARW","Version",state,history,tags,{}));
        QVERIFY(!db.addVirtualCopy(key,"different.ARW","Changed",state,history,tags,{})); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(db.projectPath(),&photos));
        QCOMPARE(photos.size(),1); QCOMPARE(photos[0].path,QStringLiteral("source.ARW")); QCOMPARE(photos[0].copyKey,key);
        const QString connection = "future-copy-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(db.projectPath()).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); QVERIFY(query.exec("UPDATE virtual_sources SET json='{\"schema\":99,\"source\":\"source.ARW\",\"name\":\"Version\"}'")); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        ProjectDatabase active; QVERIFY(active.create(dir.path(),"Active")); QVERIFY(!active.open(db.projectPath(),&photos));
        QCOMPARE(active.projectName(),QStringLiteral("Active")); QVERIFY(active.updateAdjustment("original.ARW",state)); QVERIFY(active.flush());
    }

    void relativeCatalogPathsKeepTheirDatabaseKeyWhenEditedOrCopied() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"Relative"));
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto source = QDir(db.projectPath()).filePath("source.png"); QVERIFY(image.save(source));
        AdjustmentState state; QVERIFY(db.updateAdjustment("source.png",state)); QVERIFY(db.flush());
        PhotoController controller(nullptr); QVERIFY(controller.openProject(QUrl::fromLocalFile(db.projectPath())));
        controller.setExposure(1); controller.finishInteraction(); QVERIFY(controller.flushEdits());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(db.projectPath(),&photos));
        QCOMPARE(photos.size(),1); QCOMPARE(photos[0].path,QStringLiteral("source.png")); QCOMPARE(photos[0].adjustments.exposure,1.0);
        QVERIFY(controller.createVirtualCopy("Relative copy"));
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Copied")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.library().size(),2); QCOMPARE(reopened.currentFile(),source);
        QCOMPARE(reopened.exposure(),1.0);
    }

    void legacyVirtualCopyMigrationBacksUpBeforeAtomicInsert() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"LegacyCopy"));
        AdjustmentState state; state.exposure = 0.75; EditHistory history; history.initialize(state);
        QVERIFY(db.updateAdjustment("source.ARW",state)); QVERIFY(db.flush());
        const auto folder = db.projectPath(); const QString connection = "virtual-migration-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); QVERIFY(query.exec("DROP TABLE virtual_sources")); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(QDir(QDir(folder).filePath("backups")).removeRecursively());
        QFile blocked(QDir(folder).filePath("backups")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        const auto key = "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(!db.addVirtualCopy(key,"source.ARW","Version",state,history,{},{})); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(folder,&photos)); QCOMPARE(photos.size(),1);
        QVERIFY(blocked.remove()); QVERIFY(db.addVirtualCopy(key,"source.ARW","Version",state,history,{},{}));
        QVERIFY(db.open(folder,&photos)); QCOMPARE(photos.size(),2);
        const auto backups = QDir(QDir(folder).filePath("backups")).entryList({"*.db"},QDir::Files); QCOMPARE(backups.size(),1);
        ProjectDatabase backup; QVector<ProjectDatabase::SavedPhoto> original;
        const auto backupFolder = dir.filePath("Backup.jlp"); QVERIFY(QDir().mkpath(backupFolder));
        QVERIFY(QFile::copy(QDir(folder).filePath("backups/"+backups[0]),QDir(backupFolder).filePath("Project.db")));
        QVERIFY(backup.open(backupFolder,&original)); QCOMPARE(original.size(),1); QCOMPARE(original[0].adjustments.exposure,0.75);
    }

    void virtualCopyRenameRemovalPreservesOriginalSelectionAndHistory() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path));
        QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly)); const auto bytes = source.readAll(); source.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        controller.setExposure(.5); controller.finishInteraction();
        QVERIFY(!controller.renameCurrentVirtualCopy("Original")); QVERIFY(!controller.removeCurrentVirtualCopy());
        QVERIFY(controller.createVirtualCopy("First")); controller.setExposure(-1); controller.finishInteraction(); controller.undo();
        QVERIFY(controller.createVirtualCopy("Second")); controller.setExposure(2); controller.finishInteraction();
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"ManageCopies")); QVERIFY(controller.flushEdits());
        controller.selectPhoto(1); const auto key = controller.library()[1].toMap().value("id").toString();
        QVERIFY(controller.canRedo()); QVERIFY(controller.renameCurrentVirtualCopy(" Renamed "));
        QVERIFY(controller.canRedo()); QCOMPARE(controller.library()[1].toMap().value("versionName").toString(),QStringLiteral("Renamed"));
        QVERIFY(!controller.renameCurrentVirtualCopy("")); QVERIFY(!controller.renameCurrentVirtualCopy(QString(81,'x')));
        QVERIFY(controller.setPhotoSelection({0,1,2})); QVERIFY(controller.removeCurrentVirtualCopy());
        QCOMPARE(controller.library().size(),2); QCOMPARE(controller.selectedIndices(),(QVariantList{0,1}));
        QCOMPARE(controller.exposure(),2.0); QVERIFY(controller.canUndo());
        controller.selectPhoto(0); QCOMPARE(controller.exposure(),.5); QVERIFY(controller.canUndo());
        QVERIFY(controller.flushEdits()); QCoreApplication::processEvents(); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.library().size(),2); QCOMPARE(reopened.exposure(),.5);
        reopened.selectPhoto(1); QCOMPARE(reopened.library()[1].toMap().value("versionName").toString(),QStringLiteral("Second"));
        QCOMPARE(reopened.exposure(),2.0); QVERIFY(reopened.canUndo());
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),bytes);
        for (const auto &row : reopened.library()) QVERIFY(row.toMap().value("id").toString() != key);
    }

    void virtualCopyDeleteRollsBackAllTablesOnFailureAndCanRetry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"CopyRollback"));
        AdjustmentState state; state.exposure = .75; EditHistory history; history.initialize(state);
        CatalogTags tags; tags.keywords = {"keep"}; const auto key = "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(db.updateAdjustment("source.ARW",state)); QVERIFY(db.flush());
        QVERIFY(db.addVirtualCopy(key,"source.ARW","Version",state,history,tags,{4,"pick"}));
        QVERIFY(!db.removeVirtualCopy("source.ARW")); QVERIFY(!db.renameVirtualCopy("source.ARW","Changed"));
        const QString connection = "copy-delete-fixture";
        auto trigger = [&](bool enable) {
            { auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(db.projectPath()).filePath("Project.db")); QVERIFY(sql.open());
              QSqlQuery query(sql); QVERIFY(query.exec(enable ? "CREATE TRIGGER block_copy_delete BEFORE DELETE ON curation BEGIN SELECT RAISE(ABORT,'blocked'); END" : "DROP TRIGGER block_copy_delete")); sql.close(); }
            QSqlDatabase::removeDatabase(connection);
        };
        trigger(true); QVERIFY(!db.removeVirtualCopy(key)); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(db.projectPath(),&photos)); QCOMPARE(photos.size(),2);
        QCOMPARE(photos[1].copyKey,key); QCOMPARE(photos[1].tags.keywords,QStringList{"keep"}); QCOMPARE(photos[1].rating,4);
        QCOMPARE(photos[1].adjustments.exposure,.75); trigger(false);
        QVERIFY(db.renameVirtualCopy(key,"Retained")); QVERIFY(db.open(db.projectPath(),&photos)); QCOMPARE(photos[1].versionName,QStringLiteral("Retained"));
        QVERIFY(db.removeVirtualCopy(key)); QVERIFY(!db.removeVirtualCopy(key)); QVERIFY(db.flush());
        QVERIFY(db.open(db.projectPath(),&photos)); QCOMPARE(photos.size(),1); QCOMPARE(photos[0].path,QStringLiteral("source.ARW"));
    }

    void deletingLastCatalogCopyClearsCanvasWithoutTouchingSource() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path));
        ProjectDatabase db; QVERIFY(db.create(dir.path(),"OnlyCopy")); AdjustmentState state; EditHistory history; history.initialize(state);
        const auto key = "jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(db.addVirtualCopy(key,path,"Only",state,history,{},{}));
        PhotoController controller(nullptr); QVERIFY(controller.openProject(QUrl::fromLocalFile(db.projectPath())));
        QVERIFY(controller.hasImage()); controller.setViewport(64,64,1,0,.5,.5);
        QVERIFY(controller.removeCurrentVirtualCopy()); QCOMPARE(controller.library().size(),0);
        QCOMPARE(controller.currentIndex(),-1); QVERIFY(!controller.hasImage()); QVERIFY(!controller.loading()); QVERIFY(!controller.rendering());
        QCOMPARE(controller.scopesPixelCount(),0); QVERIFY(!controller.canUndo()); QVERIFY(QFileInfo::exists(path));
        QTest::qWait(50); QVERIFY(controller.previewUrl().isEmpty()); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
    }

    void catalogTimelineRejectsInvalidDatesWithoutGuessingCaptureTime() {
        PhotoTimeline timeline; timeline.captureTime=PhotoTimeline::cameraTime("2025:02:28 12:34:56"); timeline.captureChecked=true;
        timeline.importedAt=1740000000000LL; timeline.editedAt=1740000000123LL;
        PhotoTimeline restored; QVERIFY(PhotoTimeline::fromJson(timeline.toJson(),&restored)); QCOMPARE(restored.toJson(),timeline.toJson());
        QCOMPARE(restored.captureTime,QStringLiteral("2025-02-28 12:34:56")); QVERIFY(restored.captureOrder()>0);
        QVERIFY(PhotoTimeline::cameraTime("2025:02:29 12:34:56").isEmpty()); QVERIFY(PhotoTimeline::cameraTime("2025:02:28 25:34:56").isEmpty());
        QVERIFY(PhotoTimeline::cameraTime("invalid").isEmpty()); QCOMPARE(PhotoTimeline{}.captureOrder(),0);
        for (const auto &change : QVector<QJsonObject>{{{"schema",99}},{{"capture","x"}},{{"imported",-.5}},{{"edited",1.5}},{{"captureChecked",false}},{{"unknown",true}}}) {
            auto invalid=timeline.toJson(); for (auto it=change.begin();it!=change.end();++it) invalid.insert(it.key(),it.value());
            QVERIFY(!PhotoTimeline::fromJson(invalid,&restored)); QCOMPARE(restored.toJson(),timeline.toJson());
        }
    }

    void captureIndexAndPerVersionDevelopDatesPersistAcrossProjects() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto jpeg=dir.filePath("dated.jpg"), png=dir.filePath("undated.png"); QVERIFY(image.save(jpeg)); QVERIFY(image.save(png));
        { auto photo=Exiv2::ImageFactory::open(jpeg.toStdString()); QVERIFY(photo.get()); photo->readMetadata();
          auto exif=photo->exifData(); exif["Exif.Photo.DateTimeOriginal"]="2025:01:02 03:04:05"; photo->setExifData(exif); photo->writeMetadata(); }
        QFile source(jpeg); QVERIFY(source.open(QIODevice::ReadOnly)); const auto bytes=source.readAll(); source.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(jpeg))); QVERIFY(controller.importFile(QUrl::fromLocalFile(png)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.library()[0].toMap().value("captureChecked").toBool() && controller.library()[1].toMap().value("captureChecked").toBool(),10000);
        QCOMPARE(controller.library()[0].toMap().value("captureTime").toString(),QStringLiteral("2025-01-02 03:04:05"));
        QCOMPARE(controller.library()[1].toMap().value("captureOrder").toLongLong(),0);
        const auto imported=controller.library()[0].toMap().value("importedAt").toLongLong(); QVERIFY(imported>0);
        controller.setExposure(.5); controller.finishInteraction(); const auto edited=controller.library()[0].toMap().value("editedAt").toLongLong(); QVERIFY(edited>=imported);
        QVERIFY(controller.createVirtualCopy("Dated copy")); QCOMPARE(controller.library()[2].toMap().value("editedAt").toLongLong(),0);
        controller.setExposure(-.5); controller.finishInteraction(); QVERIFY(controller.library()[2].toMap().value("editedAt").toLongLong()>0);
        QCOMPARE(controller.library()[0].toMap().value("editedAt").toLongLong(),edited);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Dates")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.library()[0].toMap().value("captureTime").toString(),QStringLiteral("2025-01-02 03:04:05"));
        QCOMPARE(reopened.library()[0].toMap().value("importedAt").toLongLong(),imported); QCOMPARE(reopened.library()[0].toMap().value("editedAt").toLongLong(),edited);
        QVERIFY(reopened.createProject(QUrl::fromLocalFile(dir.path()),"CopiedDates")); QVERIFY(reopened.flushEdits());
        PhotoController final(nullptr); QVERIFY(final.openProject(QUrl::fromLocalFile(reopened.projectPath())));
        QCOMPARE(final.library()[0].toMap().value("importedAt").toLongLong(),imported);
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),bytes);
    }

    void legacyDateMigrationBacksUpAndRollsBackEditsBeforeRetry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"DateMigration"));
        AdjustmentState original; original.exposure=.5; QVERIFY(db.updateAdjustment("source.ARW",original)); QVERIFY(db.flush());
        const auto folder=db.projectPath(); const QString connection="date-migration-fixture";
        { auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
          QSqlQuery query(sql); QVERIFY(query.exec("DROP TABLE catalog_dates")); QVERIFY(query.exec("UPDATE photos SET imported_at='2020-01-02 03:04:05'")); sql.close(); }
        QSqlDatabase::removeDatabase(connection);
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(folder,&photos)); QCOMPARE(photos[0].timeline.importedAt,PhotoTimeline::sqlImportTime("2020-01-02 03:04:05"));
        QVERIFY(QDir(QDir(folder).filePath("backups")).removeRecursively()); QFile blocked(QDir(folder).filePath("backups")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        auto edited=original; edited.exposure=1; auto dates=photos[0].timeline; dates.editedAt=1740000000123LL;
        QVERIFY(db.updateBatch({{"source.ARW",edited}},{},{{"source.ARW",dates}})); QVERIFY(!db.flush()); QVERIFY(blocked.remove());
        QVERIFY(db.updateBatch({{"source.ARW",edited}},{},{{"source.ARW",dates}})); QVERIFY(db.flush());
        const auto backups=QDir(QDir(folder).filePath("backups")).entryList({"*.db"},QDir::Files); QCOMPARE(backups.size(),1);
        { auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("backups/"+backups[0])); QVERIFY(sql.open());
          QSqlQuery query(sql); QVERIFY(query.exec("SELECT adjustment_json FROM photos")); QVERIFY(query.next()); QCOMPARE(AdjustmentState::fromJson(QJsonDocument::fromJson(query.value(0).toByteArray()).object()).exposure,.5); sql.close(); }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(db.open(folder,&photos)); QCOMPARE(photos[0].adjustments.exposure,1.0); QCOMPARE(photos[0].timeline.editedAt,dates.editedAt);
        { auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
          QSqlQuery query(sql); QVERIFY(query.exec("UPDATE catalog_dates SET json='{\"schema\":99}'")); sql.close(); }
        QSqlDatabase::removeDatabase(connection);
        ProjectDatabase active; QVERIFY(active.create(dir.path(),"WorkingDates")); QVERIFY(!active.open(folder,&photos)); QCOMPARE(active.projectName(),QStringLiteral("WorkingDates")); QVERIFY(active.flush());
    }

    void realSonyAsShotHistoryPersistsIntentAfterMetadataResolution() {
        const auto folder = qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if (folder.isEmpty()) QSKIP("Sony real fixtures are not configured");
        QString source;
        for (const auto &name : QDir(folder).entryList({"*.arw","*.ARW"},QDir::Files)) {
            const auto path = QDir(folder).filePath(name);
            if (MetadataReader::read(path).value("sonyLook").toMap().value("autoEligible").toBool()) { source = path; break; }
        }
        QVERIFY2(!source.isEmpty(),"No verified Sony As Shot fixture found");
        QTemporaryDir dir; QVERIFY(dir.isValid()); PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(source)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentMetadata().value("sonyLook").toMap().value("autoEligible").toBool(),60000);
        controller.setViewport(320,240,1,0,.5,.5);
        controller.setExposure(.5); controller.finishInteraction();
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"AsShot")); QVERIFY(controller.flushEdits());
        controller.setExposure(1); controller.finishInteraction(); QVERIFY(controller.flushEdits());
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY2(db.open(controller.projectPath(),&photos),qPrintable(db.lastError()));
        QCOMPARE(photos.size(),1); QCOMPARE(photos[0].adjustments.look.mode,QStringLiteral("as-shot"));
        QVERIFY(photos[0].adjustments.look.code.isEmpty()); QVERIFY(photos[0].adjustments.look.parameters.isEmpty());
        QVERIFY(photos[0].history.canUndo()); QCOMPARE(photos[0].history.undo().exposure,.5);
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        reopened.undo(); QCOMPARE(reopened.exposure(),.5); QVERIFY(reopened.canRedo()); QVERIFY(reopened.flushEdits());
    }

    void failedAnnotationWritesRetainHistoryAndTagsForRetry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path)); PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path))); controller.setExposure(.5); controller.finishInteraction();
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Retry")); QVERIFY(controller.flushEdits());
        const auto folder = controller.projectPath(); const QString connection = "controller-retry-fixture";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(QDir(folder).filePath("Project.db")); QVERIFY(sql.open());
            QSqlQuery query(sql); QVERIFY(query.exec("DROP TABLE catalog_tags")); sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(QDir(QDir(folder).filePath("backups")).removeRecursively());
        QFile blocked(QDir(folder).filePath("backups")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        QVERIFY(controller.setSelectionKeywords("retained"));
        // A later successful curation batch must not mask a failed tags batch.
        QVERIFY(controller.setSelectionRating(4)); QVERIFY(!controller.flushEdits());
        QCoreApplication::processEvents(); // Deliver writer failure and refill all dirty snapshots.
        QVERIFY(blocked.remove()); QVERIFY(controller.flushEdits());
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY2(db.open(folder,&photos),qPrintable(db.lastError()));
        QCOMPARE(photos[0].tags.keywords,QStringList{"retained"}); QVERIFY(photos[0].history.canUndo());
        QCOMPARE(photos[0].history.undo().exposure,0.0);
    }

    void olderResolvedAsShotCatalogHistoryRecoversOnlyMetadataDifferences() {
        AdjustmentState intent; intent.look.mode = "as-shot"; intent.exposure = .5;
        EditHistory history; history.initialize(intent);
        auto rendered = intent; rendered.look.code = "ST"; rendered.look.parameters.insert("clarity",1);
        EditHistory restored; QVERIFY(restored.restore(history.toJson(),rendered));
        rendered.exposure = 1; QVERIFY(!restored.restore(history.toJson(),rendered));
        rendered.exposure = .5; rendered.look.strength = .5; QVERIFY(!restored.restore(history.toJson(),rendered));
        rendered.look.strength = 1;
        QTemporaryDir dir; QVERIFY(dir.isValid()); ProjectDatabase db; QVERIFY(db.create(dir.path(),"AsShotDraft"));
        QVERIFY(db.updateBatch({{"source.ARW",rendered}},{{"source.ARW",history}})); QVERIFY(db.flush());
        QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(db.projectPath(),&photos));
        QVERIFY(photos[0].adjustments.look.code.isEmpty()); QVERIFY(photos[0].adjustments.look.parameters.isEmpty());
        QCOMPARE(photos[0].adjustments.exposure,.5);
    }

    void sharedGeometryHslCurveCommandsMatchGuiAndRejectPartialMutations() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path=dir.filePath("source.png"); QVERIFY(image.save(path)); PhotoController controller(nullptr);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path))); AdjustmentState state;
        const QVector<QJsonObject> commands{
            {{"command","develop.set"},{"parameter","exposure"},{"value",.5}},
            {{"command","geometry.crop"},{"x",0.0},{"y",.25},{"width",.5},{"height",.5}},
            {{"command","geometry.rotate"},{"quarterTurns",-1}},
            {{"command","geometry.flip"},{"axis","vertical"}},
            {{"command","hsl.set"},{"band",2},{"component","saturation"},{"value",25}},
            {{"command","curve.set"},{"channel","red"},{"point",2},{"value",.6}}
        };
        for (const auto &command : commands) QVERIFY(CommandRegistry::execute(state,command));
        controller.setExposure(.5); controller.finishInteraction(); controller.setCrop(0,.25,.5,.5);
        controller.rotatePhoto(-1); controller.flipPhoto(false); controller.setColorMix(2,1,25); controller.setCurvePoint(1,2,.6);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Commands")); QVERIFY(controller.flushEdits());
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(db.open(controller.projectPath(),&photos));
        QCOMPARE(photos[0].adjustments.toJson(),state.toJson());
        const auto before=state.toJson();
        const QVector<QJsonObject> invalid{
            {{"command","geometry.crop"},{"x",.8},{"y",0.0},{"width",.5},{"height",1.0}},
            {{"command","geometry.rotate"},{"quarterTurns",.5}},
            {{"command","geometry.flip"},{"axis","diagonal"}},
            {{"command","hsl.set"},{"band",8},{"component","hue"},{"value",1}},
            {{"command","curve.set"},{"channel","red"},{"point",2},{"value","invalid"}},
            {{"command","develop.reset"},{"unexpected",true}}
        };
        for (const auto &command : invalid) { QVERIFY(!CommandRegistry::execute(state,command)); QCOMPARE(state.toJson(),before); }
        QVERIFY(controller.executeEditCommand({{"command","geometry.reset"}})); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),0);
        controller.undo(); QCOMPARE(controller.geometry()["quarterTurns"].toInt(),3);
        QVERIFY(!controller.executeEditCommand({{"command","curve.set"},{"channel","red"},{"point",5},{"value",.5}}));
        QVERIFY(CommandRegistry::execute(state,{{"command","curve.reset"},{"channel","red"}})); QCOMPARE(state.redCurve[2],.5);
        QVERIFY(CommandRegistry::execute(state,{{"command","develop.reset"}})); QCOMPARE(state.toJson(),AdjustmentState{}.toJson());
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
