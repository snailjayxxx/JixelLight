#include "core/scopes/ScopesEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

ScopesResult ScopesEngine::analyze(const QImage &image, int bins) {
    ScopesResult result;
    bins = std::clamp(bins, 256, 1024);
    std::vector<quint64> r(bins), g(bins), b(bins), l(bins);
    if (image.isNull()) return result;

    const QImage src = image.convertToFormat(QImage::Format_RGBA8888);
    quint64 shadowClipped = 0, highlightClipped = 0, count = 0;
    for (int y = 0; y < src.height(); ++y) {
        const auto *line = src.constScanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            const auto *p = line + x * 4;
            auto bin = [bins](int v) { return std::min(bins - 1, (v * bins) / 256); };
            ++r[bin(p[0])]; ++g[bin(p[1])]; ++b[bin(p[2])];
            const int y709 = std::clamp(static_cast<int>(std::lround(0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2])), 0, 255);
            ++l[bin(y709)];
            if (p[0] <= 1 && p[1] <= 1 && p[2] <= 1) ++shadowClipped;
            if (p[0] >= 254 || p[1] >= 254 || p[2] >= 254) ++highlightClipped;
            ++count;
        }
    }

    auto toVariant = [](const std::vector<quint64> &src) {
        QVariantList out; out.reserve(static_cast<qsizetype>(src.size()));
        for (auto v : src) out.push_back(QVariant::fromValue<qulonglong>(v));
        return out;
    };
    result.red = toVariant(r); result.green = toVariant(g); result.blue = toVariant(b); result.luma = toVariant(l);
    if (count) {
        result.shadowClipPercent = 100.0 * static_cast<double>(shadowClipped) / static_cast<double>(count);
        result.highlightClipPercent = 100.0 * static_cast<double>(highlightClipped) / static_cast<double>(count);
    }
    return result;
}
