#include "ScopePlot.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QRgba64>
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
ScopePlotResult renderEncodedScopePlot(const QImage &encoded,const QString &mode,const CancelToken &cancel) {
    ScopePlotResult result;
    try {
        if (encoded.isNull() || encoded.format()!=QImage::Format_RGBA64 || cancelled(cancel)) return {};
        PerformanceSpan timing("cpu_scope_plot_encoded",{{"mode",mode},{"pixels",qint64(encoded.width())*encoded.height()}});
        ScopePlotCounts counts(mode);
        if (!counts.add(encoded,cancel)) return {};
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
        PerformanceSpan timing("cpu_scope_plot",{{"mode",request.mode},{"full",request.fullResolution}});
        const auto source=request.geometry.apply(request.source,cancel);
        if (source.isNull() || cancelled(cancel)) return result;
        if (!ImagePipeline::preflightTiledResearchRequest(source.size(),request.plan,128,cancel,&result.error))
            return cancelled(cancel) ? ScopePlotResult{} : result;
        // The same processRegion path and halos as exact histogram/JPEG; bounded
        // rendered rows instead of an additional full-size output allocation.
        for (int y=0;y<source.height();y+=128) {
            if (cancelled(cancel)) return {};
            const auto region=ImagePipeline::processRegion(source,request.plan,{0,y,source.width(),std::min(128,source.height()-y)},cancel);
            if (!counts.add(region,cancel)) { result.error="Scope plot rendering failed"; return result; }
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
        PerformanceSpan timing("cpu_scope_plots_shared",{{"full",request.fullResolution},{"modes",3}});
        std::array<ScopePlotCounts,3> counts{ScopePlotCounts("waveform"),ScopePlotCounts("parade"),ScopePlotCounts("vectorscope")};
        const auto source=request.geometry.apply(request.source,cancel);
        if (source.isNull() || cancelled(cancel)) return {};
        QString preflightError;
        if (!ImagePipeline::preflightTiledResearchRequest(source.size(),request.plan,128,cancel,&preflightError))
            return cancelled(cancel) ? std::array<ScopePlotResult,3>{} : fail(preflightError);
        for (int y=0;y<source.height();y+=128) {
            if (cancelled(cancel)) return {};
            const auto tile=ImagePipeline::processRegion(source,request.plan,{0,y,source.width(),std::min(128,source.height()-y)},cancel);
            for (auto &count : counts) if (!count.add(tile,cancel))
                return cancelled(cancel) ? std::array<ScopePlotResult,3>{} : fail("Scope plot rendering failed");
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
