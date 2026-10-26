#include <QtTest>
#include <QImage>
#include <QRgba64>
#include <QTemporaryDir>
#include <QImageReader>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QFile>
#include <atomic>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QDataStream>
#include <QStandardPaths>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "diagnostics/LoggingEngine.h"
#include <cstring>
#include <random>
#include "core/pipeline/ProcessingPlan.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/async/LatestJob.h"
#include "core/async/ParallelRows.h"
#include "core/cache/SourceCache.h"
#include "core/cache/RenderedPreviewCache.h"
#include "core/cache/FullScopesCache.h"
#include "core/cache/ScopePlotCache.h"
#include "diagnostics/PerformanceRecorder.h"
#include "core/preview/PreviewTasks.h"
#include "core/pipeline/FloatSceneResampler.h"
#include "core/project/ProjectDatabase.h"
#include "core/export/JpegExporter.h"
#include "core/scopes/ScopesEngine.h"
QImage legacyProcess(const QImage &,const AdjustmentState &,ImagePipeline::InputEncoding);
QImage correctedReferenceProcess(const QImage &,const AdjustmentState &,ImagePipeline::InputEncoding);
namespace {
QImage fixture(int width=512,int height=341) {
    QImage image(width,height,QImage::Format_RGBA64);
    std::mt19937 rng(78412);
    for (int y=0;y<height;++y) {
        auto *row=reinterpret_cast<QRgba64 *>(image.scanLine(y));
        for (int x=0;x<width;++x) row[x]=QRgba64::fromRgba64(rng()%50000,rng()%50000,rng()%50000,65535);
    }
    return image;
}
ProcessingPlan nonRawLinearPlan(const AdjustmentState &state={},ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb) {
    return ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0.0f);
}
bool sameExposureBits(const std::vector<float> &a,const std::vector<float> &b) {
    return a.size()==b.size()
        && (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0);
}
}
class PerformanceTests:public QObject {
    Q_OBJECT
private slots:
    void brushFrameRequestCancelsWithoutPublishingPartialImage() {
        namespace BM=BrushMaskGeometry; namespace BD=BrushMaskDocument;
        BM::Stroke stroke{BM::Style{.25,.4,.7},{}};
        stroke.dabs.assign(128,{{.5,.5},1});
        BD::Layer layer;layer.id="cancel_load";layer.enabled=true;
        layer.exposureEv=1;layer.strokes={stroke};
        AdjustmentState state;state.brushMasks.layers={layer};
        const auto source=fixture(1024,1024);
        const auto plan=nonRawLinearPlan(state);
        auto token=std::make_shared<std::atomic_bool>(false);
        std::atomic_bool entered{false};
        auto future=QtConcurrent::run([&] {
            entered.store(true);
            return ImagePipeline::processWithPlan(source,plan,token,false);
        });
        QElapsedTimer wait;wait.start();
        while(!entered.load() && wait.elapsed()<5000) QTest::qWait(1);
        QTest::qWait(10);
        const bool pending=!future.isFinished();
        token->store(true);future.waitForFinished();
        QVERIFY(entered.load());QVERIFY(pending);
        QVERIFY(future.result().isNull());
        // A subsequent independent request is unaffected by the old token.
        QVERIFY(!ImagePipeline::processWithPlan(fixture(32,24),nonRawLinearPlan(),{},false).isNull());
    }

