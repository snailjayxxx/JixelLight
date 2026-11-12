#include <QtTest>
#include <QApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QSettings>
#include <QStandardPaths>
#include <QQuickStyle>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <cstring>
#include <limits>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/commands/CommandRegistry.h"
#include "core/commands/AdjustmentTransfer.h"
#include "core/commands/NamedPresets.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/StageGraph.h"
#include "core/cache/RenderedPreviewCache.h"
#include "core/export/RasterExporter.h"
#include "core/metadata/XmpSidecar.h"
class PointColorWorkflowTests:public QObject {
    Q_OBJECT
    static QImage scene(QSize size={67,71},bool alphaEnabled=true) {
        QImage image(size,QImage::Format_RGBA32FPx4);const float alpha[]{0,.01f,.5f,1};
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            p[0]=.1f+.25f*x/size.width();p[1]=.08f+.2f*y/size.height();p[2]=.15f;p[3]=alphaEnabled?alpha[(x+y)%4]:1;}
        return image;
    }
    static ProcessingPlan plan(const AdjustmentState &s={},int output=0){return ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),false,0);}
    static AdjustmentState edits(){AdjustmentState s;s.pointColorEnabled=true;s.pointColorLab={.65,.02,.01};s.pointColorHue=59;s.pointColorSaturation=41;s.pointColorLuminance=17;s.pointColorHueRange=180;s.pointColorChromaRange=1;s.pointColorLightnessRange=1;return s;}
    static QByteArray bytes(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
    static void equalLab(PointColor::Lab a,PointColor::Lab b,float tolerance=1.e-6f){QVERIFY(std::abs(a.L-b.L)<=tolerance);QVERIFY(std::abs(a.a-b.a)<=tolerance);QVERIFY(std::abs(a.b-b.b)<=tolerance);}
    static PointColor::Lab fromLinear(const QJsonArray &v){const double r=v[0].toDouble(),g=v[1].toDouble(),b=v[2].toDouble();const double l=std::cbrt(.4122214708*r+.5363325363*g+.0514459929*b),m=std::cbrt(.2119034982*r+.6806995451*g+.1073969566*b),s=std::cbrt(.0883024619*r+.2817188376*g+.6299787005*b);return {float(.2104542553*l+.793617785*m-.0040720468*s),float(1.9779984951*l-2.428592205*m+.4505937099*s),float(.0259040371*l+.7827717662*m-.808675766*s)};}
private slots:
    void optionalSnapshotStrictCenterAndCommands() {
        AdjustmentState s;QVERIFY(!s.toJson().contains("pointColor"));QVERIFY(CommandRegistry::execute(s,{{"command","point.color.select"},{"color",QJsonArray{.2,.4,.7}}}));
        const auto lab=PointColor::fromEncodedSrgb({.2f,.4f,.7f});QCOMPARE(s.pointColorLab,(std::array<double,3>{lab.L,lab.a,lab.b}));QVERIFY(s.pointColorEnabled);
        const auto saved=s.toJson();QCOMPARE(saved["pointColor"].toObject()["space"].toString(),QString("oklab"));QCOMPARE(AdjustmentState::fromJson(saved).toJson(),saved);
        QVERIFY(CommandRegistry::execute(s,{{"command","point.color.enable"},{"enabled",false}}));QVERIFY(s.toJson().contains("pointColor"));
        for(const auto command : {QJsonObject{{"command","point.color.select"},{"color",QJsonArray{1,0,2}}},QJsonObject{{"command","point.color.select.lab"},{"lab",QJsonArray{0,0,5}}},QJsonObject{{"command","point.color.enable"},{"enabled",1}}}){const auto before=s.toJson();QVERIFY(!CommandRegistry::execute(s,command));QCOMPARE(s.toJson(),before);}
        QVERIFY(CommandRegistry::execute(s,{{"command","point.color.select.lab"},{"lab",QJsonArray{.7,-.2,.1}}}));QCOMPARE(s.pointColorLab,(std::array<double,3>{.7,-.2,.1}));
        const auto before=s.toJson();QVERIFY(!CommandRegistry::executeBatch(s,{QJsonObject{{"command","point.color.reset"}},QJsonObject{{"command","develop.set"},{"parameter","pointColorHueRange"},{"value",0},{"extra",true}}}));QCOMPARE(s.toJson(),before);
        s.exposure=1;QVERIFY(CommandRegistry::execute(s,{{"command","point.color.reset"}}));QVERIFY(!s.toJson().contains("pointColor"));QCOMPARE(s.exposure,1.0);
    }
    void malformedSnapshotsAreRejected() {
        const auto good=edits().toJson();const auto object=good["pointColor"].toObject();QVERIFY(AdjustmentState::validPointColorJson(good));
        for(const auto &key:object.keys()){auto bad=good;auto section=object;section.remove(key);bad["pointColor"]=section;QVERIFY(!AdjustmentState::validPointColorJson(bad));}
        for(const auto value:{QJsonValue(true),QJsonValue("0.5"),QJsonValue(),QJsonValue(5.)}){auto bad=good;auto section=object;section["center"]=QJsonArray{value,0,0};bad["pointColor"]=section;QVERIFY(!AdjustmentState::validPointColorJson(bad));}
        for(const auto &key:QStringList{"space","schema","future"}){auto bad=good;auto section=object;section[key]=key=="space"?QJsonValue("srgb"):QJsonValue(2);bad["pointColor"]=section;QVERIFY(!AdjustmentState::validPointColorJson(bad));}
        auto bad=edits();bad.pointColorLab[0]=std::numeric_limits<double>::infinity();QVERIFY(!AdjustmentState::validPointColorJson(bad.toJson()));
        const auto base=plan();QCOMPARE(int(ProcessingPlan::SlotCount),61);QCOMPARE(sizeof(base.data),std::size_t(976));QVERIFY(base.validPointColor());
        for(int slot=58;slot<61;++slot){auto packed=base;packed.data[slot].x=1;QVERIFY(!packed.validPointColor());QVERIFY(ImagePipeline::processWithPlan(scene(),packed).isNull());}
    }
    void transferPresetsAndXmpKeepSelection() {
        auto s=edits();s.exposure=.4;AdjustmentState target;target.temperature=19;QVERIFY(AdjustmentTransfer::apply(target,s,{"color"}));QCOMPARE(target.pointColorLab,s.pointColorLab);QCOMPARE(target.pointColorHue,s.pointColorHue);QCOMPARE(target.temperature,19.0);QCOMPARE(target.exposure,0.0);
        QTemporaryDir dir;QString error;NamedPresets presets(dir.filePath("presets.json"));QVERIFY(presets.load(&error));QVERIFY(presets.save("Point Color",s,&error));AdjustmentState loaded;QVERIFY(presets.get("Point Color",&loaded));QCOMPARE(loaded.toJson(),s.toJson());
        const auto path=dir.filePath("point.xmp");QVERIFY2(XmpSidecar::writeNew(path,s,{},0,"none",&error),qPrintable(error));XmpSidecar::Document decoded;QVERIFY2(XmpSidecar::read(path,&decoded,&error),qPrintable(error));QCOMPARE(decoded.adjustments.toJson(),s.toJson());
    }
    void disabledAndZeroAdjustmentsKeepCompletePixels() {
        const auto source=scene();const auto identity=ImagePipeline::processWithPlan(source,plan());auto s=edits();s.pointColorEnabled=false;QCOMPARE(ImagePipeline::processWithPlan(source,plan(s)),identity);
        s.pointColorEnabled=true;s.pointColorHue=0;s.pointColorSaturation=0;s.pointColorLuminance=0;QCOMPARE(ImagePipeline::processWithPlan(source,plan(s)),identity);
        s=edits();const auto active=ImagePipeline::processWithPlan(source,plan(s));QVERIFY(active!=identity);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)QCOMPARE(reinterpret_cast<const QRgba64 *>(active.constScanLine(y))[x].alpha(),reinterpret_cast<const QRgba64 *>(identity.constScanLine(y))[x].alpha());
    }
    void pickerMatchesWorkingBoundaryAndIgnoresOutputAndPointEdits() {
        auto source=scene({1,1});reinterpret_cast<float *>(source.scanLine(0))[3]=1;auto s=edits();s.exposure=.4;s.hue=23;s.gradingEnabled=true;s.gradingHue[1]=31;s.gradingSaturation[1]=27;
        s.pointColorEnabled=false;const auto d=ImagePipeline::diagnoseWithPlan(source,plan(s));QVERIFY(!d.image.isNull());QJsonArray rgb;
        for(auto v:d.stages["entries"].toArray())if(v.toObject()["id"]==QString("perceptual_look"))rgb=v.toObject()["first_rgb"].toArray();QCOMPARE(rgb.size(),3);
        const auto picked=ImagePipeline::samplePointColor(source,plan(s),{0,0});QVERIFY(picked);equalLab(*picked,fromLinear(rgb));
        for(int output=0;output<4;++output){s.pointColorEnabled=true;s.pointColorHue=-129;s.grainAmount=71;s.sharpenAmount=95;auto p=plan(s,output);const auto other=ImagePipeline::samplePointColor(source,p,{0,0});QVERIFY(other);equalLab(*other,*picked,0);}
    }
    void pickerSpatialHalosAndBrushCoordinatesMatchWholeInput() {
        auto source=scene();reinterpret_cast<float *>(source.scanLine(0))[3]=1;
        auto s=edits();s.denoiseLuma=73;s.denoiseChroma=91;s.dehazeAmount=57;s.textureAmount=63;s.clarityAmount=-41;s.pointColorEnabled=false;
        BrushMaskGeometry::Stroke paint;paint.style={.3,.5,1};paint.dabs={{{.03,.03},1}};
        for(bool brush:{false,true}) {
            if(brush)s.brushMasks.layers={{"picker",true,1,{paint}}};
            for(const QRectF frame:{QRectF(0,0,1,1),QRectF(.1,.2,.7,.6)}) {
                auto p=plan(s);QVERIFY(ImagePipeline::resolveDehazeAirlight(source,p));p.setFrameRect(frame);
                const auto reference=brush?ImagePipeline::DiagnosticResult{}:ImagePipeline::diagnoseWithPlan(source,p);if(!brush)QVERIFY(!reference.image.isNull());QJsonArray rgb;
                // The first-pixel oracle runs every full spatial pass and the existing observer path.
                if(!brush)for(auto v:reference.stages["entries"].toArray())if(v.toObject()["id"]==QString("perceptual_look"))rgb=v.toObject()["first_rgb"].toArray();
                const auto sample=ImagePipeline::samplePointColor(source,p,{0,0});QVERIFY(sample);
                if(!brush){QCOMPARE(rgb.size(),3);equalLab(*sample,fromLinear(rgb),2.e-6f);}
                else {auto plain=s;plain.brushMasks={};auto unpainted=p;unpainted.state.brushMasks={};const auto before=ImagePipeline::samplePointColor(source,unpainted,{0,0});QVERIFY(before);QVERIFY(sample->L!=before->L);}
            }
        }
    }
    void pickerRejectsTransparentInvalidAndCancelledInput() {
        const auto source=scene();QVERIFY(!ImagePipeline::samplePointColor(source,plan(),{0,0}));QVERIFY(!ImagePipeline::samplePointColor(source,plan(),{-1,1}));
        auto token=std::make_shared<std::atomic_bool>(true);QVERIFY(!ImagePipeline::samplePointColor(source,plan(),{1,0},token));auto bad=source.copy();reinterpret_cast<float *>(bad.scanLine(10))[0]=std::numeric_limits<float>::quiet_NaN();QVERIFY(!ImagePipeline::samplePointColor(bad,plan(),{1,0}));
    }
    void downstreamPointEditsReuseAllCompletedSpatialWork() {
        const auto source=scene();auto s=edits();s.denoiseLuma=71;s.denoiseChroma=83;s.dehazeAmount=53;s.textureAmount=61;s.clarityAmount=37;RenderedPreviewCache cache;
        auto p=plan(s);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));const auto initial=cache.snapshot();
        for(int i=0;i<4;++i){s.pointColorHue=31*i;s.pointColorSaturation=-17*i;s.pointColorLuminance=9*i;s.pointColorLab[1]=.01*i;s.pointColorHueRange=15+30*i;p=plan(s,i);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));}
        const auto final=cache.snapshot();for(const auto *prefix:{"denoise","dehaze"}){const QString key=QString(prefix)+"_stage_";QCOMPARE(final[key+"misses"],initial[key+"misses"]);QCOMPARE(final[key+"hits"].toInt(),initial[key+"hits"].toInt()+4);}
        QCOMPARE(final["budget_bytes"].toInteger(),64LL*1024*1024);QVERIFY(final["charged_bytes"].toInteger()<=64LL*1024*1024);
    }
    void completeExportsScopesAndRegionsAgree() {
        const auto source=scene();auto s=edits();s.geometry.crop={.1,.1,.8,.8};s.geometry.quarterTurns=1;QTemporaryDir dir;QString error;
        for(int space=0;space<4;++space){const auto corrected=s.geometry.apply(source);const auto expected=ImagePipeline::processWithPlan(corrected,plan(s,space));QVERIFY(!expected.isNull());
            QCOMPARE(ImagePipeline::processRegion(corrected,plan(s,space),{3,4,31,29}),expected.copy(3,4,31,29));const auto stats=ScopesEngine::analyzeFull(corrected,plan(s,space)),reference=ScopesEngine::analyze(expected);QCOMPARE(stats.red,reference.red);QCOMPARE(stats.green,reference.green);QCOMPARE(stats.blue,reference.blue);QCOMPARE(stats.luma,reference.luma);
            const auto path=dir.filePath(QString::number(space)+".png");QVERIFY2(exportRaster(source,s,path,ColorManagement::OutputSpace(space),RasterFormat::Png16,100,{},&error,false,0),qPrintable(error));const QImage decoded(path);QCOMPARE(decoded.convertToFormat(QImage::Format_RGBA64),expected);QCOMPARE(decoded.colorSpace(),ColorManagement::colorSpace(ColorManagement::OutputSpace(space)));}
    }
    void controllerPickerHistoryProjectAndRevision() {
        QTemporaryDir dir;const auto path=dir.filePath("source.png");QVERIFY(scene({400,300}).convertToFormat(QImage::Format_RGBA64).save(path));const auto original=bytes(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setViewport(200,150,1,0,.5,.5);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        QVERIFY(!c.pickPointColor(-1,1,200,150));QVERIFY(c.pickPointColor(75,60,200,150));QTRY_VERIFY_WITH_TIMEOUT(c.pointColorEnabled(),10000);QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);
        const auto selected=c.gpuPlan(true).state.pointColorLab;const auto before=c.editHistory().size();for(double hue:{10.,30.,70.})c.setPointColorHue(hue);c.finishInteraction();QCOMPARE(c.editHistory().size(),before+1);QCOMPARE(c.editHistory().last().toMap()["action"].toString(),QString("point_color_adjustment"));
        c.setPointColorSaturation(-39);c.finishInteraction();c.setExposure(.3);c.finishInteraction();const auto saved=c.gpuPlan(true).state.toJson();
        c.resetPointColor();QVERIFY(!c.pointColorEnabled());QCOMPARE(c.exposure(),.3);c.undo();QCOMPARE(c.gpuPlan(true).state.toJson(),saved);
        QVERIFY(c.createVirtualCopy("Color copy"));QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Point Color"));QVERIFY(c.flushEdits());PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);QCOMPARE(reopened.gpuPlan(true).state.pointColorLab,selected);reopened.selectPhoto(0);QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);
        // A submitted picker cannot overwrite a later adjustment snapshot.
        QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);QVERIFY(c.pickPointColor(150,120,200,150));c.setPointColorHue(-83);c.finishInteraction();
        const auto revised=c.gpuPlan(true).state.toJson();QTest::qWait(200);QCOMPARE(c.gpuPlan(true).state.toJson(),revised);
        QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);QVERIFY(c.pickPointColor(150,120,200,150));c.selectPhoto(0);
        QTest::qWait(200);QCOMPARE(c.gpuPlan(true).state.toJson(),saved);QCOMPARE(bytes(path),original);
    }
    void invalidProjectWritesAndOpenPreserveOriginalCatalog() {
        QTemporaryDir dir;ProjectDatabase active;QVERIFY(active.create(dir.path(),"Active"));const auto current=active.projectPath();QVERIFY(active.addOrUpdatePhoto("source.png",edits()));QVERIFY(active.flush());const auto dbPath=QDir(current).filePath("Project.db");const auto original=bytes(dbPath);
        auto bad=edits();bad.pointColorHueRange=0;QVERIFY(!active.updateBatch({{"source.png",bad}}));QVERIFY(active.flush());QCOMPARE(bytes(dbPath),original);
        ProjectDatabase candidate;QVERIFY(candidate.create(dir.path(),"Bad"));QVERIFY(candidate.addOrUpdatePhoto("source.png",edits()));QVERIFY(candidate.flush());const auto candidatePath=QDir(candidate.projectPath()).filePath("Project.db");
        auto json=edits().toJson();auto section=json["pointColor"].toObject();section["space"]="monitor-rgb";json["pointColor"]=section;
        {auto db=QSqlDatabase::addDatabase("QSQLITE","point-color-invalid");db.setDatabaseName(candidatePath);QVERIFY(db.open());QSqlQuery q(db);q.prepare("UPDATE photos SET adjustment_json=?");q.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));QVERIFY(q.exec());}QSqlDatabase::removeDatabase("point-color-invalid");
        const auto corrupt=bytes(candidatePath);QVector<ProjectDatabase::SavedPhoto> loaded;QVERIFY(!active.open(candidate.projectPath(),&loaded));QCOMPARE(active.projectPath(),current);QCOMPARE(bytes(candidatePath),corrupt);QCOMPARE(bytes(dbPath),original);
    }
    void realSonyDecodedCropRetainsSourceAndOutputProfiles() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixture configured; not a photo-quality pass");}
        const auto path=QDir(folder).filePath("sony_a7_iv_07.arw");const auto original=QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256);QVERIFY(!original.isEmpty());
        const auto loaded=loadSourceFresh(path,{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));QCOMPARE(loaded.image.format(),QImage::Format_RGBA64);
        const QRect roi((loaded.image.width()-1600)/2,(loaded.image.height()-1000)/2,1600,1000);const auto source=loaded.image.copy(roi);QVERIFY(!source.isNull());
        const auto pixelHash=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(source.constBits()),source.sizeInBytes()),QCryptographicHash::Sha256);
        auto state=edits();const auto root=qEnvironmentVariable("JIXELLIGHT_POINT_COLOR_EVIDENCE");if(!root.isEmpty())QVERIFY(QDir().mkpath(root));
        QJsonArray observations;QElapsedTimer timer;timer.start();
        for(int space=0;space<4;++space) {
            const auto p=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,loaded.metadata["rawBaseExposureStops"].toFloat());
            const auto before=ImagePipeline::processWithPlan(source,ProcessingPlan::compile({},p.encoding,p.output,true,p.baseExposureStops)),after=ImagePipeline::processWithPlan(source,p);QVERIFY(!before.isNull()&&!after.isNull());QVERIFY(before!=after);QCOMPARE(after.colorSpace(),ColorManagement::colorSpace(p.output));
            for(const QRect part : {QRect(0,0,137,139),QRect(719,431,149,157),QRect(1463,861,137,139)})QCOMPARE(ImagePipeline::processRegion(source,p,part),after.copy(part));
            const auto a=ScopesEngine::analyze(before),b=ScopesEngine::analyze(after);QCOMPARE(a.pixelCount,b.pixelCount);
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)QCOMPARE(reinterpret_cast<const QRgba64 *>(before.constScanLine(y))[x].alpha(),reinterpret_cast<const QRgba64 *>(after.constScanLine(y))[x].alpha());
            observations.append(QJsonObject{{"output_space",space},{"before",StageGraph::outputFingerprint(before)},{"after",StageGraph::outputFingerprint(after)},{"before_highlights_percent",a.highlightClipPercent},{"after_highlights_percent",b.highlightClipPercent}});
            if(!root.isEmpty()){QVERIFY(before.copy(608,308,384,384).save(QDir(root).filePath(QString("sony-crop-before-%1.png").arg(space))));QVERIFY(after.copy(608,308,384,384).save(QDir(root).filePath(QString("sony-crop-after-%1.png").arg(space))));}
        }
        QCOMPARE(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(source.constBits()),source.sizeInBytes()),QCryptographicHash::Sha256),pixelHash);QCOMPARE(QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256),original);
        qInfo()<<"Sony real decoded crop"<<source.size()<<"ISO"<<loaded.metadata["iso"]<<"four spaces elapsed ms"<<timer.elapsed();
        if(!root.isEmpty()){QFile report(QDir(root).filePath("sony-crop-report.json"));QVERIFY(report.open(QIODevice::WriteOnly));const QJsonObject result{{"source_sha256",QString::fromLatin1(original.toHex())},{"decoded_width",loaded.image.width()},{"decoded_height",loaded.image.height()},{"roi",QJsonArray{roi.x(),roi.y(),roi.width(),roi.height()}},{"metadata",QJsonObject::fromVariantMap(loaded.metadata)},{"elapsed_ms",timer.elapsed()},{"observations",observations},{"limits","one existing Sony ISO1600 scene/crop; decoded RGBA64, not native FP32 RAW; no broad ISO/camera/skin or blind quality acceptance"}};QVERIFY(report.write(QJsonDocument(result).toJson())>0);}
    }
    void realBilingualPanelControls_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("compact")<<300;
        QTest::newRow("production-minimum")<<320;
        QTest::newRow("wide")<<350;
    }
    void realBilingualPanelControls() {
        QFETCH(int,width);
        QTemporaryDir dir;const auto path=dir.filePath("source.png");QVERIFY(scene({800,600},false).convertToFormat(QImage::Format_RGBA64).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setLanguage("en_US");c.setViewport(200,150,1,0,.5,.5);
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        QVERIFY(c.selectPointColor(QColor::fromRgbF(.4,.3,.2)));QQmlEngine engine;engine.rootContext()->setContextProperty("photoController",&c);
        QQmlComponent host(&engine,QUrl::fromLocalFile(QFINDTESTDATA("fixtures/FilmGrainWindow.qml")));
        std::unique_ptr<QObject> hostObject(host.create());auto *window=qobject_cast<QQuickWindow *>(hostObject.get());
        QVERIFY2(window,qPrintable(host.errorString()));
        QQmlComponent component(&engine,QUrl::fromLocalFile(QFINDTESTDATA("../ui/DevelopRightPanel.qml")));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"controller",QVariant::fromValue(&c)},{"pointColorExpanded",true},{"basicExpanded",false},{"colorExpanded",false},{"grainExpanded",false},{"sharpenExpanded",false}}));
        QVERIFY2(object,qPrintable(component.errorString()));auto *item=qobject_cast<QQuickItem *>(object.get());QVERIFY(item);
        window->resize(width,780);item->setParentItem(window->contentItem());item->setSize(window->size());window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *lightness=object->findChild<QQuickItem *>("pointColorHueSlider"),*reset=object->findChild<QQuickItem *>("pointColorReset"),*toggle=object->findChild<QQuickItem *>("pointColorToggle");
        QVERIFY(lightness&&reset&&toggle);QTRY_VERIFY(lightness->width()>50&&lightness->isVisible());QVERIFY(toggle->property("text").toString().contains("POINT COLOR"));
        auto *flick=qvariant_cast<QQuickItem *>(object->property("brushToolFlick"));QVERIFY(flick);
        const auto sectionY=toggle->mapToItem(flick,{0,0}).y()+flick->property("contentY").toDouble();
        flick->setProperty("contentY",std::clamp(sectionY-10,0.0,std::max(0.0,flick->property("contentHeight").toDouble()-flick->height())));
        for(auto *slider : {lightness}) {
            const auto point=slider->mapToScene({slider->width()*.75,slider->height()*.5}).toPoint();QVERIFY(window->contentItem()->contains(point));
            QVERIFY(flick->contains(slider->mapToItem(flick,{slider->width()*.75,slider->height()*.5})));
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point);c.finishInteraction();
        }
        QTRY_VERIFY(c.pointColorHue()>25);
        const auto controlsWithinWindow=[&] {
            auto *section=object->findChild<QQuickItem *>("pointColorSection");if(!section)return false;
            for(auto *row : section->childItems())for(auto *control : row->childItems()) {
                if(!control->isVisible())continue;
                const auto left=control->mapToScene({0,0}).x(),right=control->mapToScene({control->width(),0}).x();
                if(left<0||right>window->width()+.5)return false;
            }
            return reset->mapToScene({reset->width(),0}).x()<=window->width()+.5;
        };
        QTRY_VERIFY(controlsWithinWindow());QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);
        const auto captureRoot=qEnvironmentVariable("JIXELLIGHT_POINT_COLOR_EVIDENCE");
        const auto capture=[&](const QString &language) {
            if(captureRoot.isEmpty())return true;
            return QDir().mkpath(captureRoot)&&!window->grabWindow().isNull()&&window->grabWindow().save(QDir(captureRoot).filePath(QString("pointColor-panel-%1-%2.png").arg(language).arg(width)));
        };
        QVERIFY(capture("en"));c.setLanguage("zh_CN");QTRY_VERIFY(toggle->property("text").toString().contains(QStringLiteral("点颜色")));QTRY_VERIFY(controlsWithinWindow());QVERIFY(capture("zh"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,lightness->mapToScene({lightness->width()*.25,lightness->height()*.5}).toPoint());c.finishInteraction();
        QTRY_VERIFY(c.pointColorHue()<-25);QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);QVERIFY(capture("zh-negative"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,reset->mapToScene({reset->width()*.5,reset->height()*.5}).toPoint());QTRY_COMPARE(c.pointColorHue(),0.0);
        c.undo();QVERIFY(c.pointColorHue()<-25);item->setParentItem(nullptr);
    }
    void twentyFourMegapixelOwnedPointColorWithSpatialBrush() {
        QElapsedTimer timer;timer.start();const auto source=scene({6000,4000},false);QVERIFY(!source.isNull());const auto hash=[&]{return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(source.constBits()),source.sizeInBytes()),QCryptographicHash::Sha256);};const auto before=hash();auto state=edits();state.denoiseLuma=83;state.denoiseChroma=91;state.dehazeAmount=73;state.textureAmount=65;state.clarityAmount=41;state.sharpenAmount=71;state.sharpenRadius=5.7;
        BrushMaskGeometry::Stroke paint;paint.style={.03,.43,.7};paint.dabs={{{.2,.3},.8},{{.7,.6},.5}};auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.2,.3},.4}};state.brushMasks.layers={{"large-point-color",true,.8,{paint,erase}}};auto budget=std::make_shared<BrushRenderBudget>();BrushRenderRequest::Spec spec;QString reason;
        auto whole=BrushRenderRequest::create(source,state,spec,{},&reason,budget);QVERIFY2(whole,qPrintable(reason));const auto reserved=whole->reservedBytes();auto lease=whole->render({},&reason);QVERIFY2(lease,qPrintable(reason));whole.reset();QCOMPARE(budget->snapshot().charged,lease.image().sizeInBytes());
        for(const QRect roi : {QRect(0,0,139,129),QRect(1117,1163,137,139),QRect(5863,3861,137,139)}){spec.region=roi;auto part=BrushRenderRequest::create(source,state,spec,{},&reason,budget);QVERIFY2(part,qPrintable(reason));const auto result=part->render({},&reason);QVERIFY2(result,qPrintable(reason));QCOMPARE(result.image(),lease.image().copy(roi));}
        QCOMPARE(hash(),before);lease={};QCOMPARE(budget->snapshot().charged,0);QVERIFY(budget->snapshot().peak<=BrushRenderBudget::DefaultBytes);QVERIFY(timer.elapsed()<60000);
        qInfo()<<"24MP FP32 Paint/Erase + denoise + dehaze + texture + clarity + Point Color + sharpen: elapsed ms"<<timer.elapsed()<<"whole reservation"<<reserved<<"peak owned bytes"<<budget->snapshot().peak;
    }
};
int main(int argc,char **argv) {
    QQuickStyle::setStyle("Fusion");QApplication app(argc,argv);QCoreApplication::setOrganizationName("JixelLightOwnedTests");QCoreApplication::setApplicationName("PointColorWorkflow");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QStandardPaths::setTestModeEnabled(true);PointColorWorkflowTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "PointColorWorkflowTests.moc"
