#include "core/scopes/ScopesEngine.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/async/ParallelRows.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QRgba64>
#include <QColorSpace>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <atomic>
#include <limits>

void HistogramCounts::add(const HistogramCounts &other) {
    for (size_t i=0; i<bins.size(); ++i) bins[i]+=other.bins[i];
    shadows+=other.shadows; highlights+=other.highlights; pixels+=other.pixels;
}
namespace {
bool countLinearFrame(const QImage &image,HistogramCounts *output,const CancelToken &token) {
    if(!output||image.isNull()||cancelled(token)||image.format()!=QImage::Format_RGBA32FPx4
        ||image.colorSpace()!=QColorSpace(QColorSpace::SRgbLinear))return false;
    const int tasks=std::max(1,std::min(ParallelRows::threadBudget(),int((qint64(image.height())+63)/64)));
    std::vector<HistogramCounts> partials(std::size_t(tasks),HistogramCounts{});std::atomic_bool invalid{false};
    const auto rows=(qint64(image.height())+tasks-1)/tasks;
    const auto work=int(std::min(qint64(std::numeric_limits<int>::max()),qint64(image.width())*rows));
    ParallelRows::run(tasks,work,token,[&](int task) {
        auto &counts=partials[std::size_t(task)];
        const int first=int(qint64(task)*image.height()/tasks),last=int(qint64(task+1)*image.height()/tasks);
        for(int y=first;y<last&&!cancelled(token)&&!invalid.load(std::memory_order_relaxed);++y) {
            const auto *row=reinterpret_cast<const float *>(image.constScanLine(y));
            for(int x=0;x<image.width();++x) {
                const auto *p=row+qsizetype(4)*x;
                if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2])||!std::isfinite(p[3])||p[3]<0||p[3]>1){invalid.store(true,std::memory_order_relaxed);return;}
                const double r=p[0],g=p[1],b=p[2];const std::array<double,4> channels{r,g,b,(2126*r+7152*g+722*b)/10000};
                for(int c=0;c<4;++c){const auto value=std::clamp(channels[std::size_t(c)],0.,1.);const int bin=std::min(1023,int(value*1024));++counts.bins[std::size_t(c*1024+bin)];}
                counts.shadows+=r<=0&&g<=0&&b<=0;counts.highlights+=r>=1||g>=1||b>=1;++counts.pixels;
            }
        }
    });
    if(cancelled(token)||invalid.load(std::memory_order_relaxed))return false;
    HistogramCounts complete;for(const auto &part:partials)complete.add(part);if(complete.pixels!=quint64(image.width())*image.height())return false;
    *output=std::move(complete);return true;
}
void countRows(const QImage &image, int start, int end, HistogramCounts &counts, const CancelToken &token) {
    for (int y=start; y<end && !cancelled(token); ++y) {
        const auto *line = reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
        for (int x=0; x<image.width(); ++x) {
            const quint16 r=line[x].red(),g=line[x].green(),b=line[x].blue();
            const int l=int((2126u*r+7152u*g+722u*b+5000u)/10000u);
            ++counts.bins[r>>6]; ++counts.bins[1024+(g>>6)];
            ++counts.bins[2048+(b>>6)]; ++counts.bins[3072+(l>>6)];
            counts.shadows += r<=257 && g<=257 && b<=257;
            counts.highlights += r>=65278 || g>=65278 || b>=65278;
            ++counts.pixels;
        }
    }
}
}
ScopesResult ScopesEngine::analyzeLinear(const QImage &image,int bins,const CancelToken &token) {
    HistogramCounts counts;if(countLinearFrame(image,&counts,token))return result(counts,bins);
    ScopesResult failed;failed.error=cancelled(token)?QString("Linear analysis cancelled"):QString("Linear analysis requires a complete finite unassociated linear-sRGB FP32 frame");return failed;
}
ScopesResult ScopesEngine::analyzeWorking(BrushAnalysisRequest &request,const CancelToken &token,QString *error) {
    HistogramCounts total;
    const bool complete=request.visitStrips([&](const BrushImageLease &lease,int){HistogramCounts part;if(!countLinearFrame(lease.image(),&part,token))return false;total.add(part);return true;},token,error);
    if(complete&&!cancelled(token)&&total.pixels==quint64(request.size().width())*request.size().height())return result(total);
    ScopesResult failed;failed.error=error&&!error->isEmpty()?*error:QString("Working analysis did not complete");return failed;
}
ScopesResult ScopesEngine::analyzeDomain(const QImage &source,const ProcessingPlan &requestedPlan,AnalysisDomain domain,const CancelToken &token) {
    if(domain==AnalysisDomain::Display)return analyzeFull(source,requestedPlan,token);
    const auto fail=[](const QString &message){ScopesResult result;result.error=message;return result;};
    if(domain!=AnalysisDomain::Source&&domain!=AnalysisDomain::Working)return fail("Invalid analysis domain");
    if(source.isNull()||cancelled(token))return {};
    auto plan=requestedPlan;
    if(domain==AnalysisDomain::Working) {
        if(BrushMaskTileRaster::anyEnabled(plan.state.brushMasks))return fail("Working brush statistics require float-output admission");
        if(!ImagePipeline::resolveDehazeAirlight(source,plan,token))return fail("Analysis airlight resolution failed");
    }
    HistogramCounts total;
    for(int y=0;y<source.height();y+=128) {
        if(cancelled(token))return {};
        QString error;const auto strip=ImagePipeline::analysisRegion(source,plan,{0,y,source.width(),std::min(128,source.height()-y)},domain,token,&error);
        HistogramCounts part;
        if(!countLinearFrame(strip,&part,token))return cancelled(token)?ScopesResult{}:fail(error.isEmpty()?QString("Linear histogram rendering failed"):error);
        total.add(part);
    }
    if(cancelled(token)||total.pixels!=quint64(source.width())*source.height())return {};
    return result(total);
}
ScopesResult ScopesEngine::result(const HistogramCounts &counts, int bins) {
    ScopesResult result;
    bins = bins <= 256 ? 256 : bins <= 512 ? 512 : 1024;
    // Supported resolutions are 256, 512 and 1024; every rebin is exact.
    QVariantList *channels[]{&result.red,&result.green,&result.blue,&result.luma};
    for (int c=0; c<4; ++c) {
        std::vector<quint64> values(size_t(bins),0);
        for (int i=0; i<1024; ++i) values[size_t(i*bins/1024)]+=counts.bins[size_t(c*1024+i)];
        channels[c]->reserve(bins);
        for (const auto value : values) channels[c]->push_back(QVariant::fromValue<qulonglong>(value));
    }
    result.pixelCount=counts.pixels;
    if (counts.pixels) {
        result.shadowClipPercent=100.0*double(counts.shadows)/double(counts.pixels);
        result.highlightClipPercent=100.0*double(counts.highlights)/double(counts.pixels);
    }
    return result;
}
ScopesResult ScopesEngine::analyze(const QImage &image, int bins, const CancelToken &token) {
    if (image.isNull() || cancelled(token)) return {};
    PerformanceSpan timing("cpu_scopes",{{"pixels",qint64(image.width())*image.height()}});
    const QImage source=image.format()==QImage::Format_RGBA64 ? image : image.convertToFormat(QImage::Format_RGBA64);
    const int blocks=(source.height()+63)/64;
    std::vector<HistogramCounts> partials(size_t(blocks),HistogramCounts{});
    ParallelRows::run(blocks,source.width()*64,token,[&](int block) {
        countRows(source,block*64,std::min(source.height(),(block+1)*64),partials[size_t(block)],token);
    });
    if (cancelled(token)) return {};
    HistogramCounts total;
    for (const auto &part:partials) total.add(part);
    return result(total,bins);
}
ScopesResult ScopesEngine::analyzeFull(const QImage &source, const ProcessingPlan &requestedPlan, const CancelToken &token,
                                     const RenderedStripObserver &completedStrip) {
    if (source.isNull()) return {};
    auto plan=requestedPlan;if(!ImagePipeline::resolveDehazeAirlight(source,plan,token))return {};
    if (!ImagePipeline::preflightTiledResearchRequest(source.size(),plan,128,token)) return {};
    PerformanceSpan timing("full_resolution_scopes",{{"pixels",qint64(source.width())*source.height()}});
    // FP32 scenes must enter the *same* color kernel as the preview and
    // export. The former integer adapter truncated values >1 before user
    // exposure, making full-resolution scopes disagree with the actual edit.
    const bool floating=source.format()==QImage::Format_RGBA32FPx4;
    const QImage input=floating || source.format()==QImage::Format_RGBA64
        ? source : source.convertToFormat(QImage::Format_RGBA64);
    if (input.isNull()) return {};
    HistogramCounts total;
    bool collecting=bool(completedStrip);
    for (int y=0; y<input.height() && !cancelled(token); y+=128) {
        const QImage rendered=ImagePipeline::processRegion(input,plan,QRect(0,y,input.width(),std::min(128,input.height()-y)),token);
        if (rendered.isNull()) return {};
        countRows(rendered,0,rendered.height(),total,token);
        if (collecting && !cancelled(token)) {
            try { completedStrip(rendered,y); }
            catch (...) {
                // Optional collection must not fail a valid streamed histogram.
                collecting=false;
                PerformanceRecorder::count("full_scopes_collector_failures");
            }
        }
    }
    return cancelled(token) ? ScopesResult{} : result(total);
}
ScopesResult ScopesEngine::fromGpu(const QByteArray &bytes, quint64 expectedPixels) {
    if (bytes.size()!=HistogramCounts::GpuWords*int(sizeof(quint32))) return {};
    std::array<quint32,HistogramCounts::GpuWords> values{};
    std::memcpy(values.data(),bytes.constData(),size_t(bytes.size()));
    HistogramCounts counts;
    for (size_t i=0;i<counts.bins.size();++i) counts.bins[i]=values[i];
    counts.shadows=values[4096]; counts.highlights=values[4097]; counts.pixels=values[4098];
    if (counts.pixels!=expectedPixels) return {};
    for (int c=0;c<4;++c) {
        quint64 sum=0; for (int i=0;i<1024;++i) sum+=counts.bins[size_t(c*1024+i)];
        if (sum!=expectedPixels) return {};
    }
    return result(counts);
}
ScopesResult ScopesEngine::analyzeAdmitted(BrushRenderRequest &request,const CancelToken &token,
                                          QString *error,std::array<ScopePlotResult,3> *plots,const RenderedStripObserver &completedStrip) {
    if(error)error->clear();
    const auto refuse=[&](const QString &message){if(error)*error=message;return ScopesResult{};};
    if(request.layout()!=BrushRenderRequest::Layout::Stripes128)return refuse("Scopes require a streamed admitted brush request");
    try {
        HistogramCounts total;int rows=0;bool collecting=bool(completedStrip);
        std::vector<ScopePlotCounts> counts;
        if(plots)for(const auto &mode:{"waveform","parade","vectorscope"})counts.emplace_back(mode);
        if(!request.visitStrips([&](const BrushImageLease &lease,int y) {
            const auto &image=lease.image();
            if(y!=rows||image.width()!=request.size().width()||image.format()!=QImage::Format_RGBA64)return false;
            countRows(image,0,image.height(),total,token);rows+=image.height();
            for(auto &plot:counts)if(!plot.add(image,token))return false;
            if(collecting&&!cancelled(token)) {
                try{completedStrip(image,y);}
                catch(...){collecting=false;PerformanceRecorder::count("full_scopes_collector_failures");}
            }
            return !cancelled(token);
        },token,error))return {};
        if(rows!=request.size().height()||total.pixels!=quint64(request.size().width())*request.size().height())return refuse("Incomplete admitted scopes");
        auto histogram=result(total);
        if(plots) {
            std::array<ScopePlotResult,3> completed;
            for(std::size_t i=0;i<completed.size();++i) {
                completed[i]={counts[i].image(),counts[i].pixels,{}};
                if(completed[i].image.isNull()||counts[i].pixels!=total.pixels)return refuse("Admitted scope plot allocation failed");
            }
            if(cancelled(token))return refuse("Cancelled");
            *plots=std::move(completed);
        }
        return histogram;
    }catch(...){return refuse("Admitted scopes allocation failed");}
}
