#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QRgba64>
#include <cstring>
#include "app/PhotoController.h"
#include "app/SmokeCaptureCoordinates.h"
#include "core/commands/CommandRegistry.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/StageGraph.h"

namespace {
QImage scene() {
    QImage image(2203,1207,QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(4000+x*17,6000+y*23,8000+(x+y)*9,65535);
    return image;
}
QByteArray hashFile(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
QImage reference(const QImage &source,const ProcessingPlan &plan) {
    const auto input=plan.state.geometry.apply(source);QImage image(input.size(),QImage::Format_RGBA64);image.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<input.height();y+=128) {
        const auto strip=ImagePipeline::processRegion(input,plan,{0,y,input.width(),std::min(128,input.height()-y)});if(strip.isNull())return {};
        for(int row=0;row<strip.height();++row)std::memcpy(image.scanLine(y+row),strip.constScanLine(row),std::size_t(input.width())*8);
    }
    return image;
}
QJsonObject state(const PhotoController &c){return c.gpuPlan(true).state.toJson();}
int cursor(const PhotoController &c){const auto rows=c.editHistory();for(int i=0;i<rows.size();++i)if(rows[i].toMap()["current"].toBool())return i;return -1;}
}
class BrushGestureTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){QStandardPaths::setTestModeEnabled(true);}
    void clippedNativeCaptureSamplesTheVisibleStrokeAtDeviceScale() {
        // macOS CI captured1180x720 from an1180x800 request. The old height
        // ratio read y315 (unchanged), while the visible brush was at y350.
        const QColor base(130,139,157),painted(179,200,220);
        QImage clipped(1180,720,QImage::Format_RGB32);clipped.fill(base);
        for(int y=338;y<=361;++y)for(int x=365;x<=645;++x)clipped.setPixelColor(x,y,painted);
        QCOMPARE(SmokeCaptureCoordinates::sample(clipped,{508,350},1),painted);
        QCOMPARE(SmokeCaptureCoordinates::sample(clipped,{508,315},1),base);
        QVERIFY(!SmokeCaptureCoordinates::sample(clipped,{508,760},1).isValid());
        QCOMPARE(SmokeCaptureCoordinates::region(clipped,{790,600,390,200},1),QRect(790,600,390,120));
        QImage retina(2360,1440,QImage::Format_RGB32);retina.fill(base);retina.setPixelColor(1016,700,painted);
        QCOMPARE(SmokeCaptureCoordinates::sample(retina,{508,350},2),painted);
        QImage fractional(1475,900,QImage::Format_RGB32);fractional.fill(base);fractional.setPixelColor(635,437,painted);
        QCOMPARE(SmokeCaptureCoordinates::sample(fractional,{508,350},1.25),painted);
        QVERIFY(!SmokeCaptureCoordinates::sample(clipped,{508,350},0).isValid());
        QVERIFY(SmokeCaptureCoordinates::region(clipped,{-100,-100,20,20},1).isEmpty());
    }
    void completedStrokeIsOneUndoAndProducesAllExportsAfterReopen() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("owned.png");QVERIFY(scene().save(path));const auto hash=hashFile(path);
        PhotoController c(nullptr);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        const auto before=state(c);const auto history=cursor(c);QVERIFY(c.beginBrushStroke("",.25,.6,.04,.6,.8,1));QVERIFY(c.brushDrawing());
        QCOMPARE(state(c),before);QCOMPARE(cursor(c),history);
        for(int i=1;i<=8;++i)QVERIFY(c.appendBrushStroke(.25+.5*i/8,.6));
        QCOMPARE(state(c),before);const auto id=c.endBrushStroke();QVERIFY2(!id.isEmpty(),qPrintable(c.statusMessage()));QVERIFY(!c.brushDrawing());
        QCOMPARE(cursor(c),history+1);const auto painted=state(c);const auto doc=c.gpuPlan(true).state.brushMasks;QCOMPARE(doc.layers.size(),std::size_t(1));
        QCOMPARE(doc.layers[0].id,id);QCOMPARE(doc.layers[0].strokes.size(),std::size_t(1));QVERIFY(doc.layers[0].strokes[0].dabs.size()>8);
        c.undo();QCOMPARE(state(c),before);c.redo();QCOMPARE(state(c),painted);QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        QVERIFY(c.setBrushLayerExposure(id,.3));QVERIFY(c.setBrushLayerExposure(id,.5));c.finishInteraction();QCOMPARE(cursor(c),history+2);
        c.undo();QCOMPARE(state(c),painted);c.redo();QCOMPARE(c.gpuPlan(true).state.brushMasks.layers[0].exposureEv,.5);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"BrushGesture"));QVERIFY(c.flushEdits());
        PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));QTRY_VERIFY_WITH_TIMEOUT(reopened.brushReady(),15000);
        QCOMPARE(state(reopened),state(c));const auto source=loadSourceFresh(path,{});const auto expected=reference(source.image,reopened.gpuPlan(true));QVERIFY(!expected.isNull());
        QSignalSpy done(&reopened,&PhotoController::exportFinished);
        for(const auto &suffix:{"png","tif","webp","jpg"}) {
            done.clear();const auto output=dir.filePath(QString("painted.")+suffix);QVERIFY(reopened.exportCurrent(QUrl::fromLocalFile(output),"srgb",100));
            QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QCOMPARE(done[0][0].toInt(),1);QCOMPARE(done[0][1].toInt(),0);
            const QImage actual(output);QCOMPARE(actual.colorSpace(),expected.colorSpace());
            if(QString(suffix)=="png"||QString(suffix)=="tif")QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);
            else if(QString(suffix)=="webp")QCOMPARE(actual.convertToFormat(QImage::Format_RGBA8888),expected.convertToFormat(QImage::Format_RGBA8888));
            else {
                const auto a=actual.convertToFormat(QImage::Format_RGB888),b=expected.convertToFormat(QImage::Format_RGB888);int maximum=0;
                for(int y=0;y<a.height();++y)for(int x=0;x<a.width()*3;++x)maximum=std::max(maximum,std::abs(int(a.constScanLine(y)[x])-int(b.constScanLine(y)[x])));
                QVERIFY2(maximum<=8,qPrintable(QString::number(maximum)));
            }
        }
        QVERIFY(reopened.setBrushLayerEnabled(id,false));QTRY_VERIFY_WITH_TIMEOUT(reopened.brushReady(),15000);QVERIFY(!reopened.beginBrushStroke(id,.5,.5,.04,.6,1,1));
        QVERIFY(reopened.setBrushLayerEnabled(id,true));QTRY_VERIFY_WITH_TIMEOUT(reopened.brushReady(),15000);QVERIFY(reopened.removeBrushLayer(id));QVERIFY(!state(reopened).contains("brushMasks"));
        reopened.undo();QCOMPARE(reopened.gpuPlan(true).state.brushMasks.layers[0].id,id);QCOMPARE(hashFile(path),hash);
    }
    void geometryViewportCancellationAndSourceGuards() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("one.png"),other=dir.filePath("two.png");QVERIFY(scene().save(path));QVERIFY(scene().save(other));
        PhotoController c(nullptr);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        c.setCrop(.1,.05,.55,.72);c.rotatePhoto(1);c.finishInteraction();c.setViewport(640,480,1,1,.62,.42);QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        const auto source=loadSourceFresh(path,{});QCOMPARE(c.brushFrameSize(),c.gpuPlan(true).state.geometry.apply(source.image).size());
        const auto frame=c.previewFrameRect();QVERIFY(frame.width()<1);const auto p=c.maskCanvasPoint(160,288,640,480);
        QVERIFY(std::abs(p.x()-(frame.x()+.25*frame.width()))<1e-12);QVERIFY(std::abs(p.y()-(frame.y()+.6*frame.height()))<1e-12);
        QVERIFY(c.beginBrushStroke("",p.x(),p.y(),.03,.5,.75,1));const auto initial=state(c);c.cancelBrushStroke();QCOMPARE(state(c),initial);
        QVERIFY(c.beginBrushStroke("",p.x(),p.y(),.03,.5,.75,1));c.setViewport(680,500,1,1,.61,.43);QVERIFY(!c.brushDrawing());QVERIFY(c.endBrushStroke().isEmpty());QCOMPARE(state(c),initial);
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);QVERIFY(c.beginBrushStroke("",.5,.5,.03,.5,1,1));c.setExposure(.2);QVERIFY(!c.brushDrawing());QVERIFY(c.endBrushStroke().isEmpty());
        c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);QVERIFY(c.beginCrop());QVERIFY(!c.beginBrushStroke("",.5,.5,.03,.5,1,1));c.cancelCrop();
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);QVERIFY(c.beginBrushStroke("",.5,.5,.03,.5,1,1));QVERIFY(c.importFile(QUrl::fromLocalFile(other)));c.selectPhoto(1);QVERIFY(!c.brushDrawing());
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);QVERIFY(!state(c).contains("brushMasks"));
        QVERIFY(c.beginBrushStroke("",.5,.5,.03,.5,1,1));
        const auto connection=connect(&c,&PhotoController::brushDrawingChanged,this,[&]{if(!c.brushDrawing())c.selectPhoto(0);});
        QVERIFY(c.endBrushStroke().isEmpty());disconnect(connection);QVERIFY(!state(c).contains("brushMasks"));
        c.selectPhoto(1);QVERIFY(!state(c).contains("brushMasks"));
    }
    void invalidLongAndOverBudgetGesturesNeverPublishPartialState() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("owned.png");QVERIFY(scene().save(path));PhotoController c(nullptr);c.setGpuEnabled(false);
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);const auto initial=state(c);const auto history=cursor(c);
        QVERIFY(!c.beginBrushStroke("",-.1,.5,.04,.6,1,1));QVERIFY(!c.beginBrushStroke("",.5,.5,.001,.6,1,1));
        QVERIFY(c.beginBrushStroke("",.5,.5,.04,.6,1,1));QVERIFY(!c.appendBrushStroke(.5,.5,2));QVERIFY(!c.brushDrawing());QCOMPARE(state(c),initial);
        QVERIFY(c.beginBrushStroke("",.4,.5,.5,.6,1,1));for(int i=0;i<600;++i)QVERIFY(c.appendBrushStroke(i%2?.4:.6,.5));
        QVERIFY(c.endBrushStroke().isEmpty());QVERIFY(c.statusMessage().contains("limit")||c.statusMessage().contains("限制"));QCOMPARE(state(c),initial);QCOMPARE(cursor(c),history);
        QVERIFY(c.beginBrushStroke("",.1,.1,.002,.6,1,1));bool refused=false;
        for(int i=0;i<40&&!refused;++i)refused=!c.appendBrushStroke(i%2?.1:.9,.1);
        QVERIFY(refused);QCOMPARE(state(c),initial);QCOMPARE(cursor(c),history);QVERIFY(c.endBrushStroke().isEmpty());
        QVERIFY(c.beginBrushStroke("",.4,.5,.04,.6,1,1));QVERIFY(c.appendBrushStroke(.6,.5));QVERIFY(!c.endBrushStroke().isEmpty());QCOMPARE(cursor(c),history+1);
    }
    void layerRemovalCommandsPreserveOtherLayersAndRoundTrip() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("owned.png");QVERIFY(scene().save(path));const auto hash=hashFile(path);
        PhotoController c(nullptr);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        c.setExposure(.2);c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        const auto paint=[&](const QString &id,double y,double exposure) {
            if(!c.beginBrushStroke(id,.3,y,.03,.6,.7,exposure)||!c.appendBrushStroke(.6,y))return QString{};
            return c.endBrushStroke();
        };
        const auto a=paint("",.3,.8);QVERIFY(!a.isEmpty());QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        QCOMPARE(paint(a,.5,.8),a);QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        const auto b=paint("",.7,-.4);QVERIFY(!b.isEmpty());QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        const auto before=c.gpuPlan(true).state;QCOMPARE(before.brushMasks.layers.size(),std::size_t(2));QCOMPARE(before.brushMasks.layers[0].strokes.size(),std::size_t(2));
        for(const auto &name:{"mask.brush.remove","mask.brush.remove-last-stroke"}) {
            const QList<QJsonObject> invalid{{{"command",name}},{{"command",name},{"layer",1}},{{"command",name},{"layer",QJsonValue{}}},
                {{"command",name},{"layer",""}},{{"command",name},{"layer","missing"}},{{"command",name},{"layer",a},{"extra",true}}};
            for(const auto &command:invalid){auto staged=before;QVERIFY(!CommandRegistry::execute(staged,command));QCOMPARE(staged.toJson(),before.toJson());}
        }
        auto staged=before;QVERIFY(!CommandRegistry::executeBatch(staged,{QJsonObject{{"command","mask.brush.remove-last-stroke"},{"layer",a}},QJsonObject{{"command","mask.brush.remove"},{"layer","missing"}}}));QCOMPARE(staged.toJson(),before.toJson());
        auto malformed=before;malformed.brushMasks.layers[0].strokes.clear();QVERIFY(!CommandRegistry::execute(malformed,{{"command","mask.brush.remove"},{"layer",a}}));QCOMPARE(malformed.brushMasks.layers.size(),std::size_t(2));
        const auto index=cursor(c);QVERIFY(!c.removeLastBrushStroke("missing"));QCOMPARE(cursor(c),index);QVERIFY(c.removeLastBrushStroke(a));QCOMPARE(cursor(c),index+1);
        auto reduced=before;reduced.brushMasks.layers[0].strokes.pop_back();QCOMPARE(state(c),reduced.toJson());c.undo();QCOMPARE(state(c),before.toJson());c.redo();QCOMPARE(state(c),reduced.toJson());
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"BrushLayers"));QVERIFY(c.flushEdits());QVERIFY(c.removeLastBrushStroke(a));QVERIFY(c.flushEdits());
        auto sole=before;sole.brushMasks.layers.erase(sole.brushMasks.layers.begin());QCOMPARE(state(c),sole.toJson());
        PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));QTRY_VERIFY_WITH_TIMEOUT(reopened.brushReady(),15000);
        QCOMPARE(state(reopened),sole.toJson());reopened.undo();QCOMPARE(state(reopened),reduced.toJson());reopened.redo();QCOMPARE(state(reopened),sole.toJson());
        QVERIFY(!reopened.removeLastBrushStroke(a));QVERIFY(reopened.removeLastBrushStroke(b));QVERIFY(!state(reopened).contains("brushMasks"));QCOMPARE(reopened.exposure(),.2);
        reopened.undo();QCOMPARE(state(reopened),sole.toJson());QVERIFY(reopened.setBrushLayerEnabled(b,false));QVERIFY(reopened.removeLastBrushStroke(b));QVERIFY(!state(reopened).contains("brushMasks"));
        auto remaining=reduced;remaining.brushMasks.layers.pop_back();
        const QJsonArray commands{QJsonObject{{"command","develop.set"},{"parameter","exposure"},{"value",before.exposure}},
            QJsonObject{{"command","mask.brush.replace"},{"document",before.toJson()["brushMasks"]}},
            QJsonObject{{"command","mask.brush.remove-last-stroke"},{"layer",a}},QJsonObject{{"command","mask.brush.remove"},{"layer",b}}};
        const auto commandFile=dir.filePath("remove.json");QFile f(commandFile);QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(commands).toJson());f.close();
        auto binary=QDir(QCoreApplication::applicationDirPath()).filePath("JixelLightCli");
