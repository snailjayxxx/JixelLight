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
class ColorMaskWorkflowTests:public QObject {
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
    static AdjustmentState edits() {AdjustmentState s;s.colorMaskEnabled=true;s.colorMaskExposure=1.3;s.colorMaskLab={.6,.02,.01};s.colorMaskHueRange=180;s.colorMaskChromaRange=1;s.colorMaskLightnessRange=1;return s;}
    static ProcessingPlan plan(const AdjustmentState &s={},int space=0) {return ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),false,0);}
    static QByteArray bytes(const QString &path) {QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
    static QByteArray pixels(const QImage &image) {QCryptographicHash h(QCryptographicHash::Sha256);for(int y=0;y<image.height();++y)h.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*image.depth()/8));return h.result();}
private slots:
    void analyticColorWeightGainAndInversion() {
        ColorRangeMask::Parameters p;p.selection.enabled=true;p.exposure=1;QVERIFY(ColorRangeMask::valid(p));
        QCOMPARE(ColorRangeMask::gain({.6f,.2f,0},p),2.f);QCOMPARE(ColorRangeMask::gain({2,0,0},p),1.f);
        QVERIFY(std::abs(ColorRangeMask::gain({.9f,.2f,0},p)-std::sqrt(2.f))<1.e-6f);
        p.inverted=true;p.exposure=-2;QCOMPARE(ColorRangeMask::gain({.6f,.2f,0},p),1.f);QCOMPARE(ColorRangeMask::gain({2,0,0},p),.25f);
        p.selection.enabled=false;QCOMPARE(ColorRangeMask::gain({2,0,0},p),1.f);p.selection.enabled=true;p.exposure=0;QCOMPARE(ColorRangeMask::gain({2,0,0},p),1.f);
        p.exposure=1;QCOMPARE(ColorRangeMask::gain({std::numeric_limits<float>::quiet_NaN(),0,0},p),1.f);
    }
    void strictCanonicalSelectionAndAtomicCommands() {
        AdjustmentState s;QVERIFY(!s.toJson().contains("colorMask"));QVERIFY(CommandRegistry::execute(s,{{"command","mask.color.select.lab"},{"lab",QJsonArray{.6,.02,.01}}}));QCOMPARE(s.colorMaskLab,(std::array<double,3>{.6,.02,.01}));
        QVERIFY(CommandRegistry::execute(s,{{"command","mask.color.set"},{"parameter","exposure"},{"value",1.3}}));const auto saved=s.toJson();QVERIFY(AdjustmentState::validColorMaskJson(saved));QCOMPARE(AdjustmentState::fromJson(saved).toJson(),saved);
        const auto section=saved["colorMask"].toObject();for(const auto &key:section.keys()){auto bad=saved;auto m=section;m.remove(key);bad["colorMask"]=m;QVERIFY(!AdjustmentState::validColorMaskJson(bad));}
        for(const auto &key:QStringList{"space","schema","future","enabled"}){auto bad=saved;auto m=section;m[key]=key=="space"?QJsonValue("monitor-rgb"):QJsonValue(2);bad["colorMask"]=m;QVERIFY(!AdjustmentState::validColorMaskJson(bad));}
        for(const auto command:{QJsonObject{{"command","mask.color.select"},{"color",QJsonArray{true,0,0}}},QJsonObject{{"command","mask.color.select.lab"},{"lab",QJsonArray{0,0,5}}},QJsonObject{{"command","mask.color.set"},{"parameter","hueRange"},{"value",0}},QJsonObject{{"command","mask.color.invert"},{"inverted",1}}}){QVERIFY(!CommandRegistry::execute(s,command));QCOMPARE(s.toJson(),saved);}
        QVERIFY(!CommandRegistry::executeBatch(s,{QJsonObject{{"command","mask.color.reset"}},QJsonObject{{"command","mask.color.set"},{"parameter","chromaRange"},{"value",0}}}));QCOMPARE(s.toJson(),saved);
        s.exposure=.4;QVERIFY(CommandRegistry::execute(s,{{"command","mask.color.reset"}}));QVERIFY(!s.toJson().contains("colorMask"));QCOMPARE(s.exposure,.4);
    }
    void neutralIdentityPrefixAndInvalidPackedPlans() {
        const auto source=scene();const auto identity=ImagePipeline::processWithPlan(source,plan());auto s=edits();s.colorMaskEnabled=false;QCOMPARE(pixels(ImagePipeline::processWithPlan(source,plan(s))),pixels(identity));s.colorMaskEnabled=true;s.colorMaskExposure=0;QCOMPARE(pixels(ImagePipeline::processWithPlan(source,plan(s))),pixels(identity));
        const auto base=plan(),active=plan(edits());QCOMPARE(sizeof(base.data),std::size_t(1040));QVERIFY(active.validColorMask());for(int i=0;i<63;++i)QCOMPARE(QByteArrayView(reinterpret_cast<const char *>(&base.data[i]),16),QByteArrayView(reinterpret_cast<const char *>(&active.data[i]),16));
        for(int slot=63;slot<65;++slot){auto bad=base;bad.data[slot].y=1;QVERIFY(!bad.validColorMask());QVERIFY(ImagePipeline::processWithPlan(source,bad).isNull());}
    }
    void narrowSelectionPrecisionPolicyMatchesControllerAndNeutralBypass() {
        QTemporaryDir dir;const auto path=dir.filePath("source.png");QVERIFY(scene().convertToFormat(QImage::Format_RGBA64).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setViewport(40,30,1,0,.5,.5);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        c.setColorMaskEnabled(true);c.setColorMaskParameter("exposure",1);
        for(double radius:{1.,2.,3.,5.,10.,20.,44.9,45.,180.}){c.setColorMaskParameter("hueRange",radius);const bool expected=radius<45;QCOMPARE(c.colorMaskCpuPrecision(),expected);QCOMPARE(c.gpuPlan(false).colorMaskNeedsCpuPrecision(),expected);}
        for(const auto key:{"chromaRange","lightnessRange"}){c.setColorMaskParameter(key,.001);QVERIFY(c.colorMaskCpuPrecision());c.setColorMaskParameter(key,.003);QVERIFY(!c.colorMaskCpuPrecision());}
        c.setColorMaskParameter("hueRange",1);c.setColorMaskParameter("exposure",0);QVERIFY(!c.colorMaskCpuPrecision());c.setColorMaskParameter("exposure",1);c.setColorMaskEnabled(false);QVERIFY(!c.colorMaskCpuPrecision());
    }
    void sourceSamplerIgnoresColorExposureAndOutputEdits() {
        QImage source(1,1,QImage::Format_RGBA32FPx4);auto *q=reinterpret_cast<float *>(source.bits());q[0]=.2f;q[1]=.4f;q[2]=.7f;q[3]=1;const auto expected=PointColor::fromEncodedSrgb({.2f,.4f,.7f});
        for(int output=0;output<4;++output){auto s=edits();s.exposure=2;s.temperature=53;s.tint=-41;s.hue=79;s.pointColorEnabled=true;s.pointColorHue=77;s.luminanceMaskEnabled=true;s.luminanceMaskExposure=-2;s.vignetteAmount=-1.3;s.linearMaskEnabled=true;s.linearMaskExposure=1.8;
            auto p=ProcessingPlan::compile(s,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output),false,0);const auto actual=ImagePipeline::sampleMaskColor(source,p,{0,0});QVERIFY(actual);QVERIFY(std::abs(actual->L-expected.L)<2.e-6f);QVERIFY(std::abs(actual->a-expected.a)<2.e-6f);QVERIFY(std::abs(actual->b-expected.b)<2.e-6f);
        }
        auto cancelled=std::make_shared<std::atomic_bool>(true);QVERIFY(!ImagePipeline::sampleMaskColor(source,plan(),{0,0},cancelled));QVERIFY(!ImagePipeline::sampleMaskColor(source,plan(),{-1,0}));q[3]=0;QVERIFY(!ImagePipeline::sampleMaskColor(source,plan(),{0,0}));
    }
    void colorAndLuminanceSelectionsUseTheSameInputBeforeEitherGain() {
        QImage source(4,3,QImage::Format_RGBA32FPx4);for(int y=0;y<3;++y)for(int x=0;x<4;++x){auto *q=reinterpret_cast<float *>(source.scanLine(y))+4*x;q[0]=q[1]=q[2]=.1f;q[3]=x==0?0:x==1?.4f:1;}
        auto base=edits();const auto lab=ImagePipeline::sampleMaskColor(source,plan(),{2,1});QVERIFY(lab);base.colorMaskLab={lab->L,lab->a,lab->b};base.colorMaskHueRange=180;base.colorMaskChromaRange=1;base.colorMaskLightnessRange=1;base.colorMaskExposure=1;base.luminanceMaskEnabled=true;base.luminanceMaskExposure=1;base.luminanceMaskLower=.08;base.luminanceMaskUpper=.12;base.luminanceMaskFeather=.01;
        for(double ev:{-2.,0.,2.}){auto s=base;s.exposure=ev;s.temperature=33;s.tint=-21;const auto actual=ImagePipeline::processWithPlan(source,plan(s));auto ref=source.copy();for(int y=0;y<3;++y)for(int x=0;x<4;++x)for(int c=0;c<3;++c)reinterpret_cast<float *>(ref.scanLine(y))[4*x+c]*=4;
            s.colorMaskEnabled=false;s.luminanceMaskEnabled=false;QCOMPARE(pixels(actual),pixels(ImagePipeline::processWithPlan(ref,plan(s))));}
    }
    void colorMaskEditsReuseEveryCompletedSpatialStage() {
        const auto source=scene();auto s=edits();s.denoiseLuma=71;s.denoiseChroma=83;s.dehazeAmount=53;s.textureAmount=61;s.clarityAmount=37;RenderedPreviewCache cache;
        QCOMPARE(pixels(cache.render(source,plan(s))),pixels(ImagePipeline::processWithPlan(source,plan(s))));const auto before=cache.snapshot();
        for(int i=0;i<4;++i){s.colorMaskExposure=-1.1+i*.5;s.colorMaskLab[0]=.4+i*.1;s.colorMaskInverted=i%2;s.colorMaskHueRange=5+i*30;s.colorMaskChromaRange=.1+i*.1;s.colorMaskLightnessRange=.2+i*.1;const auto p=plan(s,i);QCOMPARE(pixels(cache.render(source,p)),pixels(ImagePipeline::processWithPlan(source,p)));}
        const auto after=cache.snapshot();for(const auto *prefix:{"denoise","dehaze","texture","clarity"}){const auto key=QString(prefix)+"_stage_";QCOMPARE(after[key+"misses"],before[key+"misses"]);QCOMPARE(after[key+"hits"].toInt(),before[key+"hits"].toInt()+4);}
        QVERIFY(after["charged_bytes"].toInteger()<=64LL*1024*1024);
    }
    void asynchronousPickerKeepsItsTargetAndRejectsLaterEdits() {
        QTemporaryDir dir;const auto path=dir.filePath("source.png");QVERIFY(scene({400,300}).convertToFormat(QImage::Format_RGBA64).save(path));ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setViewport(200,150,1,0,.5,.5);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        const auto pixel=QPoint(c.gpuSource().width()/2,c.gpuSource().height()/2);const auto reference=ImagePipeline::sampleMaskColor(c.gpuSource(),c.gpuPlan(),pixel);QVERIFY(reference);const auto count=c.editHistory().size();QVERIFY(c.pickColorMask(100,75,200,150));QTRY_VERIFY_WITH_TIMEOUT(c.colorMask().value("enabled").toBool(),10000);
        QCOMPARE(c.gpuPlan(true).state.colorMaskLab,(std::array<double,3>{reference->L,reference->a,reference->b}));QVERIFY(!c.pointColorEnabled());QCOMPARE(c.editHistory().size(),count+1);QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);
        const auto center=c.gpuPlan(true).state.colorMaskLab;QVERIFY(c.pickColorMask(60,40,200,150));QVERIFY(c.setColorMaskParameter("exposure",.7));c.finishInteraction();QTest::qWait(200);QCOMPARE(c.gpuPlan(true).state.colorMaskLab,center);
        QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);QVERIFY(c.pickPointColor(100,75,200,150));c.requestColorMaskPicker();QTest::qWait(200);QVERIFY(!c.pointColorEnabled());
        QVERIFY(c.pickColorMask(60,40,200,150));c.requestPointColorPicker();QTest::qWait(200);QCOMPARE(c.gpuPlan(true).state.colorMaskLab,center);
        const QColor chosen(73,131,192);QVERIFY(c.selectColorMask(chosen));QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);const auto manual=c.gpuPlan(true).state.colorMaskLab;QVERIFY(c.pickColorMask(60,40,200,150));QVERIFY(c.selectColorMask(chosen));QTest::qWait(200);QCOMPARE(c.gpuPlan(true).state.colorMaskLab,manual);
        QVERIFY(c.selectPointColor(chosen));QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);QVERIFY(c.pickColorMask(60,40,200,150));QVERIFY(c.selectPointColor(chosen));QTest::qWait(200);QCOMPARE(c.gpuPlan(true).state.colorMaskLab,manual);
        QVERIFY(c.pickColorMask(60,40,200,150));const auto second=dir.filePath("second.png");QVERIFY(scene({401,301}).convertToFormat(QImage::Format_RGBA64).save(second));QVERIFY(c.importFile(QUrl::fromLocalFile(second)));c.selectPhoto(1);QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);QTest::qWait(200);QVERIFY(!c.colorMask().value("enabled").toBool());
        QVERIFY(!c.pickColorMask(-1,2,200,150));QVERIFY(!c.pickColorMask(2,2,0,150));
    }
    void masksTransferPresetsXmpAndProjectHistoryPreserveColorIntent() {
        auto s=edits();s.exposure=.7;s.luminanceMaskEnabled=true;s.luminanceMaskExposure=-.5;AdjustmentState target;target.temperature=19;QVERIFY(AdjustmentTransfer::apply(target,s,{"masks"}));QCOMPARE(target.colorMaskLab,s.colorMaskLab);QCOMPARE(target.colorMaskExposure,s.colorMaskExposure);QCOMPARE(target.exposure,0.0);QCOMPARE(target.temperature,19.0);
        auto bad=s;bad.colorMaskLab[0]=std::numeric_limits<double>::quiet_NaN();const auto before=target.toJson();QVERIFY(!AdjustmentTransfer::apply(target,bad,{"masks"}));QCOMPARE(target.toJson(),before);
        QTemporaryDir dir;QString error;NamedPresets presets(dir.filePath("presets.json"));QVERIFY(presets.load(&error));QVERIFY(presets.save("Color mask",s,&error));AdjustmentState restored;QVERIFY(presets.get("Color mask",&restored));QCOMPARE(restored.toJson(),s.toJson());QVERIFY(!presets.save("Invalid",bad,&error));
        const auto xmp=dir.filePath("color.xmp");QVERIFY2(XmpSidecar::writeNew(xmp,s,{},0,"none",&error),qPrintable(error));XmpSidecar::Document decoded;QVERIFY(XmpSidecar::read(xmp,&decoded,&error));QCOMPARE(decoded.adjustments.toJson(),s.toJson());
        const auto path=dir.filePath("source.png");QVERIFY(scene({400,300}).convertToFormat(QImage::Format_RGBA64).save(path));const auto original=bytes(path);ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setViewport(200,150,1,0,.5,.5);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        QVERIFY(c.selectColorMask(QColor(73,131,192)));c.finishInteraction();const auto count=c.editHistory().size();for(double ev:{.2,.6,1.3})QVERIFY(c.setColorMaskParameter("exposure",ev));c.finishInteraction();QCOMPARE(c.editHistory().size(),count+1);QVERIFY(c.setColorMaskInverted(true));c.setExposure(.3);c.finishInteraction();const auto saved=c.gpuPlan(true).state.toJson();QVERIFY(c.resetColorMask());QCOMPARE(c.exposure(),.3);c.undo();QCOMPARE(c.gpuPlan(true).state.toJson(),saved);QVERIFY(c.createVirtualCopy("Color mask copy"));QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Color mask"));QVERIFY(c.flushEdits());PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);reopened.selectPhoto(0);QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);QCOMPARE(bytes(path),original);
    }
    void malformedColorMaskCannotWriteOrReplaceTheCurrentProject() {
        QTemporaryDir dir;ProjectDatabase active;QVERIFY(active.create(dir.path(),"Active"));const auto current=active.projectPath();QVERIFY(active.addOrUpdatePhoto("source.png",edits()));QVERIFY(active.flush());const auto path=QDir(current).filePath("Project.db");const auto original=bytes(path);auto bad=edits();bad.colorMaskHueRange=0;QVERIFY(!active.updateBatch({{"source.png",bad}}));QVERIFY(active.flush());QCOMPARE(bytes(path),original);
        ProjectDatabase candidate;QVERIFY(candidate.create(dir.path(),"Invalid"));QVERIFY(candidate.addOrUpdatePhoto("source.png",edits()));QVERIFY(candidate.flush());const auto invalid=QDir(candidate.projectPath()).filePath("Project.db");auto json=edits().toJson();auto mask=json["colorMask"].toObject();mask["space"]="monitor-rgb";json["colorMask"]=mask;
        {auto db=QSqlDatabase::addDatabase("QSQLITE","color-mask-invalid");db.setDatabaseName(invalid);QVERIFY(db.open());QSqlQuery query(db);query.prepare("UPDATE photos SET adjustment_json=?");query.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));QVERIFY(query.exec());}QSqlDatabase::removeDatabase("color-mask-invalid");const auto corrupt=bytes(invalid);QVector<ProjectDatabase::SavedPhoto> loaded;QVERIFY(!active.open(candidate.projectPath(),&loaded));QCOMPARE(active.projectPath(),current);QCOMPARE(bytes(invalid),corrupt);QCOMPARE(bytes(path),original);
    }
    void completeFourSpaceExportsScopesAndTilesAgree() {
        const auto source=scene();const auto original=pixels(source);auto s=edits();s.geometry.crop={.1,.1,.8,.8};s.geometry.quarterTurns=1;s.denoiseLuma=63;s.denoiseChroma=71;s.dehazeAmount=41;s.textureAmount=35;s.clarityAmount=-23;s.sharpenAmount=61;s.luminanceMaskEnabled=true;s.luminanceMaskExposure=.4;QTemporaryDir dir;QString error;
        for(int space=0;space<4;++space){const auto corrected=s.geometry.apply(source);const auto p=plan(s,space),off=plan({},space);const auto expected=ImagePipeline::processWithPlan(corrected,p);QVERIFY(!expected.isNull());QCOMPARE(pixels(ImagePipeline::processRegion(corrected,p,{3,4,31,29})),pixels(expected.copy(3,4,31,29)));
            const auto a=ScopesEngine::analyzeFull(corrected,p),b=ScopesEngine::analyze(expected);QCOMPARE(a.red,b.red);QCOMPARE(a.green,b.green);QCOMPARE(a.blue,b.blue);QCOMPARE(a.luma,b.luma);const auto path=dir.filePath(QString::number(space)+".png");QVERIFY2(exportRaster(source,s,path,ColorManagement::OutputSpace(space),RasterFormat::Png16,100,{},&error,false,0),qPrintable(error));const QImage decoded(path);QCOMPARE(pixels(decoded.convertToFormat(QImage::Format_RGBA64)),pixels(expected));QCOMPARE(decoded.colorSpace().iccProfile(),ColorManagement::colorSpace(ColorManagement::OutputSpace(space)).iccProfile());
            const auto reference=ImagePipeline::processWithPlan(corrected,off);for(int y=0;y<expected.height();++y)for(int x=0;x<expected.width();++x)QCOMPARE(reinterpret_cast<const QRgba64 *>(expected.constScanLine(y))[x].alpha(),reinterpret_cast<const QRgba64 *>(reference.constScanLine(y))[x].alpha());}
        QCOMPARE(pixels(source),original);
    }
    void realSonyCropHasExactTilesScopesAndUnchangedSource() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");if(folder.isEmpty())QSKIP("JIXELLIGHT_SONY_FIXTURES is not set");
        const auto path=QDir(folder).filePath("sony_a7_iv_07.arw");const auto original=QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256);QCOMPARE(original.toHex(),QByteArray("aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d"));
        const auto loaded=loadSourceFresh(path,{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));const auto source=loaded.image.copy(1024,768,1600,1000);QCOMPARE(source.size(),QSize(1600,1000));const auto inputHash=pixels(source);auto s=edits();const auto sampled=ImagePipeline::sampleMaskColor(source,plan(),{800,500});QVERIFY(sampled);s.colorMaskLab={sampled->L,sampled->a,sampled->b};s.colorMaskHueRange=35;s.colorMaskChromaRange=.12;s.colorMaskLightnessRange=.4;
        const auto root=qEnvironmentVariable("JIXELLIGHT_COLOR_MASK_EVIDENCE");if(!root.isEmpty())QVERIFY(QDir().mkpath(root));QJsonArray observations;
        for(int space=0;space<4;++space) {const auto p=ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,loaded.metadata["rawBaseExposureStops"].toFloat());const auto after=ImagePipeline::processWithPlan(source,p);auto off=p;off.data[ProcessingPlan::ColorMask]={};off.data[ProcessingPlan::ColorMaskRange]={};off.state.colorMaskEnabled=false;const auto before=ImagePipeline::processWithPlan(source,off);QVERIFY(!after.isNull()&&pixels(after)!=pixels(before));
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
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"controller",QVariant::fromValue(&c)},{"colorMaskExpanded",true},{"basicExpanded",false},{"colorExpanded",false}}));QVERIFY2(object,qPrintable(component.errorString()));auto *item=qobject_cast<QQuickItem *>(object.get());QVERIFY(item);
        window->resize(width,780);item->setParentItem(window->contentItem());item->setSize(window->size());window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *toggle=object->findChild<QQuickItem *>("colorMaskToggle"),*enable=object->findChild<QQuickItem *>("colorMaskEnable"),*exposure=object->findChild<QQuickItem *>("colorMaskExposureSlider"),*reset=object->findChild<QQuickItem *>("colorMaskReset"),*invert=object->findChild<QQuickItem *>("colorMaskInvert");QVERIFY(toggle&&enable&&exposure&&reset&&invert);
        auto *flick=qvariant_cast<QQuickItem *>(object->property("brushToolFlick"));QVERIFY(flick);
        const auto reveal=[&] {const double y=toggle->mapToItem(flick,{0,0}).y()+flick->property("contentY").toDouble();flick->setProperty("contentY",std::clamp(y-10,0.,std::max(0.,flick->property("contentHeight").toDouble()-flick->height())));};
        QTRY_VERIFY(exposure->width()>50);reveal();
        const auto click=[&](QQuickItem *control,double x){const auto point=control->mapToScene({control->width()*x,control->height()*.5}).toPoint();QVERIFY(flick->contains(control->mapToItem(flick,{control->width()*x,control->height()*.5})));QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point);c.finishInteraction();};
        click(enable,.1);QTRY_VERIFY(c.colorMask().value("enabled").toBool());click(exposure,.75);QTRY_VERIFY(c.colorMask().value("exposure").toDouble()>.5);
        const auto within=[&] {auto *section=object->findChild<QQuickItem *>("colorMaskSection");if(!section)return false;for(auto *row:section->childItems())for(auto *control:row->childItems()) {if(!control->isVisible())continue;const auto a=control->mapToScene({0,0}),b=control->mapToScene({control->width(),control->height()});if(a.x()<-.5||b.x()>window->width()+.5)return false;}return true;};
        QTRY_VERIFY(within());QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);
        const auto capture=[&](const QString &language) {const auto root=qEnvironmentVariable("JIXELLIGHT_COLOR_MASK_EVIDENCE");return root.isEmpty()||(QDir().mkpath(root)&&window->grabWindow().save(QDir(root).filePath(QString("color-mask-panel-%1-%2.png").arg(language).arg(width))));};
        QVERIFY(capture("en"));c.setLanguage("zh_CN");QTRY_VERIFY(toggle->property("text").toString().contains(QStringLiteral("颜色范围")));reveal();QTRY_VERIFY(within());QVERIFY(capture("zh"));
        click(invert,.1);QTRY_VERIFY(c.colorMask().value("inverted").toBool());click(reset,.5);QTRY_VERIFY(!c.colorMask().value("enabled").toBool());c.undo();QVERIFY(c.colorMask().value("inverted").toBool());item->setParentItem(nullptr);
    }
    void twentyFourMegapixelBrushAndSpatialStackStayOwnedAndTileExact() {
        QElapsedTimer timer;timer.start();const auto source=scene({6000,4000});const auto original=pixels(source);auto s=edits();s.luminanceMaskEnabled=true;s.luminanceMaskExposure=.8;s.denoiseLuma=83;s.denoiseChroma=91;s.dehazeAmount=73;s.textureAmount=65;s.clarityAmount=41;s.pointColorEnabled=true;s.pointColorHue=27;s.pointColorHueRange=180;s.pointColorChromaRange=1;s.pointColorLightnessRange=1;s.sharpenAmount=71;s.sharpenRadius=5.7;
        BrushMaskGeometry::Stroke paint;paint.style={.03,.43,.7};paint.dabs={{{.2,.3},.8},{{.7,.6},.5}};auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.2,.3},.4}};s.brushMasks.layers={{"large-color-mask",true,.8,{paint,erase}}};
        auto budget=std::make_shared<BrushRenderBudget>();BrushRenderRequest::Spec spec;QString error;auto request=BrushRenderRequest::create(source,s,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));const auto reservation=request->reservedBytes();auto lease=request->render({},&error);QVERIFY2(lease,qPrintable(error));request.reset();QCOMPARE(budget->snapshot().charged,lease.image().sizeInBytes());
        for(const auto &roi:{QRect(0,0,139,129),QRect(1117,1163,137,139),QRect(5863,3861,137,139)}) {spec.region=roi;auto part=BrushRenderRequest::create(source,s,spec,{},&error,budget);QVERIFY2(part,qPrintable(error));auto actual=part->render({},&error);QVERIFY2(actual,qPrintable(error));QCOMPARE(pixels(actual.image()),pixels(lease.image().copy(roi)));}
        QCOMPARE(pixels(source),original);lease={};QCOMPARE(budget->snapshot().charged,0);QVERIFY(budget->snapshot().peak<=BrushRenderBudget::DefaultBytes);QVERIFY(timer.elapsed()<60000);
        qInfo()<<"24MP FP32 Paint/Erase + spatial stack + Point Color + color mask + luminance mask + sharpen ms"<<timer.elapsed()<<"whole reservation"<<reservation<<"peak owned bytes"<<budget->snapshot().peak;
    }
};

int main(int argc,char **argv) {
    QQuickStyle::setStyle("Fusion");QApplication app(argc,argv);QCoreApplication::setOrganizationName("JixelLightOwnedTests");QCoreApplication::setApplicationName("ColorMaskWorkflow");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QStandardPaths::setTestModeEnabled(true);ColorMaskWorkflowTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "ColorMaskWorkflowTests.moc"
