#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>
#include <QRgba64>
#include <cstring>
#include <stdexcept>
#include "app/PhotoController.h"
#include "core/cache/AnalysisFrameCache.h"
#include "core/cache/FullScopesCache.h"
#include "core/cache/ScopePlotCache.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/StageGraph.h"
#include "diagnostics/PerformanceRecorder.h"

namespace {
QImage sourceImage(bool floating=false) {
    QImage image(2203,1207,floating?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
        if(floating) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            p[0]=float(x%47)/23.f-.17f;p[1]=float(y%43)/29.f;p[2]=float((x+y)%37)/31.f-.08f;p[3]=(x+y)%7?.61f:0.f;
        }else reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(4000+x*17,6000+y*23,8000+(x+y)*9,65535);
    }
    return image;
}
AdjustmentState painted() {
    AdjustmentState state;state.exposure=-.4;state.saturation=11;
    state.look.mode="manual";state.look.code="FL";state.look.parameters={{"clarity",3},{"sharpness",4}};
    BrushMaskGeometry::Stroke stroke;stroke.style={.025,.43,.71};
    for(int i=0;i<13;++i)stroke.dabs.push_back({{.21+i*.041,.32+i*.019},.6+(i%3)*.1});
    state.brushMasks.layers={{"scope",true,.8,{stroke}}};return state;
}
QImage reference(const QImage &source,const ProcessingPlan &plan,const GeometryState &geometry) {
    const auto input=geometry.apply(source);QImage result(input.size(),QImage::Format_RGBA64);
    result.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<input.height();y+=128) {
        const auto strip=ImagePipeline::processRegion(input,plan,{0,y,input.width(),std::min(128,input.height()-y)});
        if(strip.isNull())return {};
        for(int row=0;row<strip.height();++row)std::memcpy(result.scanLine(y+row),strip.constScanLine(row),std::size_t(input.width())*8);
    }
    return result;
}
bool sameHistogram(const ScopesResult &a,const ScopesResult &b) {
    return a.pixelCount==b.pixelCount&&a.red==b.red&&a.green==b.green&&a.blue==b.blue&&a.luma==b.luma
        &&a.shadowClipPercent==b.shadowClipPercent&&a.highlightClipPercent==b.highlightClipPercent;
}
QByteArray fileHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file)?hash.result().toHex():QByteArray{};
}
qint64 counter(const QString &name){return PerformanceRecorder::snapshot()["counters"].toObject()[name].toInteger();}
QByteArray storedZipEntry(const QString &path,const QByteArray &entry) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};const auto bytes=file.readAll();qsizetype offset=0;
    while(offset+30<=bytes.size()&&qFromLittleEndian<quint32>(bytes.constData()+offset)==0x04034b50) {
        const auto size=qFromLittleEndian<quint32>(bytes.constData()+offset+18);
        const auto nameSize=qFromLittleEndian<quint16>(bytes.constData()+offset+26),extra=qFromLittleEndian<quint16>(bytes.constData()+offset+28);
        const auto start=offset+30+nameSize+extra;if(start>bytes.size()||size>bytes.size()-start)return {};
        if(bytes.mid(offset+30,nameSize)==entry)return bytes.mid(start,size);offset=start+size;
    }
    return {};
}
}
class BrushScopeRoutingTests final:public QObject {
    Q_OBJECT
private slots:
    void originalGeometryAndCompletedFrameReuse() {
        const auto source=sourceImage(true);const auto before=StageGraph::outputFingerprint(source);
        auto state=painted();state.geometry.crop={.1,.08,.53,.61};state.geometry.straighten=.7;state.geometry.quarterTurns=1;
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,.3f);
        auto geometry=state.geometry;geometry.crop={0,0,1,1}; // The actual uncropped crop-editor view.
        const auto expected=reference(source,plan,geometry);QVERIFY(!expected.isNull());
        const auto counts=ScopesEngine::analyze(expected);QVERIFY(counts.pixelCount>2*1024*1024);
        auto budget=std::make_shared<BrushRenderBudget>();auto frames=std::make_shared<AnalysisFrameCache>(64*1024*1024);
        FullScopesCache histograms(4*1024*1024,frames,budget);ScopeRequest request{source,plan,7,true,geometry};
        const auto actual=histograms.analyze(request);QVERIFY2(actual.pixelCount,qPrintable(actual.error));QVERIFY(sameHistogram(actual,counts));
        QCOMPARE(budget->snapshot().charged,0);QCOMPARE(frames->snapshot()["entries"].toInt(),1);
        const auto key=StageGraph::analysisFrameKey(source,plan,geometry);auto lease=frames->find(key);QVERIFY(lease);QCOMPARE(lease.image(),expected);
        auto denied=std::make_shared<BrushRenderBudget>(1);ScopePlotCache plots(16*1024*1024,frames,denied);
        const auto beforeEncoded=counter("scope_plot_encoded_passes");
        for(const auto &mode:{"waveform","parade","vectorscope"}) {
            const auto plot=plots.render({source,plan,geometry,mode,7,true});const auto oracle=renderEncodedScopePlot(expected,mode);
            QVERIFY2(!plot.image.isNull(),qPrintable(plot.error));QCOMPARE(plot.image,oracle.image);QCOMPARE(plot.pixels,counts.pixelCount);
        }
        QCOMPARE(counter("scope_plot_encoded_passes"),beforeEncoded+1);QCOMPARE(plots.snapshot()["hits"].toInteger(),2);
        QCOMPARE(denied->snapshot().charged,0);
        auto changed=request;changed.plan.state.geometry.flipHorizontal=true;
        const auto invalid=histograms.analyze(changed);QVERIFY(!invalid.error.isEmpty());QVERIFY(!invalid.retryableBudget);QCOMPARE(invalid.pixelCount,0);
        const auto badPlot=plots.render({source,changed.plan,geometry,"waveform",7,true});QVERIFY(!badPlot.error.isEmpty());QCOMPARE(badPlot.pixels,0);
        auto different=geometry;different.flipHorizontal=true;
        const auto miss=plots.render({source,plan,different,"waveform",7,true});QVERIFY(miss.retryableBudget);QCOMPARE(miss.pixels,0);
        QCOMPARE(StageGraph::outputFingerprint(source),before);
        ScopePlotCache streamed(16*1024*1024,{},budget);ScopePlotCache single(0,{},budget);
        const auto oracle=renderEncodedScopePlot(expected,"parade");
        QCOMPARE(streamed.render({source,plan,geometry,"parade",7,true}).image,oracle.image);
        QCOMPARE(single.render({source,plan,geometry,"parade",7,true}).image,oracle.image);QCOMPARE(budget->snapshot().charged,0);
    }
    void admissionFailuresAndCollectorCancellation() {
        const auto source=sourceImage();const auto state=painted();const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        QVERIFY(!ScopesEngine::analyzeFull(source,plan).pixelCount); // Keep the old research boundary.
        auto denied=std::make_shared<BrushRenderBudget>(1);auto frames=std::make_shared<AnalysisFrameCache>();
        FullScopesCache cache(4*1024*1024,frames,denied);ScopeRequest request{source,plan,9,true,{}};
        const auto rejected=cache.analyze(request);QVERIFY(rejected.retryableBudget);QVERIFY(rejected.error.contains("memory budget"));
        QCOMPARE(cache.snapshot()["entries"].toInt(),0);QCOMPARE(frames->snapshot()["entries"].toInt(),0);
        ScopePlotCache plots(16*1024*1024,frames,denied);QVERIFY(plots.render({source,plan,{},"waveform",9,true}).retryableBudget);
        BrushAdmissionFailure failure=BrushAdmissionFailure::None;QString error;auto budget=std::make_shared<BrushRenderBudget>();
        auto malformed=plan;malformed.data[ProcessingPlan::Tonal].x+=.1f;
        QVERIFY(!BrushRenderRequest::createOriginal(source,malformed,{},BrushRenderRequest::Layout::Stripes128,{},&error,budget,&failure));
        QCOMPARE(failure,BrushAdmissionFailure::Invalid);QCOMPARE(budget->snapshot().charged,0);
        auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(!BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,token,&error,budget,&failure));
        QCOMPARE(failure,BrushAdmissionFailure::Cancelled);token->store(false);
        auto admitted=BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,{},&error,budget,&failure);
        QVERIFY2(admitted,qPrintable(error));QCOMPARE(failure,BrushAdmissionFailure::None);
        int observers=0;const auto before=counter("full_scopes_collector_failures");
        const auto survived=ScopesEngine::analyzeAdmitted(*admitted,{},&error,nullptr,[&](const QImage &,int){++observers;throw std::runtime_error("optional collector fixture");});
        QVERIFY2(survived.pixelCount,qPrintable(error));QCOMPARE(observers,1);QCOMPARE(counter("full_scopes_collector_failures"),before+1);
        admitted.reset();QCOMPARE(budget->snapshot().charged,0);
        admitted=BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,token,&error,budget);
        QVERIFY2(admitted,qPrintable(error));auto writer=frames->begin("cancelled-admitted",source.size());QVERIFY(writer);
        std::array<ScopePlotResult,3> unpublished;unpublished[0].error="sentinel";int completed=0;
        const auto cancelledResult=ScopesEngine::analyzeAdmitted(*admitted,token,&error,&unpublished,[&](const QImage &strip,int y){
            if(!writer.append(strip,y,token))throw std::runtime_error("append failed");if(++completed==2)token->store(true);
        });
        QCOMPARE(completed,2);QCOMPARE(cancelledResult.pixelCount,0);QCOMPARE(unpublished[0].error,QString("sentinel"));
        QVERIFY(!writer.publish(token));writer={};QCOMPARE(frames->snapshot()["entries"].toInt(),0);
        admitted.reset();QCOMPARE(budget->snapshot().charged,0);token->store(false);
        FullScopesCache healthy(4*1024*1024,{},budget);QVERIFY(sameHistogram(healthy.analyze(request),survived));QCOMPARE(budget->snapshot().charged,0);
    }
    void nativeSonyFullStatisticsReuse() {
        const auto fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()) {if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required public Sony RAW unavailable");QSKIP("No public Sony dataset configured");}
        const auto path=QDir(fixtures).filePath("sony_a7_iv_07.arw");const QByteArray hash="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";
        QCOMPARE(fileHash(path),hash);const auto source=loadSourceFresh(path,{});QVERIFY2(!source.image.isNull(),qPrintable(source.error));
        QCOMPARE(source.image.size(),QSize(7028,4688));QCOMPARE(source.image.format(),QImage::Format_RGBA64);
        const auto before=StageGraph::outputFingerprint(source.image);auto state=painted();state.look={};state.geometry.crop={.2,.1,.4,.6};
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,0);
        const GeometryState view;const auto expected=reference(source.image,plan,view);QVERIFY(!expected.isNull());
        const auto counts=ScopesEngine::analyze(expected);QCOMPARE(counts.pixelCount,32947264);
        auto budget=std::make_shared<BrushRenderBudget>();auto frames=std::make_shared<AnalysisFrameCache>();
        FullScopesCache histograms(4*1024*1024,frames,budget);const auto actual=histograms.analyze({source.image,plan,11,true,view});
        QVERIFY2(actual.pixelCount,qPrintable(actual.error));QVERIFY(sameHistogram(actual,counts));QCOMPARE(budget->snapshot().charged,0);
        auto denied=std::make_shared<BrushRenderBudget>(1);ScopePlotCache plots(16*1024*1024,frames,denied);
        const auto beforeEncoded=counter("scope_plot_encoded_passes");
        for(const auto &mode:{"waveform","parade","vectorscope"}) {
            const auto actualPlot=plots.render({source.image,plan,view,mode,11,true});const auto expectedPlot=renderEncodedScopePlot(expected,mode);
            QVERIFY2(!actualPlot.image.isNull(),qPrintable(actualPlot.error));QCOMPARE(actualPlot.pixels,32947264);QCOMPARE(actualPlot.image,expectedPlot.image);
        }
        QCOMPARE(counter("scope_plot_encoded_passes"),beforeEncoded+1);QCOMPARE(plots.snapshot()["hits"].toInteger(),2);QCOMPARE(denied->snapshot().charged,0);
        QCOMPARE(StageGraph::outputFingerprint(source.image),before);QCOMPARE(fileHash(path),hash);
        qInfo().noquote()<<"BRUSH_SCOPE_ROUTING_EVIDENCE "<<QJsonDocument(QJsonObject{
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"case","native-sony-full-statistics"},{"raw_sha256",QString::fromLatin1(hash)},
            {"source_pixels",32947264},{"statistics_pixels",qint64(actual.pixelCount)},{"exact_histogram_bins",4096},{"exact_plot_modes",3},
            {"uncropped_view_geometry",true},{"completed_frame_plot_reuse",true},{"operation_peak_reserved_bytes",budget->snapshot().peak},
            {"operation_end_charged_bytes",budget->snapshot().charged},{"source_unchanged",true},{"native_float_raw_decoder",false},{"native_gpu_brush",false}}).toJson(QJsonDocument::Compact);
    }
    void controllerFailuresRetryAndDiagnostic() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto original=dir.filePath("source.png");
        const auto source=sourceImage();QVERIFY(source.save(original));const auto hash=fileHash(original);const auto state=painted();QString error;
        BrushRenderRequest::Spec spec;spec.layout=BrushRenderRequest::Layout::Stripes128;
        auto measured=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(measured,qPrintable(error));const auto needed=measured->reservedBytes();measured.reset();
        auto budget=std::make_shared<BrushRenderBudget>(needed+source.sizeInBytes());
        auto blocker=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(blocker,qPrintable(error));
        ProcessedImageProvider provider;
        {
            PhotoController controller(&provider,nullptr,budget);controller.setGpuEnabled(false);
            QVERIFY(controller.importFile(QUrl::fromLocalFile(original)));
            QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady()&&!controller.loading()&&!controller.rendering()&&controller.m_sourceIsFull,15000);
            QVERIFY(controller.flushEdits());controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();controller.m_refineTimer.stop();
            const auto saved=controller.m_photos[controller.m_currentIndex].state;
            const auto restore=qScopeGuard([&]{controller.m_photos[controller.m_currentIndex].state=saved;controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();});
            controller.m_photos[controller.m_currentIndex].state=state;++controller.m_requestedRevision;controller.m_scopesRank=-1;controller.m_scopesUpdating=true;
            controller.scheduleRender(false);controller.setExactScopes(true);controller.setScopeMode("waveform");
            QTRY_VERIFY_WITH_TIMEOUT(controller.m_deferredBudgetJobs[0].pending&&controller.m_deferredBudgetJobs[1].pending&&!controller.rendering(),10000);
            QVERIFY(!controller.m_scopesUpdating);QVERIFY(controller.scopesStatus().contains("内存")||controller.scopesStatus().contains("memory"));QVERIFY(!controller.scopePlotCurrent());
            QTRY_VERIFY_WITH_TIMEOUT(controller.m_deferredBudgetJobs[2].pending,10000);
            blocker.reset();
            // An actual completed independent export wakes deferred work immediately.
            QImage small(31,23,QImage::Format_RGBA64);small.fill(QColor(70,90,110));
            QSignalSpy finished(controller.m_exportQueue.get(),&ExportQueue::finished);
            QVERIFY(controller.m_exportQueue->start({{dir.filePath("small.png"),dir.filePath("small-output.png"),small,{},ColorManagement::OutputSpace::SRgb,100,true}}));
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,10000);QCOMPARE(finished[0][0].toInt(),1);
            QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering()&&controller.m_scopesRank==1&&controller.scopePlotCurrent(),30000);
            for(const auto &pending:controller.m_deferredBudgetJobs)QVERIFY(!pending.pending);
            QCOMPARE(controller.scopesPixelCount(),qulonglong(source.width())*source.height());QVERIFY(controller.m_scopesError.isEmpty());
            const auto expected=reference(controller.m_fullSource,controller.gpuPlan(true),controller.previewGeometry());
            QVERIFY(sameHistogram(controller.m_scopes,ScopesEngine::analyze(expected)));
            const auto report=controller.reportBug();QVERIFY2(!report.isEmpty(),qPrintable(controller.statusMessage()));
            const auto cleanup=qScopeGuard([&]{QFile::remove(report);});
            const auto trace=QJsonDocument::fromJson(storedZipEntry(report,"stage_outputs.json")).object();QVERIFY(!trace.isEmpty());
            QVERIFY(trace["cpu_srgb_output"].toObject()["available"].toBool());
            const auto stages=trace["color_stages"].toObject();QVERIFY(!stages["available"].toBool());QVERIFY2(stages["final_frame_admitted"].toBool(),qPrintable(stages["error"].toString()));
            QCOMPARE(fileHash(original),hash);
        }
        provider.setImage({});provider.setScopePlot({});QCOMPARE(budget->snapshot().charged,0);
    }
};
QTEST_MAIN(BrushScopeRoutingTests)
#include "BrushScopeRoutingTests.moc"