#ifdef Q_OS_WIN
        binary+=".exe";
#endif
        const auto output=dir.filePath("remaining.png");QProcess process;process.start(binary,{"--commands",commandFile,path,output});QVERIFY(process.waitForStarted());QVERIFY(process.waitForFinished(30000));QVERIFY2(process.exitCode()==0,process.readAllStandardError().constData());
        const auto source=loadSourceFresh(path,{});const auto expected=reference(source.image,ProcessingPlan::compile(remaining,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0));const QImage actual(output);
        QVERIFY(!expected.isNull());QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);QCOMPARE(actual.colorSpace(),expected.colorSpace());
        auto bad=commands;bad.append(QJsonObject{{"command","mask.brush.remove-last-stroke"},{"layer","missing"}});QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(QJsonDocument(bad).toJson());f.close();
        const auto rejected=dir.filePath("rejected.png");QProcess failure;failure.start(binary,{"--commands",commandFile,path,rejected});QVERIFY(failure.waitForStarted());QVERIFY(failure.waitForFinished(30000));QVERIFY(failure.exitCode()!=0);QVERIFY(!QFile::exists(rejected));QCOMPARE(hashFile(path),hash);
        qInfo().noquote()<<"BRUSH_LAYER_EVIDENCE "<<QJsonDocument(QJsonObject{{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"source_pixels",2659021},
            {"strict_commands",true},{"atomic_batch",true},{"one_undo",true},{"project_reopen",true},{"other_layers_preserved",true},{"exact_cli_png16_icc",true},{"source_unchanged",true}}).toJson(QJsonDocument::Compact);
    }
    void selectedStrokeUsesFrozenIdentityAndPreservesRemainingIntent() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("owned.png");QVERIFY(scene().save(path));const auto hash=hashFile(path);
        PhotoController c(nullptr);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        const BrushMaskGeometry::Style style{.025,.6,.7};
        const BrushMaskGeometry::Stroke first{style,{{{.2,.3},1},{{.4,.3},1},{{.6,.3},1}}};
        const BrushMaskGeometry::Stroke second{style,{{{.3,.5},1},{{.5,.5},1},{{.7,.5},1}}};
        const BrushMaskGeometry::Stroke other{style,{{{.25,.7},1},{{.45,.7},1},{{.65,.7},1}}};
        BrushMaskDocument::Document document{{{"a",true,.8,{first,second}},{"b",true,-.4,{other}}}};QJsonObject json;
        QVERIFY(BrushMaskDocument::toJson(document,&json));
        c.setExposure(.2);c.finishInteraction();QVERIFY(c.executeEditCommand({{"command","mask.brush.replace"},{"document",json}}));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);const auto before=c.gpuPlan(true).state;const auto history=cursor(c);
        const auto selection=c.brushStrokeSelection("a",0);const auto token=selection["token"].toString();QVERIFY(!token.isEmpty());
        const auto points=selection["points"].toList();QCOMPARE(points.size(),3);QCOMPARE(points[0].toMap()["x"].toDouble(),.2);QCOMPARE(points[2].toMap()["y"].toDouble(),.3);
        QCOMPARE(state(c),before.toJson());QCOMPARE(cursor(c),history); // Selection is not an edit.
        QVERIFY(c.brushStrokeSelection("missing",0).isEmpty());QVERIFY(c.brushStrokeSelection("a",-1).isEmpty());QVERIFY(c.brushStrokeSelection("a",2).isEmpty());
        QVERIFY(!c.removeBrushStroke("a",0,{}));QVERIFY(!c.removeBrushStroke("a",0,"forged"));QVERIFY(!c.removeBrushStroke("b",0,token));QVERIFY(!c.removeBrushStroke("a",1,token));
        QCOMPARE(state(c),before.toJson());QCOMPARE(cursor(c),history);
        const QJsonObject command{{"command","mask.brush.remove-stroke"},{"layer","a"},{"index",0}};
        for(const auto &value:QJsonArray{-1,2,64,.5,"0",true,QJsonValue{}}) {
            auto bad=command;bad["index"]=value;auto staged=before;QVERIFY(!CommandRegistry::execute(staged,bad));QCOMPARE(staged.toJson(),before.toJson());
        }
        for(const auto &bad:QList<QJsonObject>{{{"command","mask.brush.remove-stroke"},{"layer","a"}},
                {{"command","mask.brush.remove-stroke"},{"layer",1},{"index",0}},
                {{"command","mask.brush.remove-stroke"},{"layer","missing"},{"index",0}},
                {{"command","mask.brush.remove-stroke"},{"layer","a"},{"index",0},{"extra",true}}}) {
            auto staged=before;QVERIFY(!CommandRegistry::execute(staged,bad));QCOMPARE(staged.toJson(),before.toJson());
        }
        auto malformed=before;malformed.brushMasks.layers[0].strokes.clear();QVERIFY(!CommandRegistry::execute(malformed,command));QCOMPARE(malformed.brushMasks.layers.size(),std::size_t(2));
        auto staged=before;QVERIFY(!CommandRegistry::executeBatch(staged,{command,QJsonObject{{"command","mask.brush.remove-stroke"},{"layer","a"},{"index",1}}}));QCOMPARE(staged.toJson(),before.toJson());
        auto remaining=before;remaining.brushMasks.layers[0].strokes.erase(remaining.brushMasks.layers[0].strokes.begin());
        QVERIFY(c.removeBrushStroke("a",0,token));QCOMPARE(state(c),remaining.toJson());QCOMPARE(cursor(c),history+1);
        QVERIFY(!c.removeBrushStroke("a",0,token));QCOMPARE(state(c),remaining.toJson());QCOMPARE(cursor(c),history+1); // Shifted index cannot be reused.
        c.undo();QCOMPARE(state(c),before.toJson());c.redo();QCOMPARE(state(c),remaining.toJson());
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"SelectedStroke"));QVERIFY(c.flushEdits());
        const auto staleOwner=c.brushStrokeSelection("a",0)["token"].toString();QVERIFY(!staleOwner.isEmpty());
        QVERIFY(c.createVirtualCopy("Identical brush intent"));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        QCOMPARE(state(c),remaining.toJson());const auto copyHistory=cursor(c);QVERIFY(!c.removeBrushStroke("a",0,staleOwner));QCOMPARE(cursor(c),copyHistory);
        c.selectPhoto(0);QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);QVERIFY(!c.removeBrushStroke("a",0,staleOwner)); // A reloaded source has a new epoch.
        const auto staleLayer=c.brushStrokeSelection("a",0)["token"].toString();QVERIFY(c.setBrushLayerEnabled("a",false));c.finishInteraction();
        const auto disabled=c.gpuPlan(true).state;const auto disabledHistory=cursor(c);QVERIFY(!c.removeBrushStroke("a",0,staleLayer));QCOMPARE(cursor(c),disabledHistory);
        const auto disabledToken=c.brushStrokeSelection("a",0)["token"].toString();QVERIFY(!disabledToken.isEmpty());QVERIFY(c.removeBrushStroke("a",0,disabledToken));
        auto sole=disabled;sole.brushMasks.layers.erase(sole.brushMasks.layers.begin());QCOMPARE(state(c),sole.toJson());QCOMPARE(cursor(c),disabledHistory+1);
        c.undo();QCOMPARE(state(c),disabled.toJson());c.redo();QCOMPARE(state(c),sole.toJson());QVERIFY(c.flushEdits());
        PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));reopened.selectPhoto(0);
        QTRY_VERIFY_WITH_TIMEOUT(reopened.brushReady(),15000);QCOMPARE(state(reopened),sole.toJson());reopened.undo();QCOMPARE(state(reopened),disabled.toJson());reopened.redo();QCOMPARE(state(reopened),sole.toJson());
        QVERIFY(!reopened.removeBrushStroke("a",0,disabledToken)); // Catalog/source ownership does not survive reopen.
        // Worst-case stored path is downsampled for the guide, with exact endpoints.
        BrushMaskGeometry::Stroke longStroke{{.002,.6,1},{}};
        for(std::size_t i=0;i<BrushMaskGeometry::MaxDabsPerStroke;++i)longStroke.dabs.push_back({{double(i)/(BrushMaskGeometry::MaxDabsPerStroke-1),.4},1});
        BrushMaskDocument::Document bounded{{{"bounded",false,1,{longStroke,longStroke}}}};QVERIFY(BrushMaskDocument::toJson(bounded,&json));
        QVERIFY(reopened.executeEditCommand({{"command","mask.brush.replace"},{"document",json}}));QTRY_VERIFY_WITH_TIMEOUT(reopened.brushReady(),15000);
        const auto boundBefore=state(reopened);const auto boundHistory=cursor(reopened);const auto boundedSelection=reopened.brushStrokeSelection("bounded",0);
        const auto boundedPoints=boundedSelection["points"].toList();QCOMPARE(boundedPoints.size(),1024);QCOMPARE(boundedPoints.front().toMap()["x"].toDouble(),0.);QCOMPARE(boundedPoints.back().toMap()["x"].toDouble(),1.);
        QCOMPARE(state(reopened),boundBefore);QCOMPARE(cursor(reopened),boundHistory);
        QVERIFY(reopened.removeBrushStroke("bounded",0,boundedSelection["token"].toString()));
        QVERIFY(!reopened.removeBrushStroke("bounded",0,boundedSelection["token"].toString())); // Identical strokes still have positional identity.
        const QJsonArray commands{QJsonObject{{"command","develop.set"},{"parameter","exposure"},{"value",before.exposure}},
            QJsonObject{{"command","mask.brush.replace"},{"document",before.toJson()["brushMasks"]}},command};
        const auto commandFile=dir.filePath("selected.json");QFile file(commandFile);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(commands).toJson());file.close();
        auto binary=QDir(QCoreApplication::applicationDirPath()).filePath("JixelLightCli");
