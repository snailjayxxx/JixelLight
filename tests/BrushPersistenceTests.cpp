#include <QtTest>
#include <QColorSpace>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QImageWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QRgba64>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <jpeglib.h>
#include "app/PhotoController.h"
#include "core/commands/CommandRegistry.h"
#include "core/commands/NamedPresets.h"
#include "core/commands/AdjustmentTransfer.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/metadata/XmpSidecar.h"
#include "core/pipeline/StageGraph.h"

namespace {
AdjustmentState edits() {
    AdjustmentState state;state.exposure=-.4;
    BrushMaskGeometry::Stroke stroke;stroke.style={.04,.43,.71};
    for(int i=0;i<13;++i)stroke.dabs.push_back({{.21+i*.041,.32+i*.019},.6+(i%3)*.1});
    state.brushMasks.layers={{"portrait",true,.8,{stroke}},{"retained",false,-.3,{stroke}}};return state;
}
QJsonObject document(const AdjustmentState &state){return state.toJson()["brushMasks"].toObject();}
QImage sourceImage() {
    QImage image(2203,1207,QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(4000+x*17,6000+y*23,8000+(x+y)*9,65535);
    return image;
}
QImage reference(const QImage &source,const ProcessingPlan &plan,const GeometryState &geometry) {
    const auto input=geometry.apply(source);QImage result(input.size(),QImage::Format_RGBA64);result.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<input.height();y+=128) {
        const auto strip=ImagePipeline::processRegion(input,plan,{0,y,input.width(),std::min(128,input.height()-y)});if(strip.isNull())return {};
        for(int row=0;row<strip.height();++row)std::memcpy(result.scanLine(y+row),strip.constScanLine(row),std::size_t(input.width())*8);
    }
    return result;
}
QByteArray bytes(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
QByteArray fileHash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
bool writeJson(const QString &path,const QJsonArray &value){QFile f(path);const auto b=QJsonDocument(value).toJson(QJsonDocument::Compact);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();}
QString cliPath() {
    auto path=QDir(QCoreApplication::applicationDirPath()).filePath("JixelLightCli");
#ifdef Q_OS_WIN
    path+=".exe";
#endif
    return path;
}
// Independent codec oracle consumes already-rendered reference pixels. It
// never calls brush admission or the application's streaming JPEG exporter.
QImage jpegReference(const QImage &encoded,int quality) {
    const auto rgb=encoded.convertToFormat(QImage::Format_RGB888);jpeg_compress_struct info{};jpeg_error_mgr error{};
    info.err=jpeg_std_error(&error);jpeg_create_compress(&info);unsigned char *buffer=nullptr;unsigned long size=0;
    jpeg_mem_dest(&info,&buffer,&size);info.image_width=rgb.width();info.image_height=rgb.height();info.input_components=3;info.in_color_space=JCS_RGB;
    jpeg_set_defaults(&info);jpeg_set_quality(&info,quality,TRUE);info.optimize_coding=FALSE;
    for(int c=0;c<3;++c){info.comp_info[c].h_samp_factor=1;info.comp_info[c].v_samp_factor=1;}
    jpeg_start_compress(&info,TRUE);
    while(info.next_scanline<info.image_height){auto *row=const_cast<JSAMPLE *>(rgb.constScanLine(int(info.next_scanline)));jpeg_write_scanlines(&info,&row,1);}
    jpeg_finish_compress(&info);auto image=QImage::fromData(buffer,int(size),"JPEG");jpeg_destroy_compress(&info);std::free(buffer);
    image.setColorSpace(encoded.colorSpace());return image; // Metadata only; no pixel conversion.
}
QImage webpReference(const QImage &encoded,int quality) {
    QByteArray bytes;QBuffer buffer(&bytes);if(!buffer.open(QIODevice::WriteOnly))return {};
    QImageWriter writer(&buffer,"webp");writer.setQuality(quality);
    if(!writer.write(encoded.convertToFormat(QImage::Format_RGBA8888)))return {};
    return QImage::fromData(bytes,"WEBP");
}
}
class BrushPersistenceTests final:public QObject {
    Q_OBJECT
private slots:
    void activeSnapshotsAndStrictCommandsPersistAtomically() {
        QTemporaryDir dir;QVERIFY(dir.isValid());auto state=edits();state.geometry.crop={.1,.2,.7,.6};state.linearMaskEnabled=true;state.linearMaskExposure=.3;
        const auto saved=state.toJson();QVERIFY(AdjustmentState::validBrushMaskJson(saved));QCOMPARE(AdjustmentState::fromJson(saved).toJson(),saved);
        QVERIFY(!AdjustmentState{}.toJson().contains("brushMasks"));
        EditHistory history;history.initialize({});QVERIFY(history.record(state,"active-brush"));EditHistory restored;QVERIFY(restored.restore(history.toJson(),state));
        QVERIFY(!restored.undo().toJson().contains("brushMasks"));QCOMPARE(restored.redo().toJson(),saved);
        AdjustmentState target;target.exposure=2;QVERIFY(AdjustmentTransfer::apply(target,state,{"masks"}));QCOMPARE(document(target),document(state));QCOMPARE(target.exposure,2);
        QString error;NamedPresets presets(dir.filePath("presets.json"));QVERIFY(presets.load(&error));QVERIFY2(presets.save("Active",state,&error),qPrintable(error));
        auto presetExpected=state;presetExpected.geometry={};const auto portable=dir.filePath("active.jpreset");QVERIFY(presets.exportFile("Active",portable,&error));
        NamedPresets imported(dir.filePath("imported.json"));QVERIFY(imported.load(&error));QVERIFY2(imported.importFile(portable,"Imported",&error),qPrintable(error));
        AdjustmentState read;QVERIFY(imported.get("Imported",&read));QCOMPARE(read.toJson(),presetExpected.toJson());
        const auto xmp=dir.filePath("active.xmp");QVERIFY2(XmpSidecar::writeNew(xmp,state,{},3,"pick",&error),qPrintable(error));
        XmpSidecar::Document sidecar;QVERIFY2(XmpSidecar::read(xmp,&sidecar,&error),qPrintable(error));QCOMPARE(sidecar.adjustments.toJson(),saved);QCOMPARE(sidecar.rating,3);
        const auto before=bytes(dir.filePath("presets.json"));auto invalid=state;invalid.brushMasks.layers[1].id="portrait";
        QVERIFY(!presets.replace("Active",invalid,&error));QCOMPARE(bytes(dir.filePath("presets.json")),before);
        QVERIFY(!XmpSidecar::writeNew(dir.filePath("invalid.xmp"),invalid,{},0,"none",&error));QVERIFY(!QFile::exists(dir.filePath("invalid.xmp")));
        auto future=document(state);future["schema"]=2;
        auto malformed=document(state);auto layers=malformed["layers"].toArray();auto badLayer=layers[0].toObject();badLayer["enabled"]=1;layers[0]=badLayer;malformed["layers"]=layers;
        const QList<QJsonObject> rejected{{{"command","mask.brush.replace"},{"document",future}},{{"command","mask.brush.replace"},{"document",malformed}},
            {{"command","mask.brush.replace"},{"document",QJsonObject{}}},{{"command","mask.brush.reset"},{"extra",true}},
            {{"command","mask.brush.enable"},{"layer","portrait"},{"enabled",1}},{{"command","mask.brush.enable"},{"layer","missing"},{"enabled",true}},
            {{"command","mask.brush.exposure"},{"layer","portrait"},{"value",3.01}},{{"command","mask.brush.exposure"},{"layer","portrait"},{"value","1"}}};
        for(const auto &command:rejected){auto staged=state;QVERIFY(!CommandRegistry::execute(staged,command,&error));QCOMPARE(staged.toJson(),saved);}
        auto staged=state;QVERIFY(!CommandRegistry::executeBatch(staged,{QJsonObject{{"command","mask.brush.reset"}},rejected.front()},&error));QCOMPARE(staged.toJson(),saved);
        QVERIFY(CommandRegistry::execute(staged,{{"command","mask.brush.enable"},{"layer","portrait"},{"enabled",false}}));QVERIFY(!staged.brushMasks.layers[0].enabled);
        QVERIFY(CommandRegistry::execute(staged,{{"command","mask.brush.exposure"},{"layer","portrait"},{"value",-.7}}));QCOMPARE(staged.brushMasks.layers[0].exposureEv,-.7);
        QVERIFY(CommandRegistry::execute(staged,{{"command","mask.brush.reset"}}));QVERIFY(staged.brushMasks.layers.empty());QVERIFY(staged.linearMaskEnabled);QCOMPARE(staged.exposure,state.exposure);
        QVERIFY(CommandRegistry::execute(staged,{{"command","mask.brush.replace"},{"document",document(state)}}));QCOMPARE(staged.toJson(),saved);
    }
    void controllerActiveProjectVersionsUndoScopesAndExports() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("owned.png");QVERIFY(sourceImage().save(path));const auto originalHash=fileHash(path);
        ProcessedImageProvider provider;PhotoController controller(&provider);controller.setGpuEnabled(false);controller.setViewport(2203,1207,1,0,.5,.5);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady()&&!controller.loading()&&!controller.rendering(),15000);
        const auto baseline=controller.gpuPlan(true).state.toJson();const auto beforeHistory=controller.editHistory().size();const auto state=edits();
        QVERIFY(controller.executeEditCommands(QVariantList{QVariantMap{{"command","develop.set"},{"parameter","exposure"},{"value",state.exposure}},
            QJsonObject{{"command","mask.brush.replace"},{"document",document(state)}}.toVariantMap()}));
        controller.finishInteraction();controller.setExactScopes(true);controller.setScopeMode("waveform");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering()&&controller.scopePlotCurrent()&&controller.scopesPixelCount()==2659021,20000);
        const auto active=controller.gpuPlan(true).state.toJson();QCOMPARE(document(controller.gpuPlan(true).state),document(state));QCOMPARE(controller.editHistory().size(),beforeHistory+1);
        controller.undo();QCOMPARE(controller.gpuPlan(true).state.toJson(),baseline);controller.redo();QCOMPARE(controller.gpuPlan(true).state.toJson(),active);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering()&&controller.scopePlotCurrent(),20000);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"ActiveBrush"));QVERIFY(controller.flushEdits());
        QVERIFY(controller.createVirtualCopy("Dark brush"));QVERIFY(controller.executeEditCommand({{"command","mask.brush.exposure"},{"layer","portrait"},{"value",-.7}}));
        controller.finishInteraction();QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY2(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())),qPrintable(reopened.statusMessage()));
        QCOMPARE(reopened.library().size(),2);reopened.selectPhoto(0);QCOMPARE(document(reopened.gpuPlan(true).state),document(state));
        reopened.selectPhoto(1);QCOMPARE(reopened.gpuPlan(true).state.brushMasks.layers[0].exposureEv,-.7);QVERIFY(reopened.canUndo());
        reopened.undo();QCOMPARE(document(reopened.gpuPlan(true).state),document(state));reopened.redo();QCOMPARE(reopened.gpuPlan(true).state.brushMasks.layers[0].exposureEv,-.7);
        QTRY_VERIFY_WITH_TIMEOUT(reopened.previewReady()&&!reopened.loading()&&!reopened.rendering(),15000);
        QSignalSpy exported(&reopened,&PhotoController::exportFinished);const auto destination=dir.filePath("reopened.tif");
        QVERIFY(reopened.exportCurrent(QUrl::fromLocalFile(destination),"srgb",100));QTRY_COMPARE_WITH_TIMEOUT(exported.count(),1,15000);QCOMPARE(exported[0][0].toInt(),1);QCOMPARE(exported[0][1].toInt(),0);
        const auto loaded=loadSourceFresh(path,{});QVERIFY(!loaded.image.isNull());auto changed=state;changed.brushMasks.layers[0].exposureEv=-.7;
        const auto expected=reference(loaded.image,ProcessingPlan::compile(changed,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0),{});
        const QImage actual(destination);QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);QCOMPARE(actual.colorSpace(),expected.colorSpace());
        QVERIFY(reopened.flushEdits());const auto database=QDir(controller.projectPath()).filePath("Project.db");const auto databaseHash=fileHash(database);
        const auto outputs=dir.filePath("catalog-outputs");QVERIFY(QDir().mkpath(outputs));QProcess catalog;
        catalog.start(cliPath(),{"--catalog",controller.projectPath(),"--output-dir",outputs,"--format","png","--name-template","v-{seq}"});
        QVERIFY(catalog.waitForStarted());QVERIFY(catalog.waitForFinished(30000));QVERIFY2(catalog.exitCode()==0,catalog.readAllStandardError().constData());
        const QImage first(QDir(outputs).filePath("v-1.png")),second(QDir(outputs).filePath("v-2.png"));
        const auto originalExpected=reference(loaded.image,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0),{});
        QCOMPARE(first.convertToFormat(QImage::Format_RGBA64),originalExpected);QCOMPARE(second.convertToFormat(QImage::Format_RGBA64),expected);
        QCOMPARE(first.colorSpace(),originalExpected.colorSpace());QCOMPARE(second.colorSpace(),expected.colorSpace());
        QCOMPARE(fileHash(database),databaseHash);QCOMPARE(fileHash(path),originalHash);
    }
    void realSonyCliEnabledBrushExport_data() {
        QTest::addColumn<QString>("format");for(const auto &format:{"png","tif","webp","jpg"})QTest::newRow(format)<<QString(format);
    }
    void realSonyCliEnabledBrushExport() {
        QFETCH(QString,format);const auto fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required public Sony RAW unavailable");QSKIP("No public Sony fixture configured");}
        const auto path=QDir(fixtures).filePath("sony_a7_iv_07.arw");const QByteArray hash="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";
        QCOMPARE(fileHash(path),hash);const auto source=loadSourceFresh(path,{});QVERIFY2(!source.image.isNull(),qPrintable(source.error));QCOMPARE(source.image.size(),QSize(7028,4688));
        const auto fingerprint=StageGraph::outputFingerprint(source.image);auto state=edits();state.look.mode="as-shot";state.geometry.crop={.07,.05,.4,.4};state.geometry.straighten=.7;state.geometry.quarterTurns=1;
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto commands=dir.filePath("commands.json"),target=dir.filePath("cli."+format);
        const QJsonArray batch{QJsonObject{{"command","mask.brush.replace"},{"document",document(state)}},
            QJsonObject{{"command","develop.set"},{"parameter","exposure"},{"value",state.exposure}},
            QJsonObject{{"command","geometry.crop"},{"x",.07},{"y",.05},{"width",.4},{"height",.4}},
            QJsonObject{{"command","geometry.straighten"},{"degrees",.7}},QJsonObject{{"command","geometry.rotate"},{"quarterTurns",1}}};
        QVERIFY(writeJson(commands,batch));
        const auto space=format=="png"||format=="tif"?ColorManagement::OutputSpace::DisplayP3:ColorManagement::OutputSpace::SRgb;
        const auto binary=cliPath();
        QVERIFY(QFile::exists(binary));QProcess process;process.start(binary,{"--commands",commands,"--space",ColorManagement::key(space),path,target});
        QVERIFY(process.waitForStarted());QVERIFY2(process.waitForFinished(110000),qPrintable(process.errorString()));
        const auto output=process.readAllStandardOutput(),errors=process.readAllStandardError();QCOMPARE(process.exitStatus(),QProcess::NormalExit);QVERIFY2(process.exitCode()==0,errors.constData());
        const auto report=QJsonDocument::fromJson(output).object();QVERIFY(report["ok"].toBool());QCOMPARE(report["backend"].toString(),QString("cpu-reference"));
        const auto resolved=LookProfiles::resolveAsShot(state,source.metadata,true);const float base=source.metadata.value("rawBaseExposureStops",0.).toFloat();
        const auto expected=reference(source.image,ProcessingPlan::compile(resolved,ImagePipeline::InputEncoding::LinearProPhoto,space,true,base),state.geometry);QVERIFY(!expected.isNull());
        QVERIFY(qint64(expected.width())*expected.height()>2*1024*1024);const QImage actual(target);QVERIFY(!actual.isNull());QCOMPARE(actual.colorSpace(),expected.colorSpace());
        if(format=="png"||format=="tif")QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);
        else if(format=="webp")QCOMPARE(actual.convertToFormat(QImage::Format_RGBA8888),webpReference(expected,92).convertToFormat(QImage::Format_RGBA8888));
        else QCOMPARE(actual.convertToFormat(QImage::Format_RGB888),jpegReference(expected,92).convertToFormat(QImage::Format_RGB888));
        if(format=="png") {
            auto invalid=batch;invalid.append(QJsonObject{{"command","mask.brush.enable"},{"layer","missing"},{"enabled",true}});QVERIFY(writeJson(commands,invalid));
            const auto failed=dir.filePath("rejected.png");QProcess bad;bad.start(binary,{"--commands",commands,path,failed});QVERIFY(bad.waitForStarted());QVERIFY(bad.waitForFinished(110000));QVERIFY(bad.exitCode()!=0);QVERIFY(!QFile::exists(failed));
        }
        QCOMPARE(fileHash(path),hash);QCOMPARE(StageGraph::outputFingerprint(source.image),fingerprint);
        qInfo().noquote()<<"BRUSH_PERSISTENCE_EVIDENCE "<<QJsonDocument(QJsonObject{{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"case","real-sony-cli-enabled-brush"},{"format",format},{"source_pixels",32947264},{"output_pixels",qint64(actual.width())*actual.height()},
            {"raw_sha256",QString::fromLatin1(hash)},{"source_override",false},{"source_unchanged",true},{"producer","cpu-reference"},
            {"native_float_raw_decoder",false},{"native_gpu_brush",false}}).toJson(QJsonDocument::Compact);
    }
};
QTEST_MAIN(BrushPersistenceTests)
#include "BrushPersistenceTests.moc"
