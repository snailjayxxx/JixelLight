#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QtConcurrent/QtConcurrentRun>
#include <cstring>
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/export/RasterExporter.h"
#include "core/scopes/ScopesEngine.h"
#include "diagnostics/PerformanceRecorder.h"
#include "brush_tile_oracle/BrushTileOriginalPipeline.h"
#define BrushMaskTileRaster OriginalBrushTileRaster
#include "brush_tile_oracle/BrushMaskTileRasterOriginal.h"
#undef BrushMaskTileRaster

namespace {
namespace R=BrushMaskTileRaster;
namespace O=OriginalBrushTileRaster;
using namespace BrushMaskGeometry;
QByteArray pixels(const QImage &im) {
    QByteArray bytes;
    for(int y=0;y<im.height();++y)
        bytes.append(reinterpret_cast<const char*>(im.constScanLine(y)),im.width()*im.depth()/8);
    return bytes;
}
QByteArray hash(const QImage &im) {
    return QCryptographicHash::hash(pixels(im),QCryptographicHash::Sha256).toHex();
}
bool same(const std::vector<float>& a,const std::vector<float>& b) {
    return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0;
}
BrushMaskDocument::Document document(int count=1025) {
    Stroke paint{{.19,.83,.71},{{{.501,.497},.98}},StrokeMode::Paint};
    for(int i=1;i<count;++i)
        paint.dabs.push_back({{.501+i*9e-9,.497+7e-6*std::sin(i*.037)},.23+.08*std::sin(i*.049)});
    Stroke erase{{.09,.41,.47},{{{.49,.51},.73},{{.51,.49},.54}},StrokeMode::Erase};
    Stroke last{{.035,.21,.67},{{{.503,.493},.64},{{.498,.502},.81}},StrokeMode::Paint};
    return {{{"ordered",true,.87,{paint,erase,last}},
             {"disabled",false,2.1,{paint}},
             {"negative",true,-.31,{last}}}};
}
QImage scene(bool floating,int w=313,int h=271) {
    QImage out(w,h,floating?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
    out.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        if(floating) {
            auto *p=reinterpret_cast<float*>(out.scanLine(y))+4*x;
            p[0]=float(x%41)/17.f-.17f;p[1]=float(y%47)/23.f;
            p[2]=float((x+3*y)%53)/31.f-.09f;p[3]=(x+2*y)%11?.67f:0.f;
        } else {
            reinterpret_cast<QRgba64*>(out.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*419+y*17)%65536,(y*431+x*73)%65536,(x*179+y*311)%65536,
                (x+2*y)%11?quint16(41729):quint16(0));
        }
    }
    return out;
}
AdjustmentState edits(int count=1025) {
    AdjustmentState state;state.exposure=-.42;state.saturation=7;
    state.brushMasks=document(count);return state;
}
QImage original(const QImage &input,const AdjustmentState &state,const BrushRenderRequest::Spec &spec) {
    auto plan=ProcessingPlan::compile(state,spec.encoding,spec.output,spec.rawSource,spec.rawBaseExposureStops);
    plan.setFrameRect(spec.normalizedFrame);
    auto out=BrushTileOriginalPipeline::processWithPlan(input,plan,{},false);
    return spec.region.isNull()?out:out.copy(spec.region);
}
}
class BrushTileLowerBoundIntegrationTests final:public QObject {
    Q_OBJECT
    QJsonArray rows;
    void record(QJsonObject row) {
        row["method"]=QString::fromLatin1(QTest::currentTestFunction());
        rows.append(row);
        qInfo().noquote()<<QJsonDocument(row).toJson(QJsonDocument::Compact);
    }
private slots:
    void initTestCase() {
        const auto path=qEnvironmentVariable("JIXEL_BRUSH_TILE_EVIDENCE_PATH");
        QVERIFY(!path.isEmpty());QVERIFY(!QFile::exists(path));
    }
    void indexedPlanPreservesOrderedPaintEraseWeightsAndOriginalVisits() {
        auto doc=document(2049);R::FramePlan plan;O::FramePlan old;
        QVERIFY(R::planFrame(doc,289,273,{0,0,289,273},&plan));
        QVERIFY(O::planFrame(doc,289,273,{0,0,289,273},&old));
        QCOMPARE(plan.candidateVisits,old.candidateVisits);
        quint64 skipped=0,refreshes=0,checked=0;
        for(std::size_t i=0;i<plan.tiles.size();++i) {
            R::ExposureOutput actual;O::ExposureOutput expected;R::CpuTileExecutionStats stats;
            QVERIFY(R::exposureOnly(plan,i,&actual,nullptr,&stats));
            QVERIFY(O::exposureOnly(old,i,&expected));
            QVERIFY(same(actual.weightedExposureEv,expected.weightedExposureEv));
            QCOMPARE(actual.visitedDabPixels,expected.visitedDabPixels);
            QCOMPARE(stats.candidateVisits,actual.visitedDabPixels);
            skipped+=stats.skippedScalarComparisons;refreshes+=stats.refreshedBlocks;
            checked+=actual.weightedExposureEv.size();
        }
        QVERIFY(skipped>0);
        record({{"tiles",int(plan.tiles.size())},{"exact_float_weights",double(checked)},
                {"original_candidate_visits",double(plan.candidateVisits)},
                {"actual_skipped_comparisons",double(skipped)},{"actual_refreshed_blocks",double(refreshes)}});
    }
    void plannedFallbackAndChangedTransformsMatchOriginalPublicRaster() {
        int profiles=0;quint64 weights=0;
        for(const QSize size:{QSize(263,197),QSize(197,263),QSize(16379,1),QSize(1,16379)}) {
            for(const R::NormalizedFrame frame:{R::NormalizedFrame{},R::NormalizedFrame{.41,.43,.19,.17}}) {
                auto doc=document(67);R::FramePlan indexed,scan;O::FramePlan old;
                QVERIFY(R::planFrame(doc,size.width(),size.height(),{0,0,size.width(),size.height()},&indexed,nullptr,frame));
                QVERIFY(R::planFrame(doc,size.width(),size.height(),{0,0,size.width(),size.height()},&scan,nullptr,frame,0));
                QVERIFY(O::planFrame(doc,size.width(),size.height(),{0,0,size.width(),size.height()},&old,nullptr,
                                    {frame.x,frame.y,frame.width,frame.height}));
                QCOMPARE(indexed.candidateVisits,old.candidateVisits);
                for(std::size_t i=0;i<indexed.tiles.size();++i) {
                    R::ExposureOutput a,b;O::ExposureOutput c;R::CpuTileExecutionStats stats;
                    QVERIFY(R::exposureOnly(indexed,i,&a));QVERIFY(R::exposureOnly(scan,i,&b,nullptr,&stats));
                    QVERIFY(O::exposureOnly(old,i,&c));
                    QVERIFY(same(a.weightedExposureEv,c.weightedExposureEv));QVERIFY(same(b.weightedExposureEv,c.weightedExposureEv));
                    QCOMPARE(a.visitedDabPixels,c.visitedDabPixels);QCOMPARE(b.visitedDabPixels,c.visitedDabPixels);
                    QVERIFY(!stats.indexed);QCOMPARE(stats.skippedScalarComparisons,std::size_t(0));
                    weights+=c.weightedExposureEv.size();
                }
                ++profiles;
            }
        }
        record({{"profiles",profiles},{"exact_float_weights_per_route",double(weights)},{"scan_oracle_unchanged",true}});
    }
    void cancelForgedSummaryAndWorkRefusalPreserveOutputsAndStatistics() {
        auto doc=document(33);R::FramePlan plan;QVERIFY(R::planFrame(doc,127,127,{0,0,127,127},&plan));
        R::ExposureOutput out{{.125f,.75f},912};const auto saved=out;
        R::CpuTileExecutionStats stats;stats.candidateVisits=713;stats.skippedScalarComparisons=919;
        std::atomic_bool stop{true};QVERIFY(!R::exposureOnly(plan,0,&out,&stop,&stats));
        QVERIFY(same(out.weightedExposureEv,saved.weightedExposureEv));QCOMPARE(out.visitedDabPixels,saved.visitedDabPixels);
        QCOMPARE(stats.candidateVisits,std::size_t(713));QCOMPARE(stats.skippedScalarComparisons,std::size_t(919));
        auto forged=plan;++forged.tiles[0].candidateVisits;QVERIFY(!R::exposureOnly(forged,0,&out,nullptr,&stats));
        QVERIFY(same(out.weightedExposureEv,saved.weightedExposureEv));QCOMPARE(stats.candidateVisits,std::size_t(713));
        Stroke dense{{.5,1,1},{},StrokeMode::Paint};
        const std::size_t perDab=127*127;const int accepted=int(R::MaxDabPixelVisits/perDab);
        dense.dabs.assign(std::size_t(accepted),{{.5,.5},1});
        BrushMaskDocument::Document boundary{{{"limit",true,.5,{dense}}}};
        R::FramePlan a;O::FramePlan b;QVERIFY(R::planFrame(boundary,127,127,{0,0,127,127},&a));
        QVERIFY(O::planFrame(boundary,127,127,{0,0,127,127},&b));QCOMPARE(a.candidateVisits,b.candidateVisits);
        R::ExposureOutput fresh;R::CpuTileExecutionStats measured;QVERIFY(R::exposureOnly(a,0,&fresh,nullptr,&measured));
        QCOMPARE(fresh.visitedDabPixels,std::size_t(accepted)*perDab);
        boundary.layers[0].strokes[0].dabs.push_back({{.5,.5},1});
        QVERIFY(!R::planFrame(boundary,127,127,{0,0,127,127},&a));
        QVERIFY(!O::planFrame(boundary,127,127,{0,0,127,127},&b));
        record({{"unchanged_8Mi_visits",double(R::MaxDabPixelVisits)},{"accepted_boundary_dabs",accepted},
                {"over_limit_rejected",true},{"cancel_and_forgery_outputs_preserved",true}});
    }
    void immutablePlanRunsWithConcurrentIndependentStrokeSummaries() {
        auto doc=document(513);R::FramePlan plan;O::FramePlan old;
        QVERIFY(R::planFrame(doc,261,259,{0,0,261,259},&plan));
        QVERIFY(O::planFrame(doc,261,259,{0,0,261,259},&old));
        doc.layers[0].strokes.clear();doc.layers.clear();
        std::vector<QFuture<bool>> jobs;
        for(std::size_t t=0;t<4;++t) jobs.push_back(QtConcurrent::run([plan,old,t] {
            R::ExposureOutput a;O::ExposureOutput b;R::CpuTileExecutionStats stats;
            return R::exposureOnly(plan,t,&a,nullptr,&stats)&&O::exposureOnly(old,t,&b)
                &&a.visitedDabPixels==b.visitedDabPixels&&same(a.weightedExposureEv,b.weightedExposureEv);
        }));
        for(auto &job:jobs){job.waitForFinished();QVERIFY(job.result());}
        record({{"concurrent_CPU_tasks",4},{"original_document_destroyed_after_snapshot",true},{"matching_complete_tiles",4}});
    }
    void admittedWholeStripesAndSignedFloatMatchExactOriginalPipeline() {
        int variants=0;quint64 pixelsChecked=0;const auto before=PerformanceRecorder::snapshot()["counters"].toObject();
        for(bool floating:{false,true})for(auto space:{ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3}) {
            auto input=scene(floating);const auto sourceHash=hash(input);const auto state=edits(1025);
            BrushRenderRequest::Spec spec;spec.sourceStage=BrushRenderRequest::SourceStage::Prepared;
            spec.encoding=ImagePipeline::InputEncoding::LinearProPhoto;spec.output=space;spec.parallel=true;
            const auto expected=original(input,state,spec);QVERIFY(!expected.isNull());QString error;
            auto budget=std::make_shared<BrushRenderBudget>();
            auto request=BrushRenderRequest::create(input,state,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));
            auto output=request->render({},&error);QVERIFY2(output,qPrintable(error));QCOMPARE(pixels(output.image()),pixels(expected));
            output={};request.reset();QCOMPARE(budget->snapshot().charged,0);
            spec.layout=BrushRenderRequest::Layout::Stripes128;
            auto stream=BrushRenderRequest::create(input,state,spec,{},&error,budget);QVERIFY2(stream,qPrintable(error));
            QImage joined(expected.size(),QImage::Format_RGBA64);int rowsDone=0;
            QVERIFY2(stream->visitStrips([&](const BrushImageLease& strip,int y) {
                if(y!=rowsDone)return false;
                for(int row=0;row<strip.image().height();++row)
                    std::memcpy(joined.scanLine(y+row),strip.image().constScanLine(row),std::size_t(joined.width())*8);
                rowsDone+=strip.image().height();return true;
            },{},&error),qPrintable(error));
            QCOMPARE(rowsDone,expected.height());QCOMPARE(pixels(joined),pixels(expected));stream.reset();
            QCOMPARE(budget->snapshot().charged,0);QCOMPARE(hash(input),sourceHash);pixelsChecked+=quint64(input.width())*input.height()*2;++variants;
        }
        const auto after=PerformanceRecorder::snapshot()["counters"].toObject();
        const double helpers=after["brush_research_helper_tasks"].toDouble()-before["brush_research_helper_tasks"].toDouble();
        QVERIFY(helpers>0);
        record({{"variants",variants},{"exact_RGBA64_pixels",double(pixelsChecked)},{"actual_helper_tasks",helpers},
                {"signed_FP32_sources",2},{"camera_RAW_decode_claim",false}});
    }
    void extraScratchChargeRefusesBeforeFreezeAndReleasesOutputAliases() {
        auto source=scene(false,97,89);const auto state=edits(17);BrushRenderRequest::Spec spec;
        const qint64 p=qint64(97)*89;
        const qint64 expected=48*p+64LL*1024*1024+32LL*384*1024+32LL*qint64(R::MaxCpuTileScratchExtraBytes);
        const auto originalHash=hash(source);QString error;BrushAdmissionFailure failure;
        auto shortBudget=std::make_shared<BrushRenderBudget>(expected-1);
        QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&error,shortBudget,&failure));
        QCOMPARE(failure,BrushAdmissionFailure::BudgetUnavailable);QCOMPARE(shortBudget->snapshot().charged,0);QCOMPARE(shortBudget->snapshot().peak,0);
        auto budget=std::make_shared<BrushRenderBudget>(expected);
        auto request=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));
        QCOMPARE(request->reservedBytes(),expected);QCOMPARE(budget->snapshot().charged,expected);
        auto output=request->render({},&error);QVERIFY2(output,qPrintable(error));
        auto alias=output.sharedImage();const auto retained=alias.sizeInBytes();request.reset();output={};
        QCOMPARE(budget->snapshot().charged,retained);alias={};QCOMPARE(budget->snapshot().charged,0);
        QCOMPARE(hash(source),originalHash);QCOMPARE(BrushRenderBudget::DefaultBytes,3LL*1024*1024*1024);
        record({{"summary_bytes_per_worker",double(R::MaxCpuTileSummaryBytes)},
                {"extra_scratch_bound_per_worker",double(R::MaxCpuTileScratchExtraBytes)},
                {"extra_reservation_for_32_workers",double(32*R::MaxCpuTileScratchExtraBytes)},
                {"one_byte_short_refuses_without_charge",true},{"all_leases_released",true}});
    }
    void newUniquePacketSamplerIntentAndExplicitRecoveryReachOriginalPixels() {
        StrokeBuilder builder(307,211,{.006,.43,.72});
        for(int i=0;i<=7000;++i)QVERIFY(builder.append({{.47+i*1e-9,.53+1e-7*std::sin(i*.021)},i%2?.35:.8}));
        const Dab last{{.470007001,.53},.12};QVERIFY(builder.append(last));const auto stroke=builder.finish();QVERIFY(stroke);
        QCOMPARE(stroke->dabs.back().pressure,last.pressure);QCOMPARE(stroke->dabs.back().center.x,last.center.x);
        BrushMaskDocument::Document doc{{{"packets",true,.63,{*stroke}}}};QJsonObject encoded;
        QVERIFY(BrushMaskDocument::toJson(doc,&encoded));BrushMaskDocument::Document restored;
        QVERIFY(BrushMaskDocument::fromJson(encoded,&restored));
        R::FramePlan plan;O::FramePlan old;QVERIFY(R::planFrame(restored,307,211,{0,0,307,211},&plan));
        QVERIFY(O::planFrame(restored,307,211,{0,0,307,211},&old));
        for(std::size_t i=0;i<plan.tiles.size();++i){R::ExposureOutput a;O::ExposureOutput b;
            QVERIFY(R::exposureOnly(plan,i,&a));QVERIFY(O::exposureOnly(old,i,&b));QVERIFY(same(a.weightedExposureEv,b.weightedExposureEv));}
        StrokeBuilder recovery(307,211,{.006,.43,.72});QVERIFY(recovery.setRecoveryLimit(257));
        StrokeBuilder::RecoveryResult end=StrokeBuilder::RecoveryResult::Appended;int consumed=0;
        for(int i=0;i<9013&&end==StrokeBuilder::RecoveryResult::Appended;++i) {
            end=recovery.appendForRecovery({{.47+i*1e-9,.53},i%2?.35:.8});++consumed;
        }
        QCOMPARE(end,StrokeBuilder::RecoveryResult::Capacity);
        const auto explicitPrefix=recovery.finish();QVERIFY(explicitPrefix);QVERIFY(explicitPrefix->dabs.size()<=257);
        QVERIFY(!recovery.append(last));
        record({{"new_unique_input_packets",7002},{"original_sampler_dabs",int(stroke->dabs.size())},
                {"roundtrip_original_weights",true},{"capacity_refusal_at_packet",consumed},
                {"explicit_recovery_dabs",int(explicitPrefix->dabs.size())},{"GUI_or_physical_pressure_claim",false}});
    }
    void actualAdmittedHistogramPlotsAndPng16MatchOriginalFullPixels() {
        const auto input=scene(false,277,267);const auto sourceHash=hash(input);const auto state=edits(257);
        BrushRenderRequest::Spec spec;spec.encoding=ImagePipeline::InputEncoding::LinearProPhoto;spec.output=ColorManagement::OutputSpace::SRgb;
        const auto expected=original(input,state,spec);QVERIFY(!expected.isNull());QString error;
        spec.layout=BrushRenderRequest::Layout::Stripes128;
        auto request=BrushRenderRequest::create(input,state,spec,{},&error);QVERIFY2(request,qPrintable(error));
        std::array<ScopePlotResult,3> plots;const auto actual=ScopesEngine::analyzeAdmitted(*request,{},&error,&plots);
        QVERIFY2(actual.error.isEmpty(),qPrintable(actual.error));const auto baseline=ScopesEngine::analyze(expected);
        QCOMPARE(actual.pixelCount,baseline.pixelCount);QCOMPARE(actual.red,baseline.red);QCOMPARE(actual.green,baseline.green);
        QCOMPARE(actual.blue,baseline.blue);QCOMPARE(actual.luma,baseline.luma);
        QCOMPARE(actual.shadowClipPercent,baseline.shadowClipPercent);
        QCOMPARE(actual.highlightClipPercent,baseline.highlightClipPercent);
        const auto expectedPlots=renderEncodedScopePlots(expected);
        for(int i=0;i<3;++i){QVERIFY(plots[i].error.isEmpty());QCOMPARE(plots[i].pixels,expectedPlots[i].pixels);
            QCOMPARE(pixels(plots[i].image),pixels(expectedPlots[i].image));}
        const auto dir=QFileInfo(qEnvironmentVariable("JIXEL_BRUSH_TILE_EVIDENCE_PATH")).absolutePath();
        const auto path=dir+"/brush-tile-lowerbound-export.png";QVERIFY(!QFile::exists(path));spec.layout=BrushRenderRequest::Layout::Whole;
        auto exporting=BrushRenderRequest::create(input,state,spec,{},&error);QVERIFY2(exporting,qPrintable(error));
        QVERIFY2(exportRaster(*exporting,path,RasterFormat::Png16,100,{},&error),qPrintable(error));
        const QImage decoded(path);QVERIFY(!decoded.isNull());QCOMPARE(pixels(decoded.convertToFormat(QImage::Format_RGBA64)),pixels(expected));
        QCOMPARE(decoded.colorSpace(),expected.colorSpace());QCOMPARE(hash(input),sourceHash);
        QFile png(path);QVERIFY(png.open(QIODevice::ReadOnly));
        record({{"actual_full_pixels",double(actual.pixelCount)},{"histogram_channels",4},{"bins_per_channel",1024},
                {"exact_full_scope_plots",3},{"PNG16_path",path},
                {"PNG16_sha256",QString::fromLatin1(QCryptographicHash::hash(png.readAll(),QCryptographicHash::Sha256).toHex())},
                {"expected_pixel_sha256",QString::fromLatin1(hash(expected))},{"real_RAW_Sony_or_GPU_claim",false}});
    }
    void cleanupTestCase() {
        QCOMPARE(rows.size(),8);const auto path=qEnvironmentVariable("JIXEL_BRUSH_TILE_EVIDENCE_PATH");QVERIFY(!QFile::exists(path));
        QJsonObject data{{"schema",1},{"qa_mode","planned_cpu_tile_lowerbounds"},
            {"methods",rows},{"compiled_commit",QString::fromLatin1(JIXELLIGHT_GIT_COMMIT)},
            {"production_or_RAW_GUI_GPU_full_jitter_accepted",false}};
        QSaveFile file(path);file.setDirectWriteFallback(false);QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes=QJsonDocument(data).toJson(QJsonDocument::Indented);
        QCOMPARE(file.write(bytes),qint64(bytes.size()));QVERIFY(file.commit());
    }
};
QTEST_GUILESS_MAIN(BrushTileLowerBoundIntegrationTests)
#include "BrushTileLowerBoundTests.moc"