    void brushRepresentativeWorkloads_data() {
        QTest::addColumn<int>("width"); QTest::addColumn<int>("height");
        QTest::addColumn<int>("dabs"); QTest::addColumn<double>("radius");
        QTest::addColumn<bool>("viewport"); QTest::addColumn<bool>("offscreen");
        QTest::addColumn<int>("distribution"); // 0: historical, 1: spread, 2: mostly offscreen
        QTest::newRow("sparse-landscape")<<512<<384<<8<<.025<<false<<false<<0;
        QTest::newRow("overlap-portrait")<<384<<512<<96<<.12<<false<<false<<0;
        QTest::newRow("dense-viewport")<<1024<<768<<64<<.07<<true<<false<<0;
        QTest::newRow("empty-viewport")<<512<<384<<8<<.025<<true<<true<<0;
        // Dabs are per layer: these reach 4K/16K document dabs without
        // exceeding 64 strokes per layer or the 16,384-dab document cap.
        QTest::newRow("distributed-sparse-4k")<<512<<384<<2048<<.002<<false<<false<<1;
        QTest::newRow("distributed-sparse-16k")<<512<<384<<8192<<.002<<false<<false<<1;
        QTest::newRow("mostly-offscreen-4k")<<512<<384<<2048<<.002<<true<<false<<2;
        QTest::newRow("mostly-offscreen-16k")<<512<<384<<8192<<.002<<true<<false<<2;
        // Large enough to admit parallel tiles even when coverage is sparse;
        // the overlap case measures candidate work on the same source size.
        QTest::newRow("parallel-sparse-landscape")<<1024<<768<<8<<.025<<false<<false<<0;
        QTest::newRow("parallel-overlap-landscape")<<1024<<768<<96<<.12<<false<<false<<0;
    }
    void brushRepresentativeWorkloads() {
        namespace BT=BrushMaskTileRaster;
        namespace BD=BrushMaskDocument;
        namespace BM=BrushMaskGeometry;
        QFETCH(int,width); QFETCH(int,height); QFETCH(int,dabs);
        QFETCH(double,radius); QFETCH(bool,viewport); QFETCH(bool,offscreen);
        QFETCH(int,distribution);
        BD::Document doc;
        for(int layerIndex=0;layerIndex<2;++layerIndex) {
            BD::Layer layer;layer.id=QStringLiteral("perf_%1").arg(layerIndex);
            layer.enabled=true;layer.exposureEv=layerIndex?-.7:1.2;
            const int dabsPerStroke=distribution?128:dabs;
            for(int start=0;start<dabs;start+=dabsPerStroke) {
                BM::Stroke stroke{BM::Style{radius,.4,.7},{}};
                for(int i=start;i<std::min(start+dabsPerStroke,dabs);++i) {
                    BM::Point point{.25+.5*(i%8)/7.,.25+.5*((i/8)%8)/7.};
                    if(distribution) {
                        const int columns=dabs==2048?64:128;
                        const int rows=dabs/columns;
                        point={(i%columns+.5+.1*layerIndex)/columns,
                               (i/columns+.5+.1*layerIndex)/rows};
                        if(distribution==2) {
                            // Only eight dabs/layer intersect the small panned
                            // viewport. The remaining strokes are irrelevant.
                            if(i<8)point={.84+.012*(i%4),.85+.018*(i/4)};
                            else point={.1+.5*point.x,.1+.5*point.y};
                        }
                    }
                    stroke.dabs.push_back({point,.5+.5*(i%3)/2.});
                }
                layer.strokes.push_back(std::move(stroke));
            }
            doc.layers.push_back(layer);
        }
        QVERIFY(BD::validDocument(doc));
        const BT::NormalizedFrame frame=(offscreen || distribution==2)?BT::NormalizedFrame{.8,.8,.2,.2}
            : viewport?BT::NormalizedFrame{.2,.15,.6,.7}:BT::NormalizedFrame{};
        std::vector<double> planningMs,rasterMs,indexedMs,oracleMs;
        std::size_t visits=0,tileCount=0,candidateReferences=0,indexBytes=0;
        for(int repeat=0;repeat<3;++repeat) {
            BT::FramePlan plan;QElapsedTimer timer;timer.start();
            QVERIFY(BT::planFrame(doc,width,height,{0,0,width,height},&plan,nullptr,frame));
            planningMs.push_back(timer.nsecsElapsed()/1e6);
            QVERIFY(plan.hasCandidateIndex());
            candidateReferences=plan.candidateReferences();indexBytes=plan.candidateIndexBytes();
            if(distribution==2) {
                QVERIFY(candidateReferences>0);
                QVERIFY(candidateReferences<=64); // only the 16 visible dabs can intersect tiles
            }
            BT::FramePlan fallback;
            if(repeat==0) {
                QVERIFY(BT::planFrame(doc,width,height,{0,0,width,height},&fallback,nullptr,frame,0));
                QCOMPARE(fallback.candidateVisits,plan.candidateVisits);
                QCOMPARE(fallback.candidateReferences(),candidateReferences);
                QCOMPARE(fallback.candidateIndexBytes(),std::size_t(0));
                if(candidateReferences)QVERIFY(!fallback.hasCandidateIndex());
            }
            visits=0;tileCount=plan.tiles.size();double raster=0,indexed=0,oracle=0;
            for(std::size_t tileIndex=0;tileIndex<plan.tiles.size();++tileIndex) {
                const auto &tile=plan.tiles[tileIndex];
                BT::ExposureOutput indexedOutput;timer.restart();
                QVERIFY(BT::exposureOnly(plan,tileIndex,&indexedOutput));
                indexed+=timer.nsecsElapsed()/1e6;
                BT::ExposureOutput actual;timer.restart();
                QVERIFY(BT::exposureOnly(doc,width,height,tile.rect,&actual,nullptr,frame));
                raster+=timer.nsecsElapsed()/1e6;
                QCOMPARE(actual.visitedDabPixels,tile.candidateVisits);
                QCOMPARE(indexedOutput.visitedDabPixels,actual.visitedDabPixels);
                QVERIFY(sameExposureBits(indexedOutput.weightedExposureEv,actual.weightedExposureEv));
                if(repeat==0) {
                    BT::ExposureOutput fallbackOutput;
                    QVERIFY(BT::exposureOnly(fallback,tileIndex,&fallbackOutput));
                    QCOMPARE(fallbackOutput.visitedDabPixels,actual.visitedDabPixels);
                    QVERIFY(sameExposureBits(fallbackOutput.weightedExposureEv,actual.weightedExposureEv));
                }
                visits+=actual.visitedDabPixels;
                std::vector<float> rgba(actual.weightedExposureEv.size()*4,.25f);
                BT::Output reference;timer.restart();
                QVERIFY(BT::render(doc,width,height,tile.rect,rgba,&reference,nullptr,frame));
                oracle+=timer.nsecsElapsed()/1e6;
                QVERIFY(sameExposureBits(actual.weightedExposureEv,reference.weightedExposureEv));
            }
            QCOMPARE(visits,plan.candidateVisits);
            rasterMs.push_back(raster);indexedMs.push_back(indexed);oracleMs.push_back(oracle);
        }
        for(auto *times:{&planningMs,&rasterMs,&indexedMs,&oracleMs}) std::sort(times->begin(),times->end());
        qInfo()<<"brush-workload"<<QTest::currentDataTag()<<"pixels"<<width*height
               <<"tiles"<<tileCount<<"candidate_visits"<<visits
               <<"median_plan_ms"<<planningMs[1]<<"median_exposure_ms"<<rasterMs[1]
               <<"median_rgba_oracle_ms"<<oracleMs[1];
        qInfo()<<"brush-candidate-index"<<QTest::currentDataTag()<<"document_dabs"<<2*dabs
               <<"document_strokes"<<2*doc.layers.front().strokes.size()
               <<"candidate_references"<<candidateReferences<<"index_capacity_bytes"<<indexBytes
               <<"median_plan_ms"<<planningMs[1]<<"median_indexed_exposure_ms"<<indexedMs[1]
               <<"median_unindexed_exposure_ms"<<rasterMs[1];
        // Timings are evidence, not a portable GPU/interactive latency claim.
        std::atomic_bool cancel{true};BT::FramePlan sentinel;
        sentinel.candidateVisits=42;
        QVERIFY(!BT::planFrame(doc,width,height,{0,0,width,height},&sentinel,&cancel,frame));
        QCOMPARE(sentinel.candidateVisits,std::size_t(42));
        const auto source=fixture(width,height);auto state=AdjustmentState{};state.brushMasks=doc;
        auto plan=nonRawLinearPlan(state);
        plan.setFrameRect(QRectF(frame.x,frame.y,frame.width,frame.height));
        BT::FramePlan framePlan;
        QVERIFY(BT::planFrame(doc,width,height,{0,0,width,height},&framePlan,nullptr,frame));
        BT::ExposureOutput cancelled{{9.f},42};
        QVERIFY(!BT::exposureOnly(framePlan,0,&cancelled,&cancel));
        QCOMPARE(cancelled.visitedDabPixels,std::size_t(42));
        QVERIFY(cancelled.weightedExposureEv==std::vector<float>{9.f});
        const auto expectedPruned=std::count_if(framePlan.tiles.begin(),framePlan.tiles.end(),
            [](const auto &tile){return tile.candidateVisits==0;});
        const auto counter=[](const char *key) {
            return PerformanceRecorder::snapshot()["counters"].toObject().value(key).toInteger();
        };
        const auto beforePruned=counter("brush_research_pruned_tiles");
        const auto beforeRaster=counter("brush_research_rasterized_tiles");
        const auto beforeTiles=counter("brush_research_cpu_tiles");
        const auto beforeVisits=counter("brush_research_dab_pixel_visits");
        const auto beforeReferences=counter("brush_research_candidate_references");
        const auto beforeIndexBytes=counter("brush_research_candidate_index_bytes");
        const auto beforeFallbacks=counter("brush_research_candidate_index_fallbacks");
        const auto beforeHelpers=counter("brush_research_helper_tasks");
        const auto beforeParallel=counter("brush_research_parallel_frames");
        const auto beforeSerial=counter("brush_research_serial_frames");
        std::vector<double> cpuMs;QImage processed;
        for(int repeat=0;repeat<3;++repeat) {
            QElapsedTimer timer;timer.start();
            const auto result=ImagePipeline::processWithPlan(source,plan,{},false);
            cpuMs.push_back(timer.nsecsElapsed()/1e6);
            QVERIFY(!result.isNull());
            if(repeat)QCOMPARE(result,processed);
            processed=result;
        }
        QCOMPARE(counter("brush_research_pruned_tiles")-beforePruned,qint64(3*expectedPruned));
        QCOMPARE(counter("brush_research_rasterized_tiles")-beforeRaster,
                 qint64(3*(framePlan.tiles.size()-expectedPruned)));
        QCOMPARE(counter("brush_research_cpu_tiles")-beforeTiles,qint64(3*framePlan.tiles.size()));
        QCOMPARE(counter("brush_research_dab_pixel_visits")-beforeVisits,qint64(3*framePlan.candidateVisits));
        QCOMPARE(counter("brush_research_candidate_references")-beforeReferences,
                 qint64(3*framePlan.candidateReferences()));
        QCOMPARE(counter("brush_research_candidate_index_bytes")-beforeIndexBytes,
                 qint64(3*framePlan.candidateIndexBytes()));
        QCOMPARE(counter("brush_research_candidate_index_fallbacks"),beforeFallbacks);
        QCOMPARE(counter("brush_research_helper_tasks"),beforeHelpers);
        QCOMPARE(counter("brush_research_parallel_frames"),beforeParallel);
        QCOMPARE(counter("brush_research_serial_frames")-beforeSerial,
                 framePlan.candidateVisits ? qint64(3) : qint64(0));
        if(offscreen) {
            QCOMPARE(framePlan.candidateVisits,std::size_t(0));
            QCOMPARE(processed,ImagePipeline::processWithPlan(source,nonRawLinearPlan(),{},false));
        }
        std::sort(cpuMs.begin(),cpuMs.end());
        qInfo()<<"brush-frame-pruning"<<QTest::currentDataTag()<<"tiles"<<framePlan.tiles.size()
               <<"pruned_tiles"<<expectedPruned<<"median_cpu_frame_ms"<<cpuMs[1];

        const auto parallelBefore=PerformanceRecorder::snapshot()["counters"].toObject();
        const int budget=ParallelRows::threadBudget();
        const bool eligible=framePlan.candidateVisits && framePlan.tiles.size()>1 && budget>1
            && std::size_t(width)*height+framePlan.candidateVisits>=262144;
        std::vector<double> parallelMs;
        for(int repeat=0;repeat<3;++repeat) {
            const auto helpers=counter("brush_research_helper_tasks");
            const auto parallelFrames=counter("brush_research_parallel_frames");
            const auto serialFrames=counter("brush_research_serial_frames");
            QElapsedTimer timer;timer.start();
            const auto result=ImagePipeline::processWithPlan(source,plan,{},true);
            parallelMs.push_back(timer.nsecsElapsed()/1e6);
            QVERIFY(!result.isNull());QCOMPARE(result,processed);
            const auto submitted=counter("brush_research_helper_tasks")-helpers;
            if(!framePlan.candidateVisits) {
                QCOMPARE(submitted,qint64(0));
                QCOMPARE(counter("brush_research_parallel_frames"),parallelFrames);
                QCOMPARE(counter("brush_research_serial_frames"),serialFrames);
            } else {
                QVERIFY(submitted>=0 && submitted<=std::max(0,budget-1));
                if(!eligible) QCOMPARE(submitted,qint64(0));
                QCOMPARE(counter("brush_research_parallel_frames")-parallelFrames,
                         submitted>0 ? qint64(1) : qint64(0));
                QCOMPARE(counter("brush_research_serial_frames")-serialFrames,
                         submitted>0 ? qint64(0) : qint64(1));
            }
        }
        const auto delta=[&](const char *key) { return counter(key)-parallelBefore.value(key).toInteger(); };
        QCOMPARE(delta("brush_research_cpu_tiles"),qint64(3*framePlan.tiles.size()));
        QCOMPARE(delta("brush_research_pruned_tiles"),qint64(3*expectedPruned));
        QCOMPARE(delta("brush_research_rasterized_tiles"),qint64(3*(framePlan.tiles.size()-expectedPruned)));
        QCOMPARE(delta("brush_research_dab_pixel_visits"),qint64(3*framePlan.candidateVisits));
        QCOMPARE(delta("brush_research_candidate_references"),qint64(3*framePlan.candidateReferences()));
        QCOMPARE(delta("brush_research_candidate_index_bytes"),qint64(3*framePlan.candidateIndexBytes()));
        QCOMPARE(delta("brush_research_candidate_index_fallbacks"),qint64(0));
        std::sort(parallelMs.begin(),parallelMs.end());
        qInfo()<<"brush-frame-parallel"<<QTest::currentDataTag()<<"thread_budget"<<budget
               <<"parallel_eligible"<<eligible<<"helper_tasks"<<delta("brush_research_helper_tasks")
               <<"parallel_frames"<<delta("brush_research_parallel_frames")
               <<"serial_frames"<<delta("brush_research_serial_frames")
               <<"median_serial_cpu_frame_ms"<<cpuMs[1]<<"median_parallel_cpu_frame_ms"<<parallelMs[1];
        auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(ImagePipeline::processWithPlan(source,plan,token,false).isNull());
    }

