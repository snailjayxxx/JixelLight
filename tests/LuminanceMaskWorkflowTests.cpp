#include <QtTest>
#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QQuickStyle>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QImageReader>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QCryptographicHash>
#include <limits>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/cache/RenderedPreviewCache.h"
#include "core/commands/AdjustmentTransfer.h"
#include "core/commands/NamedPresets.h"
#include "core/commands/CommandRegistry.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/StageGraph.h"
#include "core/export/RasterExporter.h"
#include "core/metadata/XmpSidecar.h"
class LuminanceMaskWorkflowTests:public QObject {
    Q_OBJECT
    static QImage scene(QSize size={83,79}) {
        QImage image(size,QImage::Format_RGBA32FPx4);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            p[0]=-.02f+.4f*x/size.width();p[1]=.01f+.3f*y/size.height();p[2]=.1f;
            p[3]=(x+y)%7==0?0:(x+y)%3==0?.4f:1;
        }
        return image;
    }
    static AdjustmentState edits() {AdjustmentState s;s.luminanceMaskEnabled=true;s.luminanceMaskExposure=1.3;s.luminanceMaskLower=.04;s.luminanceMaskUpper=.2;s.luminanceMaskFeather=.03;return s;}
    static ProcessingPlan plan(const AdjustmentState &s={},int space=0) {return ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),false,0);}
    static QByteArray bytes(const QString &path) {QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
    static QByteArray pixels(const QImage &image) {QCryptographicHash h(QCryptographicHash::Sha256);for(int y=0;y<image.height();++y)h.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*image.depth()/8));return h.result();}
