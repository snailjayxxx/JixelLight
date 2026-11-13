#include "ScopePlot.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QRgba64>
#include <QColorSpace>
#include <algorithm>
#include <cmath>
#include <exception>

ScopePlotCounts::ScopePlotCounts(const QString &kind) : mode(kind) {
    if (mode=="waveform" || mode=="parade") { width=Columns*(mode=="parade" ? 3 : 1); height=Levels; }
    else if (mode=="vectorscope") { width=height=ChromaSize; }
    bins.fill(0,width*height);
}
bool ScopePlotCounts::add(const QImage &encoded, const CancelToken &cancel) {
    if (bins.isEmpty() || encoded.isNull() || cancelled(cancel)) return false;
    const auto source=encoded.format()==QImage::Format_RGBA64 ? encoded : encoded.convertToFormat(QImage::Format_RGBA64);
    if (source.isNull()) return false;
    for (int y=0;y<source.height();++y) {
        if (cancelled(cancel)) return false;
        const auto row=reinterpret_cast<const QRgba64 *>(source.constScanLine(y));
        for (int x=0;x<source.width();++x) {
            const quint32 r=row[x].red(), g=row[x].green(), b=row[x].blue();
            const quint32 luma=(2126*r+7152*g+722*b+5000)/10000;
            const int column=source.width()>1 ? int(qint64(x)*(Columns-1)/(source.width()-1)) : Columns/2;
            if (mode=="waveform") ++bins[(Levels-1-int(luma>>6))*width+column];
            else if (mode=="parade") {
                ++bins[(Levels-1-int(r>>6))*width+column];
                ++bins[(Levels-1-int(g>>6))*width+Columns+column];
                ++bins[(Levels-1-int(b>>6))*width+2*Columns+column];
            } else {
                const double red=r/65535.0, green=g/65535.0, blue=b/65535.0;
                const double yPrime=.2126*red+.7152*green+.0722*blue;
                const auto cx=int(std::lround(std::clamp(.5+(blue-yPrime)/1.8556,0.0,1.0)*(ChromaSize-1)));
                const auto cy=int(std::lround(std::clamp(.5-(red-yPrime)/1.5748,0.0,1.0)*(ChromaSize-1)));
                ++bins[cy*width+cx];
            }
            ++pixels;
        }
    }
    return true;
}
bool ScopePlotCounts::addLinear(const QImage &source,const CancelToken &cancel) {
    const auto fail=[&]{bins.fill(0);pixels=0;return false;};
    if(bins.isEmpty()||source.isNull()||cancelled(cancel)||source.format()!=QImage::Format_RGBA32FPx4
        ||source.colorSpace()!=QColorSpace(QColorSpace::SRgbLinear))return fail();
    const auto level=[](double v){return std::min(Levels-1,int(std::clamp(v,0.,1.)*Levels));};
    for(int y=0;y<source.height();++y) {
        if(cancelled(cancel))return fail();
        const auto *row=reinterpret_cast<const float *>(source.constScanLine(y));
        for(int x=0;x<source.width();++x) {
            const auto *p=row+qsizetype(4)*x;
            if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2])||!std::isfinite(p[3])||p[3]<0||p[3]>1)return fail();
            const double r=p[0],g=p[1],b=p[2],luma=(2126*r+7152*g+722*b)/10000;
            const int column=source.width()>1?int(qint64(x)*(Columns-1)/(source.width()-1)):Columns/2;
            if(mode=="waveform")++bins[(Levels-1-level(luma))*width+column];
            else if(mode=="parade") {
                ++bins[(Levels-1-level(r))*width+column];
                ++bins[(Levels-1-level(g))*width+Columns+column];
                ++bins[(Levels-1-level(b))*width+2*Columns+column];
            } else {
                // Linear Y/color-difference coordinates. Preserve signed/HDR
                // RGB through the calculation, clamping only the graph index.
                const int cx=int(std::lround(std::clamp(.5+(b-luma)/1.8556,0.,1.)*(ChromaSize-1)));
                const int cy=int(std::lround(std::clamp(.5-(r-luma)/1.5748,0.,1.)*(ChromaSize-1)));
                ++bins[cy*width+cx];
            }
            ++pixels;
        }
    }
    return cancelled(cancel)?fail():true;
}
QImage ScopePlotCounts::image() const {
    if (bins.isEmpty() || !pixels) return {};
    const auto maximum=*std::max_element(bins.cbegin(),bins.cend());
    if (!maximum) return {};
    QImage image(width,height,QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    const double scale=1.0/std::log1p(double(maximum));
    for (int y=0;y<height;++y) {
        auto row=reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x=0;x<width;++x) {
            const int v=int(std::lround(255*std::log1p(double(bins[y*width+x]))*scale));
            if (mode=="parade") row[x]=x<Columns ? qRgb(v,v/5,v/5) : x<2*Columns ? qRgb(v/5,v,v/5) : qRgb(v/4,v/3,v);
            else row[x]=qRgb(v*4/5,v,v*9/10);
        }
    }
    // A three-pixel trace remains visible when the panel downsamples the plot.
    // Dilation affects display ink only, never counts or the source picture.
    QImage ink=image.copy();
    if (ink.isNull()) return {};
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) if (bins[y*width+x]) {
        const auto color=image.pixel(x,y);
        for (const auto delta : {QPoint(-1,0),QPoint(1,0),QPoint(0,-1),QPoint(0,1)}) {
            const int xx=x+delta.x(), yy=y+delta.y();
            if (xx<0 || xx>=width || yy<0 || yy>=height || (mode=="parade" && xx/Columns!=x/Columns)) continue;
            auto &pixel=reinterpret_cast<QRgb *>(ink.scanLine(yy))[xx];
            pixel=qRgb(std::max(qRed(pixel),qRed(color)),std::max(qGreen(pixel),qGreen(color)),std::max(qBlue(pixel),qBlue(color)));
        }
    }
    // Plot colors are UI ink, not image pixels requiring monitor conversion.
    return ink;
}
namespace {
// Dispatch once per plot; the independent ScopePlotCounts::add reference keeps
// its generic path. These loops retain every RGBA64 sample and exact formulas.
template<int Mode> bool addEncodedPlot(ScopePlotCounts &counts,const QImage &encoded,const CancelToken &cancel) {
    constexpr int columns=ScopePlotCounts::Columns,levels=ScopePlotCounts::Levels,chroma=ScopePlotCounts::ChromaSize;
    auto *bins=counts.bins.data();
    for (int y=0;y<encoded.height();++y) {
        if (cancelled(cancel)) return false;
        const auto *row=reinterpret_cast<const QRgba64 *>(encoded.constScanLine(y));
        for (int x=0;x<encoded.width();++x) {
            const quint32 r=row[x].red(),g=row[x].green(),b=row[x].blue();
            if constexpr (Mode==0) {
                const quint32 luma=(2126*r+7152*g+722*b+5000)/10000;
                const int column=encoded.width()>1 ? int(qint64(x)*(columns-1)/(encoded.width()-1)):columns/2;
                ++bins[(levels-1-int(luma>>6))*columns+column];
            } else if constexpr (Mode==1) {
                const int column=encoded.width()>1 ? int(qint64(x)*(columns-1)/(encoded.width()-1)):columns/2;
                ++bins[(levels-1-int(r>>6))*3*columns+column];
                ++bins[(levels-1-int(g>>6))*3*columns+columns+column];
                ++bins[(levels-1-int(b>>6))*3*columns+2*columns+column];
            } else {
                const double red=r/65535.0,green=g/65535.0,blue=b/65535.0;
                const double yPrime=.2126*red+.7152*green+.0722*blue;
                const auto cx=int(std::lround(std::clamp(.5+(blue-yPrime)/1.8556,0.0,1.0)*(chroma-1)));
                const auto cy=int(std::lround(std::clamp(.5-(red-yPrime)/1.5748,0.0,1.0)*(chroma-1)));
                ++bins[cy*chroma+cx];
            }
        }
    }
    counts.pixels=quint64(encoded.width())*encoded.height();
    return !cancelled(cancel);
}
}
ScopePlotResult renderEncodedScopePlot(const QImage &encoded,const QString &mode,const CancelToken &cancel) {
    ScopePlotResult result;
    try {
        if (encoded.isNull() || encoded.format()!=QImage::Format_RGBA64 || cancelled(cancel)) return {};
        PerformanceSpan timing("cpu_scope_plot_encoded",{{"mode",mode},{"pixels",qint64(encoded.width())*encoded.height()}});
        ScopePlotCounts counts(mode);
        const bool added=mode=="waveform" ? addEncodedPlot<0>(counts,encoded,cancel)
            :mode=="parade" ? addEncodedPlot<1>(counts,encoded,cancel)
            :mode=="vectorscope" ? addEncodedPlot<2>(counts,encoded,cancel):false;
        if (!added) return {};
        if (cancelled(cancel)) return {};
        result={counts.image(),counts.pixels,{}};
        if (result.image.isNull()) result.error="Scope plot image allocation failed";
    } catch (const std::exception &e) { result.error=QString::fromUtf8(e.what()); }
      catch (...) { result.error="Scope plot allocation or counting failed"; }
    return cancelled(cancel) ? ScopePlotResult{} : result;
}
std::array<ScopePlotResult,3> renderEncodedScopePlots(const QImage &encoded,const CancelToken &cancel) {
    std::array<ScopePlotResult,3> results;
    auto fail=[&](const QString &error) {
        for (auto &result:results) {result={};result.error=error;}
        return results;
    };
    try {
        if (encoded.isNull() || encoded.format()!=QImage::Format_RGBA64 || cancelled(cancel)) return {};
        PerformanceSpan timing("cpu_scope_plots_encoded",{{"pixels",qint64(encoded.width())*encoded.height()},{"modes",3}});
        std::array<ScopePlotCounts,3> counts{ScopePlotCounts("waveform"),ScopePlotCounts("parade"),ScopePlotCounts("vectorscope")};
        // All three plots consume the same immutable encoded frame. Read each
        // pixel once, retaining the individual reference modes' exact integer
        // levels, horizontal bins and floating-point chroma rounding.
        auto *wave=counts[0].bins.data();
        auto *parade=counts[1].bins.data();
        auto *vector=counts[2].bins.data();
        constexpr int columns=ScopePlotCounts::Columns, levels=ScopePlotCounts::Levels;
        constexpr int chroma=ScopePlotCounts::ChromaSize;
        for (int y=0;y<encoded.height();++y) {
            if (cancelled(cancel)) return {};
            const auto row=reinterpret_cast<const QRgba64 *>(encoded.constScanLine(y));
            for (int x=0;x<encoded.width();++x) {
                const quint32 r=row[x].red(), g=row[x].green(), b=row[x].blue();
                const quint32 luma=(2126*r+7152*g+722*b+5000)/10000;
                const int column=encoded.width()>1 ? int(qint64(x)*(columns-1)/(encoded.width()-1)) : columns/2;
                ++wave[(levels-1-int(luma>>6))*columns+column];
                ++parade[(levels-1-int(r>>6))*3*columns+column];
                ++parade[(levels-1-int(g>>6))*3*columns+columns+column];
                ++parade[(levels-1-int(b>>6))*3*columns+2*columns+column];
                const double red=r/65535.0, green=g/65535.0, blue=b/65535.0;
                const double yPrime=.2126*red+.7152*green+.0722*blue;
                const auto cx=int(std::lround(std::clamp(.5+(blue-yPrime)/1.8556,0.0,1.0)*(chroma-1)));
                const auto cy=int(std::lround(std::clamp(.5-(red-yPrime)/1.5748,0.0,1.0)*(chroma-1)));
                ++vector[cy*chroma+cx];
            }
        }
        for (auto &count:counts) count.pixels=quint64(encoded.width())*encoded.height();
        for (int i=0;i<3;++i) {
            if (cancelled(cancel)) return {};
            results[i]={counts[i].image(),counts[i].pixels,{}};
            if (results[i].image.isNull()) return fail("Scope plot image allocation failed");
        }
        if (cancelled(cancel)) return {};
        PerformanceRecorder::count("scope_plot_encoded_passes");
        PerformanceRecorder::count("scope_plot_encoded_pixels",qint64(encoded.width())*encoded.height());
    } catch (const std::exception &e) { return cancelled(cancel) ? std::array<ScopePlotResult,3>{} : fail(QString::fromUtf8(e.what())); }
      catch (...) { return cancelled(cancel) ? std::array<ScopePlotResult,3>{} : fail("Scope plot allocation or counting failed"); }
    return results;
}
ScopePlotResult renderScopePlot(const ScopePlotRequest &request, const CancelToken &cancel) {
    ScopePlotResult result;
    try {
        ScopePlotCounts counts(request.mode);
        if (request.source.isNull() || counts.bins.isEmpty() || cancelled(cancel)) return result;
        if(analysisDomainKey(request.domain).isEmpty()){result.error="Invalid analysis domain";return result;}
        PerformanceSpan timing("cpu_scope_plot",{{"mode",request.mode},{"full",request.fullResolution}});
        const auto source=request.geometry.apply(request.source,cancel);
        if (source.isNull() || cancelled(cancel)) return result;
        auto plan=request.plan;
        if(request.domain==AnalysisDomain::Working&&!ImagePipeline::resolveDehazeAirlight(source,plan,cancel)){result.error="Analysis airlight resolution failed";return result;}
        if (request.domain==AnalysisDomain::Display&&!ImagePipeline::preflightTiledResearchRequest(source.size(),plan,128,cancel,&result.error))
            return cancelled(cancel) ? ScopePlotResult{} : result;
        // The same processRegion path and halos as exact histogram/JPEG; bounded
        // rendered rows instead of an additional full-size output allocation.
        for (int y=0;y<source.height();y+=128) {
            if (cancelled(cancel)) return {};
            const auto roi=QRect(0,y,source.width(),std::min(128,source.height()-y));
            const auto region=request.domain==AnalysisDomain::Display?ImagePipeline::processRegion(source,plan,roi,cancel)
                :ImagePipeline::analysisRegion(source,plan,roi,request.domain,cancel,&result.error);
            if (!(request.domain==AnalysisDomain::Display?counts.add(region,cancel):counts.addLinear(region,cancel))) { if(result.error.isEmpty())result.error="Scope plot rendering failed"; return cancelled(cancel)?ScopePlotResult{}:result; }
        }
        if (cancelled(cancel)) return {};
        result.image=counts.image(); result.pixels=counts.pixels;
        if (result.image.isNull()) result.error="Scope plot image allocation failed";
    } catch (const std::exception &e) { result.error=QString::fromUtf8(e.what()); }
      catch (...) { result.error="Scope plot allocation or rendering failed"; }
    return result;
}
std::array<ScopePlotResult,3> renderScopePlots(const ScopePlotRequest &request,const CancelToken &cancel) {
    std::array<ScopePlotResult,3> results;
    auto fail=[&](const QString &error) {
        for (auto &result : results) { result={}; result.error=error; }
        return results;
    };
    try {
        if (request.source.isNull() || cancelled(cancel)) return {};
        if(analysisDomainKey(request.domain).isEmpty())return fail("Invalid analysis domain");
        PerformanceSpan timing("cpu_scope_plots_shared",{{"full",request.fullResolution},{"modes",3}});
        std::array<ScopePlotCounts,3> counts{ScopePlotCounts("waveform"),ScopePlotCounts("parade"),ScopePlotCounts("vectorscope")};
        const auto source=request.geometry.apply(request.source,cancel);
        if (source.isNull() || cancelled(cancel)) return {};
        QString preflightError;
        auto plan=request.plan;
        if(request.domain==AnalysisDomain::Working&&!ImagePipeline::resolveDehazeAirlight(source,plan,cancel))return fail("Analysis airlight resolution failed");
        if (request.domain==AnalysisDomain::Display&&!ImagePipeline::preflightTiledResearchRequest(source.size(),plan,128,cancel,&preflightError))
            return cancelled(cancel) ? std::array<ScopePlotResult,3>{} : fail(preflightError);
        for (int y=0;y<source.height();y+=128) {
            if (cancelled(cancel)) return {};
            const auto roi=QRect(0,y,source.width(),std::min(128,source.height()-y));
            const auto tile=request.domain==AnalysisDomain::Display?ImagePipeline::processRegion(source,plan,roi,cancel)
                :ImagePipeline::analysisRegion(source,plan,roi,request.domain,cancel,&preflightError);
            for (auto &count : counts) if (!(request.domain==AnalysisDomain::Display?count.add(tile,cancel):count.addLinear(tile,cancel)))
                return cancelled(cancel) ? std::array<ScopePlotResult,3>{} : fail(preflightError.isEmpty()?QString("Scope plot rendering failed"):preflightError);
            PerformanceRecorder::count("scope_plot_shared_tiles");
        }
        for (int i=0;i<3;++i) {
            if (cancelled(cancel)) return {};
            results[i]={counts[i].image(),counts[i].pixels,{}};
            if (results[i].image.isNull()) return fail("Scope plot image allocation failed");
        }
        if (cancelled(cancel)) return {};
        PerformanceRecorder::count("scope_plot_shared_passes");
    } catch (const std::exception &e) { return cancelled(cancel) ? std::array<ScopePlotResult,3>{} : fail(QString::fromUtf8(e.what())); }
      catch (...) { return cancelled(cancel) ? std::array<ScopePlotResult,3>{} : fail("Scope plot allocation or rendering failed"); }
    return results;
}
ScopePlotResult renderScopePlot(BrushRenderRequest &request,const QString &mode,const CancelToken &cancel) {
    ScopePlotResult result;
    try {
        ScopePlotCounts counts(mode);
        if(counts.bins.isEmpty()){result.error="Unknown scope plot mode";return result;}
        if(!request.visitStrips([&](const BrushImageLease &lease,int){return counts.add(lease.image(),cancel);},cancel,&result.error))
            return cancelled(cancel)?ScopePlotResult{}:result;
        if(cancelled(cancel))return {};
        result.image=counts.image();result.pixels=counts.pixels;
        if(result.image.isNull())result.error="Scope plot allocation failed";
    }catch(...){result.error="Admitted scope plot failed";}
    return result;
}
std::array<ScopePlotResult,3> renderScopePlots(BrushRenderRequest &request,const CancelToken &cancel) {
    std::array<ScopePlotResult,3> results;
    const auto failed=[&](const QString &error){for(auto &r:results)r={{},0,error};return results;};
    try {
        std::array<ScopePlotCounts,3> counts{ScopePlotCounts("waveform"),ScopePlotCounts("parade"),ScopePlotCounts("vectorscope")};
        QString error;
        if(!request.visitStrips([&](const BrushImageLease &lease,int){
            for(auto &count:counts)if(!count.add(lease.image(),cancel))return false;
            PerformanceRecorder::count("scope_plot_shared_tiles");return true;
        },cancel,&error))return cancelled(cancel)?std::array<ScopePlotResult,3>{}:failed(error);
        for(int i=0;i<3;++i) {
            if(cancelled(cancel))return {};
            results[std::size_t(i)]={counts[std::size_t(i)].image(),counts[std::size_t(i)].pixels,{}};
            if(results[std::size_t(i)].image.isNull())return failed("Scope plot allocation failed");
        }
        PerformanceRecorder::count("scope_plot_shared_passes");
    }catch(...){return cancelled(cancel)?std::array<ScopePlotResult,3>{}:failed("Admitted scope plots failed");}
    return results;
}
ScopePlotResult renderScopePlot(BrushAnalysisRequest &request,const QString &mode,const CancelToken &cancel) {
    ScopePlotResult result;
    try {
        ScopePlotCounts counts(mode);
        if(counts.bins.isEmpty()){result.error="Unknown scope plot mode";return result;}
        if(!request.visitStrips([&](const BrushImageLease &lease,int){return counts.addLinear(lease.image(),cancel);},cancel,&result.error))
            return cancelled(cancel)?ScopePlotResult{}:result;
        if(cancelled(cancel))return {};
        if(counts.pixels!=quint64(request.size().width())*request.size().height()){result.error="Incomplete working scope plot";return result;}
        result={counts.image(),counts.pixels,{}};
        if(result.image.isNull())result.error="Working scope plot allocation failed";
    }catch(...){result={};result.error="Admitted working scope plot failed";}
    return result;
}
std::array<ScopePlotResult,3> renderScopePlots(BrushAnalysisRequest &request,const CancelToken &cancel) {
    std::array<ScopePlotResult,3> results;
    const auto fail=[&](const QString &error){for(auto &r:results)r={{},0,error};return results;};
    try {
        std::array<ScopePlotCounts,3> counts{ScopePlotCounts("waveform"),ScopePlotCounts("parade"),ScopePlotCounts("vectorscope")};
        QString error;
        if(!request.visitStrips([&](const BrushImageLease &lease,int){
            for(auto &count:counts)if(!count.addLinear(lease.image(),cancel))return false;
            return true;
        },cancel,&error))return cancelled(cancel)?std::array<ScopePlotResult,3>{}:fail(error.isEmpty()?QString("Working scope plot rendering failed"):error);
        for(int i=0;i<3;++i) {
            if(cancelled(cancel))return {};
            if(counts[i].pixels!=quint64(request.size().width())*request.size().height())return fail("Incomplete working scope plots");
            results[i]={counts[i].image(),counts[i].pixels,{}};
            if(results[i].image.isNull())return fail("Working scope plot allocation failed");
        }
        PerformanceRecorder::count("scope_plot_working_shared_passes");
    }catch(...){return cancelled(cancel)?std::array<ScopePlotResult,3>{}:fail("Admitted working scope plots failed");}
    return results;
}