#ifdef Q_OS_WIN
        binary+=".exe";
#endif
        const auto output=dir.filePath("selected.png");QProcess process;process.start(binary,{"--commands",commandFile,path,output});QVERIFY(process.waitForStarted());QVERIFY(process.waitForFinished(30000));QVERIFY2(process.exitCode()==0,process.readAllStandardError().constData());
        const auto source=loadSourceFresh(path,{});const auto expected=reference(source.image,ProcessingPlan::compile(remaining,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0));const QImage actual(output);
        QVERIFY(!expected.isNull());QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);QCOMPARE(actual.colorSpace(),expected.colorSpace());
        auto bad=commands;bad.append(QJsonObject{{"command","mask.brush.remove-stroke"},{"layer","b"},{"index",1}});
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(bad).toJson());file.close();const auto rejected=dir.filePath("rejected.png");
        QProcess failure;failure.start(binary,{"--commands",commandFile,path,rejected});QVERIFY(failure.waitForStarted());QVERIFY(failure.waitForFinished(30000));QVERIFY(failure.exitCode()!=0);QVERIFY(!QFile::exists(rejected));QCOMPARE(hashFile(path),hash);
        qInfo().noquote()<<"BRUSH_STROKE_SELECTION_EVIDENCE "<<QJsonDocument(QJsonObject{{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"strict_index_command",true},{"atomic_batch",true},{"bounded_guide_points",1024},{"read_only_selection",true},{"frozen_owner_layer_source",true},
            {"earlier_stroke_only",true},{"other_layers_preserved",true},{"one_undo",true},{"disabled_layer_pruned_undo",true},{"project_reopen",true},
            {"exact_cli_png16_icc",true},{"source_pixels",2659021},{"source_unchanged",true}}).toJson(QJsonDocument::Compact);
    }
    void realSonyGestureProducesExactFullStatistics() {
        const auto fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required public Sony RAW unavailable");QSKIP("No public Sony fixture configured");}
        const auto path=QDir(fixtures).filePath("sony_a7_iv_07.arw");const QByteArray hash="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";QCOMPARE(hashFile(path),hash);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),30000);
        QCOMPARE(c.brushFrameSize(),QSize(7028,4688));const auto before=cursor(c);QVERIFY(c.beginBrushStroke("",.3,.55,.04,.6,.8,1));
        for(int i=1;i<=8;++i)QVERIFY(c.appendBrushStroke(.3+.4*i/8,.55));QVERIFY2(!c.endBrushStroke().isEmpty(),qPrintable(c.statusMessage()));QCOMPARE(cursor(c),before+1);
        c.setExactScopes(true);c.setScopeMode("waveform");QTRY_VERIFY_WITH_TIMEOUT(!c.rendering()&&c.scopePlotCurrent()&&c.scopesPixelCount()==32947264,90000);
        const auto source=loadSourceFresh(path,{});QVERIFY(!source.image.isNull());const auto fingerprint=StageGraph::outputFingerprint(source.image);
        const auto expected=reference(source.image,c.gpuPlan(true));QVERIFY(!expected.isNull());const auto histogram=ScopesEngine::analyze(expected);
        QCOMPARE(c.redHistogram(),histogram.red);QCOMPARE(c.greenHistogram(),histogram.green);QCOMPARE(c.blueHistogram(),histogram.blue);QCOMPARE(c.lumaHistogram(),histogram.luma);
        const auto wave=renderEncodedScopePlot(expected,"waveform");QSize size;QCOMPARE(provider.requestImage("scopes/test",&size,{}),wave.image);
        QCOMPARE(StageGraph::outputFingerprint(source.image),fingerprint);QCOMPARE(hashFile(path),hash);
        qInfo().noquote()<<"BRUSH_GESTURE_EVIDENCE "<<QJsonDocument(QJsonObject{{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"case","real-sony-controller-gesture"},{"source_pixels",32947264},{"statistics_pixels",qint64(c.scopesPixelCount())},
            {"exact_histogram_bins",4096},{"exact_waveform",true},{"one_undo",true},{"raw_sha256",QString::fromLatin1(hash)},
            {"source_unchanged",true},{"native_float_raw_decoder",false},{"native_gpu_brush",false}}).toJson(QJsonDocument::Compact);
    }
};
QTEST_MAIN(BrushGestureTests)
#include "BrushGestureTests.moc"