private slots:
    void strictOptionalSnapshotCommandsAndAtomicRange() {
        AdjustmentState s;QVERIFY(!s.toJson().contains("luminanceMask"));
        QVERIFY(CommandRegistry::execute(s,{{"command","mask.luminance.range"},{"lower",1.2},{"upper",1.7},{"feather",.1}}));
        QVERIFY(CommandRegistry::execute(s,{{"command","mask.luminance.enable"},{"enabled",true}}));
        QVERIFY(CommandRegistry::execute(s,{{"command","mask.luminance.set"},{"parameter","exposure"},{"value",1.2}}));
        const auto saved=s.toJson();QVERIFY(AdjustmentState::validLuminanceMaskJson(saved));QCOMPARE(AdjustmentState::fromJson(saved).toJson(),saved);
        const auto section=saved["luminanceMask"].toObject();
        for(const auto &key:section.keys()) {auto bad=saved;auto m=section;m.remove(key);bad["luminanceMask"]=m;QVERIFY(!AdjustmentState::validLuminanceMaskJson(bad));}
        for(const auto &key:QStringList{"schema","future","enabled","upper"}) {auto bad=saved;auto m=section;m[key]=key=="upper"?QJsonValue(.1):QJsonValue(2);bad["luminanceMask"]=m;QVERIFY(!AdjustmentState::validLuminanceMaskJson(bad));}
        for(const auto command:{QJsonObject{{"command","mask.luminance.enable"},{"enabled",1}},QJsonObject{{"command","mask.luminance.set"},{"parameter","upper"},{"value",.2}},QJsonObject{{"command","mask.luminance.range"},{"lower",1.8},{"upper",1.7},{"feather",.1}},QJsonObject{{"command","mask.luminance.reset"},{"extra",true}}}) {QVERIFY(!CommandRegistry::execute(s,command));QCOMPARE(s.toJson(),saved);}
        QVERIFY(!CommandRegistry::executeBatch(s,{QJsonObject{{"command","mask.luminance.reset"}},QJsonObject{{"command","mask.luminance.set"},{"parameter","feather"},{"value",0}}}));QCOMPARE(s.toJson(),saved);
        s.exposure=.4;QVERIFY(CommandRegistry::execute(s,{{"command","mask.luminance.reset"}}));QVERIFY(!s.toJson().contains("luminanceMask"));QCOMPARE(s.exposure,.4);
    }
    void packedPrefixNeutralIdentityAndInvalidPlans() {
        const auto source=scene(),identity=ImagePipeline::processWithPlan(source,plan());QVERIFY(!identity.isNull());
        auto s=edits();s.luminanceMaskEnabled=false;QCOMPARE(ImagePipeline::processWithPlan(source,plan(s)),identity);
        s.luminanceMaskEnabled=true;s.luminanceMaskExposure=0;QCOMPARE(ImagePipeline::processWithPlan(source,plan(s)),identity);
        const auto base=plan(),active=plan(edits());QVERIFY(active.validLuminanceMask());QVERIFY(active.luminanceMaskActive());
        QCOMPARE(sizeof(base.data),std::size_t(1040));for(int i=0;i<61;++i)QCOMPARE(QByteArrayView(reinterpret_cast<const char *>(&base.data[i]),16),QByteArrayView(reinterpret_cast<const char *>(&active.data[i]),16));
        for(int slot=61;slot<63;++slot) {auto bad=base;bad.data[slot].y=1;QVERIFY(!bad.validLuminanceMask());QVERIFY(ImagePipeline::processWithPlan(source,bad).isNull());}
        s=edits();s.luminanceMaskLower=1.5;QVERIFY(!plan(s).validLuminanceMask());QVERIFY(ImagePipeline::processWithPlan(source,plan(s)).isNull());
    }
    void analyticSceneGainSelectionPrecedesWhiteBalanceAndExposure() {
        QImage source(5,4,QImage::Format_RGBA32FPx4);
        for(int y=0;y<4;++y)for(int x=0;x<5;++x) {auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.1f;p[1]=.1f;p[2]=.1f;p[3]=x==0?0:x==1?.4f:1;}
        for(double global:{-2.,0.,2.})for(bool inverted:{false,true}) {
            auto s=edits();s.exposure=global;s.temperature=33;s.tint=-21;s.luminanceMaskLower=.08;s.luminanceMaskUpper=.12;s.luminanceMaskFeather=.01;s.luminanceMaskExposure=1;s.luminanceMaskInverted=inverted;
            s.linearMaskEnabled=true;s.linearMaskExposure=.2;s.radialMaskEnabled=true;s.radialMaskExposure=-.3;s.vignetteAmount=-.2;
            const auto actual=ImagePipeline::processWithPlan(source,plan(s));auto expectedSource=source.copy();
            if(!inverted)for(int y=0;y<4;++y)for(int x=0;x<5;++x)for(int c=0;c<3;++c)reinterpret_cast<float *>(expectedSource.scanLine(y))[4*x+c]*=2;
            s.luminanceMaskEnabled=false;const auto expected=ImagePipeline::processWithPlan(expectedSource,plan(s));QCOMPARE(pixels(actual),pixels(expected));
        }
    }
    void fourSpacesCompleteScopesExportsAndRegionsAgree() {
        const auto source=scene();const auto original=pixels(source);auto s=edits();s.geometry.crop={.1,.1,.8,.8};s.geometry.quarterTurns=1;s.denoiseLuma=63;s.denoiseChroma=71;s.dehazeAmount=41;s.textureAmount=35;s.clarityAmount=-23;s.sharpenAmount=61;
        QTemporaryDir dir;QString error;
        for(int space=0;space<4;++space) {const auto corrected=s.geometry.apply(source);const auto p=plan(s,space);const auto expected=ImagePipeline::processWithPlan(corrected,p);QVERIFY(!expected.isNull());
            for(const auto &roi:{QRect(0,0,19,21),QRect(15,13,27,23),QRect(corrected.width()-19,corrected.height()-21,19,21)})QCOMPARE(pixels(ImagePipeline::processRegion(corrected,p,roi)),pixels(expected.copy(roi)));
            const auto a=ScopesEngine::analyzeFull(corrected,p),b=ScopesEngine::analyze(expected);QCOMPARE(a.red,b.red);QCOMPARE(a.green,b.green);QCOMPARE(a.blue,b.blue);QCOMPARE(a.luma,b.luma);QCOMPARE(a.pixelCount,b.pixelCount);
            const auto path=dir.filePath(QString::number(space)+".png");QVERIFY2(exportRaster(source,s,path,ColorManagement::OutputSpace(space),RasterFormat::Png16,100,{},&error,false,0),qPrintable(error));const QImage decoded(path);QCOMPARE(pixels(decoded.convertToFormat(QImage::Format_RGBA64)),pixels(expected));QCOMPARE(decoded.colorSpace().iccProfile(),ColorManagement::colorSpace(ColorManagement::OutputSpace(space)).iccProfile());
            auto off=s;off.luminanceMaskEnabled=false;const auto neutral=ImagePipeline::processWithPlan(corrected,plan(off,space));
            for(int y=0;y<expected.height();++y)for(int x=0;x<expected.width();++x)QCOMPARE(reinterpret_cast<const QRgba64 *>(expected.constScanLine(y))[x].alpha(),reinterpret_cast<const QRgba64 *>(neutral.constScanLine(y))[x].alpha());
        }
        QCOMPARE(pixels(source),original);
    }
    void downstreamMaskControlsReuseCompletedSpatialWork() {
        const auto source=scene();auto s=edits();s.denoiseLuma=71;s.denoiseChroma=83;s.dehazeAmount=53;s.textureAmount=61;s.clarityAmount=37;RenderedPreviewCache cache;
        QCOMPARE(pixels(cache.render(source,plan(s))),pixels(ImagePipeline::processWithPlan(source,plan(s))));const auto initial=cache.snapshot();
        for(int i=0;i<4;++i) {s.luminanceMaskExposure=-1.1+.5*i;s.luminanceMaskLower=.02*i;s.luminanceMaskUpper=.15+.02*i;s.luminanceMaskFeather=.01+.02*i;s.luminanceMaskInverted=i%2;const auto p=plan(s,i);QCOMPARE(pixels(cache.render(source,p)),pixels(ImagePipeline::processWithPlan(source,p)));}
        const auto final=cache.snapshot();for(const auto *prefix:{"denoise","dehaze","texture","clarity"}) {const auto key=QString(prefix)+"_stage_";QCOMPARE(final[key+"misses"],initial[key+"misses"]);QCOMPARE(final[key+"hits"].toInt(),initial[key+"hits"].toInt()+4);}
        QCOMPARE(final["budget_bytes"].toInteger(),64LL*1024*1024);QVERIFY(final["charged_bytes"].toInteger()<=64LL*1024*1024);
    }
    void maskTransferPresetsAndXmpPreserveIndependentState() {
        auto s=edits();s.exposure=.7;AdjustmentState target;target.temperature=19;QVERIFY(AdjustmentTransfer::apply(target,s,{"masks"}));QCOMPARE(target.luminanceMaskExposure,s.luminanceMaskExposure);QCOMPARE(target.exposure,0.0);QCOMPARE(target.temperature,19.0);
        auto bad=s;bad.luminanceMaskLower=std::numeric_limits<double>::quiet_NaN();const auto before=target.toJson();QVERIFY(!AdjustmentTransfer::apply(target,bad,{"masks"}));QCOMPARE(target.toJson(),before);
        QTemporaryDir dir;QString error;NamedPresets presets(dir.filePath("presets.json"));QVERIFY(presets.load(&error));QVERIFY(presets.save("Luminance mask",s,&error));AdjustmentState loaded;QVERIFY(presets.get("Luminance mask",&loaded));QCOMPARE(loaded.toJson(),s.toJson());
        const auto path=dir.filePath("mask.xmp");QVERIFY2(XmpSidecar::writeNew(path,s,{},0,"none",&error),qPrintable(error));XmpSidecar::Document decoded;QVERIFY2(XmpSidecar::read(path,&decoded,&error),qPrintable(error));QCOMPARE(decoded.adjustments.toJson(),s.toJson());QVERIFY(!presets.save("Invalid",bad,&error));
    }
    void controllerHistoryCopiesProjectAndFailureAreAtomic() {
        QTemporaryDir dir;const auto path=dir.filePath("source.png");QVERIFY(scene({400,300}).convertToFormat(QImage::Format_RGBA64).save(path));const auto original=bytes(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setViewport(200,150,1,0,.5,.5);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        QVERIFY(c.setLuminanceMaskEnabled(true));c.finishInteraction();const auto count=c.editHistory().size();for(double v:{.2,.5,1.2})QVERIFY(c.setLuminanceMaskParameter("exposure",v));c.finishInteraction();QCOMPARE(c.editHistory().size(),count+1);QCOMPARE(c.editHistory().last().toMap()["action"].toString(),QString("luminance_mask_adjustment"));
        QVERIFY(c.setLuminanceMaskRange(.05,.2,.03));c.finishInteraction();QVERIFY(c.setLuminanceMaskInverted(true));c.setExposure(.3);c.finishInteraction();const auto saved=c.gpuPlan(true).state.toJson();const auto history=c.editHistory();
        QVERIFY(!c.setLuminanceMaskRange(.8,.2,.03));QCOMPARE(c.gpuPlan(true).state.toJson(),saved);QCOMPARE(c.editHistory(),history);
        QVERIFY(c.resetLuminanceMask());QVERIFY(!c.gpuPlan(true).state.toJson().contains("luminanceMask"));QCOMPARE(c.exposure(),.3);c.undo();QCOMPARE(c.gpuPlan(true).state.toJson(),saved);c.redo();c.undo();
        QVERIFY(c.createVirtualCopy("Masked copy"));QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Luminance"));QVERIFY(c.flushEdits());PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);reopened.selectPhoto(0);QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);QCOMPARE(bytes(path),original);
    }
    void malformedProjectCannotReplaceOrRewriteTheActiveCatalog() {
        QTemporaryDir dir;ProjectDatabase active;QVERIFY(active.create(dir.path(),"Active"));const auto current=active.projectPath();QVERIFY(active.addOrUpdatePhoto("source.png",edits()));QVERIFY(active.flush());const auto dbPath=QDir(current).filePath("Project.db");const auto original=bytes(dbPath);
        auto bad=edits();bad.luminanceMaskLower=.9;QVERIFY(!active.updateBatch({{"source.png",bad}}));QVERIFY(active.flush());QCOMPARE(bytes(dbPath),original);
        ProjectDatabase candidate;QVERIFY(candidate.create(dir.path(),"Invalid mask"));QVERIFY(candidate.addOrUpdatePhoto("source.png",edits()));QVERIFY(candidate.flush());const auto candidatePath=QDir(candidate.projectPath()).filePath("Project.db");
        auto json=edits().toJson();auto section=json["luminanceMask"].toObject();section["feather"]=0;json["luminanceMask"]=section;
        {auto db=QSqlDatabase::addDatabase("QSQLITE","luminance-invalid");db.setDatabaseName(candidatePath);QVERIFY(db.open());QSqlQuery q(db);q.prepare("UPDATE photos SET adjustment_json=?");q.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));QVERIFY(q.exec());}QSqlDatabase::removeDatabase("luminance-invalid");
        const auto corrupt=bytes(candidatePath);QVector<ProjectDatabase::SavedPhoto> loaded;QVERIFY(!active.open(candidate.projectPath(),&loaded));QCOMPARE(active.projectPath(),current);QCOMPARE(bytes(candidatePath),corrupt);QCOMPARE(bytes(dbPath),original);
    }
    void realSonyCropHasExactTilesScopesAndUnchangedSource() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");if(folder.isEmpty())QSKIP("JIXELLIGHT_SONY_FIXTURES is not set");
        const auto path=QDir(folder).filePath("sony_a7_iv_07.arw");const auto original=QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256);QCOMPARE(original.toHex(),QByteArray("aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d"));
        const auto loaded=loadSourceFresh(path,{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));const auto source=loaded.image.copy(1024,768,1600,1000);QCOMPARE(source.size(),QSize(1600,1000));const auto inputHash=pixels(source);auto s=edits();s.luminanceMaskLower=.02;s.luminanceMaskUpper=.18;s.luminanceMaskFeather=.015;
        const auto root=qEnvironmentVariable("JIXELLIGHT_LUMINANCE_MASK_EVIDENCE");if(!root.isEmpty())QVERIFY(QDir().mkpath(root));QJsonArray observations;
        for(int space=0;space<4;++space) {const auto p=ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,loaded.metadata["rawBaseExposureStops"].toFloat());const auto after=ImagePipeline::processWithPlan(source,p);auto off=p;off.data[ProcessingPlan::LuminanceMask]={};off.data[ProcessingPlan::LuminanceMaskOptions]={};off.state.luminanceMaskEnabled=false;const auto before=ImagePipeline::processWithPlan(source,off);QVERIFY(!after.isNull()&&pixels(after)!=pixels(before));
            for(const auto &roi:{QRect(0,0,137,139),QRect(719,431,149,157),QRect(1463,861,137,139)})QCOMPARE(pixels(ImagePipeline::processRegion(source,p,roi)),pixels(after.copy(roi)));
            const auto a=ScopesEngine::analyzeFull(source,p),b=ScopesEngine::analyze(after);QCOMPARE(a.red,b.red);QCOMPARE(a.green,b.green);QCOMPARE(a.blue,b.blue);QCOMPARE(a.luma,b.luma);
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)QCOMPARE(reinterpret_cast<const QRgba64 *>(after.constScanLine(y))[x].alpha(),reinterpret_cast<const QRgba64 *>(source.constScanLine(y))[x].alpha());
            observations.append(QJsonObject{{"output_space",space},{"before_sha256",QString::fromLatin1(pixels(before).toHex())},{"after_sha256",QString::fromLatin1(pixels(after).toHex())}});
            if(!root.isEmpty()){QVERIFY(before.copy(608,308,384,384).save(QDir(root).filePath(QString("sony-before-%1.png").arg(space))));QVERIFY(after.copy(608,308,384,384).save(QDir(root).filePath(QString("sony-after-%1.png").arg(space))));}
        }
        QCOMPARE(pixels(source),inputHash);QCOMPARE(QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256),original);
        if(!root.isEmpty()){QFile report(QDir(root).filePath("sony-crop-report.json"));QVERIFY(report.open(QIODevice::WriteOnly));QVERIFY(report.write(QJsonDocument(QJsonObject{{"source_sha256",QString::fromLatin1(original.toHex())},{"decoded_width",loaded.image.width()},{"decoded_height",loaded.image.height()},{"iso",loaded.metadata["iso"].toInt()},{"observations",observations},{"native_fp32_RAW",false},{"broad_quality_acceptance",false}}).toJson())>0);}
    }
    void realBilingualPanelControls_data() {
        QTest::addColumn<int>("width");QTest::newRow("compact")<<300;QTest::newRow("minimum")<<320;QTest::newRow("wide")<<350;
    }
    void realBilingualPanelControls() {
        QFETCH(int,width);QTemporaryDir dir;const auto path=dir.filePath("source.png");QVERIFY(scene({400,300}).convertToFormat(QImage::Format_RGBA64).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setLanguage("en_US");c.setViewport(200,150,1,0,.5,.5);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        QQmlEngine engine;engine.rootContext()->setContextProperty("photoController",&c);QQmlComponent host(&engine,QUrl::fromLocalFile(QFINDTESTDATA("fixtures/FilmGrainWindow.qml")));std::unique_ptr<QObject> hostObject(host.create());auto *window=qobject_cast<QQuickWindow *>(hostObject.get());QVERIFY2(window,qPrintable(host.errorString()));
        QQmlComponent component(&engine,QUrl::fromLocalFile(QFINDTESTDATA("../ui/DevelopRightPanel.qml")));QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"controller",QVariant::fromValue(&c)},{"luminanceMaskExpanded",true},{"basicExpanded",false},{"colorExpanded",false}}));QVERIFY2(object,qPrintable(component.errorString()));auto *item=qobject_cast<QQuickItem *>(object.get());QVERIFY(item);
        window->resize(width,780);item->setParentItem(window->contentItem());item->setSize(window->size());window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *toggle=object->findChild<QQuickItem *>("luminanceMaskToggle"),*enable=object->findChild<QQuickItem *>("luminanceMaskEnable"),*exposure=object->findChild<QQuickItem *>("luminanceMaskExposureSlider"),*reset=object->findChild<QQuickItem *>("luminanceMaskReset"),*invert=object->findChild<QQuickItem *>("luminanceMaskInvert");QVERIFY(toggle&&enable&&exposure&&reset&&invert);
        auto *flick=qvariant_cast<QQuickItem *>(object->property("brushToolFlick"));QVERIFY(flick);
        const auto reveal=[&] {const double y=toggle->mapToItem(flick,{0,0}).y()+flick->property("contentY").toDouble();flick->setProperty("contentY",std::clamp(y-10,0.,std::max(0.,flick->property("contentHeight").toDouble()-flick->height())));};
        QTRY_VERIFY(exposure->width()>50);reveal();
        const auto click=[&](QQuickItem *control,double x){const auto point=control->mapToScene({control->width()*x,control->height()*.5}).toPoint();QVERIFY(flick->contains(control->mapToItem(flick,{control->width()*x,control->height()*.5})));QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point);c.finishInteraction();};
        click(enable,.1);QTRY_VERIFY(c.luminanceMask().value("enabled").toBool());click(exposure,.75);QTRY_VERIFY(c.luminanceMask().value("exposure").toDouble()>.5);
        const auto within=[&] {auto *section=object->findChild<QQuickItem *>("luminanceMaskSection");if(!section)return false;for(auto *row:section->childItems())for(auto *control:row->childItems()) {if(!control->isVisible())continue;const auto a=control->mapToScene({0,0}),b=control->mapToScene({control->width(),control->height()});if(a.x()<-.5||b.x()>window->width()+.5)return false;}return true;};
        QTRY_VERIFY(within());QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);
        const auto capture=[&](const QString &language) {const auto root=qEnvironmentVariable("JIXELLIGHT_LUMINANCE_MASK_EVIDENCE");return root.isEmpty()||(QDir().mkpath(root)&&window->grabWindow().save(QDir(root).filePath(QString("luminance-panel-%1-%2.png").arg(language).arg(width))));};
        QVERIFY(capture("en"));c.setLanguage("zh_CN");QTRY_VERIFY(toggle->property("text").toString().contains(QStringLiteral("亮度范围")));reveal();QTRY_VERIFY(within());QVERIFY(capture("zh"));
        click(invert,.1);QTRY_VERIFY(c.luminanceMask().value("inverted").toBool());click(reset,.5);QTRY_VERIFY(!c.luminanceMask().value("enabled").toBool());c.undo();QVERIFY(c.luminanceMask().value("inverted").toBool());item->setParentItem(nullptr);
    }
    void twentyFourMegapixelBrushAndSpatialStackStayOwnedAndTileExact() {
        QElapsedTimer timer;timer.start();const auto source=scene({6000,4000});const auto original=pixels(source);auto s=edits();s.denoiseLuma=83;s.denoiseChroma=91;s.dehazeAmount=73;s.textureAmount=65;s.clarityAmount=41;s.pointColorEnabled=true;s.pointColorHue=27;s.pointColorHueRange=180;s.pointColorChromaRange=1;s.pointColorLightnessRange=1;s.sharpenAmount=71;s.sharpenRadius=5.7;
        BrushMaskGeometry::Stroke paint;paint.style={.03,.43,.7};paint.dabs={{{.2,.3},.8},{{.7,.6},.5}};auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.2,.3},.4}};s.brushMasks.layers={{"large-luminance",true,.8,{paint,erase}}};
        auto budget=std::make_shared<BrushRenderBudget>();BrushRenderRequest::Spec spec;QString error;auto request=BrushRenderRequest::create(source,s,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));const auto reservation=request->reservedBytes();auto lease=request->render({},&error);QVERIFY2(lease,qPrintable(error));request.reset();QCOMPARE(budget->snapshot().charged,lease.image().sizeInBytes());
        for(const auto &roi:{QRect(0,0,139,129),QRect(1117,1163,137,139),QRect(5863,3861,137,139)}) {spec.region=roi;auto part=BrushRenderRequest::create(source,s,spec,{},&error,budget);QVERIFY2(part,qPrintable(error));auto actual=part->render({},&error);QVERIFY2(actual,qPrintable(error));QCOMPARE(pixels(actual.image()),pixels(lease.image().copy(roi)));}
        QCOMPARE(pixels(source),original);lease={};QCOMPARE(budget->snapshot().charged,0);QVERIFY(budget->snapshot().peak<=BrushRenderBudget::DefaultBytes);QVERIFY(timer.elapsed()<60000);
        qInfo()<<"24MP FP32 Paint/Erase + spatial stack + Point Color + luminance mask + sharpen ms"<<timer.elapsed()<<"whole reservation"<<reservation<<"peak owned bytes"<<budget->snapshot().peak;
    }
};
int main(int argc,char **argv) {
    QQuickStyle::setStyle("Fusion");QApplication app(argc,argv);QCoreApplication::setOrganizationName("JixelLightOwnedTests");QCoreApplication::setApplicationName("LuminanceMaskWorkflow");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QStandardPaths::setTestModeEnabled(true);LuminanceMaskWorkflowTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "LuminanceMaskWorkflowTests.moc"
