#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QStandardPaths>
#include <QImageReader>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <limits>
#include "app/PhotoController.h"
#include "core/commands/AdjustmentTransfer.h"
#include "core/commands/NamedPresets.h"
#include "core/commands/CommandRegistry.h"
#include "core/export/RasterExporter.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/metadata/XmpSidecar.h"
#include "core/pipeline/FilmGrain.h"
#include "core/pipeline/StageGraph.h"

class FilmGrainTests : public QObject {
    Q_OBJECT
    static QImage flat(QSize size={640,480}) {
        QImage image(size,QImage::Format_RGBA64); image.fill(QColor(128,128,128)); return image;
    }
    static ProcessingPlan plan(const AdjustmentState &state={}) {
        return ProcessingPlan::compile(state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace::SRgb,false,0);
    }
    static QByteArray bytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{}; }
private slots:
    void fixedNoiseAndDefaultIdentity() {
        // Independent integer oracle values, including signed cell coordinates.
        QCOMPARE(FilmGrain::lattice(0,0),18511.f/32768-1);
        QCOMPARE(FilmGrain::lattice(1,0),15013.f/32768-1);
        QCOMPARE(FilmGrain::lattice(0,1),57975.f/32768-1);
        QCOMPARE(FilmGrain::lattice(17,31),5231.f/32768-1);
        QCOMPARE(FilmGrain::lattice(-2,5),57738.f/32768-1);
        const auto source=flat(); const auto original=source.copy();
        const auto baseline=ImagePipeline::processWithPlan(source,plan());
        QVERIFY(!AdjustmentState{}.toJson().contains("grain"));
        AdjustmentState disabled; disabled.grainSize=4;
        QCOMPARE(ImagePipeline::processWithPlan(source,plan(disabled)),baseline);
        AdjustmentState state; state.grainAmount=100; state.grainSize=2;
        const auto rendered=ImagePipeline::processWithPlan(source,plan(state));
        QVERIFY(rendered!=baseline);
        QCOMPARE(rendered,ImagePipeline::processWithPlan(source,plan(state),{},false));
        qint64 sum=0,absolute=0;int maximum=0;
        for(int y=0;y<source.height();++y) {
            const auto *a=reinterpret_cast<const QRgba64 *>(rendered.constScanLine(y));
            const auto *b=reinterpret_cast<const QRgba64 *>(baseline.constScanLine(y));
            for(int x=0;x<source.width();++x) {
                const int delta=int(a[x].red())-int(b[x].red());sum+=delta;absolute+=std::abs(delta);maximum=std::max(maximum,std::abs(delta));
                QVERIFY(std::abs((int(a[x].green())-int(b[x].green()))-delta)<=1);
                QVERIFY(std::abs((int(a[x].blue())-int(b[x].blue()))-delta)<=1);
                QCOMPARE(a[x].alpha(),b[x].alpha());
            }
        }
        const auto count=qint64(source.width())*source.height();
        QVERIFY(maximum<=3933); QVERIFY(absolute/count>250); QVERIFY(std::abs(sum/count)<80);
        QCOMPARE(source,original);
        auto black=flat();black.fill(Qt::black);
        QCOMPARE(ImagePipeline::processWithPlan(black,plan(state)),ImagePipeline::processWithPlan(black,plan()));
        // A unit-white source is below encoded white after the existing
        // shoulder. Raise it to the actual output endpoint before testing it.
        auto white=flat();white.fill(Qt::white);auto bright=state;bright.exposure=5;
        auto noGrain=bright;noGrain.grainAmount=0;
        const auto whiteReference=ImagePipeline::processWithPlan(white,plan(noGrain));
        QCOMPARE(whiteReference.pixelColor(0,0),QColor(Qt::white));
        QCOMPARE(ImagePipeline::processWithPlan(white,plan(bright)),whiteReference);
    }
    void tilesAlphaScopesAndPlots() {
        QImage source(389,277,QImage::Format_RGBA64);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64((x*127+y*109)%65536,(x*43+y*173)%65536,(x*191+y*17)%65536,(x+y)%3?23456:0);
        for(bool raw : {false,true})for(int space=0;space<4;++space) {
            AdjustmentState state;state.grainAmount=73;state.grainSize=3.2;
            state.look.mode="manual";state.look.code="FL";state.look.parameters={{"clarity",3},{"sharpness",4}};
            const auto p=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),raw,0);
            const auto full=ImagePipeline::processWithPlan(source,p);QVERIFY(!full.isNull());
            for(const QRect region : {QRect(0,0,389,128),QRect(0,128,389,128),QRect(0,256,389,21),QRect(29,73,213,131)})
                QCOMPARE(ImagePipeline::processRegion(source,p,region),full.copy(region));
            const auto expected=ScopesEngine::analyze(full),actual=ScopesEngine::analyzeFull(source,p);
            QCOMPARE(actual.red,expected.red);QCOMPARE(actual.green,expected.green);QCOMPARE(actual.blue,expected.blue);QCOMPARE(actual.luma,expected.luma);
            QCOMPARE(actual.shadowClipPercent,expected.shadowClipPercent);QCOMPARE(actual.highlightClipPercent,expected.highlightClipPercent);
            if(space==0) {
                const auto plots=renderScopePlots({source,p,{},"waveform",1,true},{});
                const QStringList modes{"waveform","parade","vectorscope"};
                for(int i=0;i<3;++i) {ScopePlotCounts reference(modes[i]);QVERIFY(reference.add(full));QCOMPARE(plots[i].image,reference.image());QCOMPARE(plots[i].pixels,quint64(source.width())*source.height());}
            }
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
                QCOMPARE(reinterpret_cast<const QRgba64 *>(full.constScanLine(y))[x].alpha(),reinterpret_cast<const QRgba64 *>(source.constScanLine(y))[x].alpha());
        }
    }
    void viewportAnchoringAndSmallPreviewFiltering() {
        AdjustmentState state;state.grainAmount=100;state.grainSize=2;
        const auto full=ImagePipeline::processWithPlan(flat({512,384}),plan(state));
        auto viewport=plan(state);viewport.setFrameRect({.25,.25,.5,.5});
        QCOMPARE(ImagePipeline::processWithPlan(flat({256,192}),viewport),full.copy(128,96,256,192));
        state.grainSize=.5;
        QCOMPARE(ImagePipeline::processWithPlan(flat({64,48}),plan(state)),ImagePipeline::processWithPlan(flat({64,48}),plan()));
        auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(ImagePipeline::processWithPlan(flat(),plan(state),token).isNull());
    }
    void commandsSnapshotsPresetsXmpAndHistory() {
        AdjustmentState state;state.exposure=.25;state.vignetteAmount=-1;
        QVERIFY(CommandRegistry::set(state,"grainAmount",67));QVERIFY(CommandRegistry::set(state,"grainSize",2.3));
        const auto saved=state.toJson();QVERIFY(AdjustmentState::validGrainJson(saved));QCOMPARE(AdjustmentState::fromJson(saved).toJson(),saved);
        EditHistory history;history.initialize({});QVERIFY(history.record(state,"grain_adjustment"));
        EditHistory restored;QVERIFY(restored.restore(history.toJson(),state));restored.undo();QCOMPARE(restored.redo().toJson(),saved);
        QTemporaryDir dir;QString error;NamedPresets presets(dir.filePath("presets.json"));QVERIFY(presets.load(&error));
        QVERIFY2(presets.save("Grain",state,&error),qPrintable(error));QVERIFY(presets.exportFile("Grain",dir.filePath("grain.jixelpreset.json"),&error));
        NamedPresets reloaded(dir.filePath("presets.json"));QVERIFY(reloaded.load(&error));
        AdjustmentState fromPreset;QVERIFY(reloaded.get("Grain",&fromPreset));QCOMPARE(fromPreset.toJson(),saved);
        QVERIFY(reloaded.importFile(dir.filePath("grain.jixelpreset.json"),"Imported",&error));
        const auto xmp=dir.filePath("grain.xmp");QVERIFY2(XmpSidecar::writeNew(xmp,state,{},0,"none",&error),qPrintable(error));
        XmpSidecar::Document document;QVERIFY2(XmpSidecar::read(xmp,&document,&error),qPrintable(error));QCOMPARE(document.adjustments.toJson(),saved);
        AdjustmentState target;target.exposure=1;QVERIFY(AdjustmentTransfer::apply(target,state,{"effects"}));QCOMPARE(target.grainAmount,67.0);QCOMPARE(target.exposure,1.0);
        QVERIFY(CommandRegistry::execute(target,{{"command","grain.reset"}}));QCOMPARE(target.grainSize,1.0);QCOMPARE(target.vignetteAmount,-1.0);QCOMPARE(target.exposure,1.0);
        QVERIFY(!CommandRegistry::executeBatch(state,{QJsonObject{{"command","develop.set"},{"parameter","grainAmount"},{"value",42}},QJsonObject{{"command","grain.reset"},{"extra",true}}},&error));QCOMPARE(state.toJson(),saved);
        QVERIFY(CommandRegistry::set(target,"grainAmount",200));QCOMPARE(target.grainAmount,100.0);
        QVERIFY(!CommandRegistry::set(target,"grainSize",std::numeric_limits<double>::infinity()));
    }
    void malformedSnapshotsPreserveCatalogWriter() {
        QTemporaryDir dir;ProjectDatabase active,candidate;QVERIFY(active.create(dir.path(),"Active"));QVERIFY(candidate.create(dir.path(),"Candidate"));
        AdjustmentState state;state.grainAmount=40;QVERIFY(candidate.addOrUpdatePhoto("source.png",state));QVERIFY(candidate.flush());
        const auto activePath=active.projectPath(),dbPath=QDir(candidate.projectPath()).filePath("Project.db");
        const auto valid=state.toJson()["grain"].toObject();QList<QJsonValue> invalid{QJsonArray{},QJsonObject{}};
        for(const auto *key : {"schema","amount","size"}) {auto g=valid;g.remove(key);invalid.append(g);}
        for(const auto *key : {"amount","size"})for(const auto value : {QJsonValue(-1),QJsonValue(101),QJsonValue(true),QJsonValue("1"),QJsonValue()}) {auto g=valid;g[key]=value;invalid.append(g);}
        auto future=valid;future["schema"]=2;invalid.append(future);auto extra=valid;extra["seed"]=1;invalid.append(extra);
        invalid.append(QJsonObject{{"schema",1},{"amount",0},{"size",1}});
        for(const auto &g : invalid) {
            auto snapshot=state.toJson();snapshot["grain"]=g;QVERIFY(!AdjustmentState::validGrainJson(snapshot));
            const QString connection="grain-corrupt-owned";
            {auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(dbPath);QVERIFY(db.open());QSqlQuery q(db);q.prepare("UPDATE photos SET adjustment_json=?");q.addBindValue(QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)));QVERIFY(q.exec());}
            QSqlDatabase::removeDatabase(connection);const auto before=bytes(dbPath);QVector<ProjectDatabase::SavedPhoto> photos;
            QVERIFY(!active.open(candidate.projectPath(),&photos));QVERIFY(active.lastError().contains("grain"));QCOMPARE(active.projectPath(),activePath);QVERIFY(active.flush());QCOMPARE(bytes(dbPath),before);QVERIFY(photos.isEmpty());
        }
        auto invalidState=state;invalidState.grainSize=-1;AdjustmentState destination;destination.exposure=2;const auto before=destination.toJson();
        QVERIFY(!AdjustmentTransfer::apply(destination,invalidState,{"exposure","effects"}));QCOMPARE(destination.toJson(),before);
    }
    void diagnosticsCacheAndRasterExport() {
        const auto source=flat({384,288});AdjustmentState state;state.grainAmount=80;state.grainSize=3.5;
        const auto captured=ImagePipeline::diagnoseWithPlan(source,plan(state));
        QCOMPARE(captured.image,ImagePipeline::processWithPlan(source,plan(state)));QCOMPARE(captured.stages["entries"].toArray().size(),10);
        const auto plain=ImagePipeline::diagnoseWithPlan(source,plan());QCOMPARE(plain.stages["entries"].toArray().size(),9);
        const auto evidence=qEnvironmentVariable("JIXELLIGHT_GRAIN_EVIDENCE");
        if(!evidence.isEmpty()) {QVERIFY(QDir().mkpath(evidence));QVERIFY(captured.image.save(QDir(evidence).filePath("grain-rendered.png")));QVERIFY(plain.image.save(QDir(evidence).filePath("grain-disabled.png")));}
        for(int i=0;i<9;++i)QCOMPARE(captured.stages["entries"].toArray()[i],plain.stages["entries"].toArray()[i]);
        QCOMPARE(captured.stages["entries"].toArray()[9].toObject()["id"].toString(),QString("grain"));
        QVERIFY(StageGraph::renderKey(source,plan(state))!=StageGraph::renderKey(source,plan()));
        PrepareRequest request;request.image=source;request.geometry=state.geometry;
        const auto metadata=StageGraph::describe("owned",request,state,false,0)["grain"].toObject();QVERIFY(metadata["active"].toBool());QVERIFY(!metadata["monitor_icc"].toBool());
        QTemporaryDir dir;QString error;
        for(int space=0;space<4;++space)for(const auto format : {RasterFormat::Png16,RasterFormat::Tiff16,RasterFormat::WebP8}) {
            const auto output=ColorManagement::OutputSpace(space);const auto expected=ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0));
            const QString extension=format==RasterFormat::Png16?"png":format==RasterFormat::Tiff16?"tif":"webp";
            const auto path=dir.filePath(QString::number(space)+"."+extension);
            QVERIFY2(exportRaster(source,state,path,output,format,100,{},&error,false,0),qPrintable(error));
            const QImage loaded(path);QVERIFY(!loaded.isNull());QCOMPARE(loaded.colorSpace(),ColorManagement::colorSpace(output));
            if(format==RasterFormat::WebP8)QCOMPARE(loaded.convertToFormat(QImage::Format_RGBA8888),expected.convertToFormat(QImage::Format_RGBA8888));
            else QCOMPARE(loaded.convertToFormat(QImage::Format_RGBA64),expected);
        }
    }
    void controllerHistoryScopesProjectAndCopies() {
        QTemporaryDir dir;const auto path=dir.filePath("source.png");QVERIFY(flat().save(path));const auto sourceBytes=bytes(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setLanguage("en_US");
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        c.setExposure(.2);c.finishInteraction();const auto initial=c.editHistory().size();
        for(double amount : {10,30,70})c.setGrainAmount(amount);c.finishInteraction();QCOMPARE(c.editHistory().size(),initial+1);
        QCOMPARE(c.editHistory().last().toMap()["action"].toString(),QString("grain_adjustment"));
        c.setGrainSize(3);c.finishInteraction();const auto saved=c.gpuPlan(true).state.toJson();
        c.setExactScopes(true);QTRY_VERIFY_WITH_TIMEOUT(!c.rendering()&&c.scopesStatus().contains("Full-resolution statistics"),10000);
        const auto full=ImagePipeline::processWithPlan(c.gpuSource(),c.gpuPlan(true));const auto counts=ScopesEngine::analyze(full);
        QCOMPARE(c.redHistogram(),counts.red);QCOMPARE(c.lumaHistogram(),counts.luma);
        c.resetGrain();QCOMPARE(c.grainAmount(),0.0);QCOMPARE(c.exposure(),.2);c.undo();QCOMPARE(c.gpuPlan(true).state.toJson(),saved);
        QVERIFY(c.createVirtualCopy("Grain copy"));c.resetGrain();c.undo();c.selectPhoto(0);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Grain"));QVERIFY(c.flushEdits());
        PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));
        QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);reopened.selectPhoto(1);QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);
        QVERIFY(reopened.canRedo());reopened.redo();QCOMPARE(reopened.grainAmount(),0.0);QCOMPARE(reopened.exposure(),.2);QCOMPARE(bytes(path),sourceBytes);
    }
    void realBilingualPanelControls_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("compact")<<300;
        QTest::newRow("wide")<<350;
    }
    void realBilingualPanelControls() {
        QFETCH(int,width);
        QTemporaryDir dir;const auto path=dir.filePath("source.png");QVERIFY(flat().save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setLanguage("en_US");
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        QQmlEngine engine;engine.rootContext()->setContextProperty("photoController",&c);
        QQmlComponent host(&engine,QUrl::fromLocalFile(QFINDTESTDATA("fixtures/FilmGrainWindow.qml")));
        std::unique_ptr<QObject> hostObject(host.create());auto *window=qobject_cast<QQuickWindow *>(hostObject.get());
        QVERIFY2(window,qPrintable(host.errorString()));
        QQmlComponent component(&engine,QUrl::fromLocalFile(QFINDTESTDATA("../ui/DevelopRightPanel.qml")));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"controller",QVariant::fromValue(&c)},{"grainExpanded",true},{"basicExpanded",false},{"colorExpanded",false}}));
        QVERIFY2(object,qPrintable(component.errorString()));auto *item=qobject_cast<QQuickItem *>(object.get());QVERIFY(item);
        window->resize(width,780);item->setParentItem(window->contentItem());item->setSize(window->size());window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *slider=object->findChild<QQuickItem *>("grainAmountSlider"),*reset=object->findChild<QQuickItem *>("grainReset");auto *toggle=object->findChild<QQuickItem *>("grainToggle");
        QVERIFY(slider&&reset&&toggle);QTRY_VERIFY(slider->width()>50&&slider->isVisible());QVERIFY(toggle->property("text").toString().contains("FILM GRAIN"));
        auto *flick=qvariant_cast<QQuickItem *>(object->property("brushToolFlick"));QVERIFY(flick);
        const auto sectionY=toggle->mapToItem(flick,{0,0}).y()+flick->property("contentY").toDouble();
        flick->setProperty("contentY",std::clamp(sectionY-10,0.0,std::max(0.0,flick->property("contentHeight").toDouble()-flick->height())));
        const auto point=slider->mapToScene({slider->width()*.75,slider->height()*.5}).toPoint();QVERIFY(window->contentItem()->contains(point));
        QVERIFY(flick->contains(slider->mapToItem(flick,{slider->width()*.75,slider->height()*.5})));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point);QTRY_VERIFY(c.grainAmount()>40);c.finishInteraction();
        c.setExactScopes(true);QTRY_VERIFY_WITH_TIMEOUT(!c.rendering()&&c.scopesStatus().contains("Full-resolution statistics"),10000);
        const auto captureRoot=qEnvironmentVariable("JIXELLIGHT_GRAIN_EVIDENCE");
        if(!captureRoot.isEmpty()) {QVERIFY(QDir().mkpath(captureRoot));QTRY_VERIFY(!window->grabWindow().isNull());QVERIFY(window->grabWindow().save(QDir(captureRoot).filePath(QString("grain-panel-en-%1.png").arg(width))));}
        c.setLanguage("zh_CN");QTRY_VERIFY(toggle->property("text").toString().contains(QStringLiteral("胶片颗粒")));
        if(!captureRoot.isEmpty()) {QVERIFY(QDir().mkpath(captureRoot));QTRY_VERIFY(!window->grabWindow().isNull());QVERIFY(window->grabWindow().save(QDir(captureRoot).filePath(QString("grain-panel-zh-%1.png").arg(width))));}
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,reset->mapToScene({reset->width()*.5,reset->height()*.5}).toPoint());QTRY_COMPARE(c.grainAmount(),0.0);
        c.undo();QVERIFY(c.grainAmount()>40);item->setParentItem(nullptr);
    }
};

int main(int argc,char **argv) {
    QQuickStyle::setStyle("Fusion");
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("JixelLightOwnedTests");QCoreApplication::setApplicationName("FilmGrain");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QStandardPaths::setTestModeEnabled(true);
    FilmGrainTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "FilmGrainTests.moc"