    void scopePlotCacheHonorsLruOversizeAndRetainedLutCosts() {
        const auto source=fixture(16,9); ScopePlotRequest a{source,nonRawLinearPlan(),{},"waveform",1,true};
        ScopePlotCache probe; const auto original=probe.render(a); QVERIFY(!original.image.isNull());
        const auto cost=probe.snapshot()["charged_bytes"].toInteger(); QVERIFY(cost>=5*1024*1024);
        ScopePlotCache cache(2*cost); auto b=a,c=a; b.plan.state.exposure=.3; b.plan=nonRawLinearPlan(b.plan.state);
        c.plan.state.exposure=.6; c.plan=nonRawLinearPlan(c.plan.state);
        const auto first=cache.render(a); cache.render(b); a.mode="vectorscope"; cache.render(a); cache.render(c);
        QCOMPARE(cache.snapshot()["entries"].toInt(),2); QCOMPARE(cache.snapshot()["evictions"].toInteger(),1);
        b.mode="parade"; cache.render(b); QCOMPARE(cache.snapshot()["misses"].toInteger(),4);
        ScopePlotCache disabled(0); a.mode="waveform"; QCOMPARE(disabled.render(a).image,original.image); QCOMPARE(disabled.snapshot()["entries"].toInt(),0);
        ScopePlotCache small(cost-1024); QCOMPARE(small.render(a).image,original.image); QCOMPARE(small.snapshot()["bypasses"].toInteger(),1);
        auto lut=std::make_shared<LookLut>(*LookLut::identity(5)); std::weak_ptr<const LookLut> weak=lut;
        AdjustmentState state; state.look.mode="calibrated"; state.look.lut=lut; auto withLut=a; withLut.plan=nonRawLinearPlan(state);
        ScopePlotCache guarded(cost+4096); guarded.render(withLut);
        QVERIFY(guarded.snapshot()["charged_bytes"].toInteger()>cost); state.look.lut.reset(); withLut.plan.state.look.lut.reset(); lut.reset(); QVERIFY(!weak.expired());
        guarded.render(a); QVERIFY(weak.expired());
        auto largeLut=LookLut::identity(33); state.look.lut=largeLut; withLut.plan=nonRawLinearPlan(state);
        ScopePlotCache oversized(cost); oversized.render(a); const auto held=oversized.render(a).image.cacheKey();
        QCOMPARE(oversized.render(withLut).image,renderScopePlot(withLut,{}).image); QCOMPARE(oversized.snapshot()["bypasses"].toInteger(),1);
        QCOMPARE(oversized.render(a).image.cacheKey(),held); QCOMPARE(oversized.snapshot()["entries"].toInt(),1);
    }
    void scopePlotCacheDropsCancelledBatchesAndSharesConcurrentModes() {
        ScopePlotCache cache; const auto source=fixture(24,17); ScopePlotRequest request{source,nonRawLinearPlan(),{},"waveform",1,true};
        auto token=std::make_shared<std::atomic_bool>(true); QVERIFY(cache.render(request,token).image.isNull());
        const auto complete=cache.render(request); QVERIFY(cache.render(request,token).image.isNull());
        const auto entries=cache.snapshot()["entries"].toInt(); request.source=fixture(2048,1536);
        AdjustmentState state; state.vignetteAmount=-1.5; state.vignetteMidpoint=.3; state.vignetteFeather=.7;
        state.look.mode="manual"; state.look.code="FL"; state.look.parameters={{"clarity",4},{"sharpness",5}}; request.plan=nonRawLinearPlan(state);
        token->store(false); const auto tiles=PerformanceRecorder::snapshot()["counters"].toObject()["scope_plot_shared_tiles"].toInteger();
        auto future=QtConcurrent::run([&] { return cache.render(request,token); });
        QElapsedTimer wait; wait.start(); while (PerformanceRecorder::snapshot()["counters"].toObject()["scope_plot_shared_tiles"].toInteger()==tiles && !future.isFinished() && wait.elapsed()<5000) QTest::qWait(1);
        const bool started=PerformanceRecorder::snapshot()["counters"].toObject()["scope_plot_shared_tiles"].toInteger()>tiles && !future.isFinished(); token->store(true); future.waitForFinished();
        QVERIFY(started); QVERIFY(future.result().image.isNull()); QCOMPARE(cache.snapshot()["entries"].toInt(),entries); QCOMPARE(cache.snapshot()["cancelled_requests"].toInteger(),3);
        request.source=source; request.plan=nonRawLinearPlan(); request.mode="waveform"; QCOMPARE(cache.render(request).image.cacheKey(),complete.image.cacheKey());
        ScopePlotCache concurrent; std::array<QFuture<ScopePlotResult>,6> work; const QStringList modes{"waveform","parade","vectorscope"};
        for (int i=0;i<6;++i) { auto r=request; r.mode=modes[i%3]; work[i]=QtConcurrent::run([&concurrent,r] { return concurrent.render(r); }); }
        for (int i=0;i<6;++i) { work[i].waitForFinished(); auto r=request; r.mode=modes[i%3]; QCOMPARE(work[i].result().image,renderScopePlot(r,{}).image); QCOMPARE(work[i].result().pixels,quint64(24*17)); }
        QCOMPARE(concurrent.snapshot()["entries"].toInt(),1); QVERIFY(concurrent.snapshot()["charged_bytes"].toInteger()<=concurrent.snapshot()["budget_bytes"].toInteger());
    }
    void vignetteIsDeterministicAndViewportCoordinatesInvalidateRenderCache() {
        const auto source=fixture(319,277); const auto before=source.copy();
        AdjustmentState state; state.vignetteAmount=-2; state.vignetteMidpoint=.3; state.vignetteFeather=.6;
        state.look.mode="manual"; state.look.code="FL"; state.look.parameters={{"clarity",4},{"sharpness",5}};
        auto plan=nonRawLinearPlan(state); plan.setFrameRect({.17,.28,.52,.61});
        const bool hadBudget=qEnvironmentVariableIsSet("JIXELLIGHT_CPU_THREADS"); const auto previous=qgetenv("JIXELLIGHT_CPU_THREADS");
        auto restore=qScopeGuard([&] { if (hadBudget) qputenv("JIXELLIGHT_CPU_THREADS",previous); else qunsetenv("JIXELLIGHT_CPU_THREADS"); });
        qputenv("JIXELLIGHT_CPU_THREADS","1"); const auto serial=ImagePipeline::processWithPlan(source,plan);
        qputenv("JIXELLIGHT_CPU_THREADS","8"); QCOMPARE(ImagePipeline::processWithPlan(source,plan),serial);
        for (int y=0;y<source.height();y+=128) {
            const QRect roi(0,y,source.width(),std::min(128,source.height()-y)); QCOMPARE(ImagePipeline::processRegion(source,plan,roi),serial.copy(roi));
        }
        RenderedPreviewCache cache; const auto first=cache.render(source,plan); QCOMPARE(first,serial);
        auto shifted=plan; shifted.setFrameRect({.2,.28,.52,.61});
        const auto next=cache.render(source,shifted); QVERIFY(next!=first); QCOMPARE(cache.snapshot()["misses"].toInteger(),2);
        QCOMPARE(cache.render(source,plan).cacheKey(),first.cacheKey());
        QCOMPARE(source,before); QVERIFY(cache.snapshot()["charged_bytes"].toInteger()<=cache.snapshot()["budget_bytes"].toInteger());
    }
    void floatPreviewResizeIsDeterministicAcrossThreadBudgetsAndBounded() {
        QImage scene(800,600,QImage::Format_RGBA32FPx4);
        for(int y=0;y<scene.height();++y) for(int x=0;x<scene.width();++x) {
            auto *p=reinterpret_cast<float *>(scene.scanLine(y))+4*x;
            p[0]=float(2.2+.002*x+.001*y);
            p[1]=float(.08+.002*x);
            p[2]=float(-.4+.0004*y);
            p[3]=(x+y)%13==0?0.0f:(x+y)%5==0?.375f:1.0f;
        }
        const bool hadBudget=qEnvironmentVariableIsSet("JIXELLIGHT_CPU_THREADS");
        const auto previous=qgetenv("JIXELLIGHT_CPU_THREADS");
        auto restore=qScopeGuard([&] {
            if(hadBudget)qputenv("JIXELLIGHT_CPU_THREADS",previous);
            else qunsetenv("JIXELLIGHT_CPU_THREADS");
        });
        const auto before=scene.copy();
        qputenv("JIXELLIGHT_CPU_THREADS","1");
        const auto serial=FloatSceneResampler::scale(scene,{480,360});
        QVERIFY(!serial.isNull());
        qputenv("JIXELLIGHT_CPU_THREADS","8");
        const auto parallel=FloatSceneResampler::scale(scene,{480,360});
        QCOMPARE(parallel,serial);
        QCOMPARE(scene,before);
        QCOMPARE(serial.sizeInBytes(),qint64(480)*360*16);
        QVERIFY(reinterpret_cast<const float *>(serial.constScanLine(200))[4*200]>1.0f);
        QVERIFY(reinterpret_cast<const float *>(serial.constScanLine(200))[4*200+2]<0.0f);
        // Same-size requests must not allocate/copy pixel storage.
        const auto same=FloatSceneResampler::scale(scene,scene.size());
        QCOMPARE(same.cacheKey(),scene.cacheKey());
        auto cancelledToken=std::make_shared<std::atomic_bool>(true);
        QVERIFY(FloatSceneResampler::scale(scene,{480,360},cancelledToken).isNull());
    }
    void manualGeometryIsDeterministicAcrossCpuBudgetsAndCancellation() {
        const QImage source=fixture(1100,800); const auto before=source.copy();
        GeometryState g; g.perspectiveHorizontal=.12; g.perspectiveVertical=-.08; g.distortion=.15;
        g.redCa=1; g.blueCa=-1; g.straighten=3;
        const bool hadBudget=qEnvironmentVariableIsSet("JIXELLIGHT_CPU_THREADS"); const auto previous=qgetenv("JIXELLIGHT_CPU_THREADS");
        qputenv("JIXELLIGHT_CPU_THREADS","1"); const auto serial=g.apply(source);
        qputenv("JIXELLIGHT_CPU_THREADS","8"); const auto parallel=g.apply(source);
        if (hadBudget) qputenv("JIXELLIGHT_CPU_THREADS",previous); else qunsetenv("JIXELLIGHT_CPU_THREADS");
        QVERIFY(!serial.isNull()); QCOMPARE(parallel,serial); QCOMPARE(source,before);
        const auto token=std::make_shared<std::atomic_bool>(false);
        auto future=QtConcurrent::run([&] { return g.apply(source,token); });
        QThread::msleep(2); token->store(true);
        future.waitForFinished(); QVERIFY(future.result().isNull()); QCOMPARE(source,before);
        QVERIFY(g.apply(source,token).isNull());
    }
    void parallelIsDeterministic() {
        const QImage image=fixture(800,512);
        AdjustmentState state; state.exposure=.6; state.temperature=30;state.tint=-15;state.saturation=12;state.hslHue[0]=28;state.masterCurve[2]=.56;
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        const auto serial=ImagePipeline::processWithPlan(image,plan,{},false);
        const auto parallel=ImagePipeline::processWithPlan(image,plan);
        QCOMPARE(serial,parallel);
    }
    void legacyReferenceBounds() {
        const QImage image=fixture(127,93);
        for (int mode=0;mode<3;++mode) {
            AdjustmentState state;
            if (mode==1) { state.exposure=.4;state.temperature=25;state.tint=-13;state.highlights=-20;state.contrast=7; }
            if (mode==2) { state.hue=-10;state.saturation=25;state.vibrance=10;state.hslSaturation[5]=-35;state.redCurve[2]=.52; }
            const auto old=legacyProcess(image,state,ImagePipeline::InputEncoding::LinearProPhoto);
            const auto expected=correctedReferenceProcess(image,state,ImagePipeline::InputEncoding::LinearProPhoto);
            const auto now=ImagePipeline::processWithPlan(image,nonRawLinearPlan(state));
            int maximum=0, originalMaximum=0, correctedPixels=0;
            for (int y=0;y<image.height();++y) {
                auto *a=reinterpret_cast<const QRgba64 *>(expected.constScanLine(y));
                auto *b=reinterpret_cast<const QRgba64 *>(now.constScanLine(y));
                auto *o=reinterpret_cast<const QRgba64 *>(old.constScanLine(y));
                for (int x=0;x<image.width();++x) {
                    maximum=std::max({maximum,std::abs(int(a[x].red())-int(b[x].red())),std::abs(int(a[x].green())-int(b[x].green())),std::abs(int(a[x].blue())-int(b[x].blue()))});
                    const int originalDelta=std::max({std::abs(int(o[x].red())-int(a[x].red())),std::abs(int(o[x].green())-int(a[x].green())),std::abs(int(o[x].blue())-int(a[x].blue()))});
                    originalMaximum=std::max(originalMaximum,originalDelta);
                    if(originalDelta>24) ++correctedPixels;
                }
            }
            qInfo()<<"independent corrected-reference max error"<<mode<<maximum
                   <<"intentional boundary correction versus alpha6 max"<<originalMaximum<<"pixels"<<correctedPixels;
            QVERIFY2(maximum<=24,qPrintable(QString::number(maximum)));
        }
    }
    void gamutBoundaryHasNoVisibleStep() {
        QImage image(257,1,QImage::Format_RGBA64);
        auto *source=reinterpret_cast<QRgba64 *>(image.bits());
        for(int x=0;x<image.width();++x) source[x]=QRgba64::fromRgba64(18743+x-128,43453,32369,65535);
        AdjustmentState state;state.hue=-23;state.saturation=18;state.vibrance=22;
        state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8;
        const auto actual=ImagePipeline::process(image,state,ImagePipeline::InputEncoding::LinearProPhoto);
        const auto *pixels=reinterpret_cast<const QRgba64 *>(actual.constBits());
        int largestStep=0;
        for(int x=1;x<image.width();++x)
            largestStep=std::max({largestStep,std::abs(int(pixels[x].red())-int(pixels[x-1].red())),
                std::abs(int(pixels[x].green())-int(pixels[x-1].green())),std::abs(int(pixels[x].blue())-int(pixels[x-1].blue()))});
        qInfo()<<"gamut boundary ramp maximum adjacent 16-bit step"<<largestStep;
        QVERIFY2(largestStep<=128,qPrintable(QString::number(largestStep)));
    }
    void cancellationDoesNotPublishPartialPixels() {
        auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(ImagePipeline::process(fixture(),{},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,token).isNull());
        QCOMPARE(ScopesEngine::analyze(fixture(),1024,token).pixelCount,quint64(0));
    }
    void latestOnlyQueue() {
        std::atomic_int calls{0};
        QVector<int> delivered;
        LatestJob<int,int> job([&](const int &value,const CancelToken &token) {
            ++calls;
            for(int i=0;i<20 && !cancelled(token);++i) QThread::msleep(1);
            return value;
        },[&](const int &,int result) { delivered.append(result); });
        for (int i=0;i<100;++i) job.submit(i);
        QTRY_COMPARE_WITH_TIMEOUT(delivered.size(),1,3000);
        QCOMPARE(delivered.first(),99);
        QVERIFY(calls.load()<=2);
    }
    void fp32SourceCacheSharesLruBudgetAndConcurrentReaders() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString key(64,'e');
        const auto signature=SourceCache::floatDecoderSignature(
            QByteArrayLiteral("camera float test: sensor black+white per channel; WB/matrix v1"));
        SourceData floating;floating.key=key;floating.fullResolution=true;
        floating.image=QImage(160,160,QImage::Format_RGBA32FPx4);
        for(int y=0;y<160;++y) for(int x=0;x<160;++x) {
            auto *p=reinterpret_cast<float *>(floating.image.scanLine(y))+4*x;
            p[0]=float(2.0+.001*x+.002*y);
            p[1]=float(-.25+.0001*y);
            p[2]=float(.1+.0001*x);
            p[3]=1.0f;
        }
        SourceCache cache(1024*1024,dir.path());
        QVERIFY(cache.storeFloatScene(floating,signature));
        const auto initial=cache.floatScene(key,signature);
        QVERIFY(!initial.image.isNull());
        QCOMPARE(initial.image.size(),floating.image.size());
        QCOMPARE(initial.image.format(),QImage::Format_RGBA32FPx4);
        const auto initialHit=cache.floatScene(key,signature);
        QCOMPARE(initialHit.image.cacheKey(),initial.image.cacheKey());
        QVERIFY(cache.memoryBytes()<=cache.budgetBytes());
        SourceData integer;
        integer.key=QString(64,'f');integer.fullResolution=true;
        integer.image=fixture(350,300); // 840 KiB, must evict 400 KiB float entry
        cache.put(integer);
        QVERIFY(cache.memoryBytes()<=cache.budgetBytes());
        QCOMPARE(cache.get(integer.key).image.cacheKey(),integer.image.cacheKey());
        const auto recovered=cache.floatScene(key,signature);
        QVERIFY(!recovered.image.isNull());
        QCOMPARE(recovered.image.format(),QImage::Format_RGBA32FPx4);
        QCOMPARE(recovered.image.size(),floating.image.size());
        QVERIFY(cache.memoryBytes()<=cache.budgetBytes());
        QVERIFY(cache.get(integer.key).image.isNull()); // single shared budget

