#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QRgba64>
#include <condition_variable>
#include <mutex>
#include "app/PhotoController.h"
#include "core/color/MonitorColorTransform.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/BrushRenderRequest.h"

namespace {
QImage sourceImage() {
    QImage image(80,48,QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<48;++y)for(int x=0;x<80;++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(9000+x*90,12000+y*100,17000,47000);
    return image;
}
QByteArray pixels(const QImage &i) {
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(i.constBits()),i.sizeInBytes()),QCryptographicHash::Sha256).toHex();
}
ProcessingPlan planFor(int sample) {
    BrushMaskGeometry::StrokeBuilder builder(80,48,{.12,.65,.7});
    for(int i=1;i<=sample;++i)builder.append({{.1+.018*i,.5},1});
    AdjustmentState state;state.brushMasks.layers={{"live",true,1,{*builder.finish()}}};
    return ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
}
struct Request{int sample=0;};
struct Result{QImage image;QString error;};
Result render(const QImage &source,const ProcessingPlan &plan,const std::shared_ptr<BrushRenderBudget> &budget,const CancelToken &token={}) {
    Result result;auto admitted=BrushRenderRequest::createPrepared(source,plan,BrushRenderRequest::Layout::Whole,token,&result.error,budget);
    if(admitted){auto lease=admitted->render(token,&result.error);if(lease)result.image=lease.sharedImage();}return result;
}
struct Gate {
    std::mutex mutex;std::condition_variable cv;int entered=0,allowed=0,active=0,peak=0;QVector<int> executed;
    bool reached(int n){std::lock_guard lock(mutex);return entered>=n;}
    void release(int n){std::lock_guard lock(mutex);allowed=n;cv.notify_all();}
    Result work(const Request &request,const CancelToken &token,const QImage &source,const std::shared_ptr<BrushRenderBudget> &budget) {
        {std::unique_lock lock(mutex);const int ordinal=++entered;++active;peak=std::max(peak,active);executed.push_back(request.sample);
         if(!cv.wait_for(lock,std::chrono::seconds(5),[&]{return allowed>=ordinal;})){--active;return {{},"Test gate timed out"};}}
        auto result=render(source,planFor(request.sample),budget,token);
        {std::lock_guard lock(mutex);--active;}return result;
    }
};
int cursor(const PhotoController &c){const auto rows=c.editHistory();for(int i=0;i<rows.size();++i)if(rows[i].toMap()["current"].toBool())return i;return -1;}
QImage swapLut(){auto atlas=MonitorColorTransform::identityLut();for(int y=0;y<atlas.height();++y){auto *row=reinterpret_cast<float *>(atlas.scanLine(y));for(int x=0;x<atlas.width();++x)std::swap(row[x*4],row[x*4+2]);}return atlas;}
}

