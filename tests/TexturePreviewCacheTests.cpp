#include <QtTest>
#include <QtConcurrent>
#include <QElapsedTimer>
#include <random>
#include <QSemaphore>
#include <QScopeGuard>
#include "tests/PreviewStageProgressProbe.h"
#include "core/cache/RenderedPreviewCache.h"
#include "core/pipeline/StageGraph.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "diagnostics/PerformanceRecorder.h"

class TexturePreviewCacheTests:public QObject {
    Q_OBJECT
    static QImage scene(QSize size={137,139},bool floating=true) {
        QImage result(size,QImage::Format_RGBA32FPx4);std::mt19937 rng(29174);std::normal_distribution<float> noise(0,.008f);const float alpha[]{0,1.e-8f,.375f,1.f};
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){auto *p=reinterpret_cast<float *>(result.scanLine(y))+4*x;p[0]=.15f+.07f*x/size.width()+noise(rng);p[1]=.12f+noise(rng);p[2]=.18f+noise(rng);p[3]=alpha[(x+y)%4];}
        return floating?result:ImagePipeline::rgba64Source(result);
    }
    static ProcessingPlan plan(AdjustmentState state={},ImagePipeline::InputEncoding encoding=ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb) {
        state.textureAmount=state.textureAmount?state.textureAmount:73;
        return ProcessingPlan::compile(state,encoding,output,false,0);
    }
    static qint64 counter(const char *name) {return PerformanceRecorder::snapshot()["counters"].toObject()[name].toInteger();}