        SourceCache parallel(1024*1024,dir.path());
        std::array<QFuture<SourceData>,6> tasks;
        for(auto &task:tasks)
            task=QtConcurrent::run([&parallel,key,signature] {
                return parallel.floatScene(key,signature);
            });
        for(auto &task:tasks) {
            task.waitForFinished();
            const auto result=task.result();
            QVERIFY(!result.image.isNull());
            QCOMPARE(result.image.format(),QImage::Format_RGBA32FPx4);
            QCOMPARE(result.image.size(),floating.image.size());
            const auto *p=reinterpret_cast<const float *>(result.image.constScanLine(100))+4*100;
            QVERIFY(p[0]>2.0f);
            QVERIFY(p[1]<0.0f);
        }
        QVERIFY(parallel.memoryBytes()<=parallel.budgetBytes());
        QCOMPARE(QDir(dir.path()).entryList({"*.jlfs"},QDir::Files).size(),1);
    }

    void sourceCacheBudgetAndDiskRoundtrip() {
        QTemporaryDir dir;
        SourceCache cache(1024*1024,dir.path());
        SourceData source;source.image=fixture(128,80);source.key=QString(64,'a');source.fullResolution=true;source.metadata={{"model","test"}};
        cache.put(source);QCOMPARE(cache.get(source.key).image,source.image);
        cache.storeDiskPreview(source);
        const auto disk=cache.diskPreview(source.key);
        QCOMPARE(disk.image,source.image);
        QCOMPARE(disk.metadata.value("model").toString(),QString("test"));
        QVERIFY(!disk.fullResolution);
        SourceData large=source;large.key=QString(64,'b');large.image=fixture(512,512);cache.put(large);
        QVERIFY(cache.get(large.key).image.isNull());
        QVERIFY(cache.memoryBytes()<=cache.budgetBytes());
        QFile corrupt(dir.filePath(source.key+".jlpv"));QVERIFY(corrupt.open(QIODevice::WriteOnly|QIODevice::Append));corrupt.write("x");corrupt.close();
        QVERIFY(cache.diskPreview(source.key).image.isNull());
        QVERIFY(cache.memoryBytes()<=cache.budgetBytes());
    }
    void fileCacheKeyInvalidation() {
        QTemporaryDir dir;
        QFile file(dir.filePath("source.raw"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("abc");file.close();
        const auto before=SourceCache::fileKey(file.fileName());
        QVERIFY(file.open(QIODevice::WriteOnly));file.write("abd");file.close();
        QVERIFY(before!=SourceCache::fileKey(file.fileName()));
    }
    void renderedPreviewCacheHonorsLruAndChargesRetainedLuts() {
        const QImage image=fixture(16,8); // 1024-byte rendered image.
        RenderedPreviewCache cache(2048);
        const auto a=nonRawLinearPlan();
        AdjustmentState state; state.exposure=.2; const auto b=nonRawLinearPlan(state);
        state.exposure=.4; const auto c=nonRawLinearPlan(state);
        const auto imageA=cache.render(image,a), imageB=cache.render(image,b);
        QCOMPARE(cache.render(image,a).cacheKey(),imageA.cacheKey()); // A becomes most recently used.
        cache.render(image,c);
        QCOMPARE(cache.snapshot().value("entries").toInt(),2);
        QCOMPARE(cache.snapshot().value("charged_bytes").toInteger(),2048);
        QCOMPARE(cache.snapshot().value("evictions").toInteger(),1);
        QCOMPARE(cache.render(image,a).cacheKey(),imageA.cacheKey());
        const auto newB=cache.render(image,b); QCOMPARE(newB,imageB); QVERIFY(newB.cacheKey()!=imageB.cacheKey());
        QCOMPARE(cache.snapshot().value("evictions").toInteger(),2);
        const auto large=fixture(32,16);
        QCOMPARE(cache.render(large,a),ImagePipeline::processWithPlan(large,a));
        QCOMPARE(cache.snapshot().value("bypasses").toInteger(),1);
        QCOMPARE(cache.snapshot().value("charged_bytes").toInteger(),2048); // Oversize doesn't flush useful entries.
        RenderedPreviewCache disabled(0);
        QCOMPARE(disabled.render(image,a),ImagePipeline::processWithPlan(image,a));
        QCOMPARE(disabled.snapshot().value("entries").toInt(),0);

        auto lut=std::make_shared<LookLut>(*LookLut::identity(5)); // 1500-byte retained payload.
        std::weak_ptr<const LookLut> weak=lut;
        state.look.mode="calibrated"; state.look.lut=lut;
        auto lutPlan=nonRawLinearPlan(state);
        cache.render(image,lutPlan); // 2524 bytes exceeds this cache, even though the image fits.
        QCOMPARE(cache.snapshot().value("bypasses").toInteger(),2);
        RenderedPreviewCache withLut(3072);
        withLut.render(image,lutPlan);
        QCOMPARE(withLut.snapshot().value("charged_bytes").toInteger(),3072);
        state.look.lut.reset(); lutPlan.state.look.lut.reset(); lut.reset();
        QVERIFY(!weak.expired()); // Pointer-key lifetime is guarded by the entry.
        withLut.render(image,a); QVERIFY(weak.expired());
        QVERIFY(withLut.snapshot().value("charged_bytes").toInteger()<=3072);
    }
    void renderedPreviewCacheDiscardsCancelledMissAndHit() {
        RenderedPreviewCache cache;
        const auto image=fixture(16,8); const auto plan=nonRawLinearPlan();
        auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(cache.render(image,plan,token).isNull());
        QCOMPARE(cache.snapshot().value("entries").toInt(),0);
        const auto complete=cache.render(image,plan);
        QVERIFY(cache.render(image,plan,token).isNull());
        QCOMPARE(cache.render(image,plan).cacheKey(),complete.cacheKey());
        QVERIFY(cache.render({},plan).isNull());

        const auto large=fixture(2048,1536); token->store(false);
        AdjustmentState complex; complex.hue=13; complex.saturation=27; complex.hslHue[0]=19; complex.masterCurve[2]=.58;
        const auto expensive=nonRawLinearPlan(complex);
        const auto misses=cache.snapshot().value("misses").toInteger();
        auto future=QtConcurrent::run([&] { return cache.render(large,expensive,token); });
        // Wait until lookup has missed, then cancel the expensive request.
        QElapsedTimer wait; wait.start();
        while (cache.snapshot().value("misses").toInteger()==misses && wait.elapsed()<5000) QTest::qWait(1);
        const bool started=cache.snapshot().value("misses").toInteger()>misses;
        token->store(true); future.waitForFinished(); QVERIFY(future.result().isNull());
        QVERIFY(started);
        QCOMPARE(cache.snapshot().value("entries").toInt(),1);
        token->store(false);
        const auto retry=cache.render(large,expensive,token); QVERIFY(!retry.isNull());
        QCOMPARE(cache.snapshot().value("misses").toInteger(),misses+2);
        QCOMPARE(cache.render(large,expensive).cacheKey(),retry.cacheKey());
        QCOMPARE(cache.snapshot().value("cancelled_requests").toInteger(),3);
    }
    void renderedPreviewCacheSupportsConcurrentImmutableSnapshots() {
        RenderedPreviewCache cache(2*1024*1024);
        const auto source=fixture(192,128);
        AdjustmentState state; state.exposure=.4; state.saturation=17; state.masterCurve[2]=.58;
        const auto plan=nonRawLinearPlan(state), other=nonRawLinearPlan();
        const auto expected=ImagePipeline::processWithPlan(source,plan), expectedOther=ImagePipeline::processWithPlan(source,other);
        QList<QFuture<QImage>> futures;
        for (int i=0;i<8;++i) futures.append(QtConcurrent::run([&,i] { return cache.render(source,i%2 ? plan : other); }));
        for (int i=0;i<futures.size();++i) {
            futures[i].waitForFinished(); QCOMPARE(futures[i].result(),i%2 ? expected : expectedOther);
        }
        QCOMPARE(cache.snapshot().value("entries").toInt(),2);
        const auto cached=cache.render(source,plan);
        QCOMPARE(cache.render(source,plan).cacheKey(),cached.cacheKey());
        QCOMPARE(cache.snapshot().value("charged_bytes").toInteger(),qint64(source.sizeInBytes())*2);
    }
    void fullScopesCacheRetainsLruCountsAndBypassesOversizeLuts() {
        ScopeRequest request; request.image=fixture(32,16); request.plan=nonRawLinearPlan(); request.fullResolution=true;
        FullScopesCache probe; probe.analyze(request);
        const auto cost=probe.snapshot().value("charged_bytes").toInteger(); QVERIFY(cost>0);
        FullScopesCache cache(cost*2);
        const auto a=cache.analyze(request); const auto planA=request.plan;
        AdjustmentState state; state.exposure=.2; request.plan=nonRawLinearPlan(state);
        const auto b=cache.analyze(request); const auto planB=request.plan;
        request.plan=planA; QCOMPARE(cache.analyze(request).red.constData(),a.red.constData());
        state.exposure=.4; request.plan=nonRawLinearPlan(state); cache.analyze(request);
        QCOMPARE(cache.snapshot().value("entries").toInt(),2); QCOMPARE(cache.snapshot().value("evictions").toInteger(),1);
        request.plan=planA; QCOMPARE(cache.analyze(request).red.constData(),a.red.constData());
        request.plan=planB; const auto newB=cache.analyze(request);
        QCOMPARE(newB.red,b.red); QVERIFY(newB.red.constData()!=b.red.constData());
        state.look.mode="calibrated"; state.look.lut=LookLut::identity(33); request.plan=nonRawLinearPlan(state);
        const auto oversize=cache.analyze(request);
        QCOMPARE(oversize.red,ScopesEngine::analyzeFull(request.image,request.plan).red);
        QCOMPARE(cache.snapshot().value("bypasses").toInteger(),1);
        QVERIFY(cache.snapshot().value("charged_bytes").toInteger()<=cost*2);
        FullScopesCache disabled(0); QCOMPARE(disabled.analyze(request).red,oversize.red);
        QCOMPARE(disabled.snapshot().value("entries").toInt(),0);
    }
    void fullScopesCacheDoesNotStoreCancelledCounts() {
        FullScopesCache cache;
        ScopeRequest request; request.image=fixture(16,8); request.plan=nonRawLinearPlan(); request.fullResolution=true;
        auto token=std::make_shared<std::atomic_bool>(true);
        QCOMPARE(cache.analyze(request,token).pixelCount,quint64(0));
        const auto complete=cache.analyze(request); QVERIFY(complete.pixelCount>0);
        QCOMPARE(cache.analyze(request,token).pixelCount,quint64(0));
        QCOMPARE(cache.analyze(request).red.constData(),complete.red.constData());
        request.image=fixture(2048,1536);
        AdjustmentState state; state.hue=13; state.saturation=27; state.hslHue[0]=19; request.plan=nonRawLinearPlan(state);
        token->store(false); const auto misses=cache.snapshot().value("misses").toInteger();
        auto future=QtConcurrent::run([&] { return cache.analyze(request,token); });
        QElapsedTimer wait; wait.start();
        while (cache.snapshot().value("misses").toInteger()==misses && wait.elapsed()<5000) QTest::qWait(1);
        const bool started=cache.snapshot().value("misses").toInteger()>misses;
        token->store(true); future.waitForFinished(); QVERIFY(started); QCOMPARE(future.result().pixelCount,quint64(0));
        QCOMPARE(cache.snapshot().value("entries").toInt(),1);
        token->store(false); const auto retry=cache.analyze(request,token);
        QCOMPARE(retry.pixelCount,quint64(2048*1536)); QCOMPARE(cache.snapshot().value("misses").toInteger(),misses+2);
        QCOMPARE(cache.analyze(request).red.constData(),retry.red.constData());
        QCOMPARE(cache.snapshot().value("cancelled_requests").toInteger(),3);
    }
    void databaseBatchPersistsFinalSnapshot() {
        QTemporaryDir dir;ProjectDatabase store;QVERIFY(store.create(dir.path(),"test"));
        QHash<QString,AdjustmentState> states;
        for(int i=0;i<100;++i) { AdjustmentState state;state.exposure=double(i)/100;states.insert(QString::number(i)+".dng",state); }
        QVERIFY(store.updateBatch(states));QVERIFY(store.flush());
        const QString connection="test-read";
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(store.projectPath()+"/Project.db");QVERIFY(db.open());
            QSqlQuery q(db);QVERIFY(q.exec("SELECT count(*) FROM photos"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),100);
            QVERIFY(q.exec("SELECT adjustment_json FROM photos WHERE path='99.dng'"));QVERIFY(q.next());
            QCOMPARE(QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object().value("exposure").toDouble(),.99);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void fullScopesMatchesFullRender() {
        auto image=fixture(257,273);AdjustmentState state;state.saturation=20;state.exposure=.3;
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        const auto full=ScopesEngine::analyzeFull(image,plan);
        const auto reference=ScopesEngine::analyze(ImagePipeline::processWithPlan(image,plan));
        QCOMPARE(full.red,reference.red);QCOMPARE(full.green,reference.green);QCOMPARE(full.blue,reference.blue);QCOMPARE(full.luma,reference.luma);
        QCOMPARE(full.pixelCount,quint64(image.width())*image.height());
    }
    void streamingExportHasProfileAndAtomicCancellation() {
        QTemporaryDir dir;auto image=fixture(128,160);QString error;
        const auto p3=ColorManagement::OutputSpace::DisplayP3;
        QVERIFY2(exportJpegTiled(image,{},dir.filePath("p3.jpg"),p3,95,{},&error),qPrintable(error));
        QImageReader reader(dir.filePath("p3.jpg"));auto output=reader.read();QVERIFY(!output.isNull());
        QCOMPARE(output.size(),image.size());QCOMPARE(output.colorSpace(),ColorManagement::colorSpace(p3));
        auto token=std::make_shared<std::atomic_bool>(false);
        const auto path=dir.filePath("cancelled.jpg");
        QVERIFY(!exportJpegTiled(image,{},path,p3,90,token,&error,[&](int){ token->store(true); }));
        QVERIFY(!QFile::exists(path));
    }

    void cacheHeaderTamperingIsRejected() {
        QTemporaryDir dir; SourceCache cache(1024*1024,dir.path());
        SourceData source;source.image=fixture(128,80);source.key=QString(64,'c');source.fullResolution=true;
        cache.storeDiskPreview(source);
        QFile file(dir.filePath(source.key+".jlpv"));QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek(4));QDataStream data(&file);data<<quint32(80)<<quint32(128);file.close();
        QVERIFY(cache.diskPreview(source.key).image.isNull());
    }
    void controllerLatestSelectionAndExportSnapshot() {
        QTemporaryDir dir;ProcessedImageProvider provider;PhotoController controller(&provider);
        controller.setGpuEnabled(false);
        const auto redPath=dir.filePath("red.jpg"),bluePath=dir.filePath("blue.jpg");
        QImage red(96,80,QImage::Format_RGB32);red.fill(QColor(160,20,20));QVERIFY(red.save(redPath));
        QImage blue(96,80,QImage::Format_RGB32);blue.fill(QColor(20,20,160));QVERIFY(blue.save(bluePath));
        controller.importFile(QUrl::fromLocalFile(redPath));controller.importFile(QUrl::fromLocalFile(bluePath));
        for(int i=0;i<40;++i) {controller.selectPhoto(i%2);controller.setExposure((i%7)/10.0);}
        controller.selectPhoto(1);controller.setExposure(.25);controller.finishInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady()&&!controller.loading()&&!controller.rendering(),10000);
        QCOMPARE(controller.currentFile(),bluePath);QCOMPARE(controller.exposure(),.25);
        QSize size;auto result=provider.requestImage("current",&size,{});
        QVERIFY(!result.isNull());QVERIFY(result.pixelColor(0,0).blue()>result.pixelColor(0,0).red());
        QVERIFY(!controller.exportCurrent(QUrl::fromLocalFile(redPath)));
        QVERIFY(!controller.exportCurrent(QUrl::fromLocalFile(bluePath)));
        QSignalSpy exported(&controller,&PhotoController::exportFinished);
        const auto destination=dir.filePath("snapshot.jpg");
        QVERIFY(controller.exportCurrent(QUrl::fromLocalFile(destination),"display-p3",95));
        controller.selectPhoto(0);controller.setExposure(-4);controller.finishInteraction();
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(),1,10000);
        QCOMPARE(exported.first().at(0).toInt(),1);QCOMPARE(exported.first().at(1).toInt(),0);
        QImageReader reader(destination);auto exportedImage=reader.read();QVERIFY(!exportedImage.isNull());
        QVERIFY(exportedImage.pixelColor(0,0).blue()>exportedImage.pixelColor(0,0).red());
        QCOMPARE(exportedImage.colorSpace(),ColorManagement::colorSpace(ColorManagement::OutputSpace::DisplayP3));
        QVERIFY(QImage(redPath).pixelColor(0,0).red()>100);
    }
    void loggingFlushRetainsLastRecord() {
        QStandardPaths::setTestModeEnabled(true);
        LoggingEngine::install();
        for(int i=0;i<100;++i) qInfo()<<"performance-test-log"<<i;
        qInfo()<<"final-record-668342";
        QVERIFY(LoggingEngine::flush());
        QFile file(LoggingEngine::currentLogPath());QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("final-record-668342"));
        LoggingEngine::shutdown();
    }

    void wideGamutExportDoesNotPassThroughSrgb() {
        QImage image(1,1,QImage::Format_RGBA64); image.fill(QColor::fromRgbF(.94,.12,.12));
        image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
        auto linear=image.convertedToColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear),QImage::Format_RGBA64);
        const auto direct=ImagePipeline::processWithPlan(linear,nonRawLinearPlan({},ColorManagement::OutputSpace::DisplayP3));
        const auto limitedSrgb=ImagePipeline::processWithPlan(linear,nonRawLinearPlan());
        const auto limited=ColorManagement::convertFromSrgb(limitedSrgb,ColorManagement::OutputSpace::DisplayP3);
        QVERIFY(std::abs(direct.pixelColor(0,0).greenF()-limited.pixelColor(0,0).greenF())>.02);
    }
};
QTEST_MAIN(PerformanceTests)
#include "PerformanceTests.moc"
