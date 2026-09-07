#include "core/scopes/ScopesEngine.h"
#include "core/async/ParallelRows.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QRgba64>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

void HistogramCounts::add(const HistogramCounts &other) {
    for (size_t i=0; i<bins.size(); ++i) bins[i]+=other.bins[i];
    shadows+=other.shadows; highlights+=other.highlights; pixels+=other.pixels;
}
namespace {
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
ScopesResult ScopesEngine::analyzeFull(const QImage &source, const ProcessingPlan &plan, const CancelToken &token) {
    if (source.isNull()) return {};
    PerformanceSpan timing("full_resolution_scopes",{{"pixels",qint64(source.width())*source.height()}});
    const QImage input=source.format()==QImage::Format_RGBA64 ? source : source.convertToFormat(QImage::Format_RGBA64);
    HistogramCounts total;
    for (int y=0; y<input.height() && !cancelled(token); y+=128) {
        const QImage view(input.constScanLine(y),input.width(),std::min(128,input.height()-y),input.bytesPerLine(),QImage::Format_RGBA64);
        const QImage rendered=ImagePipeline::processWithPlan(view,plan,token);
        if (rendered.isNull()) return {};
        countRows(rendered,0,rendered.height(),total,token);
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