private slots:
    void matchesReferenceAcrossEncodingsSpacesAndDownstreamEdits() {
        for(bool floating : {false,true})for(int encoding=0;encoding<2;++encoding) {
            const auto source=scene({137,139},floating),original=source.copy();RenderedPreviewCache cache;
            auto p=plan({},ImagePipeline::InputEncoding(encoding));QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));
            for(int space=0;space<4;++space) {
                AdjustmentState edits;edits.exposure=.2+.1*space;edits.temperature=17;edits.tint=-13;edits.saturation=12;
                edits.grainAmount=31;edits.grainSize=3.3;edits.sharpenAmount=61;edits.sharpenRadius=2.7;edits.look.mode="manual";edits.look.code="ST";edits.look.parameters={{"clarity",3},{"sharpness",4}};
                p=plan(edits,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(space));QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));if(QTest::currentTestFailed())return;
            }
            QCOMPARE(cache.snapshot()["texture_stage_misses"].toInteger(),1);QCOMPARE(cache.snapshot()["texture_stage_hits"].toInteger(),4);QCOMPARE(source,original);
            const auto hits=cache.snapshot()["texture_stage_hits"].toInteger();const auto retained=cache.render(source,p);QCOMPARE(cache.render(source,p).cacheKey(),retained.cacheKey());QCOMPARE(cache.snapshot()["texture_stage_hits"].toInteger(),hits);
            QCOMPARE(cache.snapshot()["budget_bytes"].toInteger(),64LL*1024*1024);QVERIFY(cache.snapshot()["charged_bytes"].toInteger()<=64LL*1024*1024);
        }
    }
    void invalidatesAmountsDensityEncodingInputRowsAndSource() {
        auto source=scene();RenderedPreviewCache cache;auto p=plan();
        const auto verify=[&]{const auto image=cache.render(source,p);QCOMPARE(image,ImagePipeline::processWithPlan(source,p));QVERIFY(!image.isNull());};
        verify();auto key=StageGraph::spatialPreviewKey(source,p);QVERIFY(!key.isEmpty());
        auto downstream=p;downstream.output=ColorManagement::OutputSpace::DisplayP3;downstream.rawSource=true;downstream.baseExposureStops=.5;
        QCOMPARE(StageGraph::spatialPreviewKey(source,downstream),key);
        p=plan({});p.state.textureAmount=-94;p=ProcessingPlan::compile(p.state,p.encoding,p.output,false,0);verify();
        p.setReferenceFrameSize(source.size()*4);verify();
        p.setFrameRect({.25,.125,.5,.5});verify();
        p=plan(p.state,ImagePipeline::InputEncoding::SRgb);verify();
        p.data[ProcessingPlan::Input0].x+=.005;verify();
        source=source.copy();verify();
        QCOMPARE(cache.snapshot()["texture_stage_misses"].toInteger(),7);QCOMPARE(cache.snapshot()["texture_stage_hits"].toInteger(),0);
        auto disabled=ProcessingPlan::compile({},p.encoding,p.output,false,0);QVERIFY(StageGraph::spatialPreviewKey(source,disabled).isEmpty());
    }
    void stagesAndResultsShareOriginalLruBudget() {
        const auto source=scene();const qint64 pixels=qint64(source.width())*source.height(),resultCost=(pixels*8+1023)/1024,stageCost=(pixels*16+1023)/1024;
        const qint64 bytes=(resultCost+stageCost)*1024;RenderedPreviewCache cache(bytes);auto p=plan();const auto old=cache.render(source,p);QVERIFY(!old.isNull());
        QCOMPARE(cache.snapshot()["entries"].toInt(),2);QCOMPARE(cache.snapshot()["charged_bytes"].toInteger(),bytes);
        p.state.exposure=.2;p=ProcessingPlan::compile(p.state,p.encoding,p.output,false,0);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));QCOMPARE(cache.snapshot()["texture_stage_hits"].toInteger(),1);QCOMPARE(cache.snapshot()["entries"].toInt(),2);
        const auto other=source.copy();QCOMPARE(cache.render(other,p),ImagePipeline::processWithPlan(other,p));QVERIFY(cache.snapshot()["evictions"].toInteger()>=3);QCOMPARE(cache.snapshot()["charged_bytes"].toInteger(),bytes);QCOMPARE(old,ImagePipeline::processWithPlan(source,plan()));
        RenderedPreviewCache small(resultCost*1024);QCOMPARE(small.render(source,p),ImagePipeline::processWithPlan(source,p));QCOMPARE(small.snapshot()["texture_stage_bypasses"].toInteger(),1);QCOMPARE(small.snapshot()["texture_stage_misses"].toInteger(),0);QCOMPARE(small.snapshot()["entries"].toInt(),1);
        RenderedPreviewCache disabled(0);QCOMPARE(disabled.render(source,p),ImagePipeline::processWithPlan(source,p));QCOMPARE(disabled.snapshot()["entries"].toInt(),0);QCOMPARE(disabled.snapshot()["texture_stage_bypasses"].toInteger(),1);
    }
    void combinedDenoiseAndTextureDependenciesReuseOnlyCompleteStages() {
        const auto source=scene();AdjustmentState state;state.textureAmount=73;state.clarityAmount=41;state.denoiseLuma=83;state.denoiseChroma=91;
        RenderedPreviewCache cache;auto p=plan(state);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));
        state.exposure=.3;state.temperature=17;p=plan(state);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));
        QCOMPARE(cache.snapshot()["texture_stage_hits"].toInteger(),1);QCOMPARE(cache.snapshot()["denoise_stage_hits"].toInteger(),1);
        state.denoiseChroma=19;p=plan(state);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));
        state.textureAmount=-73;p=plan(state);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));
        QCOMPARE(cache.snapshot()["texture_stage_misses"].toInteger(),3);QCOMPARE(cache.snapshot()["denoise_stage_misses"].toInteger(),3);
        state.clarityAmount=-41;p=plan(state);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));
        QCOMPARE(cache.snapshot()["texture_stage_misses"].toInteger(),4);
        state.sharpenAmount=61;state.grainAmount=19;p=plan(state);QCOMPARE(cache.render(source,p),ImagePipeline::processWithPlan(source,p));
        QCOMPARE(cache.snapshot()["texture_stage_hits"].toInteger(),2);QCOMPARE(cache.snapshot()["denoise_stage_hits"].toInteger(),2);
    }
    void brushesKeepAdmissionAndDoNotRetainUnchargedStages() {
        const auto source=scene({137,279});auto p=plan();BrushMaskGeometry::Stroke paint;paint.style={.1,.4,.8};paint.dabs={{{.2,.3},.6},{{.7,.6},.9}};auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.2,.3},.4}};
        auto edits=p.state;edits.brushMasks.layers={{"cache-bypass",true,.85,{paint,erase}}};p=ProcessingPlan::compile(edits,p.encoding,p.output,false,0);auto budget=std::make_shared<BrushRenderBudget>();
        {
            RenderedPreviewCache cache(4*1024*1024,budget);const auto before=counter("cpu_preview_texture_builds");QString error;BrushAdmissionFailure failure;
            const auto image=cache.render(source,p,{},&error,&failure);QVERIFY2(!image.isNull(),qPrintable(error));QCOMPARE(failure,BrushAdmissionFailure::None);QCOMPARE(image,ImagePipeline::processWithPlan(source,p));
            QCOMPARE(counter("cpu_preview_texture_builds"),before);QCOMPARE(cache.snapshot()["texture_stage_bypasses"].toInteger(),1);QCOMPARE(cache.snapshot()["entries"].toInt(),1);QCOMPARE(budget->snapshot().charged,image.sizeInBytes());
            QCOMPARE(cache.render(source,p).cacheKey(),image.cacheKey());QCOMPARE(cache.snapshot()["texture_stage_hits"].toInteger(),0);
        }
        QCOMPARE(budget->snapshot().charged,0);
        RenderedPreviewCache denied(4*1024*1024,std::make_shared<BrushRenderBudget>(1));QString error;BrushAdmissionFailure failure;
        QVERIFY(denied.render(source,p,{},&error,&failure).isNull());QCOMPARE(failure,BrushAdmissionFailure::BudgetUnavailable);QCOMPARE(denied.snapshot()["entries"].toInt(),0);
    }
    void cancelledAndInvalidRequestsPublishNoPartialStage() {
        RenderedPreviewCache cache;auto source=scene({1024,768});auto p=plan();auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(cache.render(source,p,token).isNull());QCOMPARE(cache.snapshot()["entries"].toInt(),0);
        token->store(false);QThreadPool pool;pool.setMaxThreadCount(1);
        QSemaphore completed,resume;std::atomic_bool completeFrame{false};
        auto future=QtConcurrent::run(&pool,[&]{
            PreviewStageProgressProbe::completed=[&](const QImage &image){
                completeFrame.store(image.size()==source.size()&&image.format()==QImage::Format_RGBA32FPx4);
                completed.release();resume.acquire();
            };
            auto clear=qScopeGuard([]{PreviewStageProgressProbe::completed={};});
            return cache.render(source,p,token);
        });
        const bool observed=completed.tryAcquire(1,5000),pending=!future.isFinished();
        token->store(true);resume.release();future.waitForFinished();
        QVERIFY(observed);QVERIFY(pending);QVERIFY(completeFrame.load());QVERIFY(future.result().isNull());QCOMPARE(cache.snapshot()["entries"].toInt(),0);
        p.data[ProcessingPlan::Texture].z=.5;QVERIFY(cache.render(source,p).isNull());QCOMPARE(cache.snapshot()["entries"].toInt(),0);
        source=scene();p=plan();const auto complete=cache.render(source,p);QVERIFY(!complete.isNull());QCOMPARE(cache.snapshot()["entries"].toInt(),2);
        QVERIFY(cache.render(source,p,token).isNull());QCOMPARE(cache.snapshot()["entries"].toInt(),2);QCOMPARE(cache.render(source,p).cacheKey(),complete.cacheKey());
    }
    void concurrentMissesKeepOneImmutableStageAndResult() {
        RenderedPreviewCache cache(16*1024*1024);const auto source=scene({513,385}),original=source.copy();const auto p=plan();const auto expected=ImagePipeline::processWithPlan(source,p);QList<QFuture<QImage>> futures;
        for(int i=0;i<4;++i)futures.append(QtConcurrent::run([&]{return cache.render(source,p);}));
        for(auto &future:futures){future.waitForFinished();QCOMPARE(future.result(),expected);}
        QCOMPARE(cache.snapshot()["entries"].toInt(),2);QVERIFY(cache.snapshot()["charged_bytes"].toInteger()<=16LL*1024*1024);QCOMPARE(source,original);
    }
    void warmCpuColorEditsReuseOneSpatialBuild() {
        const auto source=scene({769,513});auto p=plan();RenderedPreviewCache cache;QElapsedTimer timer;timer.start();QVERIFY(!cache.render(source,p).isNull());const auto cold=timer.nsecsElapsed()/1.e6;
        const auto builds=counter("cpu_preview_texture_builds");double cached=0,reference=0;
        for(int i=0;i<3;++i) {
            auto state=p.state;state.exposure=.1*(i+1);state.temperature=12;p=ProcessingPlan::compile(state,p.encoding,p.output,false,0);
            timer.restart();const auto image=cache.render(source,p);cached+=timer.nsecsElapsed()/1.e6;timer.restart();const auto expected=ImagePipeline::processWithPlan(source,p);reference+=timer.nsecsElapsed()/1.e6;QCOMPARE(image,expected);
        }
        QCOMPARE(counter("cpu_preview_texture_builds"),builds);QCOMPARE(cache.snapshot()["texture_stage_misses"].toInteger(),1);QCOMPARE(cache.snapshot()["texture_stage_hits"].toInteger(),3);
        qInfo()<<"CPU texture stage reuse"<<source.size()<<"cold ms"<<cold<<"three warm color edits ms"<<cached<<"uncached reference edits ms"<<reference<<"charged bytes"<<cache.snapshot()["charged_bytes"];
    }
};
QTEST_GUILESS_MAIN(TexturePreviewCacheTests)
#include "TexturePreviewCacheTests.moc"
