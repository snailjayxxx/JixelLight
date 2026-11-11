#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QQuickStyle>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <limits>
#include <cstring>
#include <random>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/StageGraph.h"
#include "core/commands/AdjustmentTransfer.h"
#include "core/commands/CommandRegistry.h"
#include "core/commands/NamedPresets.h"
#include "core/export/RasterExporter.h"
#include "core/metadata/XmpSidecar.h"
#include "diagnostics/PerformanceRecorder.h"

class TextureWorkflowTests:public QObject {
    Q_OBJECT
    static QImage scene(QSize size={137,279},bool alpha=true) {
        QImage image(size,QImage::Format_RGBA32FPx4);std::mt19937 random(75901);std::normal_distribution<float> noise(0,.012f);
        const float alphas[]{0,.01f,.5f,1};
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            p[0]=.08f+.3f*x/std::max(1,size.width()-1)+noise(random);
            p[1]=.12f+.2f*y/std::max(1,size.height()-1)+noise(random);p[2]=.2f+noise(random);
            p[3]=alpha?alphas[(x+y)%4]:1;
        }
        image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));return image;
    }
    static ProcessingPlan plan(const AdjustmentState &state,ColorManagement::OutputSpace space=ColorManagement::OutputSpace::SRgb) {
        return ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,false,0);
    }
    static AdjustmentState edits() {
        AdjustmentState state;state.textureAmount=73;state.denoiseLuma=87;state.denoiseChroma=93;state.sharpenAmount=71;state.sharpenRadius=5.7;
        state.grainAmount=31;state.grainSize=3.3;state.look.mode="manual";state.look.code="ST";state.look.parameters={{"sharpness",4},{"clarity",3}};return state;
    }
    static QByteArray bytes(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
private slots:
    void pipelineIdentityDensityAndUnclippedDiagnostics() {
        const auto source=scene(),original=source.copy();AdjustmentState state;const auto disabled=plan(state);
        QVERIFY(!state.toJson().contains("texture"));state.textureAmount=100;auto p=plan(state);
        const auto actual=ImagePipeline::processWithPlan(source,p);QVERIFY(!actual.isNull());QVERIFY(actual!=ImagePipeline::processWithPlan(source,disabled));
        const auto diagnostic=ImagePipeline::diagnoseWithPlan(source,p);QCOMPARE(diagnostic.image,actual);
        const auto stage=diagnostic.stages["spatial_texture"].toObject();QCOMPARE(stage["backend"].toString(),QString("cpu-reference"));
        QVERIFY(stage["input"].toObject()["available"].toBool());QVERIFY(stage["output"].toObject()["available"].toBool());
        p.setReferenceFrameSize(source.size()*4);QCOMPARE(p.texture(source.size()).y,.5f);
        p.setFrameRect({.25,.125,.25,.25});QCOMPARE(p.texture(source.size()).y,2.f);
        auto hdr=source.copy();for(int y=0;y<hdr.height();++y){auto *row=reinterpret_cast<float *>(hdr.scanLine(y));row[0]=-.04;row[3]=1;row[4*(hdr.width()-1)]=2;row[4*(hdr.width()-1)+3]=1;}
        const auto observed=ImagePipeline::diagnoseWithPlan(hdr,plan(state));QVERIFY(!observed.image.isNull());
        for(const auto *boundary:{"input","output"}) {
            const auto ranges=observed.stages["spatial_texture"].toObject()[boundary].toObject()["ranges_rgba"].toArray();
            QVERIFY(ranges[0].toArray()[0].toDouble()<0);QVERIFY(ranges[0].toArray()[1].toDouble()>1);
        }
        state.textureAmount=0;QCOMPARE(ImagePipeline::processWithPlan(source,plan(state)),ImagePipeline::processWithPlan(source,disabled));QCOMPARE(source,original);
    }
    void combinedHalosRegionsHistogramsAndPlots() {
        const auto source=scene();auto state=edits();state.clarityAmount=-43;
        for(int output=0;output<4;++output) {
            const auto p=plan(state,ColorManagement::OutputSpace(output));const auto full=ImagePipeline::processWithPlan(source,p);QVERIFY(!full.isNull());
            for(const QRect roi : {QRect(0,0,137,128),QRect(0,128,137,128),QRect(0,256,137,23),QRect(21,77,83,151)})QCOMPARE(ImagePipeline::processRegion(source,p,roi),full.copy(roi));
            const auto actual=ScopesEngine::analyzeFull(source,p),expected=ScopesEngine::analyze(full);
            QCOMPARE(actual.red,expected.red);QCOMPARE(actual.green,expected.green);QCOMPARE(actual.blue,expected.blue);QCOMPARE(actual.luma,expected.luma);
            if(output==0){const auto plots=renderScopePlots({source,p,{},"waveform",1,true},{});const QStringList modes{"waveform","parade","vectorscope"};for(int i=0;i<3;++i){ScopePlotCounts counts(modes[i]);QVERIFY(counts.add(full));QCOMPARE(plots[i].image,counts.image());}}
        }
    }
    void owningBudgetDensityAndStrips() {
        const auto source=scene();auto state=edits();state.textureAmount=0;state.denoiseLuma=0;state.denoiseChroma=0;
        BrushMaskGeometry::Stroke paint;paint.style={.13,.42,.8};paint.dabs={{{.28,.37},.5},{{.69,.62},.9}};
        auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.3,.38},.3}};
        state.brushMasks.layers={{"texture-owned",true,.85,{paint,erase}}};auto budget=std::make_shared<BrushRenderBudget>();BrushRenderRequest::Spec spec;QString reason;
        auto base=BrushRenderRequest::create(source,state,spec,{},&reason,budget);QVERIFY2(base,qPrintable(reason));const auto baseBytes=base->reservedBytes();base.reset();
        state.textureAmount=73;auto measured=BrushRenderRequest::create(source,state,spec,{},&reason,budget);QVERIFY2(measured,qPrintable(reason));
        const auto needed=measured->reservedBytes();QCOMPARE(needed-baseBytes,qint64(source.width())*source.height()*36);measured.reset();
        BrushAdmissionFailure failure;auto shortBudget=std::make_shared<BrushRenderBudget>(needed-1);QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&reason,shortBudget,&failure));QCOMPARE(failure,BrushAdmissionFailure::BudgetUnavailable);QCOMPARE(shortBudget->snapshot().peak,0);
        auto p=plan(state);p.setReferenceFrameSize(source.size()*4);const auto expected=ImagePipeline::processWithPlan(source,p);QVERIFY(!expected.isNull());
        auto whole=BrushRenderRequest::createPrepared(source,p,BrushRenderRequest::Layout::Whole,{},&reason,budget);QVERIFY2(whole,qPrintable(reason));
        auto lease=whole->render({},&reason);QVERIFY2(lease,qPrintable(reason));QCOMPARE(lease.image(),expected);whole.reset();QCOMPARE(budget->snapshot().charged,lease.image().sizeInBytes());lease={};QCOMPARE(budget->snapshot().charged,0);
        auto striped=BrushRenderRequest::createPrepared(source,p,BrushRenderRequest::Layout::Stripes128,{},&reason,budget);QVERIFY2(striped,qPrintable(reason));int rows=0;
        QVERIFY2(striped->visitStrips([&](const BrushImageLease &strip,int row){if(row!=rows||strip.image()!=expected.copy(0,row,expected.width(),strip.image().height()))return false;rows+=strip.image().height();return true;},{},&reason),qPrintable(reason));QCOMPARE(rows,expected.height());striped.reset();QCOMPARE(budget->snapshot().charged,0);
        p.data[ProcessingPlan::Texture].z=.5;QVERIFY(!BrushRenderRequest::createPrepared(source,p,BrushRenderRequest::Layout::Whole,{},&reason,budget));QCOMPARE(budget->snapshot().charged,0);
    }
    void snapshotsCommandsPresetsXmpAndSelectiveDetail() {
        auto state=edits();const auto saved=state.toJson();QVERIFY(AdjustmentState::validTextureJson(saved));QCOMPARE(AdjustmentState::fromJson(saved).toJson(),saved);
        AdjustmentState target;target.exposure=1;target.grainAmount=12;QVERIFY(AdjustmentTransfer::apply(target,state,{"detail"}));
        QCOMPARE(target.textureAmount,state.textureAmount);QCOMPARE(target.denoiseLuma,state.denoiseLuma);QCOMPARE(target.sharpenAmount,state.sharpenAmount);QCOMPARE(target.exposure,1.0);QCOMPARE(target.grainAmount,12.0);QCOMPARE(target.look.mode,QString("off"));
        auto malformed=state;malformed.textureAmount=std::numeric_limits<double>::infinity();const auto before=target.toJson();QVERIFY(!AdjustmentTransfer::apply(target,malformed,{"exposure","detail"}));QCOMPARE(target.toJson(),before);
        QString reason;QVERIFY(CommandRegistry::execute(state,{{"command","texture.reset"}},&reason));QCOMPARE(state.textureAmount,0.0);QCOMPARE(state.denoiseLuma,87.0);
        QVERIFY(CommandRegistry::execute(state,{{"command","develop.set"},{"parameter","textureAmount"},{"value",-59}},&reason));QCOMPARE(state.textureAmount,-59.0);
        const auto whole=state.toJson();QVERIFY(!CommandRegistry::executeBatch(state,QJsonArray{QJsonObject{{"command","texture.reset"}},QJsonObject{{"command","texture.reset"},{"extra",true}}},&reason));QCOMPARE(state.toJson(),whole);
        QTemporaryDir dir;NamedPresets presets(dir.filePath("presets.json"));QVERIFY(presets.load(&reason));QVERIFY(presets.save("Texture",state,&reason));AdjustmentState loaded;QVERIFY(presets.get("Texture",&loaded));QCOMPARE(loaded.toJson(),whole);
        const auto xmp=dir.filePath("texture.xmp");QVERIFY2(XmpSidecar::writeNew(xmp,state,{},0,"none",&reason),qPrintable(reason));
        XmpSidecar::Document decoded;QVERIFY2(XmpSidecar::read(xmp,&decoded,&reason),qPrintable(reason));QCOMPARE(decoded.adjustments.toJson(),whole);
    }
    void denoiseThenTextureOrderAndUnchangedAlpha() {
        const auto source=scene();auto state=edits();state.clarityAmount=41;const auto result=ImagePipeline::diagnoseWithPlan(source,plan(state));QVERIFY(!result.image.isNull());
        const auto dn=result.stages["spatial_denoise"].toObject(),texture=result.stages["spatial_texture"].toObject(),clarity=result.stages["spatial_clarity"].toObject();
        QCOMPARE(dn["output"].toObject()["pixel_sha256"],texture["input"].toObject()["pixel_sha256"]);
        QCOMPARE(texture["output"].toObject()["pixel_sha256"],clarity["input"].toObject()["pixel_sha256"]);
        QVERIFY(texture["input"].toObject()["pixel_sha256"]!=texture["output"].toObject()["pixel_sha256"]);
        const auto positive=result.image;state.textureAmount=-73;const auto negative=ImagePipeline::processWithPlan(source,plan(state));QVERIFY(!negative.isNull());QVERIFY(positive!=negative);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            const auto a=reinterpret_cast<const QRgba64 *>(positive.constScanLine(y))[x],b=reinterpret_cast<const QRgba64 *>(negative.constScanLine(y))[x];
            QCOMPARE(a.alpha(),b.alpha());QCOMPARE(a.alpha(),quint16(std::lround(reinterpret_cast<const float *>(source.constScanLine(y))[4*x+3]*65535.f)));
        }
    }
    void rasterExportsRetainIccAlphaAndVisiblePixels() {
        const auto source=scene();auto state=edits();state.geometry.crop={.1,.2,.8,.7};state.geometry.quarterTurns=1;QTemporaryDir dir;QString reason;
        for(int space=0;space<4;++space)for(const auto format : {RasterFormat::Png16,RasterFormat::Tiff16,RasterFormat::WebP8}) {
            const auto output=ColorManagement::OutputSpace(space);const auto expected=ImagePipeline::processWithPlan(state.geometry.apply(source),plan(state,output));
            const auto path=dir.filePath(QString::number(space)+(format==RasterFormat::Png16?".png":format==RasterFormat::Tiff16?".tif":".webp"));
            QVERIFY2(exportRaster(source,state,path,output,format,100,{},&reason,false,0),qPrintable(reason));const QImage image(path);QVERIFY(!image.isNull());QCOMPARE(image.colorSpace(),ColorManagement::colorSpace(output));
            if(format!=RasterFormat::WebP8)QCOMPARE(image.convertToFormat(QImage::Format_RGBA64),expected);
            else {const auto actual=image.convertToFormat(QImage::Format_RGBA8888),reference=expected.convertToFormat(QImage::Format_RGBA8888);for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x){const auto *a=actual.constScanLine(y)+4*x,*b=reference.constScanLine(y)+4*x;QCOMPARE(a[3],b[3]);if(b[3])for(int c=0;c<3;++c)QCOMPARE(a[c],b[c]);}}
        }
    }
    void malformedSnapshotsKeepExistingCatalogWriter() {
        QTemporaryDir dir;ProjectDatabase active,candidate;QVERIFY(active.create(dir.path(),"Active"));QVERIFY(candidate.create(dir.path(),"Candidate"));const auto state=edits();QVERIFY(candidate.addOrUpdatePhoto("source.png",state));QVERIFY(candidate.flush());const auto originalPath=active.projectPath(),dbPath=QDir(candidate.projectPath()).filePath("Project.db");const auto valid=state.toJson()["texture"].toObject();
        QList<QJsonValue> invalid{QJsonArray{},QJsonObject{},QJsonObject{{"schema",1},{"amount",0}}};
        for(const auto *key:{"schema","amount"}){auto object=valid;object.remove(key);invalid.append(object);}
        for(const auto value:{QJsonValue(-101),QJsonValue(101),QJsonValue(true),QJsonValue("1"),QJsonValue()}){auto object=valid;object["amount"]=value;invalid.append(object);}
        auto future=valid;future["schema"]=2;invalid.append(future);auto extra=valid;extra["future"]=true;invalid.append(extra);
        for(const auto &value:invalid){auto snapshot=state.toJson();snapshot["texture"]=value;QVERIFY(!AdjustmentState::validTextureJson(snapshot));const QString connection="texture-invalid-owned";
            {auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(dbPath);QVERIFY(db.open());QSqlQuery query(db);query.prepare("UPDATE photos SET adjustment_json=?");query.addBindValue(QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)));QVERIFY(query.exec());}
            QSqlDatabase::removeDatabase(connection);const auto original=bytes(dbPath);QVector<ProjectDatabase::SavedPhoto> photos;QVERIFY(!active.open(candidate.projectPath(),&photos));QVERIFY(active.lastError().contains("texture"));QCOMPARE(active.projectPath(),originalPath);QCOMPARE(bytes(dbPath),original);QVERIFY(active.flush());
        }
    }
    void invalidTextureWritesLeaveCatalogAndHistoryIntact() {
        QTemporaryDir dir;ProjectDatabase db;QVERIFY(db.create(dir.path(),"Write safety"));const auto valid=edits();QVERIFY(db.addOrUpdatePhoto("valid.png",valid));QVERIFY(db.flush());
        const auto path=QDir(db.projectPath()).filePath("Project.db");const auto before=bytes(path);
        for(const auto member : {&AdjustmentState::textureAmount})
        for(const double value : {-101.,101.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            auto invalid=valid;invalid.*member=value;
            QVERIFY(!db.addOrUpdatePhoto("invalid.png",invalid));QVERIFY(!db.updateBatch({{"valid.png",AdjustmentState{}},{"invalid.png",invalid}}));
            EditHistory history;history.initialize(valid);QVERIFY(history.record(invalid,"texture_adjustment"));history.undo();
            QVERIFY(!db.updateBatch({{"valid.png",valid}},{{"valid.png",history}}));
            QVERIFY(!db.addVirtualCopy("jixel-copy:11111111-1111-1111-1111-111111111111","valid.png","Invalid",invalid,history,{},{}));
            QVERIFY(db.flush());QCOMPARE(bytes(path),before);
        }
        QVector<ProjectDatabase::SavedPhoto> loaded;ProjectDatabase reopened;QVERIFY(reopened.open(db.projectPath(),&loaded));QCOMPARE(loaded.size(),1);QCOMPARE(loaded[0].adjustments.toJson(),valid.toJson());
    }
    void controllerCpuColorChangesReuseCompletedTextureStage() {
        QTemporaryDir dir;const auto path=dir.filePath("cached-source.png");QVERIFY(scene({800,600},false).convertToFormat(QImage::Format_RGBA64).save(path));const auto original=bytes(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setViewport(200,150,1,0,.5,.5);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        c.setTextureAmount(73);c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);
        const auto count=[](const char *name){return PerformanceRecorder::snapshot()["counters"].toObject()[name].toInteger();};
        const auto builds=count("cpu_preview_texture_builds"),hits=count("cpu_preview_texture_hit");QVERIFY(builds>0);
        c.setExposure(.3);c.setTemperature(16);c.setTint(-11);c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);
        QCOMPARE(count("cpu_preview_texture_builds"),builds);QVERIFY(count("cpu_preview_texture_hit")>hits);
        const auto actual=provider.requestImage("current",nullptr,{}),expected=ImagePipeline::processWithPlan(c.gpuSource(),c.gpuPlan(false));QVERIFY(!actual.isNull());QCOMPARE(actual.size(),expected.size());QCOMPARE(actual.format(),QImage::Format_RGBA64);
        for(int y=0;y<actual.height();++y)QVERIFY(std::memcmp(actual.constScanLine(y),expected.constScanLine(y),std::size_t(actual.width())*8)==0);
        c.setTextureAmount(-49);c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);QVERIFY(count("cpu_preview_texture_builds")>builds);QCOMPARE(bytes(path),original);
    }
    void controllerHistoryExactScopesProjectAndCopies() {
        QTemporaryDir dir;const auto sourcePath=dir.filePath("source.png");QVERIFY(scene({800,600},false).convertToFormat(QImage::Format_RGBA64).save(sourcePath));const auto original=bytes(sourcePath);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);c.setLanguage("en_US");c.setViewport(200,150,1,0,.5,.5);QVERIFY(c.importFile(QUrl::fromLocalFile(sourcePath)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        c.setExposure(.2);c.finishInteraction();const auto initial=c.editHistory().size();for(double amount : {10,30,70})c.setTextureAmount(amount);c.finishInteraction();QCOMPARE(c.editHistory().size(),initial+1);QCOMPARE(c.editHistory().last().toMap()["action"].toString(),QString("texture_adjustment"));
        QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);QCOMPARE(c.gpuPlan(false).texture(c.gpuSource().size()).y,.5f);const auto saved=c.gpuPlan(true).state.toJson();c.setExactScopes(true);QTRY_VERIFY_WITH_TIMEOUT(!c.rendering()&&c.scopesStatus().contains("Full-resolution statistics"),10000);
        const auto loaded=loadSourceFresh(sourcePath,{});QVERIFY(!loaded.image.isNull());const auto counts=ScopesEngine::analyze(ImagePipeline::processWithPlan(loaded.image,c.gpuPlan(true)));QCOMPARE(c.redHistogram(),counts.red);QCOMPARE(c.lumaHistogram(),counts.luma);
        c.resetTexture();QCOMPARE(c.textureAmount(),0.0);QCOMPARE(c.exposure(),.2);c.undo();QCOMPARE(c.gpuPlan(true).state.toJson(),saved);
        QVERIFY(c.createVirtualCopy("Texture copy"));c.resetTexture();c.undo();c.selectPhoto(0);QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Texture"));QVERIFY(c.flushEdits());PhotoController reopened(nullptr);reopened.setGpuEnabled(false);QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);reopened.selectPhoto(1);QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved);QVERIFY(reopened.canRedo());reopened.redo();QCOMPARE(reopened.textureAmount(),0.0);QCOMPARE(reopened.exposure(),.2);QCOMPARE(bytes(sourcePath),original);
    }
    void realSonyDecodedCropRetainsSourceAndOutputProfiles() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixture configured; not a photo-quality pass");}
        const auto path=QDir(folder).filePath("sony_a7_iv_07.arw");const auto original=QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256);QVERIFY(!original.isEmpty());
        const auto loaded=loadSourceFresh(path,{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));QCOMPARE(loaded.image.format(),QImage::Format_RGBA64);
        const QRect roi((loaded.image.width()-1600)/2,(loaded.image.height()-1000)/2,1600,1000);const auto source=loaded.image.copy(roi);QVERIFY(!source.isNull());
        const auto pixelHash=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(source.constBits()),source.sizeInBytes()),QCryptographicHash::Sha256);
        AdjustmentState state;state.textureAmount=65;const auto root=qEnvironmentVariable("JIXELLIGHT_TEXTURE_EVIDENCE");if(!root.isEmpty())QVERIFY(QDir().mkpath(root));
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
        QQmlEngine engine;engine.rootContext()->setContextProperty("photoController",&c);
        QQmlComponent host(&engine,QUrl::fromLocalFile(QFINDTESTDATA("fixtures/FilmGrainWindow.qml")));
        std::unique_ptr<QObject> hostObject(host.create());auto *window=qobject_cast<QQuickWindow *>(hostObject.get());
        QVERIFY2(window,qPrintable(host.errorString()));
        QQmlComponent component(&engine,QUrl::fromLocalFile(QFINDTESTDATA("../ui/DevelopRightPanel.qml")));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"controller",QVariant::fromValue(&c)},{"textureExpanded",true},{"basicExpanded",false},{"colorExpanded",false},{"grainExpanded",false},{"sharpenExpanded",false}}));
        QVERIFY2(object,qPrintable(component.errorString()));auto *item=qobject_cast<QQuickItem *>(object.get());QVERIFY(item);
        window->resize(width,780);item->setParentItem(window->contentItem());item->setSize(window->size());window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *lightness=object->findChild<QQuickItem *>("textureAmountSlider"),*reset=object->findChild<QQuickItem *>("textureReset"),*toggle=object->findChild<QQuickItem *>("textureToggle");
        QVERIFY(lightness&&reset&&toggle);QTRY_VERIFY(lightness->width()>50&&lightness->isVisible());QVERIFY(toggle->property("text").toString().contains("TEXTURE"));
        auto *flick=qvariant_cast<QQuickItem *>(object->property("brushToolFlick"));QVERIFY(flick);
        const auto sectionY=toggle->mapToItem(flick,{0,0}).y()+flick->property("contentY").toDouble();
        flick->setProperty("contentY",std::clamp(sectionY-10,0.0,std::max(0.0,flick->property("contentHeight").toDouble()-flick->height())));
        for(auto *slider : {lightness}) {
            const auto point=slider->mapToScene({slider->width()*.75,slider->height()*.5}).toPoint();QVERIFY(window->contentItem()->contains(point));
            QVERIFY(flick->contains(slider->mapToItem(flick,{slider->width()*.75,slider->height()*.5})));
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point);c.finishInteraction();
        }
        QTRY_VERIFY(c.textureAmount()>25);
        const auto controlsWithinWindow=[&] {
            auto *section=object->findChild<QQuickItem *>("textureSection");if(!section)return false;
            for(auto *row : section->childItems())for(auto *control : row->childItems()) {
                if(!control->isVisible())continue;
                const auto left=control->mapToScene({0,0}).x(),right=control->mapToScene({control->width(),0}).x();
                if(left<0||right>window->width()+.5)return false;
            }
            return reset->mapToScene({reset->width(),0}).x()<=window->width()+.5;
        };
        QTRY_VERIFY(controlsWithinWindow());QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);
        const auto captureRoot=qEnvironmentVariable("JIXELLIGHT_TEXTURE_EVIDENCE");
        const auto capture=[&](const QString &language) {
            if(captureRoot.isEmpty())return true;
            return QDir().mkpath(captureRoot)&&!window->grabWindow().isNull()&&window->grabWindow().save(QDir(captureRoot).filePath(QString("texture-panel-%1-%2.png").arg(language).arg(width)));
        };
        QVERIFY(capture("en"));c.setLanguage("zh_CN");QTRY_VERIFY(toggle->property("text").toString().contains(QStringLiteral("纹理")));QTRY_VERIFY(controlsWithinWindow());QVERIFY(capture("zh"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,lightness->mapToScene({lightness->width()*.25,lightness->height()*.5}).toPoint());c.finishInteraction();
        QTRY_VERIFY(c.textureAmount()<-25);QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),10000);QVERIFY(capture("zh-negative"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,reset->mapToScene({reset->width()*.5,reset->height()*.5}).toPoint());QTRY_COMPARE(c.textureAmount(),0.0);
        c.undo();QVERIFY(c.textureAmount()<-25);item->setParentItem(nullptr);
    }
    void twentyFourMegapixelOwnedPaintEraseTexture() {
        QElapsedTimer timer;timer.start();const auto source=scene({6000,4000},false);QVERIFY(!source.isNull());const auto hash=[&]{return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(source.constBits()),source.sizeInBytes()),QCryptographicHash::Sha256);};const auto before=hash();auto state=edits();state.clarityAmount=41;
        BrushMaskGeometry::Stroke paint;paint.style={.03,.43,.7};paint.dabs={{{.2,.3},.8},{{.7,.6},.5}};auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.2,.3},.4}};state.brushMasks.layers={{"large-texture",true,.8,{paint,erase}}};auto budget=std::make_shared<BrushRenderBudget>();BrushRenderRequest::Spec spec;QString reason;
        auto whole=BrushRenderRequest::create(source,state,spec,{},&reason,budget);QVERIFY2(whole,qPrintable(reason));const auto reserved=whole->reservedBytes();auto lease=whole->render({},&reason);QVERIFY2(lease,qPrintable(reason));whole.reset();QCOMPARE(budget->snapshot().charged,lease.image().sizeInBytes());
        for(const QRect roi : {QRect(0,0,139,129),QRect(1117,1163,137,139),QRect(5863,3861,137,139)}){spec.region=roi;auto part=BrushRenderRequest::create(source,state,spec,{},&reason,budget);QVERIFY2(part,qPrintable(reason));const auto result=part->render({},&reason);QVERIFY2(result,qPrintable(reason));QCOMPARE(result.image(),lease.image().copy(roi));}
        QCOMPARE(hash(),before);lease={};QCOMPARE(budget->snapshot().charged,0);QVERIFY(budget->snapshot().peak<=BrushRenderBudget::DefaultBytes);QVERIFY(timer.elapsed()<60000);
        qInfo()<<"24MP FP32 Paint/Erase + denoise + texture + clarity + sharpen: elapsed ms"<<timer.elapsed()<<"whole reservation"<<reserved<<"peak owned bytes"<<budget->snapshot().peak;
    }
};
int main(int argc,char **argv) {
    QQuickStyle::setStyle("Fusion");QApplication app(argc,argv);QCoreApplication::setOrganizationName("JixelLightOwnedTests");QCoreApplication::setApplicationName("TextureWorkflow");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QStandardPaths::setTestModeEnabled(true);TextureWorkflowTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "TextureWorkflowTests.moc"