class BrushLivePreviewTests final:public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase(){
        QVERIFY(m_settings.isValid());QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,m_settings.path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,m_settings.path());QStandardPaths::setTestModeEnabled(true);
    }
    void continuousQueueAndCancellationReleaseFutureLeases() {
        const auto source=sourceImage();const auto original=pixels(source);auto budget=std::make_shared<BrushRenderBudget>();
        ProcessedImageProvider provider;QVector<int> delivered;QImage held;bool publication=true;
        Gate gate;
        LatestFrameJob<Request,Result> job([&](const auto &r,const auto &t){return gate.work(r,t,source,budget);},
            [&](const auto &r,auto result){delivered.push_back(r.sample);publication=publication&&provider.setBrushPreview("brush-live/"+QString::number(r.sample),result.image);held=provider.requestImage("brush-live/"+QString::number(r.sample),nullptr,{});});
        const auto cleanup=qScopeGuard([&]{gate.release(100);});
        job.submit({1});QTRY_VERIFY_WITH_TIMEOUT(gate.reached(1),5000);
        for(int i=2;i<=20;++i)job.submit({i});gate.release(1);
        QTRY_VERIFY_WITH_TIMEOUT(delivered.size()==1&&gate.reached(2),5000);
        QCOMPARE(delivered,QVector<int>({1}));QVERIFY(publication);
        auto reference=ImagePipeline::processRegion(source,planFor(1),QRect(QPoint{},source.size()));QCOMPARE(pixels(held),pixels(reference));
        for(int i=21;i<=40;++i)job.submit({i});gate.release(2);
        QTRY_VERIFY_WITH_TIMEOUT(delivered.size()==2&&gate.reached(3),5000);
        QCOMPARE(delivered,QVector<int>({1,20}));QVERIFY(provider.requestImage("brush-live/1",nullptr,{}).isNull());
        reference=ImagePipeline::processRegion(source,planFor(20),QRect(QPoint{},source.size()));QCOMPARE(pixels(held),pixels(reference));
        gate.release(3);QTRY_VERIFY_WITH_TIMEOUT(!job.busy(),5000);
        QCOMPARE(gate.executed,QVector<int>({1,20,40}));QCOMPARE(delivered,gate.executed);QCOMPARE(gate.peak,1);QVERIFY(publication);
        reference=ImagePipeline::processRegion(source,planFor(40),QRect(QPoint{},source.size()));QCOMPARE(pixels(held),pixels(reference));
        provider.clearBrushPreview();QVERIFY(budget->snapshot().charged>0);held={};
        // The queue and watcher remain alive: their completed future must NOT
        // retain an otherwise invisible output after the last consumer clears.
        QCOMPARE(budget->snapshot().charged,0);
        job.submit({1});QTRY_VERIFY_WITH_TIMEOUT(gate.reached(4),5000);job.cancel();
        for(int i=2;i<=7;++i)job.submit({i});gate.release(5);QTRY_VERIFY_WITH_TIMEOUT(!job.busy(),5000);
        QCOMPARE(gate.executed,QVector<int>({1,20,40,1,7}));QCOMPARE(delivered,QVector<int>({1,20,40,7}));QCOMPARE(gate.peak,1);
        provider.clearBrushPreview();held={};QCOMPARE(budget->snapshot().charged,0);QCOMPARE(pixels(source),original);
        qInfo().noquote()<<"BRUSH_LIVE_QUEUE_EVIDENCE "<<QJsonDocument(QJsonObject{{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"one_active_one_pending",true},{"continuous_prefixes",QJsonArray{1,20,40}},{"cancel_epoch",true},{"future_leases_released_while_queue_alive",true},{"exact_reference_pixels",true},{"source_unchanged",true}}).toJson(QJsonDocument::Compact);
    }
    void versionedManagedDisplayPreservesCommittedPixelsAndLeases() {
        ProcessedImageProvider provider;QImage encoded(1,1,QImage::Format_RGBA64);encoded.setColorSpace(QColorSpace(QColorSpace::SRgb));
        reinterpret_cast<QRgba64 *>(encoded.scanLine(0))[0]=QRgba64::fromRgba64(9000,23000,47000,31000);const auto original=pixels(encoded);
        QImage base(2,2,QImage::Format_RGBA64);base.fill(QColor(20,40,60));provider.setImage(base);provider.setReference(base);provider.setScopePlot(base);
        const auto committed=pixels(provider.requestImage("current",nullptr,{}));const auto reference=pixels(provider.requestImage("reference/current",nullptr,{}));
        QVERIFY(provider.setBrushPreview("brush-live/1",encoded));QCOMPARE(pixels(provider.requestImage("current",nullptr,{})),committed);
        QCOMPARE(pixels(provider.requestImage("reference/current",nullptr,{})),reference);QCOMPARE(pixels(provider.requestImage("scopes/current",nullptr,{})),pixels(base));
        provider.setDisplayColorLut(swapLut(),"test-red-blue-swap");QVERIFY(provider.renameBrushPreview("brush-live/monitor"));
        QVERIFY(provider.requestImage("brush-live/1",nullptr,{}).isNull());auto changed=provider.requestImage("brush-live/monitor",nullptr,{});QVERIFY(!changed.isNull());
        const auto p=reinterpret_cast<const QRgba64 *>(changed.constScanLine(0))[0];QVERIFY(std::abs(int(p.red())-47000)<=1);
        QVERIFY(std::abs(int(p.green())-23000)<=1);QVERIFY(std::abs(int(p.blue())-9000)<=1);QCOMPARE(p.alpha(),quint16(31000));
        QVERIFY(!changed.colorSpace().isValid());QCOMPARE(changed.text("JixelLightDisplayManaged"),QString("true"));
        QCOMPARE(pixels(encoded),original);QCOMPARE(pixels(provider.requestImage("scopes/current",nullptr,{})),pixels(base));
        QVERIFY(!provider.setBrushPreview("current",encoded));QVERIFY(!provider.setBrushPreview("brush-live/invalid",{}));
        provider.clearBrushPreview();QVERIFY(provider.requestImage("brush-live/monitor",nullptr,{}).isNull());
        provider.setDisplayColorLut({},"identity-srgb");auto budget=std::make_shared<BrushRenderBudget>();auto result=render(sourceImage(),planFor(4),budget);
        QVERIFY(!result.image.isNull());QVERIFY(provider.setBrushPreview("brush-live/leased",result.image));result={};
        auto held=provider.requestImage("brush-live/leased",nullptr,{});auto scaled=provider.requestImage("brush-live/leased",nullptr,{40,24});
        QVERIFY(!held.isNull()&&!scaled.isNull());QVERIFY(provider.requestImage("brush-live/leased",nullptr,{32769,1024}).isNull());
        provider.clearBrushPreview();QVERIFY(budget->snapshot().charged>0);held={};QVERIFY(budget->snapshot().charged>0);scaled={};QCOMPARE(budget->snapshot().charged,0);
        qInfo().noquote()<<"BRUSH_LIVE_DISPLAY_EVIDENCE "<<QJsonDocument(QJsonObject{{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"versioned_keys",true},{"independent_monitor_rgb_alpha",true},{"committed_slot_separate",true},{"scope_pixels_unchanged",true},{"view_leases_retained_and_refunded",true},{"native_gpu_brush",false}}).toJson(QJsonDocument::Compact);
    }
    void controllerKeepsTransientStateOutOfHistoryScopesAndFinalSampling() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("owned-live.png");const auto source=sourceImage();QVERIFY(source.save(path));
        ProcessedImageProvider provider;auto budget=std::make_shared<BrushRenderBudget>();PhotoController c(&provider,nullptr,budget);c.setGpuEnabled(false);
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.m_scopeJob->busy(),15000);
        QCOMPARE(c.m_catalogEpoch,quint64(0));const auto initial=c.gpuPlan(true).state.toJson();const auto history=cursor(c);const auto revision=c.renderRevision();
        const auto histogram=c.redHistogram();const auto scopesSequence=c.m_scopeJob->sequence(),fullSequence=c.m_fullScopeJob->sequence(),plotSequence=c.m_plotJob->sequence();
        QVERIFY(c.beginBrushStroke("",.1,.5,.12,.65,.7,1));const auto layer=c.m_brushGesture->newLayer;
        BrushMaskGeometry::StrokeBuilder direct(c.brushFrameSize().width(),c.brushFrameSize().height(),{.12,.65,.7});QVERIFY(direct.append({{.1,.5},1}));
        for(int i=1;i<=40;++i){QVERIFY(c.appendBrushStroke(.1+.018*i,.5));QVERIFY(direct.append({{.1+.018*i,.5},1}));}
        const auto sample=c.m_brushGesture->sample;QTRY_VERIFY_WITH_TIMEOUT(c.m_brushPreviewSample==sample,10000);
        QVERIFY(c.brushDrawing()&&c.brushReady());QCOMPARE(c.gpuPlan(true).state.toJson(),initial);QCOMPARE(cursor(c),history);QCOMPARE(c.renderRevision(),revision);
        QCOMPARE(c.redHistogram(),histogram);QCOMPARE(c.m_scopeJob->sequence(),scopesSequence);QCOMPARE(c.m_fullScopeJob->sequence(),fullSequence);QCOMPARE(c.m_plotJob->sequence(),plotSequence);QVERIFY(!c.exportBusy());
        AdjustmentState expected;expected.brushMasks.layers={{layer,true,1,{*direct.finish()}}};auto plan=ProcessingPlan::compile(expected,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);plan.setFrameRect(c.previewFrameRect());
        auto oracle=ImagePipeline::processRegion(c.m_previewSource,plan,QRect(QPoint{},c.m_previewSource.size()));const auto id=c.m_brushPreviewId;
        auto shown=provider.requestImage(id,nullptr,{});QVERIFY(!shown.isNull());QCOMPARE(pixels(shown),pixels(oracle));shown={};
        const auto oldUrl=c.brushPreviewUrl();c.setDisplayColorLut(swapLut(),"test-live-monitor");QVERIFY(c.brushPreviewUrl()!=oldUrl);
        QVERIFY(provider.requestImage(id,nullptr,{}).isNull());QCOMPARE(c.gpuPlan(true).state.toJson(),initial);QCOMPARE(c.renderRevision(),revision);
        c.cancelBrushStroke();QVERIFY(c.brushPreviewUrl().isEmpty()&&!c.brushDrawing());QTRY_VERIFY_WITH_TIMEOUT(!c.m_brushPreviewJob->busy()&&budget->snapshot().charged==0,5000);
        QCOMPARE(c.gpuPlan(true).state.toJson(),initial);QCOMPARE(cursor(c),history);
        c.setDisplayColorLut({},"identity-srgb");QVERIFY(c.beginBrushStroke("",.1,.5,.12,.65,.7,1));
        expected.brushMasks.layers[0].id=c.m_brushGesture->newLayer;
        for(int i=1;i<=40;++i)QVERIFY(c.appendBrushStroke(.1+.018*i,.5));
        QVERIFY(!c.endBrushStroke().isEmpty());QVERIFY(c.brushPreviewUrl().isEmpty());QCOMPARE(cursor(c),history+1);
        QJsonObject actualDocument,expectedDocument;QVERIFY(BrushMaskDocument::toJson(c.gpuPlan(true).state.brushMasks,&actualDocument));
        QVERIFY(BrushMaskDocument::toJson(expected.brushMasks,&expectedDocument));QCOMPARE(actualDocument,expectedDocument);
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);const auto committed=c.gpuPlan(true).state.toJson();const auto committedHistory=cursor(c);
        QVERIFY(c.beginBrushStroke("",.4,.5,.1,.5,1,1));c.setViewport(550,380,1,1,.6,.5);
        QVERIFY(!c.brushDrawing()&&c.brushPreviewUrl().isEmpty());QTRY_VERIFY_WITH_TIMEOUT(c.brushReady(),15000);
        QCOMPARE(c.gpuPlan(true).state.toJson(),committed);QCOMPARE(cursor(c),committedHistory);
        QVERIFY(c.beginBrushStroke("",.4,.5,.1,.5,1,1));++c.m_catalogEpoch;
        QTRY_VERIFY_WITH_TIMEOUT(!c.m_brushPreviewJob->busy(),5000);QVERIFY(c.brushPreviewUrl().isEmpty());QVERIFY(!c.appendBrushStroke(.5,.5));QVERIFY(!c.brushDrawing());
        QCOMPARE(c.gpuPlan(true).state.toJson(),committed);QCOMPARE(cursor(c),committedHistory);
        qInfo().noquote()<<"BRUSH_LIVE_CONTROLLER_EVIDENCE "<<QJsonDocument(QJsonObject{{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"catalog_epoch_zero",true},{"committed_history_scopes_unchanged",true},{"snapshot_final_spacing_exact",true},{"prepared_viewport",true},{"monitor_rekeys_without_edit",true},{"cancel_refunds",true},{"one_undo",true},{"viewport_catalog_cancel",true},
            {"native_ui_acceptance",false},{"native_gpu_brush",false},{"source_pixels",80*48}}).toJson(QJsonDocument::Compact);
    }
};
QTEST_MAIN(BrushLivePreviewTests)
#include "BrushLivePreviewTests.moc"
