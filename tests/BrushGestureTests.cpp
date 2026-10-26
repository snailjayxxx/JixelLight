#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QRgba64>
#include <cstring>
#include "app/PhotoController.h"
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
