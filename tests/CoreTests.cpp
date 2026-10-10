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
#include <QtEndian>
#include <algorithm>
#include <cmath>

#include "app/PhotoController.h"
#include "core/color/ColorManagement.h"
#include "core/metadata/MetadataReader.h"
#include "core/metadata/XmpSidecar.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/pipeline/StageGraph.h"
#include "core/cache/RenderedPreviewCache.h"
#include "core/cache/FullScopesCache.h"
#include "core/cache/ScopePlotCache.h"
#include "core/export/PngExporter.h"
#include "core/export/RasterExporter.h"
#include "core/export/ExportNaming.h"
#include "core/export/ExportQueue.h"
#include "core/commands/CommandRegistry.h"
#include "core/commands/AdjustmentTransfer.h"
#include "core/commands/NamedPresets.h"
#include "core/raw/RawDecoder.h"
#include "core/scopes/ScopesEngine.h"
#include "core/scopes/ScopePlot.h"
#include "core/image/ProcessedImageProvider.h"
#include <numeric>
#include "diagnostics/ZipStoreWriter.h"
#include "diagnostics/DiagnosticBundle.h"
#include "diagnostics/PerformanceRecorder.h"

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
QJsonObject colorStage(const QJsonObject &trace, const QString &id) {
    for (const auto &entry:trace.value("entries").toArray())
        if (entry.toObject().value("id").toString()==id) return entry.toObject();
    return {};
}
QByteArray storedZipEntry(const QString &path, const QByteArray &entry) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes=file.readAll(); qsizetype offset=0;
    while (offset+30<=bytes.size() && qFromLittleEndian<quint32>(bytes.constData()+offset)==0x04034b50) {
        const auto size=qFromLittleEndian<quint32>(bytes.constData()+offset+18);
        const auto nameSize=qFromLittleEndian<quint16>(bytes.constData()+offset+26);
        const auto extraSize=qFromLittleEndian<quint16>(bytes.constData()+offset+28);
        const auto start=offset+30+nameSize+extraSize;
        if (start>bytes.size() || size>bytes.size()-start) return {};
        if (bytes.mid(offset+30,nameSize)==entry) return bytes.mid(start,size);
        offset=start+size;
    }
    return {};
}
}

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void gpuStagingPreservesEvery16BitChannelAtAllAlphaClasses() {
        QImage source(256,1024,QImage::Format_RGBA64); source.setColorSpace(QColorSpace::SRgbLinear);
        source.setDevicePixelRatio(2); source.setDotsPerMeterX(3000); source.setDotsPerMeterY(3100); source.setText("JixelLightSource","staging-test");
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x) {
            const quint16 value=quint16(x+256*(y%256));
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(value,65535-value,quint16(value*37),alpha[y/256]);
        }
        const auto before=source.copy(); const auto floating=ImagePipeline::floatSource(source); QCOMPARE(floating.format(),QImage::Format_RGBA32FPx4);
        for (int y=0;y<source.height();++y) {
            const auto *original=reinterpret_cast<const QRgba64 *>(source.constScanLine(y)); const auto *pixels=reinterpret_cast<const float *>(floating.constScanLine(y));
            for (int x=0;x<source.width();++x) {
                QCOMPARE(pixels[4*x],float(double(original[x].red())/65535)); QCOMPARE(pixels[4*x+1],float(double(original[x].green())/65535));
                QCOMPARE(pixels[4*x+2],float(double(original[x].blue())/65535)); QCOMPARE(pixels[4*x+3],float(double(original[x].alpha())/65535));
            }
        }
        QCOMPARE(ImagePipeline::rgba64Source(floating),source); QCOMPARE(source,before);
        QCOMPARE(floating.colorSpace(),source.colorSpace()); QCOMPARE(floating.devicePixelRatio(),2.0);
        QCOMPARE(floating.dotsPerMeterX(),3000); QCOMPARE(floating.dotsPerMeterY(),3100); QCOMPARE(floating.text("JixelLightSource"),QString("staging-test"));
        QCOMPARE(ImagePipeline::floatSource(floating).cacheKey(),floating.cacheKey()); QCOMPARE(ImagePipeline::rgba64Source(source).cacheKey(),source.cacheKey());
        PrepareRequest request; request.image=source; request.viewport=source.size(); request.fullResolution=true; request.zoom=1;
        const auto prepared=preparePreview(request,{}); QCOMPARE(prepared.normal,source); QCOMPARE(prepared.gpu,floating);
        const auto token=std::make_shared<std::atomic_bool>(true); QVERIFY(ImagePipeline::floatSource(source,token).isNull()); QVERIFY(ImagePipeline::rgba64Source(floating,token).isNull());
    }
    void vignetteUsesKnownSceneExposureAndPreservesDefaultsAndAlpha() {
        QImage source(9,9,QImage::Format_RGBA64); source.fill(QColor::fromRgba64(10000,10000,10000,32768));
        const auto plain=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
        const auto baseline=ImagePipeline::processWithPlan(source,plain); const auto legacy=AdjustmentState{}.toJson(); QVERIFY(!legacy.contains("vignette"));
        for (double amount : {-3.0,3.0}) {
            AdjustmentState state; state.vignetteAmount=amount; state.vignetteMidpoint=0; state.vignetteFeather=1;
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
            const auto actual=ImagePipeline::processWithPlan(source,plan);
            QCOMPARE(actual.pixelColor(4,4),baseline.pixelColor(4,4));
            const double r=8.0/9,weight=3*r*r-2*r*r*r;
            AdjustmentState exposed; exposed.exposure=amount*weight;
            const auto expected=ImagePipeline::processWithPlan(source,ProcessingPlan::compile(exposed,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0));
            const auto a=actual.pixelColor(0,0).rgba64(),b=expected.pixelColor(0,0).rgba64();
            QVERIFY(std::abs(int(a.red())-int(b.red()))<=2); QVERIFY(std::abs(int(a.green())-int(b.green()))<=2); QVERIFY(std::abs(int(a.blue())-int(b.blue()))<=2);
            for (int y=0;y<9;++y) for (int x=0;x<9;++x) {
                QCOMPARE(actual.pixelColor(x,y).rgba64().alpha(),source.pixelColor(x,y).rgba64().alpha());
                QCOMPARE(actual.pixelColor(x,y),actual.pixelColor(8-x,8-y));
            }
            QCOMPARE(ImagePipeline::processWithPlan(source,plan,{},false),actual);
            const auto capture=ImagePipeline::diagnoseWithPlan(source,plan); QCOMPARE(capture.image,actual);
            QVERIFY(!colorStage(capture.stages,"vignette").isEmpty());
            QVERIFY(colorStage(capture.stages,"vignette")["minimum_rgb"]!=colorStage(capture.stages,"wb_exposure")["minimum_rgb"]
                || colorStage(capture.stages,"vignette")["maximum_rgb"]!=colorStage(capture.stages,"wb_exposure")["maximum_rgb"]);
        }
        AdjustmentState disabled; disabled.vignetteMidpoint=.9; disabled.vignetteFeather=.01;
        QCOMPARE(ImagePipeline::processWithPlan(source,ProcessingPlan::compile(disabled,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0)),baseline);
        QCOMPARE(AdjustmentState::fromJson(legacy).toJson(),legacy);
        QImage one(1,1,QImage::Format_RGBA64); one.fill(Qt::gray); disabled.vignetteAmount=-3;
        QCOMPARE(ImagePipeline::process(one,disabled),ImagePipeline::process(one,{}));
    }
    void vignetteTilesScopesCacheAndExportsMatchFullReference() {
        QImage source(37,279,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64((x*1247+y*107)%65536,(x*557+y*311)%65536,(x*937+y*199)%65536,(x+y)%3?65535:23456);
        const auto before=source.copy();
        for (bool raw : {false,true}) for (int space=0;space<4;++space) for (int effect=0;effect<2;++effect) {
            AdjustmentState state; state.vignetteAmount=effect ? 2 : -3; state.vignetteMidpoint=effect ? .95 : .3; state.vignetteFeather=effect ? .01 : .7;
            state.look.mode="manual"; state.look.code="FL"; state.look.parameters={{"sharpness",4},{"clarity",3}};
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),raw,0);
            const auto full=ImagePipeline::processWithPlan(source,plan); QVERIFY(!full.isNull());
            for (const auto roi : {QRect(0,0,37,128),QRect(0,128,37,128),QRect(0,256,37,23),QRect(7,83,23,131)})
                QCOMPARE(ImagePipeline::processRegion(source,plan,roi),full.copy(roi));
            const auto counts=ScopesEngine::analyzeFull(source,plan); QCOMPARE(counts.red,ScopesEngine::analyze(full).red); QCOMPARE(counts.luma,ScopesEngine::analyze(full).luma);
            ScopePlotRequest request{source,plan,{},"waveform",1,true}; ScopePlotCache cache; const auto plot=cache.render(request);
            ScopePlotCounts reference("waveform"); QVERIFY(reference.add(full)); QCOMPARE(plot.image,reference.image());
            QCOMPARE(cache.render(request).image.cacheKey(),plot.image.cacheKey());
            auto changed=state; changed.vignetteAmount=0; const auto other=ProcessingPlan::compile(changed,plan.encoding,plan.output,raw,0);
            QVERIFY(StageGraph::renderKey(source,plan)!=StageGraph::renderKey(source,other));
        }
        QCOMPARE(source,before);
        QTemporaryDir dir; AdjustmentState state; state.vignetteAmount=-1.7; state.vignetteMidpoint=.4; state.vignetteFeather=.6; state.geometry.crop={.1,.2,.8,.6}; state.geometry.quarterTurns=1;
        const auto expected=ImagePipeline::process(state.geometry.apply(source),state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb);
        const auto path=dir.filePath("vignette.png"); QString error;
        QVERIFY2(exportRaster(source,state,path,ColorManagement::OutputSpace::SRgb,RasterFormat::Png16,100,{},&error,true,0),qPrintable(error));
        QCOMPARE(QImage(path).convertToFormat(QImage::Format_RGBA64),expected);
        auto cancelledToken=std::make_shared<std::atomic_bool>(true); QVERIFY(ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto),cancelledToken).isNull());
    }
    void vignetteViewportMappingKeepsFullImageLocation() {
        QImage source(241,193,QImage::Format_RGBA64); source.fill(QColor::fromRgba64(8000,12000,16000,65535));
        AdjustmentState state; state.vignetteAmount=-2; state.vignetteMidpoint=.2; state.vignetteFeather=.8;
        const auto fullPlan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
        const auto full=ImagePipeline::processWithPlan(source,fullPlan);
        for (double x : {.2,.8}) {
            PrepareRequest request; request.image=source; request.viewport={81,69}; request.fullResolution=true; request.zoom=1; request.centerX=x; request.centerY=.7;
            const auto prepared=preparePreview(request,{}); QVERIFY(prepared.viewportOnly); auto plan=fullPlan; plan.setFrameRect(prepared.frameRect);
            const auto actual=ImagePipeline::processWithPlan(prepared.normal,plan);
            const auto roi=QRect(qRound(prepared.frameRect.x()*source.width()),qRound(prepared.frameRect.y()*source.height()),actual.width(),actual.height());
            const auto reference=full.copy(roi); int maxDelta=0;
            for (int y=0;y<actual.height();++y) for (int xx=0;xx<actual.width();++xx) {
                const auto a=actual.pixelColor(xx,y).rgba64(),b=reference.pixelColor(xx,y).rgba64();
                maxDelta=std::max({maxDelta,std::abs(int(a.red())-int(b.red())),std::abs(int(a.green())-int(b.green())),std::abs(int(a.blue())-int(b.blue()))});
            }
            QVERIFY2(maxDelta<=1,qPrintable(QString::number(maxDelta)));
            QVERIFY(actual!=ImagePipeline::processWithPlan(prepared.normal,fullPlan));
        }
    }
    void vignetteSnapshotCommandsHistoryAndSelectiveTransfer() {
        AdjustmentState state; const auto original=state.toJson();
        for (const auto &p : CommandRegistry::parameters()) if (QString::fromLatin1(p.name).startsWith("vignette")) {
            QVERIFY(CommandRegistry::execute(state,{{"command","develop.set"},{"parameter",p.name},{"value",p.maximum}}));
            QCOMPARE(state.*p.member,p.maximum);
        }
        const auto json=state.toJson(); QVERIFY(AdjustmentState::validVignetteJson(json)); QCOMPARE(AdjustmentState::fromJson(json).toJson(),json);
        EditHistory history; history.initialize({}); history.record(state,"vignette"); EditHistory restored; QVERIFY(restored.restore(history.toJson(),state)); QCOMPARE(restored.undo().toJson(),original); QCOMPARE(restored.redo().toJson(),json);
        auto target=AdjustmentState{}; target.exposure=1; target.geometry.straighten=2;
        QVERIFY(AdjustmentTransfer::apply(target,state,{"effects"})); QCOMPARE(target.vignetteAmount,state.vignetteAmount); QCOMPARE(target.exposure,1.0); QCOMPARE(target.geometry.straighten,2.0);
        QVERIFY(CommandRegistry::execute(target,{{"command","vignette.reset"}})); QVERIFY(!target.toJson().contains("vignette")); QCOMPARE(target.exposure,1.0); QCOMPARE(target.geometry.straighten,2.0);
        QVERIFY(!CommandRegistry::execute(target,{{"command","vignette.reset"},{"extra",1}}));
        for (const auto field : {"schema","amount","midpoint","feather"}) {
            auto invalid=json; auto v=invalid["vignette"].toObject(); v.remove(field); invalid["vignette"]=v; QVERIFY(!AdjustmentState::validVignetteJson(invalid));
        }
        for (const auto key : {"amount","midpoint","feather"}) {
            auto invalid=json; auto v=invalid["vignette"].toObject(); v[key]=4; invalid["vignette"]=v; QVERIFY(!AdjustmentState::validVignetteJson(invalid));
        }
        auto invalid=json; auto v=invalid["vignette"].toObject(); v["schema"]=2; invalid["vignette"]=v; QVERIFY(!AdjustmentState::validVignetteJson(invalid));
        QTemporaryDir dir; NamedPresets presets(dir.filePath("presets.json")); QString error; QVERIFY2(presets.load(&error),qPrintable(error));
        QVERIFY2(presets.save("Vignette",state,&error),qPrintable(error));
        AdjustmentState preset; QVERIFY(presets.get("Vignette",&preset)); QCOMPARE(preset.toJson(),json);
        NamedPresets reloaded(dir.filePath("presets.json")); QVERIFY2(reloaded.load(&error),qPrintable(error));
        QVERIFY(reloaded.get("Vignette",&preset)); QCOMPARE(preset.toJson(),json);
        const auto exported=dir.filePath("vignette-preset.json"); QVERIFY2(reloaded.exportFile("Vignette",exported,&error),qPrintable(error));
        QVERIFY2(reloaded.importFile(exported,"Imported vignette",&error),qPrintable(error));
        QVERIFY(reloaded.get("Imported vignette",&preset)); QCOMPARE(preset.toJson(),json);
        const auto xmp=dir.filePath("vignette.xmp"); QVERIFY2(XmpSidecar::writeNew(xmp,state,{},0,"none",&error),qPrintable(error));
        XmpSidecar::Document read; QVERIFY2(XmpSidecar::read(xmp,&read,&error),qPrintable(error)); QCOMPARE(read.adjustments.toJson(),json);
    }
    void vignetteControllerKeepsViewportScopesHistoryAndProjectConsistent() {
        QTemporaryDir dir; QImage image(241,193,QImage::Format_RGB32); image.fill(QColor(64,96,128));
        const auto path=dir.filePath("source.png"); QVERIFY(image.save(path));
        QFile original(path); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes=original.readAll(); original.close();
        ProcessedImageProvider provider; PhotoController c(&provider); c.setGpuEnabled(false); c.setLanguage("en_US");
        QVERIFY(c.importFile(QUrl::fromLocalFile(path))); QTRY_VERIFY(c.previewReady() && !c.rendering());
        const auto fullSource=c.gpuSource().convertToFormat(QImage::Format_RGBA64); QCOMPARE(fullSource.size(),image.size());
        c.setExposure(.4); c.finishInteraction(); const auto initial=c.editHistory().size();
        for (double value : {-.3,-.8,-1.5}) c.setVignetteAmount(value); c.finishInteraction();
        QCOMPARE(c.editHistory().size(),initial+1); QCOMPARE(c.editHistory().last().toMap()["action"].toString(),QString("vignette_adjustment"));
        c.setVignetteMidpoint(.3); c.finishInteraction(); c.setVignetteFeather(.7); c.finishInteraction();
        const auto saved=c.gpuPlan(true).state; c.setExactScopes(true);
        QTRY_VERIFY_WITH_TIMEOUT(!c.rendering() && c.scopesStatus().contains("Full-resolution statistics"),10000);
        const auto full=ImagePipeline::processWithPlan(fullSource,c.gpuPlan(true)); const auto counts=ScopesEngine::analyze(full);
        QCOMPARE(c.redHistogram(),counts.red); QCOMPARE(c.lumaHistogram(),counts.luma);
        c.setViewport(81,69,1,1,.8,.7); QTRY_VERIFY_WITH_TIMEOUT(!c.rendering() && !c.gpuSource().isNull() && c.gpuSource().size()==QSize(81,69),10000);
        const auto mapped=c.gpuPlan(); QVERIFY(mapped.data[ProcessingPlan::FrameRect].z<1); QCOMPARE(c.gpuPlan(true).data[ProcessingPlan::FrameRect].z,1.f);
        const auto expected=ImagePipeline::processWithPlan(c.gpuSource().convertToFormat(QImage::Format_RGBA64),mapped);
        QTRY_COMPARE(StageGraph::outputFingerprint(provider.requestImage("current",nullptr,{}))["pixel_sha256"],StageGraph::outputFingerprint(expected)["pixel_sha256"]);
        QTRY_VERIFY_WITH_TIMEOUT(c.scopesStatus().contains("Full-resolution statistics"),10000);
        QCOMPARE(c.redHistogram(),counts.red); QCOMPARE(c.scopesPixelCount(),quint64(image.width())*image.height());
        c.setScopeMode("waveform"); QTRY_VERIFY_WITH_TIMEOUT(c.scopePlotCurrent(),10000);
        ScopePlotCounts ink("waveform"); QVERIFY(ink.add(full)); QCOMPARE(provider.requestImage("scopes/current",nullptr,{}),ink.image());
        QCOMPARE(c.scopePlotPixels(),quint64(image.width())*image.height());
        const auto diagnostic=c.reportBug(); QVERIFY(!diagnostic.isEmpty());
        const auto deps=QJsonDocument::fromJson(storedZipEntry(diagnostic,"performance.json")).object()["values"].toObject()["stage_dependencies"].toObject()["vignette"].toObject();
        QCOMPARE(deps["amount_ev"].toDouble(),saved.vignetteAmount); QVERIFY(deps["active"].toBool()); QVERIFY(!deps["lens_profile"].toBool());
        const auto outputs=QJsonDocument::fromJson(storedZipEntry(diagnostic,"stage_outputs.json")).object();
        QVERIFY(outputs["prepared_frame_rect"].toArray()[2].toDouble()<1);
        QCOMPARE(outputs["cpu_srgb_output"].toObject()["pixel_sha256"],StageGraph::outputFingerprint(expected)["pixel_sha256"]);
        QVERIFY(!colorStage(outputs["color_stages"].toObject(),"vignette").isEmpty()); QVERIFY(QFile::remove(diagnostic));
        c.resetVignette(); QCOMPARE(c.vignetteAmount(),0.0); QCOMPARE(c.exposure(),.4); c.undo(); QCOMPARE(c.gpuPlan(true).state.toJson(),saved.toJson());
        c.redo(); QCOMPARE(c.vignetteMidpoint(),.5); c.undo();
        QVERIFY(c.createVirtualCopy("Vignette copy")); c.resetVignette(); c.undo(); c.selectPhoto(0); QCOMPARE(c.gpuPlan(true).state.toJson(),saved.toJson());
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Vignette")); QVERIFY(c.flushEdits());
        PhotoController reopened(nullptr); reopened.setGpuEnabled(false); QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath())));
        QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved.toJson()); reopened.selectPhoto(1); QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved.toJson());
        QVERIFY(reopened.canRedo()); reopened.redo(); QVERIFY(!reopened.gpuPlan(true).state.toJson().contains("vignette")); QCOMPARE(reopened.exposure(),.4);
        reopened.selectPhoto(0); QCOMPARE(reopened.gpuPlan(true).state.toJson(),saved.toJson());
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),bytes);
    }
    void invalidVignetteProjectWithoutHistoryPreservesActiveWriterAndBytes() {
        QTemporaryDir dir; ProjectDatabase active,candidate; QVERIFY(active.create(dir.path(),"Active")); QVERIFY(candidate.create(dir.path(),"Candidate"));
        AdjustmentState state; state.vignetteAmount=-1; QVERIFY(candidate.addOrUpdatePhoto("source.png",state)); QVERIFY(candidate.flush());
        const auto activePath=active.projectPath(); const auto dbPath=QDir(candidate.projectPath()).filePath("Project.db");
        const auto valid=state.toJson()["vignette"].toObject(); QList<QJsonValue> invalid{QJsonArray{},QJsonObject{}};
        for (const auto *field : {"schema","amount","midpoint","feather"}) { auto v=valid; v.remove(field); invalid.append(v); }
        for (const auto *field : {"amount","midpoint","feather"}) { auto v=valid; v[field]=4; invalid.append(v); }
        auto future=valid; future["schema"]=2; invalid.append(future); auto extra=valid; extra["unknown"]=1; invalid.append(extra);
        invalid.append(QJsonObject{{"schema",1},{"amount",0},{"midpoint",.5},{"feather",1}});
        for (const auto &v : invalid) {
            const auto connection=QStringLiteral("invalid-vignette-test");
            { auto db=QSqlDatabase::addDatabase("QSQLITE",connection); db.setDatabaseName(dbPath); QVERIFY(db.open());
              auto json=state.toJson(); json["vignette"]=v; QSqlQuery q(db); q.prepare("UPDATE photos SET adjustment_json=?");
              q.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))); QVERIFY(q.exec()); }
            QSqlDatabase::removeDatabase(connection); QFile file(dbPath); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before=file.readAll(); file.close();
            QVector<ProjectDatabase::SavedPhoto> photos; QVERIFY(!active.open(candidate.projectPath(),&photos));
            QVERIFY(active.lastError().contains("vignette")); QCOMPARE(active.projectPath(),activePath); QVERIFY(active.isOpen()); QVERIFY(photos.isEmpty()); QVERIFY(active.flush());
            QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),before);
        }
        // The omitted field in old projects still decodes to the exact defaults.
        QVERIFY(!AdjustmentState{}.toJson().contains("vignette"));
    }
    void importNamingPreservesExtensionAndSelectionOrder() {
        const QStringList sources{"/one/photo.v1.ARW","/two/photo.v1.ARW","/two/花.png"};
        auto plan=planImportNames(sources,{"旅行_{seq:4}_{name}",7});
        QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        QCOMPARE(plan.names,QStringList({"旅行_0007_photo.v1.ARW","旅行_0008_photo.v1.ARW","旅行_0009_花.png"}));
        plan=planImportNames({"photo.ARW","flower.png"}); QVERIFY(plan.error.isEmpty());
        QCOMPARE(plan.names,QStringList({"photo.ARW","flower.png"}));
        plan=planImportNames({"a.dng","b.dng"},{"{seq:2}-{seq}-{name}",99});
        QCOMPARE(plan.names,QStringList({"99-99-a.dng","100-100-b.dng"})); // Padding never truncates.
        plan=planImportNames({"a.png","b.png"},{"{seq}",999999998});
        QCOMPARE(plan.names,QStringList({"999999998.png","999999999.png"}));
    }
    void importNamingRejectsUnsafePlansBeforeAnyCopy() {
        for (const QString &pattern : {QString("{name"),QString("{date}"),QString("{seq:0}"),QString("{seq:10}"),QString("}"),
             QString("../{name}"),QString("C:{name}"),QString("{name}\\x"),QString("CON"),QString("lpt¹"),QString("NUL.tar"),
             QString("x."),QString("x "),QString(".."),QString(241,'x'),QString(100,QChar(0x82b1))}) {
            const auto plan=planImportNames({"a.png"},{pattern,1});
            QVERIFY2(!plan.error.isEmpty(),qPrintable(pattern)); QVERIFY(plan.names.isEmpty());
        }
        QVERIFY(!planImportNames({"CON.ARW"}).error.isEmpty());
        QVERIFY(!planImportNames({"a.png","A.PNG"}).error.isEmpty());
        QVERIFY(!planImportNames({"é.png","e\u0301.png"},{"{name}",1}).error.isEmpty());
        QVERIFY(!planImportNames({"a.png","b.png"},{"{seq}",999999999}).error.isEmpty());
        QVERIFY(!planImportNames({"a.png"},{"{seq}",0}).error.isEmpty());
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkdir("out")); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::red);
        const auto source=dir.filePath("a.png"),out=dir.filePath("out"); QVERIFY(image.save(source));
        auto copied=copyImportFiles({source},out,{},{},{"../{name}",1}); QVERIFY(!copied.error.isEmpty()); QVERIFY(QDir(out).isEmpty());
        copied=copyImportFiles({source,source},out,{},{},{"{seq}",1}); QVERIFY(!copied.error.isEmpty()); QVERIFY(QDir(out).isEmpty());
    }
    void exportNamingUsesRecordedWallClockVersionAndChosenExtension() {
        const QVector<ExportNameSource> sources{{"/one/photo.v1.ARW",{},"2025-12-31 23:59:59"},
            {"/one/photo.v1.ARW","黄昏","2026-01-01 00:00:01"}};
        auto plan=planExportNames(sources,{"{capture_date}_{capture_time}_{seq:4}_{name}_{version}",7},"tiff");
        QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        QCOMPARE(plan.names,QStringList({"20251231_235959_0007_photo.v1_Original.tif","20260101_000001_0008_photo.v1_黄昏.tif"}));
        for (const auto &format : {QString("jpeg"),QString("png"),QString("webp")}) {
            const auto extension=format=="jpeg"?QString("jpg"):format;
            plan=planExportNames(sources,{"{seq:2}-{seq}",99},format);
            QCOMPARE(plan.names,QStringList({"99-99."+extension,"100-100."+extension}));
        }
        plan=planExportNames(sources,{"{seq}",999999998},"png");
        QCOMPARE(plan.names,QStringList({"999999998.png","999999999.png"}));
        QVector<ExportNameSource> many(1001,{"photo.png",{}, {}});
        QCOMPARE(planExportNames(many,{"{seq}",1},"png").names.size(),1001); // GUI keeps its existing catalog size support.
    }
    void exportNamingAvoidsDefaultCollisionsAndRejectsAmbiguousCustomPlans() {
        const QVector<ExportNameSource> sources{{"/one/a.ARW",{},{}},{"/one/a.ARW","Version",{}}};
        auto plan=planExportNames(sources,{},"png",{"A_JIXELLIGHT.PNG","a_JixelLight_1.png"});
        QCOMPARE(plan.names,QStringList({"a_JixelLight_2.png","a_JixelLight_3.png"}));
        QCOMPARE(planExportNames(sources,{{},999999999},"png").names,QStringList({"a_JixelLight.png","a_JixelLight_1.png"}));
        plan=planExportNames(sources,{"{name}",1},"png"); QVERIFY(!plan.error.isEmpty()); QVERIFY(plan.names.isEmpty());
        plan=planExportNames(sources,{"{name}_{version}",1},"png");
        QCOMPARE(plan.names,QStringList({"a_Original.png","a_Version.png"}));
        plan=planExportNames({{"a.png",{},{}}},{"{name}",1},"png",{"A.PNG"});
        QVERIFY(!plan.error.isEmpty()); QVERIFY(plan.names.isEmpty());
        plan=planExportNames({{"é.png",{},{}},{"e\u0301.png",{}, {}}},{"{name}",1},"png");
        QVERIFY(!plan.error.isEmpty()); QVERIFY(plan.names.isEmpty());
        plan=planExportNames({{"é.png",{}, {}}},{"{name}",1},"png",{"e\u0301.png"});
        QVERIFY(!plan.error.isEmpty());
        plan=planExportNames({{"a.png","../unsafe",{}}},{"{version}",1},"png");
        QVERIFY(!plan.error.isEmpty()); QVERIFY(plan.names.isEmpty());
    }
    void exportNamingRejectsUnsafeGrammarAndMissingDatesAsAWholePlan() {
        const QVector<ExportNameSource> source{{"a.ARW",{},"2025-02-28 12:34:56"}};
        for (const auto &pattern : QStringList{"{name","{unknown}","}","{seq:0}","{seq:10}","{seq:01}","../{name}",
             "C:{name}","{name}\\x","CON","LPT¹","NUL.tar","x.","x ","..",QString(161,'x'),QString(100,QChar(0x82b1)),QString("x")+QChar(1)}) {
            const auto plan=planExportNames(source,{pattern,1},"png");
            QVERIFY2(!plan.error.isEmpty(),qPrintable(pattern)); QVERIFY(!plan.errorZh.isEmpty()); QVERIFY(plan.names.isEmpty());
        }
        for (const auto &date : QStringList{{},"2025-02-29 12:34:56","2025-02-28 24:34:56","2025:02:28 12:34:56","2025-2-28 12:34:56"}) {
            auto mixed=source; mixed.push_back({"b.ARW",{},date});
            const auto plan=planExportNames(mixed,{"{seq}_{capture_date}_{capture_time}",1},"png");
            QVERIFY(!plan.error.isEmpty()); QVERIFY(plan.names.isEmpty());
        }
        QVERIFY(!planExportNames(source,{"{seq}",0}).error.isEmpty());
        QVERIFY(!planExportNames(source,{"{seq}",1000000000}).error.isEmpty());
        auto twice=source; twice+=source;
        QVERIFY(!planExportNames(twice,{"{seq}",999999999}).error.isEmpty());
        QVERIFY(!planExportNames(source,{},"pdf").error.isEmpty());
        QVERIFY(!planExportNames({}).error.isEmpty());
    }
    void batchExportNamesPreflightEntireCatalogAndFreezeVersionSnapshots() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(QDir(dir.path()).mkdir("out"));
        const auto jpeg=dir.filePath("dated.jpg"),out=dir.filePath("out");
        QImage image(32,24,QImage::Format_RGB32); image.fill(QColor(64,128,192)); QVERIFY(image.save(jpeg));
        { auto photo=Exiv2::ImageFactory::open(jpeg.toStdString()); QVERIFY(photo.get()); photo->readMetadata();
          auto exif=photo->exifData(); exif["Exif.Photo.DateTimeOriginal"]="2025:01:02 03:04:05"; photo->setExifData(exif); photo->writeMetadata(); }
        QFile original(jpeg); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes=original.readAll(); original.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(jpeg)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.library()[0].toMap()["captureChecked"].toBool(),10000);
        QVERIFY(!controller.exportAll(QUrl::fromLocalFile(dir.path()),"srgb",92,"jpeg","{name}"));
        controller.setExposure(.25); controller.finishInteraction();
        QVERIFY(controller.createVirtualCopy("Evening")); controller.setExposure(-.5); controller.finishInteraction();
        const auto history=controller.editHistory(); const auto state=controller.gpuPlan(true).state.toJson();
        auto preview=controller.exportNamePreview("{capture_date}_{capture_time}_{version}_{seq:3}",9,"png");
        QVERIFY(preview["valid"].toBool()); QCOMPARE(preview["count"].toInt(),2);
        QCOMPARE(preview["names"].toStringList(),QStringList({"20250102_030405_Original_009.png","20250102_030405_Evening_010.png"}));
        QVERIFY(!controller.exportNamePreview("{name}",1,"png")["valid"].toBool());
        QCOMPARE(controller.editHistory(),history); QCOMPARE(controller.gpuPlan(true).state.toJson(),state);
        QSignalSpy finished(&controller,&PhotoController::exportFinished);
        QVERIFY(!controller.exportAll(QUrl::fromLocalFile(out),"srgb",92,"png","{name}"));
        QVERIFY(QDir(out).isEmpty()); QVERIFY(!controller.exportBusy()); QCOMPARE(finished.size(),0);
        QVERIFY(controller.exportAll(QUrl::fromLocalFile(out),"srgb",92,"png","{capture_date}_{capture_time}_{version}_{seq:3}",9));
        controller.setExposure(2); controller.finishInteraction(); QVERIFY(controller.renameCurrentVirtualCopy("Later"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000); QCOMPARE(finished[0][0].toInt(),2); QCOMPARE(finished[0][1].toInt(),0);
        SourceCache cache(4*1024*1024,dir.filePath("cache")); const auto decoded=loadSource(cache,jpeg,{}); QVERIFY(!decoded.image.isNull());
        for (int i=0;i<2;++i) {
            AdjustmentState expected; expected.exposure=i==0?.25:-.5;
            const auto path=QDir(out).filePath(preview["names"].toStringList()[i]);
            QCOMPARE(QImage(path).convertToFormat(QImage::Format_RGBA64),ImagePipeline::processWithPlan(decoded.image,
                ProcessingPlan::compile(expected,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0)));
        }
        QCOMPARE(QDir(out).entryList({"*.png"},QDir::Files).size(),2);
        QVERIFY(QDir(out).entryList({".jixellight-export-*"},QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
        const auto undated=dir.filePath("undated.png"); QVERIFY(image.save(undated)); QVERIFY(controller.importFile(QUrl::fromLocalFile(undated)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.library()[2].toMap()["captureChecked"].toBool(),10000);
        QVERIFY(!controller.exportNamePreview("{capture_date}_{seq}",1,"png")["valid"].toBool());
        QVERIFY(!controller.exportAll(QUrl::fromLocalFile(out),"srgb",92,"png","{capture_date}_{seq}"));
        QCOMPARE(finished.size(),1); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),bytes);
    }
    void newFileExportQueuePreservesExistingTargetsAndCleansStaging() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto existing=dir.filePath("existing.png"),fresh=dir.filePath("fresh.png");
        QFile protectedFile(existing); QVERIFY(protectedFile.open(QIODevice::WriteOnly)); QCOMPARE(protectedFile.write("preserve"),8); protectedFile.close();
        QImage image(8,8,QImage::Format_RGBA64); image.fill(QColor(64,128,192));
        ExportQueue queue(std::make_shared<SourceCache>(1024*1024,dir.filePath("cache")));
        QSignalSpy finished(&queue,&ExportQueue::finished),files(&queue,&ExportQueue::fileFinished);
        QVERIFY(queue.start({{"fixture.png",existing,image,{},ColorManagement::OutputSpace::SRgb,92,true},
                             {"fixture.png",fresh,image,{},ColorManagement::OutputSpace::SRgb,92,true}}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000); QCOMPARE(finished[0][0].toInt(),1); QCOMPARE(finished[0][1].toInt(),1);
        QCOMPARE(files.size(),2); QVERIFY(!files[0][2].toBool()); QVERIFY(files[1][2].toBool());
        QVERIFY(protectedFile.open(QIODevice::ReadOnly)); QCOMPARE(protectedFile.readAll(),QByteArray("preserve"));
        QVERIFY(!QImage(fresh).isNull());
        QVERIFY(QDir(dir.path()).entryList({".jixellight-export-*"},QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
    }
    void namedCopyImportRetainsBytesAndHandlesConflictsCancellationAndRaces() {
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkdir("out")); QVERIFY(QDir(dir.path()).mkdir("other"));
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::green);
        const auto a=dir.filePath("same.png"),b=dir.filePath("other/same.png"),out=dir.filePath("out"); QVERIFY(image.save(a)); QVERIFY(image.save(b));
        QFile input(a); QVERIFY(input.open(QIODevice::ReadOnly)); const auto bytes=input.readAll(); input.close();
        auto result=copyImportFiles({a,b},out,{},{},{"image_{seq:4}",12});
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.completed.size(),2);
        QCOMPARE(QFileInfo(result.completed[0].destination).fileName(),QString("image_0012.png"));
        QCOMPARE(QFileInfo(result.completed[1].destination).fileName(),QString("image_0013.png"));
        for (const auto &entry : result.completed) { QFile output(entry.destination); QVERIFY(output.open(QIODevice::ReadOnly)); QCOMPARE(output.readAll(),bytes); }
        QVERIFY(!copyImportFiles({a,b},out,{},{},{"image_{seq:4}",12}).error.isEmpty());
        auto token=std::make_shared<std::atomic_bool>(false);
        result=copyImportFiles({a,b},out,token,[&](qint64,qint64,int done,int,const QString &stage) {
            if (done==1 && stage=="published") token->store(true);
        },{"cancel_{seq}",1});
        QVERIFY(result.wasCancelled); QCOMPARE(result.completed.size(),1); QVERIFY(!QFileInfo::exists(QDir(out).filePath("cancel_2.png")));
        result=copyImportFiles({a},out,{},[&](qint64,qint64,int,int,const QString &stage) {
            if (stage=="verify") { QFile competing(QDir(out).filePath("RACE_1.PNG")); QVERIFY(competing.open(QIODevice::WriteOnly|QIODevice::NewOnly)); QCOMPARE(competing.write("existing"),qint64(8)); }
        },{"race_{seq}",1});
        QVERIFY(!result.error.isEmpty()); QVERIFY(result.completed.isEmpty());
        QFile competing(QDir(out).filePath("RACE_1.PNG")); QVERIFY(competing.open(QIODevice::ReadOnly)); QCOMPARE(competing.readAll(),QByteArray("existing"));
        QVERIFY(QDir(out).entryList({".jixellight-import-*"},QDir::Dirs|QDir::Hidden).isEmpty());
        QVERIFY(input.open(QIODevice::ReadOnly)); QCOMPARE(input.readAll(),bytes);
    }
    void namedImportControllerPreviewAndCatalogUseTheSamePlan() {
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkdir("out")); QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::blue);
        const auto source=dir.filePath("a.png"),out=dir.filePath("out"); QVERIFY(image.save(source));
        PhotoController c(nullptr); c.setGpuEnabled(false); QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Named"));
        const QVariantList urls{QUrl::fromLocalFile(source)};
        const auto preview=c.previewImportNames(urls,"photo_{seq:4}_{name}",7); QVERIFY(preview["valid"].toBool());
        QCOMPARE(preview["rows"].toList().first().toMap()["destination"].toString(),QString("photo_0007_a.png"));
        QVERIFY(!c.previewImportNames(urls,"{unsupported}",1)["valid"].toBool());
        QVERIFY(!c.copyImport(urls,QUrl::fromLocalFile(out),"{unsupported}",1)); QVERIFY(!c.copyImportBusy()); QVERIFY(c.library().isEmpty()); QVERIFY(QDir(out).isEmpty());
        QVERIFY(c.copyImport(urls,QUrl::fromLocalFile(out),"photo_{seq:4}_{name}",7));
        QTRY_VERIFY_WITH_TIMEOUT(!c.copyImportBusy(),10000); QCOMPARE(c.library().size(),1);
        QCOMPARE(c.library().first().toMap()["path"].toString(),QDir(out).filePath("photo_0007_a.png"));
        QVERIFY(c.prepareToClose()); const auto project=c.projectPath();
        PhotoController reopened(nullptr); reopened.setGpuEnabled(false); QVERIFY(reopened.openProject(QUrl::fromLocalFile(project)));
        QCOMPARE(reopened.library().size(),1); QCOMPARE(reopened.library().first().toMap()["path"].toString(),QDir(out).filePath("photo_0007_a.png"));
        QVERIFY(QFileInfo::exists(source));
    }
    void adjustmentTransferGroupsAreCompleteIndependentAndRejectPartialInput() {
        AdjustmentState source,target;
        auto json=source.toJson(); int n=0;
        for (auto it=json.begin();it!=json.end();++it) {
            if (it.value().isDouble()) it.value()=++n*.1;
            else if (it.value().isArray()) { auto a=it.value().toArray(); for (int i=0;i<a.size();++i) a[i]=.1+i*.15; it.value()=a; }
        }
        source=AdjustmentState::fromJson(json); source.geometry.crop={.1,.2,.7,.6}; source.geometry.straighten=4;
        source.geometry.perspectiveHorizontal=.1; source.geometry.perspectiveVertical=-.2; source.geometry.distortion=.15; source.geometry.redCa=.7; source.geometry.blueCa=-.6;
        source.look.mode="calibrated"; source.look.lut=LookLut::identity(); source.look.strength=.7;
        const QMap<QString,QStringList> groups{
            {"exposure",{"exposure"}},{"white_balance",{"temperature","tint"}},
            {"tone",{"contrast","highlights","shadows","whites","blacks","highlightRecovery"}},
            {"color",{"hue","saturation","vibrance"}},{"hsl",{"hslHue","hslSaturation","hslLuminance"}},
            {"curves",{"masterCurve","redCurve","greenCurve","blueCurve"}},{"sony_look",{"look"}},{"geometry",{"geometry"}}
        };
        const auto before=target.toJson(),after=source.toJson(); QSet<QString> covered;
        for (auto group=groups.begin();group!=groups.end();++group) {
            auto actual=target; QVERIFY(AdjustmentTransfer::apply(actual,source,{group.key()}));
            auto expected=before; for (const auto &key : group.value()) { QVERIFY(!covered.contains(key)); covered.insert(key); expected[key]=after[key]; }
            QCOMPARE(actual.toJson(),expected);
            if (group.key()=="sony_look") QCOMPARE(actual.look.lut,source.look.lut);
        }
        const auto keys=after.keys(); QCOMPARE(covered,QSet<QString>(keys.begin(),keys.end()));
        QVERIFY(AdjustmentTransfer::apply(target,source,AdjustmentTransfer::allGroups())); QCOMPARE(target.toJson(),after);
        QCOMPARE(target.look.lut,source.look.lut);
        for (const QStringList &invalid : QList<QStringList>{{},{"exposure","unknown"},{"exposure","exposure"}}) {
            const auto saved=target.toJson(); QVERIFY(!AdjustmentTransfer::apply(target,AdjustmentState{},invalid)); QCOMPARE(target.toJson(),saved);
        }
        source.look={}; source.look.mode="as-shot";
        QVERIFY(AdjustmentTransfer::apply(target,source,{"sony_look"})); QCOMPARE(target.look.mode,QString("as-shot"));
        QVERIFY(target.look.code.isEmpty()); QVERIFY(target.look.parameters.isEmpty()); QVERIFY(!target.look.lut);
    }
    void selectivePasteUsesFrozenClipboardAndKeepsOtherGroupsAndUndo() {
        QTemporaryDir dir; QImage image(24,16,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto a=dir.filePath("a.png"),b=dir.filePath("b.png"); QVERIFY(image.save(a)); QVERIFY(image.save(b));
        PhotoController c(nullptr); c.setGpuEnabled(false); QVERIFY(c.importFile(QUrl::fromLocalFile(a))); QVERIFY(c.importFile(QUrl::fromLocalFile(b)));
        QVERIFY(!c.hasAdjustmentClipboard()); QVERIFY(!c.pasteAdjustmentGroups({"exposure"}));
        c.setExposure(1.5); c.setTemperature(12); c.setLookCode("FL"); c.setStraighten(3); c.copyAdjustments();
        QVERIFY(c.hasAdjustmentClipboard()); QCOMPARE(c.adjustmentClipboardName(),QString("a.png"));
        c.setExposure(2); c.setLookCode("VV"); c.selectPhoto(1); c.setExposure(-1); c.setTemperature(-8); c.setSaturation(25); c.setLookCode("ST"); c.setStraighten(-4);
        c.setRating(3); c.setSelectionKeywords("target"); c.finishInteraction(); const auto history=c.editHistory();
        QVERIFY(!c.pasteAdjustmentGroups({"exposure","bad"})); QCOMPARE(c.exposure(),-1.0); QCOMPARE(c.editHistory(),history);
        QVERIFY(c.pasteAdjustmentGroups({"exposure","sony_look"})); QCOMPARE(c.exposure(),1.5); QCOMPARE(c.lookState()["code"].toString(),QString("FL"));
        QCOMPARE(c.temperature(),-8.0); QCOMPARE(c.saturation(),25.0); QCOMPARE(c.geometry()["straighten"].toDouble(),-4.0);
        QCOMPARE(c.currentRating(),3); QCOMPARE(c.currentKeywords(),QStringList{"target"}); QCOMPARE(c.editHistory().size(),history.size()+1);
        c.undo(); QCOMPARE(c.exposure(),-1.0); QCOMPARE(c.lookState()["code"].toString(),QString("ST")); c.redo(); QCOMPARE(c.exposure(),1.5);
        const auto pastedHistory=c.editHistory(); QVERIFY(c.pasteAdjustmentGroups({"exposure","sony_look"})); QCOMPARE(c.editHistory(),pastedHistory);
        QVERIFY(c.pasteAdjustmentGroups({"geometry"})); QCOMPARE(c.geometry()["straighten"].toDouble(),3.0); c.undo(); QCOMPARE(c.geometry()["straighten"].toDouble(),-4.0);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Paste")); QVERIFY(c.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath()))); reopened.selectPhoto(1);
        QCOMPARE(reopened.exposure(),1.5); QCOMPARE(reopened.geometry()["straighten"].toDouble(),-4.0); QVERIFY(reopened.canRedo());
        reopened.redo(); QCOMPARE(reopened.geometry()["straighten"].toDouble(),3.0);
        QFile original(a); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes=original.readAll(); original.close();
        QFile second(b); QVERIFY(second.open(QIODevice::ReadOnly)); QCOMPARE(second.readAll(),bytes);
    }
    void selectiveSyncExcludesSourceAndUnselectedVersionsAndPersistsPerTargetHistory() {
        QTemporaryDir dir; QImage image(24,16,QImage::Format_RGB32); image.fill(Qt::gray); PhotoController c(nullptr); c.setGpuEnabled(false);
        for (const auto &name : {"a.png","b.png","c.png"}) { const auto path=dir.filePath(name); QVERIFY(image.save(path)); QVERIFY(c.importFile(QUrl::fromLocalFile(path))); }
        c.selectPhoto(1); c.setExposure(-1); c.setStraighten(-4); c.setRating(2); c.setSelectionKeywords("keep"); c.finishInteraction();
        const auto targetHistory=c.editHistory();
        c.selectPhoto(0); c.setExposure(1.25); c.setTemperature(17); c.setLookCode("FL"); c.setStraighten(5); c.finishInteraction();
        QVERIFY(c.createVirtualCopy("independent")); c.setExposure(-2); c.setStraighten(9); c.finishInteraction();
        c.selectPhoto(0); c.setSaturation(30); c.undo(); QVERIFY(c.canRedo()); const auto sourceHistory=c.editHistory();
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Sync")); QVERIFY(c.flushEdits());
        QVERIFY(c.setPhotoSelection({0})); QCOMPARE(c.syncAdjustmentGroups({"exposure"},true),0);
        QVERIFY(c.setPhotoSelection({0,1,3})); const auto before=c.library();
        QCOMPARE(c.syncAdjustmentGroups({"exposure","white_balance","sony_look"},true),2);
        QCOMPARE(c.editHistory(),sourceHistory); QVERIFY(c.canRedo()); QCOMPARE(c.selectedIndices(),QVariantList({0,1,3}));
        const auto once=c.library(); QCOMPARE(c.syncAdjustmentGroups({"exposure","white_balance","sony_look"},true),0); QCOMPARE(c.library(),once);
        QCOMPARE(c.syncAdjustmentGroups({"exposure","invalid"},false),-1); QCOMPARE(c.library(),once);
        QCOMPARE(c.library()[2],before[2]);
        c.selectPhoto(1); QCOMPARE(c.exposure(),1.25); QCOMPARE(c.temperature(),17.0); QCOMPARE(c.geometry()["straighten"].toDouble(),-4.0);
        QCOMPARE(c.currentRating(),2); QCOMPARE(c.currentKeywords(),QStringList{"keep"}); QCOMPARE(c.editHistory().size(),targetHistory.size()+1);
        c.undo(); QCOMPARE(c.exposure(),-1.0); QCOMPARE(c.temperature(),0.0); QVERIFY(c.canRedo()); QVERIFY(c.flushEdits());
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> saved; QVERIFY(db.readSnapshot(c.projectPath(),&saved));
        int looks=0; for (const auto &photo : saved) if (photo.adjustments.look.code=="FL") ++looks;
        QCOMPARE(looks,2); // Source + virtual copy; target B is at its pre-sync undo cursor.
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath()))); reopened.selectPhoto(1);
        QCOMPARE(reopened.exposure(),-1.0); QVERIFY(reopened.canRedo()); reopened.redo(); QCOMPARE(reopened.exposure(),1.25);
        reopened.selectPhoto(3); QCOMPARE(reopened.exposure(),1.25); QCOMPARE(reopened.geometry()["straighten"].toDouble(),9.0);
        reopened.undo(); QCOMPARE(reopened.exposure(),-2.0); QCOMPARE(reopened.geometry()["straighten"].toDouble(),9.0);
        reopened.selectPhoto(0); QCOMPARE(reopened.syncAdjustmentGroups({"exposure"},false),2); // B already matches.
        reopened.selectPhoto(2); QCOMPARE(reopened.exposure(),1.25); QVERIFY(reopened.canUndo());
    }
    void selectiveSyncDatabaseFailureRollsBackBatchAndCanRetry() {
        QTemporaryDir dir; QImage image(12,8,QImage::Format_RGB32); image.fill(Qt::gray); PhotoController c(nullptr); c.setGpuEnabled(false);
        for (const auto &name : {"a.png","b.png","c.png"}) { const auto path=dir.filePath(name); QVERIFY(image.save(path)); QVERIFY(c.importFile(QUrl::fromLocalFile(path))); }
        c.setExposure(1); c.finishInteraction(); QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"RetrySync")); QVERIFY(c.flushEdits());
        const QString connection="sync-failure-fixture";
        auto trigger=[&](bool enable) {
            { auto db=QSqlDatabase::addDatabase("QSQLITE",connection); db.setDatabaseName(QDir(c.projectPath()).filePath("Project.db")); QVERIFY(db.open());
              QSqlQuery q(db); QVERIFY(q.exec(enable ? "CREATE TRIGGER block_sync BEFORE UPDATE ON photos WHEN NEW.path LIKE '%b.png' BEGIN SELECT RAISE(ABORT,'blocked sync'); END" : "DROP TRIGGER block_sync")); db.close(); }
            QSqlDatabase::removeDatabase(connection);
        };
        trigger(true); QCOMPARE(c.syncAdjustmentGroups({"exposure"},false),2); QVERIFY(!c.flushEdits());
        const auto saveError=c.statusMessage(); QCOMPARE(c.syncAdjustmentGroups({"exposure"},false),0); QCOMPARE(c.statusMessage(),saveError);
        ProjectDatabase db; QVector<ProjectDatabase::SavedPhoto> saved; QVERIFY(db.readSnapshot(c.projectPath(),&saved));
        for (const auto &photo : saved) if (!photo.path.endsWith("a.png")) QCOMPARE(photo.adjustments.exposure,0.0);
        QCoreApplication::processEvents(); trigger(false); QCOMPARE(c.syncAdjustmentGroups({"exposure"},false),0); QVERIFY(c.flushEdits()); QVERIFY(db.readSnapshot(c.projectPath(),&saved));
        for (auto &photo : saved) { QCOMPARE(photo.adjustments.exposure,1.0); if (!photo.path.endsWith("a.png")) { QVERIFY(photo.history.canUndo()); QCOMPARE(photo.history.undo().exposure,0.0); } }
    }
    void copyImportVerifiesContentAndRejectsWholePlanConflicts() {
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkdir("out")); QVERIFY(QDir(dir.path()).mkdir("other"));
        QImage image(20,10,QImage::Format_RGB32); image.fill(Qt::red);
        const auto first=dir.filePath("photo.png"),second=dir.filePath("other/photo.png"),out=dir.filePath("out");
        QVERIFY(image.save(first)); QVERIFY(image.save(second));
        QFile original(first); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes=original.readAll(); original.close();
        auto result=copyImportFiles({first,dir.filePath("missing.png")},out); QVERIFY(!result.error.isEmpty()); QVERIFY(result.completed.isEmpty()); QVERIFY(QDir(out).isEmpty());
        result=copyImportFiles({first,second},out); QVERIFY(!result.error.isEmpty()); QVERIFY(QDir(out).isEmpty());
        result=copyImportFiles({first},out); QVERIFY(result.error.isEmpty()); QCOMPARE(result.completed.size(),1);
        QFile copied(QDir(out).filePath("photo.png")); QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(),bytes); copied.close();
        QCOMPARE(result.completed[0].sha256,QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));
        QVERIFY(!copyImportFiles({first},out).error.isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),bytes); original.close();
        QVERIFY(QFile::remove(copied.fileName()));
        { QFile upper(QDir(out).filePath("PHOTO.PNG")); QVERIFY(upper.open(QIODevice::WriteOnly|QIODevice::NewOnly)); QCOMPARE(upper.write(bytes),qint64(bytes.size())); }
        QVERIFY(!copyImportFiles({first},out).error.isEmpty());
        QCOMPARE(QDir(out).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{"PHOTO.PNG"});
    }
    void copyImportCancellationAndPublicationRaceCleanStaging() {
        QTemporaryDir dir; const auto out=dir.filePath("out"); QVERIFY(QDir(dir.path()).mkdir("out"));
        QImage image(10,10,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto first=dir.filePath("first.png"), second=dir.filePath("second.png"); QVERIFY(image.save(first)); QVERIFY(image.save(second));
        { QFile large(second); QVERIFY(large.open(QIODevice::Append)); QCOMPARE(large.write(QByteArray(3*1024*1024,'x')),qint64(3*1024*1024)); }
        auto token=std::make_shared<std::atomic_bool>(false);
        const auto result=copyImportFiles({first,second},out,token,[&](qint64,qint64,int done,int,const QString &stage) { if (done==1 && stage=="copy") token->store(true); });
        QVERIFY(result.wasCancelled); QVERIFY(result.error.isEmpty()); QCOMPARE(result.completed.size(),1);
        QCOMPARE(QDir(out).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{"first.png"});
        QVERIFY(QFileInfo::exists(first)); QVERIFY(QFileInfo::exists(second));
        QVERIFY(QFile::remove(QDir(out).filePath("first.png")));
        const auto collision=QDir(out).filePath("first.png");
        const auto raced=copyImportFiles({first},out,{},[&](qint64,qint64,int,int,const QString &stage) {
            if (stage=="verify") { QFile file(collision); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly)); QCOMPARE(file.write("existing"),qint64(8)); }
        });
        QVERIFY(!raced.error.isEmpty()); QVERIFY(raced.completed.isEmpty());
        QFile existing(collision); QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(),QByteArray("existing")); existing.close();
        QCOMPARE(QDir(out).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{"first.png"});
        QVERIFY(copyImportFiles({second},out,token).wasCancelled);
    }
    void copyImportDetectsSourceChangesAndPreservesEarlierResults() {
        QTemporaryDir dir; const auto out=dir.filePath("out"); QVERIFY(QDir(dir.path()).mkdir("out"));
        QImage image(10,10,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto first=dir.filePath("first.png"),second=dir.filePath("second.png"); QVERIFY(image.save(first)); QVERIFY(image.save(second));
        bool changed=false;
        const auto result=copyImportFiles({first,second},out,{},[&](qint64,qint64,int done,int,const QString &stage) {
            if (done==1 && stage=="published" && !changed) { QFile file(second); QVERIFY(file.open(QIODevice::Append)); QCOMPARE(file.write("changed"),qint64(7)); changed=true; }
        });
        QVERIFY(changed); QVERIFY(!result.error.isEmpty()); QCOMPARE(result.completed.size(),1);
        QCOMPARE(QDir(out).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),QStringList{"first.png"});
        QVERIFY(QFile::remove(QDir(out).filePath("first.png")));
        const auto timestamp=QFileInfo(first).lastModified();
        const auto sameSize=copyImportFiles({first},out,{},[&](qint64,qint64,int,int,const QString &stage) {
            if (stage=="verify") {
                QFile file(first); QVERIFY(file.open(QIODevice::ReadWrite)); QVERIFY(file.seek(file.size()-1));
                QCOMPARE(file.write("X"),qint64(1)); QVERIFY(file.flush()); QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime));
            }
        });
        QVERIFY(sameSize.error.contains("Source content changed")); QVERIFY(sameSize.completed.isEmpty()); QVERIFY(QDir(out).isEmpty());
    }
    void copyImportControllerKeepsProjectAndPersistsCompletedCopiesOnClose() {
        QTemporaryDir dir; const auto out=dir.filePath("out"); QVERIFY(QDir(dir.path()).mkdir("out"));
        QImage image(20,10,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto first=dir.filePath("first.png"), second=dir.filePath("second.png"); QVERIFY(image.save(first)); QVERIFY(image.save(second));
        { QFile large(second); QVERIFY(large.open(QIODevice::Append)); QCOMPARE(large.write(QByteArray(16*1024*1024,'x')),qint64(16*1024*1024)); }
        PhotoController controller(nullptr); controller.setGpuEnabled(false); QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Copied"));
        const auto project=controller.projectPath(); bool closeCollected=false,closeOk=false;
        connect(&controller,&PhotoController::copyImportChanged,&controller,[&] {
            if (!closeCollected && controller.copyImportBusy() && controller.copyImportStatus().contains("1/2")) {
                closeCollected=true; closeOk=controller.prepareToClose();
            }
        });
        QVERIFY(controller.copyImport({QUrl::fromLocalFile(first),QUrl::fromLocalFile(second)},QUrl::fromLocalFile(out)));
        QVERIFY(controller.copyImportBusy()); QVERIFY(!controller.createProject(QUrl::fromLocalFile(dir.path()),"Wrong"));
        QVERIFY(!controller.openProject(QUrl::fromLocalFile(project))); QCOMPARE(controller.projectPath(),project);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.copyImportBusy(),10000); QVERIFY(closeCollected); QVERIFY(closeOk);
        QVERIFY(!controller.library().isEmpty());
        for (const auto &row : controller.library()) QVERIFY(row.toMap()["path"].toString().startsWith(out+"/"));
        ProjectDatabase read; QVector<ProjectDatabase::SavedPhoto> saved; QVERIFY(read.readSnapshot(project,&saved)); QCOMPARE(saved.size(),controller.library().size());
        QVERIFY(QFileInfo::exists(first)); QVERIFY(QFileInfo::exists(second));
        QVERIFY(QDir(out).entryList({".jixellight-import-*"},QDir::Dirs|QDir::Hidden).isEmpty());
        // A new job immediately after synchronous collection cannot consume an
        // older watcher's queued finished/progress signal as its own result.
        const auto third=dir.filePath("third.png"); QVERIFY(image.save(third));
        QVERIFY(controller.copyImport({QUrl::fromLocalFile(third)},QUrl::fromLocalFile(out)));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.copyImportBusy(),10000); QVERIFY(QFileInfo::exists(QDir(out).filePath("third.png")));
        QVERIFY(controller.prepareToClose());
    }
    void manualGeometrySamplesKnownProjectiveRadialAndCaCoordinates() {
        QImage source(101,61,QImage::Format_RGBA64); source.setColorSpace(QColorSpace::SRgbLinear);
        source.setText("JixelLightSource","RAW"); source.setDevicePixelRatio(2); source.setDotsPerMeterX(3000);
        for (int y=0;y<61;++y) for (int x=0;x<101;++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(1001+101*x+37*y,2003+53*x+211*y,3007+13*x+17*y,65535);
        const auto before=StageGraph::outputFingerprint(source);
        for (double h : {-.4,0.0,.4}) for (double v : {-.4,0.0,.4}) for (double k : {-.3,.3}) for (double angle : {-31.0,0.0,37.0}) {
            GeometryState g; g.perspectiveHorizontal=h; g.perspectiveVertical=v; g.distortion=k;
            g.redCa=2; g.blueCa=-2; g.straighten=angle;
            const auto out=g.apply(source); QVERIFY(!out.isNull()); QCOMPARE(out.size(),g.correctedSize(source.size()));
            QCOMPARE(out.format(),QImage::Format_RGBA64); QCOMPARE(out.colorSpace(),source.colorSpace());
            QCOMPARE(out.text("JixelLightSource"),QString("RAW")); QCOMPARE(out.devicePixelRatio(),2.0); QCOMPARE(out.dotsPerMeterX(),3000);
            QVERIFY(out.width()<=101 && out.height()<=61);
            const double radians=angle*std::acos(-1)/180,c=std::cos(radians),s=std::sin(radians);
            for (int y=0;y<out.height();++y) for (int x=0;x<out.width();++x) {
                const double dx=x-(out.width()-1)/2.0,dy=y-(out.height()-1)/2.0;
                const double rx=c*dx+s*dy,ry=-s*dx+c*dy,denominator=1+h*rx/50+v*ry/30;
                QVERIFY(denominator>0); const double ux=rx/denominator,uy=ry/denominator;
                const double factor=1+k*(ux*ux+uy*uy)/3400;
                const double gx=50+ux*factor,gy=30+uy*factor;
                const double redX=50+ux*factor*1.02,redY=30+uy*factor*1.02;
                const double blueX=50+ux*factor*.98,blueY=30+uy*factor*.98;
                for (double value : {gx,redX,blueX}) QVERIFY(value>=-1e-8 && value<=100+1e-8);
                for (double value : {gy,redY,blueY}) QVERIFY(value>=-1e-8 && value<=60+1e-8);
                const auto p=reinterpret_cast<const QRgba64 *>(out.constScanLine(y))[x];
                QVERIFY(std::abs(int(p.red())-std::lround(1001+101*redX+37*redY))<=1);
                QVERIFY(std::abs(int(p.green())-std::lround(2003+53*gx+211*gy))<=1);
                QVERIFY(std::abs(int(p.blue())-std::lround(3007+13*blueX+17*blueY))<=1); QCOMPARE(p.alpha(),quint16(65535));
            }
        }
        QCOMPARE(StageGraph::outputFingerprint(source),before);
    }
    void manualGeometryTrimsOpaqueEdgesAndRetainsAssociatedAlpha() {
        const auto color=QRgba64::fromRgba64(12345,23456,34567,65535);
        for (QSize size : {QSize(101,61),QSize(61,101),QSize(1,9),QSize(9,1),QSize(1,1)})
        for (double angle : {-45.0,0.0,45.0}) for (double k : {-.3,.3}) for (double ca : {-2.0,2.0}) {
            QImage input(size,QImage::Format_RGBA64); input.fill(QColor::fromRgba64(color));
            GeometryState g; g.straighten=angle; g.perspectiveHorizontal=.4; g.perspectiveVertical=-.4;
            g.distortion=k; g.redCa=ca; g.blueCa=-ca;
            const auto out=g.apply(input); QVERIFY(!out.isNull());
            QVERIFY(out.width()<=size.width() && out.height()<=size.height());
            for (int y=0;y<out.height();++y) for (int x=0;x<out.width();++x)
                QCOMPARE(reinterpret_cast<const QRgba64 *>(out.constScanLine(y))[x],color);
        }
        QImage transparent(2,2,QImage::Format_RGBA64); transparent.fill(QColor(0,0,255,0));
        reinterpret_cast<QRgba64 *>(transparent.scanLine(0))[0]=QRgba64::fromRgba64(65535,0,0,65535);
        GeometryState g; g.redCa=2; const auto center=g.apply(transparent); QCOMPARE(center.size(),QSize(1,1));
        const auto p=reinterpret_cast<const QRgba64 *>(center.constScanLine(0))[0];
        QCOMPARE(p.red(),quint16(65535)); QCOMPARE(p.green(),quint16(0)); QCOMPARE(p.blue(),quint16(0)); QCOMPARE(p.alpha(),quint16(16384));
        g.distortion=std::numeric_limits<double>::quiet_NaN(); QVERIFY(g.apply(transparent).isNull());
    }
    void radialCaCorrectsKnownChannelMagnification() {
        QImage source(201,151,QImage::Format_RGBA64);
        auto value=[](double x,double y) { return quint16(std::lround(25000+100*x+60*y)); };
        for (int y=0;y<151;++y) for (int x=0;x<201;++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(value((x-100)/1.02,(y-75)/1.02),value(x-100,y-75),value((x-100)/.98,(y-75)/.98),65535);
        const auto edge=source.pixelColor(0,0).rgba64(); QVERIFY(std::abs(int(edge.red())-int(edge.green()))>200);
        GeometryState g; g.redCa=2; g.blueCa=-2; const auto corrected=g.apply(source); QVERIFY(!corrected.isNull());
        for (int y=0;y<corrected.height();++y) for (int x=0;x<corrected.width();++x) {
            const auto p=reinterpret_cast<const QRgba64 *>(corrected.constScanLine(y))[x];
            QVERIFY(std::abs(int(p.red())-int(p.green()))<=1); QVERIFY(std::abs(int(p.blue())-int(p.green()))<=1);
        }
    }
    void manualGeometrySharesPreparationCacheScopesAndExports() {
        QImage source(31,23,QImage::Format_RGBA64);
        for (int y=0;y<23;++y) for (int x=0;x<31;++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(1000+x*1701,500+y*2101,100+x*991+y*601,65535);
        AdjustmentState state; state.exposure=.3; auto &g=state.geometry;
        g.perspectiveHorizontal=.13; g.perspectiveVertical=-.11; g.distortion=.18; g.redCa=.6; g.blueCa=-.8; g.straighten=7;
        const auto corrected=g.apply(source); QVERIFY(!corrected.isNull());
        auto cropped=g; cropped.crop={.2,.1,.6,.8}; cropped.quarterTurns=1; cropped.flipHorizontal=true;
        GeometryState rest; rest.crop=cropped.crop; rest.quarterTurns=1; rest.flipHorizontal=true;
        QCOMPARE(cropped.apply(source),rest.apply(corrected));
        PrepareRequest prepare; prepare.image=source; prepare.geometry=g; prepare.viewport={4096,4096};
        const auto preview=preparePreview(prepare,{}); QCOMPARE(preview.normal,corrected);
        QCOMPARE(preview.gpu.convertToFormat(QImage::Format_RGBA64),corrected);
        const auto key=StageGraph::prepareKey(prepare);
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        ScopeRequest request{source,plan,1,true,g}; const auto scopesKey=StageGraph::fullScopesKey(request);
        for (const auto &p : CommandRegistry::geometryParameters()) {
            prepare.geometry=g; prepare.geometry.*p.member+=.01; QVERIFY(StageGraph::prepareKey(prepare)!=key);
            request.geometry=prepare.geometry; QVERIFY(StageGraph::fullScopesKey(request)!=scopesKey);
        }
        QTemporaryDir dir; FullScopesCache cache;
        for (bool raw : {false,true}) for (auto space : {ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::AdobeRgb,ColorManagement::OutputSpace::ProPhotoRgb}) {
            request.plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,raw,0); request.geometry=g;
            const auto counts=cache.analyze(request),fresh=ScopesEngine::analyzeFull(corrected,request.plan);
            QCOMPARE(counts.red,fresh.red); QCOMPARE(counts.green,fresh.green); QCOMPARE(counts.blue,fresh.blue); QCOMPARE(counts.luma,fresh.luma);
            QCOMPARE(counts.pixelCount,quint64(corrected.width())*corrected.height());
            ++request.revision; const auto hit=cache.analyze(request); QCOMPARE(hit.red.constData(),counts.red.constData());
            const auto rendered=ImagePipeline::processWithPlan(corrected,request.plan);
            for (auto format : {RasterFormat::Png16,RasterFormat::Tiff16,RasterFormat::WebP8}) {
                const auto path=dir.filePath(format==RasterFormat::Png16 ? "out.png" : format==RasterFormat::Tiff16 ? "out.tif" : "out.webp"); QString error;
                QVERIFY2(exportRaster(source,state,path,space,format,100,{},&error,raw),qPrintable(error));
                const auto actual=QImageReader(path).read(); QCOMPARE(actual.size(),rendered.size()); QCOMPARE(actual.colorSpace().iccProfile(),rendered.colorSpace().iccProfile());
                if (format==RasterFormat::WebP8) QCOMPARE(actual.convertToFormat(QImage::Format_RGBA8888),rendered.convertToFormat(QImage::Format_RGBA8888));
                else QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),rendered);
            }
        }
        const auto rendered=ImagePipeline::processWithPlan(corrected,plan);
        const auto plot=renderScopePlot({source,plan,g,"waveform",1,true},{}); ScopePlotCounts reference("waveform"); QVERIFY(reference.add(rendered));
        QCOMPARE(plot.image,reference.image()); QCOMPARE(plot.pixels,quint64(corrected.width())*corrected.height());
        QVERIFY(g.apply(source,std::make_shared<std::atomic_bool>(true)).isNull());
    }
    void manualGeometryHistoryProjectCopiesAndCropRemainIndependent() {
        QTemporaryDir dir; QImage image(80,60,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path=dir.filePath("source.png"); QVERIFY(image.save(path)); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll(); file.close();
        PhotoController c(nullptr); c.setGpuEnabled(false); QVERIFY(c.importFile(QUrl::fromLocalFile(path))); QTRY_VERIFY(c.previewReady() && !c.rendering());
        const auto initial=c.editHistory().size();
        for (double v : {.01,.08,.13}) QVERIFY(c.setGeometryAdjustment("perspectiveHorizontal",v)); c.finishInteraction();
        QCOMPARE(c.editHistory().size(),initial+1);
        for (const auto &p : CommandRegistry::geometryParameters()) { QVERIFY(c.setGeometryAdjustment(p.name,p.minimum/2)); c.finishInteraction(); }
        auto saved=c.geometry(); QCOMPARE(saved["schema"].toInt(),3);
        QVERIFY(!c.setGeometryAdjustment("redCa",2.01)); QCOMPARE(c.geometry(),saved);
        c.setExactScopes(true); auto geometry=GeometryState::fromJson(QJsonObject::fromVariantMap(saved)); const auto size=geometry.correctedSize(image.size());
        QTRY_COMPARE_WITH_TIMEOUT(c.scopesPixelCount(),quint64(size.width())*size.height(),10000);
        QTRY_VERIFY(!c.rendering()); QVERIFY(c.beginCrop()); QTRY_VERIFY(!c.rendering()); QCOMPARE(c.gpuSource().size(),size);
        QVERIFY(!c.setGeometryAdjustment("distortion",.1)); QVERIFY(c.applyCrop(.25,0,.5,1)); c.undo(); QCOMPARE(c.geometry(),saved);
        c.setCropAspect(1.5); const auto crop=c.geometry();
        const double expectedWidth=std::min(1.0,1.5*size.height()/size.width());
        const double expectedHeight=std::min(1.0,double(size.width())/size.height()/1.5);
        QCOMPARE(crop["width"].toDouble(),expectedWidth); QCOMPARE(crop["height"].toDouble(),expectedHeight); c.undo(); QCOMPARE(c.geometry(),saved);
        const auto diagnostic=c.reportBug(); QVERIFY(!diagnostic.isEmpty());
        const auto dependencies=QJsonDocument::fromJson(storedZipEntry(diagnostic,"performance.json")).object()["values"].toObject()["stage_dependencies"].toObject()["geometry"].toObject();
        QCOMPARE(dependencies["settings"].toObject(),QJsonObject::fromVariantMap(saved)); QCOMPARE(dependencies["backend"].toString(),QString("cpu"));
        QVERIFY(dependencies["corrections_active"].toBool()); QVERIFY(!dependencies["lens_profile"].toBool());
        const auto outputs=QJsonDocument::fromJson(storedZipEntry(diagnostic,"stage_outputs.json")).object();
        QCOMPARE(outputs["prepared_preview"].toObject()["width"].toInt(),size.width()); QVERIFY(QFile::remove(diagnostic));
        QVERIFY(c.createVirtualCopy("Corrected")); QCOMPARE(c.geometry(),saved); c.resetGeometryCorrections(); QCOMPARE(c.geometry()["schema"].toInt(),1);
        c.finishInteraction(); c.undo(); QCOMPARE(c.geometry(),saved); c.selectPhoto(0); QCOMPARE(c.geometry(),saved);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Corrected")); QVERIFY(c.flushEdits());
        PhotoController reopened(nullptr); reopened.setGpuEnabled(false); QVERIFY(reopened.openProject(QUrl::fromLocalFile(c.projectPath()))); QCOMPARE(reopened.geometry(),saved);
        reopened.selectPhoto(1); QCOMPARE(reopened.geometry(),saved); QVERIFY(reopened.canRedo()); reopened.redo(); QCOMPARE(reopened.geometry()["schema"].toInt(),1);
        reopened.selectPhoto(0); QCOMPARE(reopened.geometry(),saved);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes);
    }
    void manualGeometryCommandsAndSchemaRejectInvalidSnapshots() {
        AdjustmentState state; state.geometry.straighten=5; state.geometry.crop={.1,.2,.6,.7}; state.geometry.quarterTurns=1;
        for (const auto &p : CommandRegistry::geometryParameters()) {
            QVERIFY(CommandRegistry::execute(state,{{"command","geometry.set"},{"parameter",p.name},{"value",p.maximum}}));
            QCOMPARE(state.geometry.*p.member,p.maximum);
            QVERIFY(GeometryState::validJson(state.geometry.toJson())); QCOMPARE(AdjustmentState::fromJson(state.toJson()).toJson(),state.toJson());
            const auto before=state.toJson();
            for (const QJsonValue &value : {QJsonValue(p.minimum-.001),QJsonValue(p.maximum+.001),QJsonValue("1"),QJsonValue()}) {
                QVERIFY(!CommandRegistry::execute(state,{{"command","geometry.set"},{"parameter",p.name},{"value",value}})); QCOMPARE(state.toJson(),before);
            }
            auto json=state.geometry.toJson(); json.remove(p.name); QVERIFY(!GeometryState::validJson(json));
            json=state.geometry.toJson(); json[p.name]=p.maximum+.001; QVERIFY(!GeometryState::validJson(json));
        }
        const auto before=state.toJson();
        for (auto command : {QJsonObject{{"command","geometry.set"},{"parameter","unknown"},{"value",1}},
                QJsonObject{{"command","geometry.set"},{"parameter","redCa"},{"value",1},{"extra",1}},QJsonObject{{"command","geometry.resetCorrections"},{"extra",1}}}) {
            QVERIFY(!CommandRegistry::execute(state,command)); QCOMPARE(state.toJson(),before);
        }
        auto future=state.geometry.toJson(); future["schema"]=4; QVERIFY(!GeometryState::validJson(future));
        QVERIFY(CommandRegistry::execute(state,{{"command","geometry.resetCorrections"}}));
        QCOMPARE(state.geometry.toJson()["schema"].toInt(),2); QCOMPARE(state.geometry.straighten,5.0); QCOMPARE(state.geometry.crop,QRectF(.1,.2,.6,.7)); QCOMPARE(state.geometry.quarterTurns,1);
    }
    void straightenResamplesLinear16BitWithoutBlackCornersOrUpscaling() {
        QImage source(101,61,QImage::Format_RGBA64); source.setColorSpace(QColorSpace::SRgbLinear);
        source.setText("JixelLightSource","RAW"); source.setDotsPerMeterX(3000); source.setDevicePixelRatio(2);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(1001+101*x+37*y,2003+53*x+211*y,3007+13*x+17*y,65535);
        const auto unchanged=source.copy(); GeometryState g;
        QCOMPARE(g.apply(source).cacheKey(),source.cacheKey()); QCOMPARE(g.toJson()["schema"].toInt(),1);
        for (double degrees : {-45.0,-13.25,.1,23.0,45.0}) {
            g.straighten=degrees; const auto output=g.apply(source); QVERIFY(!output.isNull());
            QCOMPARE(output.format(),QImage::Format_RGBA64); QCOMPARE(output.colorSpace(),source.colorSpace());
            QCOMPARE(output.text("JixelLightSource"),QStringLiteral("RAW")); QCOMPARE(output.devicePixelRatio(),2.0); QCOMPARE(output.dotsPerMeterX(),3000);
            QVERIFY(output.width()<source.width()); QVERIFY(output.height()<source.height());
            const double angle=degrees*std::acos(-1)/180, c=std::cos(angle), s=std::sin(angle);
            for (int y=0;y<output.height();++y) for (int x=0;x<output.width();++x) {
                const double u=x-(output.width()-1)/2.0, v=y-(output.height()-1)/2.0;
                const double sx=50+c*u+s*v, sy=30-s*u+c*v;
                QVERIFY(sx>=-1e-9 && sx<=100+1e-9 && sy>=-1e-9 && sy<=60+1e-9);
                const auto p=reinterpret_cast<const QRgba64 *>(output.constScanLine(y))[x];
                QVERIFY(std::abs(int(p.red())-std::lround(1001+101*sx+37*sy))<=1);
                QVERIFY(std::abs(int(p.green())-std::lround(2003+53*sx+211*sy))<=1);
                QCOMPARE(p.alpha(),quint16(65535));
            }
        }
        QCOMPARE(source,unchanged);
        QImage transparent(2,2,QImage::Format_RGBA64); transparent.fill(QColor(0,0,255,0));
        reinterpret_cast<QRgba64 *>(transparent.scanLine(0))[0]=QRgba64::fromRgba64(65535,0,0,65535);
        g.straighten=45; const auto center=g.apply(transparent); QCOMPARE(center.size(),QSize(1,1));
        const auto pixel=reinterpret_cast<const QRgba64 *>(center.constScanLine(0))[0];
        QCOMPARE(pixel.red(),quint16(65535)); QCOMPARE(pixel.blue(),quint16(0)); QCOMPARE(pixel.alpha(),quint16(16384));
        QImage thin(1,7,QImage::Format_RGBA64); thin.fill(Qt::green); QCOMPARE(g.apply(thin).size(),QSize(1,1));
    }
    void straightenGeometryOrderCancellationAndPipelineAgreement() {
        QImage source(31,23,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(x*1901,y*2501,(x+y)*1007,65535);
        GeometryState angle; angle.straighten=12.5; const auto straight=angle.apply(source);
        GeometryState rest; rest.crop={.2,.1,.6,.8}; rest.quarterTurns=1; rest.flipHorizontal=true;
        auto combined=rest; combined.straighten=angle.straighten;
        const auto expected=rest.apply(straight); QCOMPARE(combined.apply(source),expected);
        const auto cancelledToken=std::make_shared<std::atomic_bool>(true);
        QVERIFY(combined.apply(source,cancelledToken).isNull());
        PrepareRequest request; request.image=source; request.geometry=combined; request.viewport={4096,4096};
        QCOMPARE(preparePreview(request,{}).normal,expected);
        auto key=StageGraph::prepareKey(request); request.geometry.straighten=13; QVERIFY(StageGraph::prepareKey(request)!=key);
        AdjustmentState state; state.geometry=combined; state.exposure=.3;
        QTemporaryDir dir;
        for (bool raw : {false,true}) {
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,raw,0);
            const auto rendered=ImagePipeline::processWithPlan(expected,plan);
            const auto plot=renderScopePlot({source,plan,combined,"waveform",1,true},{});
            ScopePlotCounts reference("waveform"); QVERIFY(reference.add(rendered)); QCOMPARE(plot.image,reference.image());
            QCOMPARE(plot.pixels,quint64(expected.width())*expected.height());
            QString error; const auto path=dir.filePath(raw ? "raw.png" : "photo.png");
            QVERIFY2(exportRaster(source,state,path,ColorManagement::OutputSpace::SRgb,RasterFormat::Png16,100,{},&error,raw),qPrintable(error));
            QCOMPARE(QImageReader(path).read().convertToFormat(QImage::Format_RGBA64),rendered);
        }
    }
    void straightenHistoryProjectAndInteractiveCropRoundTrip() {
        QTemporaryDir dir; QImage image(80,60,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto path=dir.filePath("source.png"); QVERIFY(image.save(path));
        PhotoController controller(nullptr); controller.setGpuEnabled(false); QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY(controller.previewReady() && !controller.rendering()); const auto initial=controller.editHistory().size();
        controller.setStraighten(2); controller.setStraighten(5); controller.setStraighten(8); controller.finishInteraction();
        QCOMPARE(controller.editHistory().size(),initial+1); QCOMPARE(controller.geometry()["straighten"].toDouble(),8.0);
        const auto saved=controller.geometry(); controller.setStraighten(45.1); QCOMPARE(controller.geometry(),saved);
        controller.setExactScopes(true); GeometryState g; g.straighten=8; const auto size=g.straightenedSize(image.size());
        QTRY_COMPARE_WITH_TIMEOUT(controller.scopesPixelCount(),quint64(size.width())*size.height(),10000);
        controller.undo(); QVERIFY(!controller.geometry().contains("straighten")); controller.redo(); QCOMPARE(controller.geometry(),saved);
        QTRY_VERIFY(!controller.rendering()); QVERIFY(controller.beginCrop()); QTRY_VERIFY(!controller.rendering());
        QCOMPARE(controller.gpuSource().size(),size); QVERIFY(controller.applyCrop(.25,0,.5,1));
        QCOMPARE(controller.geometry()["straighten"].toDouble(),8.0); controller.undo(); QCOMPARE(controller.geometry(),saved);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"Straightened")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); reopened.setGpuEnabled(false); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.geometry(),saved); QVERIFY(reopened.canRedo()); reopened.redo(); QCOMPARE(reopened.geometry()["width"].toDouble(),.5);
        reopened.undo(); reopened.undo(); QVERIFY(!reopened.geometry().contains("straighten")); reopened.redo(); QCOMPARE(reopened.geometry(),saved);
        AdjustmentState commands; QVERIFY(CommandRegistry::execute(commands,{{"command","geometry.straighten"},{"degrees",8.0}}));
        QCOMPARE(commands.geometry.toJson(),QJsonObject::fromVariantMap(saved));
        const auto before=commands.toJson();
        QVERIFY(!CommandRegistry::execute(commands,{{"command","geometry.straighten"},{"degrees",46.0}})); QCOMPARE(commands.toJson(),before);
        QVERIFY(!CommandRegistry::execute(commands,{{"command","geometry.straighten"},{"degrees","8"}})); QCOMPARE(commands.toJson(),before);
    }
    void invalidGeometryVersionWithoutHistoryPreservesActiveProject() {
        QTemporaryDir dir; ProjectDatabase active, candidate; QVERIFY(active.create(dir.path(),"Active")); QVERIFY(candidate.create(dir.path(),"Candidate"));
        AdjustmentState state; QVERIFY(candidate.addOrUpdatePhoto("source.png",state)); QVERIFY(candidate.flush());
        const auto activePath=active.projectPath();
        for (const auto invalid : {QJsonObject{{"schema",3},{"straighten",5}},QJsonObject{{"schema",2},{"straighten",46}}}) {
            const auto connection=QStringLiteral("invalid-geometry-test");
            { auto db=QSqlDatabase::addDatabase("QSQLITE",connection); db.setDatabaseName(QDir(candidate.projectPath()).filePath("Project.db")); QVERIFY(db.open());
              auto json=state.toJson(); json.insert("geometry",invalid); QSqlQuery q(db); q.prepare("UPDATE photos SET adjustment_json=?");
              q.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))); QVERIFY(q.exec()); }
            QSqlDatabase::removeDatabase(connection); QVector<ProjectDatabase::SavedPhoto> photos;
            QVERIFY(!active.open(candidate.projectPath(),&photos)); QCOMPARE(active.projectPath(),activePath); QVERIFY(active.isOpen());
        }
        auto legacy=GeometryState{}.toJson(); QVERIFY(GeometryState::validJson(legacy)); QVERIFY(!legacy.contains("straighten"));
        auto spoof=legacy; spoof.insert("straighten",10); QVERIFY(!GeometryState::validJson(spoof));
    }
    void scopePlotsCountEndpointsChannelsAndNeutralChroma() {
        QImage image(2,1,QImage::Format_RGBA64); auto row=reinterpret_cast<QRgba64 *>(image.scanLine(0));
        row[0]=QRgba64::fromRgba64(0,0,0,65535); row[1]=QRgba64::fromRgba64(65535,65535,65535,65535);
        ScopePlotCounts wave("waveform"); QVERIFY(wave.add(image)); QCOMPARE(wave.height,1024); QCOMPARE(wave.pixels,quint64(2));
        QCOMPARE(wave.bins[1023*wave.width],quint64(1)); QCOMPARE(wave.bins[255],quint64(1));
        QCOMPARE(std::accumulate(wave.bins.cbegin(),wave.bins.cend(),quint64(0)),quint64(2));
        ScopePlotCounts parade("parade"); QVERIFY(parade.add(image)); QCOMPARE(parade.width,768);
        for (int channel=0;channel<3;++channel) {
            QCOMPARE(parade.bins[1023*768+256*channel],quint64(1)); QCOMPARE(parade.bins[256*channel+255],quint64(1));
        }
        QCOMPARE(std::accumulate(parade.bins.cbegin(),parade.bins.cend(),quint64(0)),quint64(6));
        ScopePlotCounts vector("vectorscope"); QVERIFY(vector.add(image)); QCOMPARE(vector.pixels,quint64(2));
        quint64 center=0; for (int y=255;y<=256;++y) for (int x=255;x<=256;++x) center+=vector.bins[y*512+x];
        QCOMPARE(center,quint64(2)); QCOMPARE(vector.image().size(),QSize(512,512));
        row[0]=row[1]=QRgba64::fromRgba64(65535,0,0,65535);
        ScopePlotCounts red("vectorscope"); QVERIFY(red.add(image)); QCOMPARE(red.bins[197],quint64(2));
        QVERIFY(ScopePlotCounts("invalid").bins.isEmpty());
        const auto cancel=std::make_shared<std::atomic_bool>(true); ScopePlotCounts cancelledPlot("waveform");
        QVERIFY(!cancelledPlot.add(image,cancel)); QCOMPARE(cancelledPlot.pixels,quint64(0));
    }
    void scopePlotTiledReferenceMatchesFullImageAcrossRegionsAndGeometry() {
        QImage source(20,260,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64((x*3123+y*153)%65536,(x*543+y*731)%65536,(x*831+y*87)%65536,65535);
        AdjustmentState state; state.exposure=.5; state.geometry.crop={0,0,.75,1}; state.geometry.flipVertical=true;
        state.look.mode="manual"; state.look.code="ST"; state.look.parameters={{"sharpness",4},{"clarity",3}};
        for (bool raw : {false,true}) {
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,raw,0);
            const auto encoded=ImagePipeline::processWithPlan(state.geometry.apply(source),plan);
            for (const QString mode : {"waveform","parade","vectorscope"}) {
                ScopePlotCounts reference(mode); QVERIFY(reference.add(encoded));
                ScopePlotRequest request{source,plan,state.geometry,mode,7,true};
                const auto result=renderScopePlot(request,{}); QVERIFY(result.error.isEmpty());
                QCOMPARE(result.pixels,quint64(encoded.width())*encoded.height()); QCOMPARE(result.image,reference.image());
                QVERIFY(renderScopePlot(request,std::make_shared<std::atomic_bool>(true)).image.isNull());
            }
        }
    }
    void sharedScopePlotsMatchIndividualAndFullReferenceAcrossPlans() {
        QImage source(35,279,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64((x*1777+y*103)%65536,(x*503+y*211)%65536,(x*1301+y*37)%65536,(x+y)%3?65535:23456);
        ScopePlotCache cache; int batches=0;
        for (bool raw : {false,true}) for (int geometry=0;geometry<3;++geometry) for (int look=0;look<3;++look) {
            AdjustmentState state; state.exposure=.4; state.saturation=13;
            if (look==1) { state.look.mode="manual"; state.look.code="FL"; state.look.parameters={{"clarity",3},{"sharpness",4},{"fade",2}}; }
            if (look==2) { state.look.mode="calibrated"; state.look.lut=LookLut::identity(5); }
            if (geometry==1) { state.geometry.crop={.1,.2,.7,.6}; state.geometry.quarterTurns=1; }
            if (geometry==2) { state.geometry.perspectiveHorizontal=.1; state.geometry.distortion=.05; state.geometry.redCa=.2; }
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,raw,0);
            const auto encoded=ImagePipeline::processWithPlan(state.geometry.apply(source),plan);
            ScopePlotRequest request{source,plan,state.geometry,"waveform",1,true}; const auto shared=renderScopePlots(request,{});
            const QStringList modes{"waveform","parade","vectorscope"};
            for (int i=0;i<3;++i) {
                ScopePlotCounts counts(modes[i]); QVERIFY(counts.add(encoded)); request.mode=modes[i];
                const auto individual=renderScopePlot(request,{}); const auto actual=cache.render(request);
                QCOMPARE(shared[i].image,individual.image); QCOMPARE(shared[i].image,counts.image()); QCOMPARE(actual.image,shared[i].image);
                QCOMPARE(actual.pixels,quint64(encoded.width())*encoded.height()); QCOMPARE(actual.pixels,shared[i].pixels); QVERIFY(actual.error.isEmpty());
                ++request.revision; const auto hit=cache.render(request); QCOMPARE(hit.image.cacheKey(),actual.image.cacheKey());
                auto changed=hit.image; changed.fill(Qt::magenta); QCOMPARE(cache.render(request).image,actual.image);
            }
            ++batches;
        }
        QCOMPARE(cache.snapshot()["misses"].toInteger(),qint64(batches)); QCOMPARE(cache.snapshot()["hits"].toInteger(),qint64(batches*8));
        QVERIFY(cache.snapshot()["charged_bytes"].toInteger()<=cache.snapshot()["budget_bytes"].toInteger());
    }
    void scopePlotKeysCoverModeIndependentBatchesAndActualDependencies() {
        QImage source(12,9,QImage::Format_RGBA64); source.fill(Qt::gray);
        ScopePlotRequest request{source,ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb),{},"waveform",1,true}; const auto key=StageGraph::scopePlotsKey(request);
        request.mode="vectorscope"; request.revision=99; QCOMPARE(StageGraph::scopePlotsKey(request),key);
        request.fullResolution=false; QVERIFY(StageGraph::scopePlotsKey(request)!=key); request.fullResolution=true;
        for (const auto &p : CommandRegistry::geometryParameters()) {
            request.geometry={}; request.geometry.*p.member=.01; QVERIFY(StageGraph::scopePlotsKey(request)!=key);
        }
        request.geometry={}; request.geometry.crop={0,0,.5,1}; QVERIFY(StageGraph::scopePlotsKey(request)!=key); request.geometry={};
        auto plan=request.plan; request.plan.data[0].x+=.1f; QVERIFY(StageGraph::scopePlotsKey(request)!=key); request.plan=plan;
        request.plan.state.hue=1e-12; QVERIFY(StageGraph::scopePlotsKey(request)!=key); request.plan=plan;
        auto changed=source; changed.setPixelColor(0,0,Qt::red); request.source=changed; QVERIFY(StageGraph::scopePlotsKey(request)!=key); request.source=source;
        auto lut=std::make_shared<LookLut>(*LookLut::identity(5)); lut->digest.clear();
        auto different=std::make_shared<LookLut>(*lut); for (auto &value : different->rgb) value=0;
        AdjustmentState state; state.look.mode="calibrated"; state.look.lut=lut;
        request.plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb); const auto lutKey=StageGraph::scopePlotsKey(request);
        ScopePlotCache cache; const auto a=cache.render(request);
        state.look.lut=different; request.plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb); QVERIFY(StageGraph::scopePlotsKey(request)!=lutKey);
        const auto b=cache.render(request); QCOMPARE(b.image,renderScopePlot(request,{}).image); QVERIFY(a.image!=b.image);
        request.mode="histogram"; QVERIFY(cache.render(request).image.isNull()); QCOMPARE(cache.snapshot()["entries"].toInt(),2);
    }
    void controllerScopePlotRejectsOldEditsAndPhotosAndIgnoresMonitorLut() {
        QTemporaryDir dir; QImage image(80,40,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto first=dir.filePath("first.png"), second=dir.filePath("second.png"); QVERIFY(image.save(first));
        QVERIFY(image.scaled(20,10).save(second)); ProcessedImageProvider provider;
        PhotoController controller(&provider); controller.setGpuEnabled(false); QVERIFY(controller.importFile(QUrl::fromLocalFile(first)));
        QTRY_VERIFY(controller.previewReady() && !controller.rendering()); controller.setScopeMode("waveform");
        controller.setExactScopes(true); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        QCOMPARE(controller.scopePlotPixels(),quint64(3200)); QVERIFY(!controller.scopePlotUrl().isEmpty());
        controller.setScopeMode("parade"); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        const auto parade=provider.requestImage("scopes/plot",nullptr,{}); const auto hits=PerformanceRecorder::snapshot()["counters"].toObject()["scope_plot_cache_hit"].toInteger();
        controller.setScopeMode("vectorscope"); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        controller.setScopeMode("parade"); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        QCOMPARE(provider.requestImage("scopes/plot",nullptr,{}).cacheKey(),parade.cacheKey());
        QVERIFY(PerformanceRecorder::snapshot()["counters"].toObject()["scope_plot_cache_hit"].toInteger()>=hits+2);
        controller.setExposure(.5); QVERIFY(!controller.scopePlotCurrent()); controller.setCrop(0,0,.5,1);
        controller.setScopeMode("parade"); controller.finishInteraction(); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        QCOMPARE(controller.scopePlotPixels(),quint64(1600));
        QSize size; const auto before=provider.requestImage("scopes/plot",&size,{});
        QCOMPARE(provider.requestImage("scopes/plot",nullptr,QSize(300,150)).size(),QSize(300,150));
        controller.setDisplayColorLut({},"identity-for-scope-test"); QCOMPARE(provider.requestImage("scopes/plot",&size,{}),before);
        controller.setScopeMode("vectorscope"); QVERIFY(controller.importFile(QUrl::fromLocalFile(second))); controller.selectPhoto(1);
        QVERIFY(!controller.scopePlotCurrent()); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        QCOMPARE(controller.scopePlotPixels(),quint64(200)); QCOMPARE(provider.requestImage("scopes/plot",&size,{}).size(),QSize(512,512));
        controller.setScopeMode("histogram"); QVERIFY(controller.scopePlotUrl().isEmpty()); QCOMPARE(controller.scopePlotPixels(),quint64(0));
        controller.setScopeMode("waveform"); QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
        ProjectDatabase empty; QVERIFY(empty.create(dir.path(),"Empty"));
        QVERIFY(controller.openProject(QUrl::fromLocalFile(empty.projectPath())));
        QVERIFY(!controller.hasImage()); QVERIFY(controller.scopePlotUrl().isEmpty()); QVERIFY(!controller.scopePlotCurrent());
        QVERIFY(provider.requestImage("scopes/plot",nullptr,{}).isNull());
    }

    void tiff16AndWebpKeepTargetIccGeometryPixelsAndCancellation() {
        QTemporaryDir dir; QImage source(9,7,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(1000+x*6101,503+y*9011,123+x*997+y*997,65535);
        AdjustmentState state; state.exposure=.35; state.geometry.crop={.1,.2,.7,.7}; state.geometry.quarterTurns=1; state.geometry.straighten=3.5;
        for (const auto format : {RasterFormat::Tiff16,RasterFormat::WebP8}) for (const auto space : {ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::AdobeRgb,ColorManagement::OutputSpace::ProPhotoRgb}) for (bool raw : {false,true}) {
            QString error; const auto path=dir.filePath(format==RasterFormat::Tiff16 ? "out.tif" : "out.webp");
            QVERIFY2(exportRaster(source,state,path,space,format,100,{},&error,raw),qPrintable(error));
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,raw,0.0f);
            auto expected=ImagePipeline::processWithPlan(state.geometry.apply(source),plan);
            const auto actual=QImageReader(path).read(); QVERIFY(!actual.isNull()); QCOMPARE(actual.size(),expected.size());
            QCOMPARE(actual.colorSpace().iccProfile(),expected.colorSpace().iccProfile());
            if (format==RasterFormat::Tiff16) {
                QCOMPARE(actual.depth(),64); QCOMPARE(actual.convertToFormat(QImage::Format_RGBA64),expected);
            } else {
                QCOMPARE(actual.convertToFormat(QImage::Format_RGBA8888),expected.convertToFormat(QImage::Format_RGBA8888));
            }
            QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll(); file.close();
            const auto token=std::make_shared<std::atomic_bool>(true);
            QVERIFY(!exportRaster(source,{},path,space,format,92,token,&error,raw));
            QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes);
        }
    }

    void orientedCropCoordinatesMatchAllPixelOrientations() {
        QImage image(16,12,QImage::Format_RGBA64);
        for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
            reinterpret_cast<QRgba64 *>(image.scanLine(y))[x] = QRgba64::fromRgba64(x*3000,y*5000,(x+y)*1700,65535);
        for (int turns=0;turns<4;++turns) for (bool horizontal : {false,true}) for (bool vertical : {false,true}) {
            GeometryState g; g.crop = {.25,.25,.5,.5}; g.quarterTurns = turns; g.flipHorizontal = horizontal; g.flipVertical = vertical;
            const auto oriented = g.orientedRect(g.crop);
            QCOMPARE(g.orientedRect(oriented,true),g.crop);
            auto full = g; full.crop = {0,0,1,1}; const auto rotated = full.apply(image);
            const auto reference = rotated.copy(qRound(oriented.x()*rotated.width()),qRound(oriented.y()*rotated.height()),
                qRound(oriented.width()*rotated.width()),qRound(oriented.height()*rotated.height()));
            QCOMPARE(g.apply(image),reference);
            QCOMPARE(g.orientedRect(QRectF(0,0,1,1)),QRectF(0,0,1,1));
        }
        GeometryState g; g.quarterTurns=1; QCOMPARE(g.orientedRect({0,0,.25,.5}),QRectF(.5,0,.5,.25));
        g.flipHorizontal=true; QCOMPARE(g.orientedRect({0,0,.25,.5}),QRectF(0,0,.5,.25));
    }
    void interactiveCropDraftCancelCommitUndoAndPhotoSwitchStayIsolated() {
        QTemporaryDir dir; QImage image(80,40,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto source=dir.filePath("source.png"), other=dir.filePath("other.png"); QVERIFY(image.save(source)); QVERIFY(image.save(other));
        PhotoController controller(nullptr); controller.setGpuEnabled(false); QVERIFY(controller.importFile(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(controller.previewReady() && !controller.rendering());
        controller.setCrop(.25,0,.5,1); controller.rotatePhoto(1); controller.flipPhoto(true); controller.finishInteraction();
        QTRY_VERIFY(!controller.rendering()); const auto saved=controller.geometry(); const auto history=controller.editHistory();
        QVERIFY(controller.beginCrop()); QVERIFY(controller.cropEditing()); QTRY_VERIFY(!controller.rendering());
        QCOMPARE(controller.geometry(),saved); QCOMPARE(controller.editHistory(),history);
        QCOMPARE(controller.gpuSource().size(),QSize(40,80)); // full oriented photo; stored crop still intact
        QVERIFY(!controller.applyCrop(-1,0,1,1)); QVERIFY(controller.cropEditing());
        controller.cancelCrop(); QVERIFY(!controller.cropEditing()); QCOMPARE(controller.editHistory(),history);
        QTRY_VERIFY(!controller.rendering()); QCOMPARE(controller.gpuSource().size(),QSize(40,40));
        QVERIFY(controller.beginCrop()); QVERIFY(controller.applyCrop(.25,0,.5,.75));
        const auto expected=GeometryState::fromJson(QJsonObject::fromVariantMap(saved)).orientedRect({.25,0,.5,.75},true);
        QCOMPARE(controller.geometry().value("x").toDouble(),expected.x()); QCOMPARE(controller.geometry().value("height").toDouble(),expected.height());
        QCOMPARE(controller.editHistory().size(),history.size()+1); controller.undo(); QCOMPARE(controller.geometry(),saved);
        QTRY_VERIFY(!controller.rendering()); QVERIFY(controller.beginCrop()); controller.setExposure(.5); QVERIFY(!controller.cropEditing());
        QTRY_VERIFY(!controller.rendering()); QVERIFY(controller.beginCrop());
        QVERIFY(controller.importFile(QUrl::fromLocalFile(other))); controller.selectPhoto(1); QVERIFY(!controller.cropEditing());
        QCOMPARE(controller.geometry().value("width").toDouble(),1.0); controller.selectPhoto(0); QCOMPARE(controller.geometry(),saved);
    }

    void xmpRoundTripPreservesSonyGeometryAndProtectsExistingFiles() {
        QTemporaryDir dir; QString error; const auto path = dir.filePath("photo.ARW.xmp");
        AdjustmentState state; state.exposure = .75; state.look.mode = "as-shot";
        state.geometry.crop = {.1,.2,.7,.6}; state.geometry.quarterTurns = 3; state.geometry.flipHorizontal = true; state.geometry.straighten=7.5;
        state.geometry.perspectiveHorizontal=.1; state.geometry.distortion=-.15; state.geometry.redCa=.4; state.geometry.blueCa=-.6;
        CatalogTags tags; tags.keywords = {"A & B","旅行 <日本>"}; tags.albums = {"旅行"}; tags.label = "red";
        QVERIFY(CatalogTags::normalize(&tags.keywords,64));
        QVERIFY2(XmpSidecar::writeNew(path,state,tags,4,"reject",&error),qPrintable(error));
        XmpSidecar::Document restored;
        QVERIFY2(XmpSidecar::read(path,&restored,&error),qPrintable(error));
        QCOMPARE(restored.adjustments.toJson(),state.toJson()); QCOMPARE(restored.tags.toJson(),tags.toJson());
        QCOMPARE(restored.rating,4); QCOMPARE(restored.flag,QStringLiteral("reject")); QVERIFY(restored.hasAdjustments);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
        QVERIFY(bytes.contains("<xmp:Rating>-1</xmp:Rating>")); QVERIFY(bytes.contains("A &amp; B"));
        QVERIFY(!XmpSidecar::writeNew(path,{},tags,0,"none",&error));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes); file.close();
        QVERIFY(!XmpSidecar::writeNew(dir.filePath("source.ARW"),state,tags,4,"none",&error));
        state.look.error = "broken";
        QVERIFY(!XmpSidecar::writeNew(dir.filePath("invalid.xmp"),state,tags,4,"none",&error));
        QVERIFY(!QFileInfo::exists(dir.filePath("invalid.xmp")));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
    }
    void xmpStandardNamespacesAndPublicOverridesAreExplicit() {
        QTemporaryDir dir; const auto path = dir.filePath("external.xmp"); QString error;
        QFile file(path); auto put = [&](const QByteArray &bytes) {
            if (!file.open(QIODevice::WriteOnly)) return false;
            const bool ok = file.write(bytes) == bytes.size(); file.close(); return ok;
        };
        const QByteArray xml = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><r:RDF xmlns:r="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><r:Description r:about="" xmlns:a="http://ns.adobe.com/xap/1.0/" xmlns:d="http://purl.org/dc/elements/1.1/" xmlns:c="http://ns.adobe.com/camera-raw-settings/1.0/" a:Rating="3.0" a:Label="Blue" c:Exposure2012="2"><d:subject><r:Bag><r:li> Japan </r:li><r:li>Japan</r:li><r:li>A &amp; B</r:li></r:Bag></d:subject></r:Description></r:RDF></x:xmpmeta>)";
        QVERIFY(put(xml)); XmpSidecar::Document result;
        QVERIFY2(XmpSidecar::read(path,&result,&error),qPrintable(error));
        QVERIFY(!result.hasAdjustments); QCOMPARE(result.rating,3); QCOMPARE(result.tags.label,QStringLiteral("blue"));
        QCOMPARE(result.tags.keywords,QStringList({"A & B","Japan"})); QVERIFY(!result.hasAlbums); QCOMPARE(result.warnings.size(),1);
        auto custom = xml; custom.replace("a:Label=\"Blue\"","a:Label=\"Client choice\""); QVERIFY(put(custom));
        QVERIFY(XmpSidecar::read(path,&result,&error)); QVERIFY(!result.hasLabel); QCOMPARE(result.warnings.size(),2);
        QVERIFY(file.remove()); AdjustmentState state; state.look.mode = "as-shot";
        QVERIFY(XmpSidecar::writeNew(path,state,{},5,"reject",&error));
        QVERIFY(file.open(QIODevice::ReadOnly)); auto changed = file.readAll(); file.close();
        changed.replace("<xmp:Rating>-1</xmp:Rating>","<xmp:Rating>2</xmp:Rating>"); QVERIFY(put(changed));
        QVERIFY(XmpSidecar::read(path,&result,&error)); QCOMPARE(result.rating,2); QCOMPARE(result.flag,QStringLiteral("none"));
        QVERIFY(result.hasAdjustments); QCOMPARE(result.adjustments.look.mode,QStringLiteral("as-shot"));
    }
    void xmpRejectsMalformedDuplicateEntityAndFutureSnapshotsWithoutMutation() {
        QTemporaryDir dir; QString error; const auto path = dir.filePath("invalid.xmp"); QFile file(path);
        const QByteArray start = R"(<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description rdf:about="" xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:dc="http://purl.org/dc/elements/1.1/">)";
        const QByteArray end = "</rdf:Description></rdf:RDF>";
        const QList<QByteArray> invalid = {
            start+"<xmp:Rating>2.5</xmp:Rating>"+end,
            start+"<xmp:Rating>2</xmp:Rating><xmp:Rating>3</xmp:Rating>"+end,
            start+"<xmp:Rating><rdf:value>3</rdf:value></xmp:Rating>"+end,
            start+"<dc:subject><rdf:Seq><rdf:li>A</rdf:li></rdf:Seq></dc:subject>"+end,
            "<!DOCTYPE x [<!ENTITY test '3'>]>"+start+"<xmp:Rating>&test;</xmp:Rating>"+end,
            start+"<xmp:Rating>3</xmp:Rating>",
            start+"<dc:subject><rdf:Bag><rdf:li><rdf:Bag/></rdf:li></rdf:Bag></dc:subject>"+end
        };
        XmpSidecar::Document result; result.rating = 5; result.adjustments.exposure = 1;
        for (const auto &bytes : invalid) {
            QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes),bytes.size()); file.close();
            QVERIFY(!XmpSidecar::read(path,&result,&error)); QVERIFY(!error.isEmpty());
            QCOMPARE(result.rating,5); QCOMPARE(result.adjustments.exposure,1.0);
        }
        QVERIFY(file.remove()); QVERIFY(XmpSidecar::writeNew(path,{}, {},2,"pick",&error));
        QVERIFY(file.open(QIODevice::ReadOnly)); auto bytes = file.readAll(); file.close();
        bytes.replace(ProcessingPlan::EngineVersion,"future-engine");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bytes); file.close();
        QVERIFY(!XmpSidecar::read(path,&result,&error)); QCOMPARE(result.rating,5);
    }
    void controllerXmpImportIsPerVersionUndoableAndPersistsCatalog() {
        QTemporaryDir dir; QImage image(12,8,QImage::Format_RGB32); image.fill(Qt::gray);
        const auto source = dir.filePath("source.png"), sidecar = dir.filePath("saved.xmp"); QVERIFY(image.save(source));
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(source)));
        controller.setExposure(.5); controller.finishInteraction(); controller.setCrop(.1,.2,.8,.7);
        controller.setRating(4); controller.setFlag("pick"); QVERIFY(controller.setSelectionKeywords("Japan, Sony"));
        QVERIFY(controller.setSelectionLabel("green")); QVERIFY(controller.addSelectionToAlbum("Trip"));
        QVERIFY(controller.exportXmp(QUrl::fromLocalFile(sidecar))); QVERIFY(controller.createVirtualCopy("Alternate"));
        controller.resetAdjustments(); controller.setRating(1); QVERIFY(controller.setSelectionKeywords("Other"));
        QVERIFY(controller.importXmp(QUrl::fromLocalFile(sidecar)));
        QCOMPARE(controller.exposure(),.5); QCOMPARE(controller.geometry().value("x").toDouble(),.1);
        QCOMPARE(controller.currentRating(),4); QCOMPARE(controller.currentKeywords(),QStringList({"Japan","Sony"}));
        controller.undo(); QCOMPARE(controller.exposure(),0.0); controller.redo(); QCOMPARE(controller.exposure(),.5);
        QVERIFY(controller.createProject(QUrl::fromLocalFile(dir.path()),"XMP")); QVERIFY(controller.flushEdits());
        PhotoController reopened(nullptr); QVERIFY(reopened.openProject(QUrl::fromLocalFile(controller.projectPath())));
        QCOMPARE(reopened.library().size(),2); reopened.selectPhoto(1); QCOMPARE(reopened.exposure(),.5);
        QCOMPARE(reopened.currentColorLabel(),QStringLiteral("green")); QCOMPARE(reopened.currentAlbums(),QStringList({"Trip"}));
        reopened.undo(); QCOMPARE(reopened.exposure(),0.0); reopened.selectPhoto(0); QCOMPARE(reopened.exposure(),.5);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),original);
    }

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

    void renderedPreviewCacheMatchesFreshRenderingAcrossPlans() {
        RenderedPreviewCache cache;
        QImage source(13,7,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*5003+y*1001)%65536,(x*2417+y*997)%65536,(x*1777+y*7307)%65536,x%2?12345:65535);
        const auto sourceFingerprint=StageGraph::outputFingerprint(source);
        for (int encoding=0;encoding<2;++encoding) for (int raw=0;raw<2;++raw)
        for (int space=0;space<4;++space) for (int look=0;look<5;++look) {
            AdjustmentState state; state.exposure=.6; state.temperature=19; state.tint=-13;
            state.shadows=21; state.highlights=-17; state.contrast=11; state.highlightRecovery=23;
            state.saturation=7; state.vibrance=13; state.hue=-9; state.hslHue[2]=5;
            state.redCurve[2]=.61; state.masterCurve[3]=.81;
            if (look>0 && look<4) {
                state.look.mode="manual"; state.look.code=QStringList{"","FL","BW","SE"}[look];
                state.look.parameters={{"sharpness",4},{"clarity",3},{"fade",2}};
            }
            if (look==4) { state.look.mode="calibrated"; state.look.lut=LookLut::identity(5); state.look.strength=.7; }
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(space),raw,.3f);
            const auto expected=ImagePipeline::processWithPlan(source,plan);
            const auto first=cache.render(source,plan), second=cache.render(source,plan);
            QCOMPARE(first,expected); QCOMPARE(second,expected);
            QCOMPARE(first.cacheKey(),second.cacheKey()); // Actual reuse, not equal re-rendered pixels.
            QCOMPARE(second.colorSpace(),expected.colorSpace()); QCOMPARE(second.textKeys(),expected.textKeys());
            for (const auto &key:expected.textKeys()) QCOMPARE(second.text(key),expected.text(key));
            auto edited=second; edited.fill(Qt::red);
            QCOMPARE(cache.render(source,plan),expected); // A caller's detached copy cannot poison the cache.
        }
        QCOMPARE(cache.snapshot().value("misses").toInteger(),80);
        QCOMPARE(cache.snapshot().value("hits").toInteger(),160);
        QCOMPARE(StageGraph::outputFingerprint(source),sourceFingerprint);
    }

    void renderedPreviewKeysCoverSourceAndActualKernelDependencies() {
        QImage source(16,8,QImage::Format_RGBA64); source.fill(QColor(70,120,180));
        const auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::SRgb);
        const auto key=StageGraph::renderKey(source,plan);
        for (int slot=0;slot<ProcessingPlan::SlotCount;++slot) {
            auto changed=plan; changed.data[slot].x+=.125f;
            QVERIFY(StageGraph::renderKey(source,changed)!=key);
        }
        for (int field=0;field<7;++field) {
            auto changed=plan;
            if (field==0) changed.state.hue=std::nextafter(0.,1.);
            if (field==1) changed.state.saturation=std::nextafter(0.,1.);
            if (field==2) changed.state.vibrance=std::nextafter(0.,1.);
            if (field==3) changed.state.masterCurve[2]=std::nextafter(.5,1.);
            if (field==4) changed.state.redCurve[2]=std::nextafter(.5,1.);
            if (field==5) changed.state.greenCurve[2]=std::nextafter(.5,1.);
            if (field==6) changed.state.blueCurve[2]=std::nextafter(.5,1.);
            QVERIFY(StageGraph::renderKey(source,changed)!=key); // Doubles beyond the packed float precision.
        }
        for (int field=0;field<4;++field) {
            auto changed=plan;
            if (field==0) changed.encoding=ImagePipeline::InputEncoding::LinearProPhoto;
            if (field==1) changed.output=ColorManagement::OutputSpace::DisplayP3;
            if (field==2) changed.rawSource=true;
            if (field==3) changed.baseExposureStops=.5f;
            QVERIFY(StageGraph::renderKey(source,changed)!=key);
        }
        auto geometryOnly=plan; geometryOnly.state.geometry.quarterTurns=1;
        QCOMPARE(StageGraph::renderKey(source,geometryOnly),key); // Preparation owns geometry.
        auto changedSource=source; changedSource.setPixelColor(0,0,Qt::red);
        QVERIFY(StageGraph::renderKey(changedSource,plan)!=key);
        QVERIFY(StageGraph::renderKey(source.copy(0,0,8,8),plan)!=key);
        QVERIFY(StageGraph::renderKey(source.convertToFormat(QImage::Format_RGB32),plan)!=key);

        RenderedPreviewCache cache;
        const auto before=cache.render(source,plan);
        QCOMPARE(cache.render(changedSource,plan),ImagePipeline::processWithPlan(changedSource,plan));
        QVERIFY(cache.render(changedSource,plan)!=before);
        auto lutA=std::make_shared<LookLut>(*LookLut::identity(2)); lutA->digest.clear();
        auto lutB=std::make_shared<LookLut>(*lutA);
        for (auto &value:lutB->rgb) value=1.f-value;
        AdjustmentState state; state.look.mode="calibrated"; state.look.lut=lutA;
        const auto planA=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::SRgb);
        state.look.lut=lutB;
        const auto planB=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::SRgb);
        QVERIFY(StageGraph::renderKey(source,planA)!=StageGraph::renderKey(source,planB));
        const auto outputA=cache.render(source,planA), outputB=cache.render(source,planB);
        QCOMPARE(outputA,ImagePipeline::processWithPlan(source,planA));
        QCOMPARE(outputB,ImagePipeline::processWithPlan(source,planB)); QVERIFY(outputA!=outputB);
        QCOMPARE(cache.render(source,planA).cacheKey(),outputA.cacheKey());
    }

    void controllerCpuPreviewCacheReusesUndoRedoWithoutMonitorContamination() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage source(96,80,QImage::Format_RGB32); source.fill(QColor(80,110,160));
        const auto path=dir.filePath("source.png"); QVERIFY(source.save(path));
        ProcessedImageProvider provider; PhotoController controller(&provider); controller.setGpuEnabled(false);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady() && !controller.loading() && !controller.rendering(),10000);
        QSize size;
        const auto baseline=provider.requestImage("current",&size,{});
        controller.setExposure(.7); controller.finishInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering(),10000);
        const auto edited=provider.requestImage("current",&size,{}); QVERIFY(edited!=baseline);
        const auto hits=PerformanceRecorder::snapshot().value("counters").toObject().value("render_cache_hit").toInteger();
        controller.undo(); QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering(),10000);
        QCOMPARE(provider.requestImage("current",&size,{}),baseline);
        controller.redo(); QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering(),10000);
        QCOMPARE(provider.requestImage("current",&size,{}),edited);
        QVERIFY(PerformanceRecorder::snapshot().value("counters").toObject().value("render_cache_hit").toInteger()>=hits+2);
        // Monitor mapping happens in the provider after cache delivery.
        QImage blackAtlas(33*33,33,QImage::Format_RGBA32FPx4); blackAtlas.fill(Qt::black);
        controller.setDisplayColorLut(blackAtlas,"test-monitor");
        controller.undo(); QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering(),10000);
        QCOMPARE(provider.requestImage("current",&size,{}).pixelColor(0,0).red(),0);
        controller.setDisplayColorLut({},"identity-srgb");
        QCOMPARE(provider.requestImage("current",&size,{}),baseline);
        controller.setCrop(0,0,.5,1);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering() && provider.requestImage("current",&size,{}).width()==48,10000);
        controller.undo();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering() && provider.requestImage("current",&size,{}).size()==baseline.size(),10000);
        QCOMPARE(provider.requestImage("current",&size,{}),baseline);
    }

    void fullScopesCacheMatchesFreshCountsAcrossPlansAndGeometry() {
        FullScopesCache cache;
        QImage source(23,17,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*5003+y*1001)%65536,(x*2417+y*997)%65536,(x*1777+y*7307)%65536,x%2?12345:65535);
        for (int encoding=0;encoding<2;++encoding) for (int raw=0;raw<2;++raw)
        for (int space=0;space<4;++space) for (int geometry=0;geometry<3;++geometry) {
            AdjustmentState state; state.exposure=.4; state.temperature=13; state.saturation=17; state.redCurve[2]=.58;
            state.look.mode="manual"; state.look.code=raw ? "FL" : "BW";
            ScopeRequest request; request.image=source; request.fullResolution=true;
            request.plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(space),raw,.3f);
            if (geometry==1) { request.geometry.crop={.1,.2,.6,.7}; request.geometry.quarterTurns=1; }
            if (geometry==2) { request.geometry.straighten=7; request.geometry.flipHorizontal=true; }
            const auto transformed=request.geometry.apply(source);
            const auto expected=ScopesEngine::analyzeFull(transformed,request.plan);
            const auto first=cache.analyze(request); request.revision=99;
            const auto second=cache.analyze(request);
            for (const auto *actual:{&first,&second}) {
                QCOMPARE(actual->red,expected.red); QCOMPARE(actual->green,expected.green);
                QCOMPARE(actual->blue,expected.blue); QCOMPARE(actual->luma,expected.luma);
                QCOMPARE(actual->pixelCount,quint64(transformed.width())*transformed.height());
                QCOMPARE(actual->shadowClipPercent,expected.shadowClipPercent); QCOMPARE(actual->highlightClipPercent,expected.highlightClipPercent);
            }
            QCOMPARE(first.red.constData(),second.red.constData()); // Reuses complete histogram storage.
            auto modified=second; modified.red[0]=qulonglong(999999);
            QCOMPARE(cache.analyze(request).red,expected.red);
            QVERIFY(cache.snapshot().value("charged_bytes").toInteger()<=cache.snapshot().value("budget_bytes").toInteger());
        }
        QCOMPARE(cache.snapshot().value("misses").toInteger(),48);
        QCOMPARE(cache.snapshot().value("hits").toInteger(),96);
        ScopeRequest request; request.image=source; request.plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::SRgb);
        const auto key=StageGraph::fullScopesKey(request);
        request.geometry.crop={0,0,.5,1}; QVERIFY(StageGraph::fullScopesKey(request)!=key);
        const auto cropped=cache.analyze(request); QCOMPARE(cropped.pixelCount,quint64(12*17));
        request.geometry={}; request.plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,.5f);
        QVERIFY(StageGraph::fullScopesKey(request)!=key);
        QCOMPARE(cache.analyze(request).red,ScopesEngine::analyzeFull(source,request.plan).red);
        request.image.setPixelColor(0,0,Qt::white); QVERIFY(StageGraph::fullScopesKey(request)!=key);
        QCOMPARE(cache.analyze(request).red,ScopesEngine::analyzeFull(request.image,request.plan).red);
    }

    void controllerFullScopesReusesCompletedSourceAfterVersionSwitch() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage source(96,80,QImage::Format_RGB32); source.fill(QColor(80,110,160));
        const auto path=dir.filePath("source.png"); QVERIFY(source.save(path));
        ProcessedImageProvider provider; PhotoController controller(&provider); controller.setGpuEnabled(false);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady() && !controller.loading() && !controller.rendering(),10000);
        controller.setExactScopes(true);
        QTRY_VERIFY_WITH_TIMEOUT(controller.scopesStatus()==QString("全分辨率统计") || controller.scopesStatus()==QString("Full-resolution statistics"),10000);
        const auto hits=PerformanceRecorder::snapshot().value("counters").toObject().value("full_scopes_cache_hit").toInteger();
        QVERIFY(controller.createVirtualCopy("Stats copy"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && !controller.rendering(),10000);
        QTRY_VERIFY_WITH_TIMEOUT(PerformanceRecorder::snapshot().value("counters").toObject().value("full_scopes_cache_hit").toInteger()>hits,10000);
        QCOMPARE(controller.scopesPixelCount(),qulonglong(96*80));
        const auto reused=PerformanceRecorder::snapshot().value("counters").toObject().value("full_scopes_cache_hit").toInteger();
        QImage blackAtlas(33*33,33,QImage::Format_RGBA32FPx4); blackAtlas.fill(Qt::black);
        controller.setDisplayColorLut(blackAtlas,"stats-monitor");
        controller.selectPhoto(0);
        QTRY_VERIFY_WITH_TIMEOUT(PerformanceRecorder::snapshot().value("counters").toObject().value("full_scopes_cache_hit").toInteger()>reused,10000);
        QCOMPARE(controller.scopesPixelCount(),qulonglong(96*80));
        controller.setCrop(0,0,.5,1);
        QTRY_COMPARE_WITH_TIMEOUT(controller.scopesPixelCount(),qulonglong(48*80),10000);
        controller.undo();
        QTRY_COMPARE_WITH_TIMEOUT(controller.scopesPixelCount(),qulonglong(96*80),10000);
    }

    void stageOutputFingerprintsIgnoreRowPaddingAndSeparateIcc() {
        QByteArray a(24,'\0'), b(24,'\0');
        for (int row=0;row<2;++row) for (int byte=0;byte<9;++byte) a[row*12+byte]=b[row*12+byte]=char(20+byte);
        for (int row=0;row<2;++row) for (int byte=9;byte<12;++byte) b[row*12+byte]=char(240);
        QImage first(reinterpret_cast<uchar *>(a.data()),3,2,12,QImage::Format_RGB888);
        QImage second(reinterpret_cast<uchar *>(b.data()),3,2,12,QImage::Format_RGB888);
        const auto before=StageGraph::outputFingerprint(first);
        QCOMPARE(before.value("pixel_sha256"),StageGraph::outputFingerprint(second).value("pixel_sha256"));
        second.setColorSpace(QColorSpace::AdobeRgb); const auto tagged=StageGraph::outputFingerprint(second);
        QCOMPARE(before.value("pixel_sha256"),tagged.value("pixel_sha256")); QVERIFY(before.value("icc_sha256")!=tagged.value("icc_sha256"));
        first.setPixelColor(0,0,Qt::red); QVERIFY(before.value("pixel_sha256")!=StageGraph::outputFingerprint(first).value("pixel_sha256"));
        QVERIFY(before.value("pixel_sha256")!=StageGraph::outputFingerprint(second.copy(0,0,2,2)).value("pixel_sha256"));
        QVERIFY(!StageGraph::outputFingerprint({}).value("available").toBool());
    }

    void colorStageCaptureMatchesNormalRenderingAcrossPlans() {
        QImage source(13,7,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*5003+y*1001)%65536,(x*2417+y*997)%65536,(x*1777+y*7307)%65536,x%2?12345:65535);
        for (int encoding=0;encoding<2;++encoding) for (int raw=0;raw<2;++raw)
        for (int space=0;space<4;++space) for (int look=0;look<5;++look) {
            AdjustmentState state; state.exposure=.6; state.temperature=19; state.tint=-13;
            state.shadows=21; state.highlights=-17; state.contrast=11; state.highlightRecovery=23;
            state.saturation=7; state.vibrance=13; state.hue=-9; state.hslHue[2]=5;
            state.redCurve[2]=.61; state.masterCurve[3]=.81;
            if (look>0 && look<4) {
                state.look.mode="manual"; state.look.code=QStringList{"","FL","BW","SE"}[look];
                state.look.parameters={{"sharpness",4},{"clarity",3},{"fade",2}};
            }
            if (look==4) { state.look.mode="calibrated"; state.look.lut=LookLut::identity(5); state.look.strength=.7; }
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(space),raw,.3f);
            const auto captured=ImagePipeline::diagnoseWithPlan(source,plan);
            QVERIFY(captured.stages.value("available").toBool());
            QCOMPARE(captured.image,ImagePipeline::processWithPlan(source,plan,{},true));
            QCOMPARE(captured.image.colorSpace(),ColorManagement::colorSpace(plan.output));
            QCOMPARE(captured.stages.value("entries").toArray().size(),9);
            QCOMPARE(captured.stages.value("row_buffer_bytes").toInteger(),qint64(9*13*3*4));
            QCOMPARE(captured.stages.value("detail_output").toObject().value("pixel_sha256"),StageGraph::outputFingerprint(captured.image).value("pixel_sha256"));
            for (const auto &entry:captured.stages.value("entries").toArray()) {
                QCOMPARE(entry.toObject().value("pixels").toInteger(),qint64(13*7));
                QCOMPARE(entry.toObject().value("non_finite_values").toInteger(),qint64(0));
                QCOMPARE(entry.toObject().value("pixel_sha256").toString().size(),64);
            }
            QCOMPARE(reinterpret_cast<const QRgba64 *>(captured.image.constScanLine(0))[1].alpha(),quint16(12345));
        }
    }

    void colorStageCapturePreservesLinearValuesAndExposure() {
        QImage source(1,1,QImage::Format_RGBA64);
        reinterpret_cast<QRgba64 *>(source.scanLine(0))[0]=QRgba64::fromRgba64(12000,23000,34000,4567);
        AdjustmentState state; state.exposure=2;
        auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,.5f);
        const auto captured=ImagePipeline::diagnoseWithPlan(source,plan);
        const auto input=colorStage(captured.stages,"input_linear").value("first_rgb").toArray();
        const auto exposed=colorStage(captured.stages,"wb_exposure").value("first_rgb").toArray();
        const int codes[]{12000,23000,34000};
        for (int c=0;c<3;++c) {
            QVERIFY(std::abs(input[c].toDouble()-codes[c]/65535.0)<1e-7);
            QVERIFY(std::abs(exposed[c].toDouble()-codes[c]/65535.0*std::exp2(2.5))<4e-7);
        }
        QVERIFY(exposed[2].toDouble()>1); // No diagnostic clipping of scene values.
        // Known RGB bytes and descriptor are independently encoded in the
        // golden digest; changing channel order or endian order breaks it.
        QCOMPARE(colorStage(captured.stages,"input_linear").value("pixel_sha256").toString(),QString("d1e0e0ad560c75c945ad5c23c9acfeaf8d88af748af15bac30a3552ce2c1830e"));
        plan.data[ProcessingPlan::Wb0]={-2,0,0,0};
        const auto negative=ImagePipeline::diagnoseWithPlan(source,plan);
        QVERIFY(colorStage(negative.stages,"wb_exposure").value("minimum_rgb").toArray()[0].toDouble()<0);
        // Encoded gray independently checks sRGB transfer + the input matrix.
        source=sceneGrayImage(.5);
        const auto srgb=ImagePipeline::diagnoseWithPlan(source,ProcessingPlan::compile({},ImagePipeline::InputEncoding::SRgb));
        const double encoded=32768/65535.0, linear=std::pow((encoded+.055)/1.055,2.4);
        for (const auto &v:colorStage(srgb.stages,"input_linear").value("first_rgb").toArray())
            QVERIFY(std::abs(v.toDouble()-linear)<.0001);
    }

    void colorStageCaptureSeparatesUpstreamAndDownstreamChanges() {
        const auto source=sceneGrayImage(.18); AdjustmentState state;
        const auto capture=[&](const AdjustmentState &s,ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb) {
            return ImagePipeline::diagnoseWithPlan(source,ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0));
        };
        const auto before=capture(state), repeated=capture(state);
        QCOMPARE(before.stages.value("entries"),repeated.stages.value("entries"));
        const auto hash=[](const QJsonObject &s,const char *id) { return colorStage(s,id).value("pixel_sha256"); };
        state.exposure=.5; const auto exposed=capture(state);
        QCOMPARE(hash(before.stages,"input_linear"),hash(exposed.stages,"input_linear"));
        QVERIFY(hash(before.stages,"wb_exposure")!=hash(exposed.stages,"wb_exposure"));
        state={}; state.redCurve[1]=.1; const auto curved=capture(state);
        const auto p3=capture({},ColorManagement::OutputSpace::DisplayP3);
        for (const char *id:{"input_linear","wb_exposure","highlight_recovery","tone_neutral","perceptual_look"}) {
            QCOMPARE(hash(before.stages,id),hash(curved.stages,id));
            QCOMPARE(hash(before.stages,id),hash(p3.stages,id));
        }
        QVERIFY(hash(before.stages,"output_linear")!=hash(curved.stages,"output_linear"));
        QVERIFY(hash(before.stages,"output_linear")!=hash(p3.stages,"output_linear"));
        auto lut=std::make_shared<LookLut>(*LookLut::identity(3));
        for (qsizetype i=0;i<lut->rgb.size();i+=3) lut->rgb[i]=1-lut->rgb[i];
        lut->updateDigest(); state={}; state.look.mode="calibrated"; state.look.lut=lut; state.look.strength=.2;
        const auto weak=capture(state); state.look.strength=.8; const auto strong=capture(state);
        for (const char *id:{"input_linear","wb_exposure","highlight_recovery","tone_neutral","perceptual_look","output_linear","output_transfer"})
            QCOMPARE(hash(weak.stages,id),hash(strong.stages,id));
        QVERIFY(hash(weak.stages,"look_lut")!=hash(strong.stages,"look_lut"));
        QImage detailSource(17,9,QImage::Format_RGBA64);
        for (int y=0;y<detailSource.height();++y) for (int x=0;x<detailSource.width();++x)
            detailSource.setPixelColor(x,y,QColor((x*57+y*23)%256,(x*11+y*61)%256,(x*41+y*17)%256));
        auto detailPlan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::SRgb);
        const auto noDetail=ImagePipeline::diagnoseWithPlan(detailSource,detailPlan);
        detailPlan.data[ProcessingPlan::LookDetail]={.7f,.4f,2,3};
        const auto withDetail=ImagePipeline::diagnoseWithPlan(detailSource,detailPlan);
        QCOMPARE(noDetail.stages.value("entries"),withDetail.stages.value("entries"));
        QCOMPARE(noDetail.stages.value("quantized_color"),withDetail.stages.value("quantized_color"));
        QVERIFY(noDetail.stages.value("detail_output")!=withDetail.stages.value("detail_output"));
        QVERIFY(!noDetail.stages.value("detail_active").toBool()); QVERIFY(withDetail.stages.value("detail_active").toBool());
    }

    void colorStageCaptureIgnoresPaddingAlphaAndMonitorIcc() {
        QByteArray a(64,'\0'),b(64,char(0xff));
        for (int y=0;y<2;++y) for (int x=0;x<3;++x) {
            const auto pixel=QRgba64::fromRgba64(9000+x*7000,21000+y*3000,43000,65535);
            memcpy(a.data()+y*32+x*8,&pixel,8); memcpy(b.data()+y*32+x*8,&pixel,8);
        }
        QImage first(reinterpret_cast<uchar *>(a.data()),3,2,32,QImage::Format_RGBA64);
        QImage second(reinterpret_cast<uchar *>(b.data()),3,2,32,QImage::Format_RGBA64);
        first.setColorSpace(QColorSpace::SRgb); second.setColorSpace(QColorSpace::AdobeRgb);
        const auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
        const auto before=ImagePipeline::diagnoseWithPlan(first,plan), tagged=ImagePipeline::diagnoseWithPlan(second,plan);
        QCOMPARE(before.stages.value("entries"),tagged.stages.value("entries")); QCOMPARE(before.image,tagged.image);
        auto *pixel=reinterpret_cast<QRgba64 *>(second.scanLine(0)); pixel[0].setAlpha(3210);
        const auto alpha=ImagePipeline::diagnoseWithPlan(second,plan);
        QCOMPARE(before.stages.value("entries"),alpha.stages.value("entries"));
        QVERIFY(before.stages.value("quantized_color")!=alpha.stages.value("quantized_color"));
        QCOMPARE(reinterpret_cast<const QRgba64 *>(alpha.image.constScanLine(0))[0].alpha(),quint16(3210));
        const auto empty=ImagePipeline::diagnoseWithPlan({},plan);
        QVERIFY(empty.image.isNull()); QVERIFY(!empty.stages.value("available").toBool()); QVERIFY(!empty.stages.contains("entries"));
        const auto cancelled=ImagePipeline::diagnoseWithPlan(first,plan,std::make_shared<std::atomic_bool>(true));
        QVERIFY(cancelled.image.isNull()); QVERIFY(!cancelled.stages.value("available").toBool()); QVERIFY(!cancelled.stages.contains("entries"));
    }

    void diagnosticZipContainsStructuredStageOutputs() {
        QImage image(2,2,QImage::Format_RGBA64); image.fill(Qt::gray);
        const QJsonObject outputs{{"schema",1},{"source",StageGraph::outputFingerprint(image)}};
        const auto path=DiagnosticBundle::create(image,"synthetic.png",{}, {},0,0,"test",{},outputs); QVERIFY(!path.isEmpty());
        const auto cleanup=qScopeGuard([&] { QFile::remove(path); });
        QCOMPARE(QJsonDocument::fromJson(storedZipEntry(path,"stage_outputs.json")).object(),outputs);
        QCOMPARE(QJsonDocument::fromJson(storedZipEntry(path,"manifest.json")).object().value("stage_outputs").toObject(),outputs);
    }

    void controllerDiagnosticHashesUseCurrentParametersAndGeometry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QImage image(4,2,QImage::Format_RGB32); image.fill(QColor(64,96,128));
        const auto source=dir.filePath("source.png"); QVERIFY(image.save(source));
        PhotoController controller(nullptr); QVERIFY(controller.importFile(QUrl::fromLocalFile(source)));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && controller.previewReady(),10000);
        QStringList paths; const auto cleanup=qScopeGuard([&] { for(const auto &path:paths) QFile::remove(path); });
        const auto baseline=controller.reportBug(); paths << baseline; QVERIFY(!baseline.isEmpty());
        const auto before=QJsonDocument::fromJson(storedZipEntry(baseline,"stage_outputs.json")).object();
        const auto cache=QJsonDocument::fromJson(storedZipEntry(baseline,"performance.json")).object()
            .value("values").toObject().value("render_cache").toObject();
        QCOMPARE(cache.value("backend").toString(),QString("cpu-preview"));
        QCOMPARE(cache.value("budget_bytes").toInteger(),qint64(64*1024*1024));
        QVERIFY(cache.value("charged_bytes").toInteger()<=cache.value("budget_bytes").toInteger());
        const auto scopesCache=QJsonDocument::fromJson(storedZipEntry(baseline,"performance.json")).object()
            .value("values").toObject().value("full_scopes_cache").toObject();
        QCOMPARE(scopesCache.value("budget_bytes").toInteger(),qint64(4*1024*1024));
        const auto plotCache=QJsonDocument::fromJson(storedZipEntry(baseline,"performance.json")).object()["values"].toObject()["scope_plot_cache"].toObject();
        QCOMPARE(plotCache["backend"].toString(),QString("cpu-scope-plots")); QCOMPARE(plotCache["budget_bytes"].toInteger(),qint64(16*1024*1024));
        QVERIFY(before.value("source").toObject().value("available").toBool());
        QCOMPARE(before.value("prepared_preview").toObject().value("width").toInt(),4);
        QVERIFY(before.value("color_stages").toObject().value("available").toBool());
        QCOMPARE(before.value("color_stages").toObject().value("backend").toString(),QString("cpu-reference"));
        controller.setExposure(.5); // Deliberately capture before asynchronous display refinement.
        const auto edited=controller.reportBug(); paths << edited; QVERIFY(!edited.isEmpty()); QVERIFY(edited!=baseline);
        const auto after=QJsonDocument::fromJson(storedZipEntry(edited,"stage_outputs.json")).object();
        QCOMPARE(after.value("source"),before.value("source")); QCOMPARE(after.value("prepared_preview"),before.value("prepared_preview"));
        QCOMPARE(colorStage(after.value("color_stages").toObject(),"input_linear"),colorStage(before.value("color_stages").toObject(),"input_linear"));
        QVERIFY(colorStage(after.value("color_stages").toObject(),"wb_exposure")!=colorStage(before.value("color_stages").toObject(),"wb_exposure"));
        QVERIFY(after.value("cpu_srgb_output").toObject().value("pixel_sha256")!=before.value("cpu_srgb_output").toObject().value("pixel_sha256"));
        QCOMPARE(QJsonDocument::fromJson(storedZipEntry(edited,"manifest.json")).object().value("adjustments").toObject().value("exposure").toDouble(),.5);
        controller.setCrop(0,0,.5,1);
        const auto cropped=controller.reportBug(); paths << cropped; QVERIFY(!cropped.isEmpty());
        const auto geometry=QJsonDocument::fromJson(storedZipEntry(cropped,"stage_outputs.json")).object();
        QCOMPARE(geometry.value("source"),before.value("source"));
        QCOMPARE(geometry.value("prepared_preview").toObject().value("width").toInt(),2);
        QCOMPARE(geometry.value("cpu_srgb_output").toObject().value("width").toInt(),2);
        QCOMPARE(geometry.value("color_stages").toObject().value("width").toInt(),2);
        QCOMPARE(geometry.value("color_stages").toObject().value("detail_output"),geometry.value("cpu_srgb_output"));
        QImage capture; QVERIFY(capture.loadFromData(storedZipEntry(cropped,"current_preview.png"),"PNG"));
        QCOMPARE(StageGraph::outputFingerprint(capture).value("pixel_sha256"),geometry.value("cpu_srgb_output").toObject().value("pixel_sha256"));
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
        QCOMPARE(CommandRegistry::schema()["parameters"].toArray().size(),15);
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

    void projectSnapshotPreservesVersionsRedoAndAnActiveWriter() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ProjectDatabase writer, source;
        QVERIFY(writer.create(dir.path(),"Writer")); QVERIFY(source.create(dir.path(),"Snapshot"));
        const auto active=dir.filePath("active.png"), original=dir.filePath("original.png");
        AdjustmentState activeState; activeState.exposure=.7; QVERIFY(writer.updateAdjustment(active,activeState));
        AdjustmentState state; state.exposure=.25; EditHistory history; history.initialize({}); history.record(state,"exposure");
        auto redo=state; redo.exposure=1.25; history.record(redo,"exposure"); history.undo();
        QVERIFY(source.updateBatch({{original,state}},{{original,history}}));
        auto copy=state; copy.exposure=-.5; copy.geometry.crop={0,0,.5,1}; copy.look.mode="manual"; copy.look.code="FL";
        EditHistory copyHistory; copyHistory.initialize(copy); CatalogTags tags; tags.keywords={"portrait"};
        const auto key="jixel-copy:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(source.addVirtualCopy(key,original,"Alternative",copy,copyHistory,tags,{4,"pick"})); QVERIFY(source.flush());
        const auto writerPath=writer.projectPath(), sourcePath=source.projectPath();
        QVector<ProjectDatabase::SavedPhoto> loaded;
        QVERIFY2(writer.readSnapshot(sourcePath,&loaded),qPrintable(writer.lastError()));
        QCOMPARE(writer.projectPath(),writerPath); QCOMPARE(writer.projectName(),QStringLiteral("Writer")); QVERIFY(writer.isOpen());
        QCOMPARE(loaded.size(),2); QCOMPARE(loaded[0].adjustments.exposure,.25); QVERIFY(loaded[0].history.canRedo());
        QCOMPARE(loaded[0].history.redo().exposure,1.25); QCOMPARE(loaded[1].copyKey,key); QCOMPARE(loaded[1].versionName,QStringLiteral("Alternative"));
        QCOMPARE(loaded[1].adjustments.geometry.crop,QRectF(0,0,.5,1)); QCOMPARE(loaded[1].adjustments.look.code,QStringLiteral("FL"));
        QCOMPARE(loaded[1].tags.keywords,QStringList{"portrait"}); QCOMPARE(loaded[1].rating,4);
        ProjectDatabase readonly; QVERIFY(readonly.readSnapshot(sourcePath,&loaded)); QVERIFY(!readonly.isOpen());
        QVERIFY(!readonly.updateAdjustment(original,{}));
        QVERIFY(!writer.readSnapshot(dir.filePath("missing.jlp"),&loaded)); QCOMPARE(loaded.size(),2);
        activeState.exposure=.8; QVERIFY(writer.updateAdjustment(active,activeState)); QVERIFY(writer.flush());
        QVERIFY(writer.open(writerPath,&loaded)); QCOMPARE(loaded.size(),1); QCOMPARE(loaded[0].adjustments.exposure,.8);
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
        QFile before(dbPath); QVERIFY(before.open(QIODevice::ReadOnly)); const auto original=before.readAll(); before.close();
        ProjectDatabase snapshot; QVector<ProjectDatabase::SavedPhoto> readonly;
        QVERIFY2(snapshot.readSnapshot(folder,&readonly),qPrintable(snapshot.lastError()));
        QVERIFY(!snapshot.isOpen()); QCOMPARE(readonly.size(),1); QCOMPARE(readonly[0].adjustments.exposure,.75);
        QVERIFY(before.open(QIODevice::ReadOnly)); QCOMPARE(before.readAll(),original); before.close();
        QVERIFY(!QDir(folder).exists("backups"));
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
        QVERIFY(reopened.createVirtualCopy("Transfer target")); reopened.setLookCode("FL"); reopened.setExposure(-.5); reopened.finishInteraction();
        reopened.selectPhoto(0);
        QTRY_VERIFY_WITH_TIMEOUT(reopened.currentMetadata().value("sonyLook").toMap().value("autoEligible").toBool(),60000);
        QVERIFY(!reopened.lookState()["code"].toString().isEmpty()); reopened.copyAdjustments();
        reopened.selectPhoto(1); QVERIFY(reopened.pasteAdjustmentGroups({"sony_look"})); QCOMPARE(reopened.exposure(),-.5);
        reopened.undo(); QCOMPARE(reopened.lookState()["mode"].toString(),QString("manual"));
        reopened.selectPhoto(0); QVERIFY(reopened.setPhotoSelection({0,1})); QCOMPARE(reopened.syncAdjustmentGroups({"sony_look"},true),1);
        QVERIFY(reopened.flushEdits()); QVERIFY(db.readSnapshot(reopened.projectPath(),&photos)); QCOMPARE(photos.size(),2);
        for (const auto &photo : photos) {
            QCOMPARE(photo.adjustments.look.mode,QString("as-shot")); QVERIFY(photo.adjustments.look.code.isEmpty()); QVERIFY(photo.adjustments.look.parameters.isEmpty());
        }
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
        const auto rest=f.readAll(); f.close();
        ZipStoreWriter collision(path); QVERIFY(!collision.open());
        QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("PK\x03\x04",4)+rest);
    }
};

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
