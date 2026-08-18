#include "core/scopes/ScopesEngine.h"

#include <QRgba64>
#include <algorithm>
#include <cmath>
#include <vector>

ScopesResult ScopesEngine::analyze(const QImage &image, int bins) {
    ScopesResult result;
    bins = std::clamp(bins, 256, 1024);
    std::vector<quint64> r(bins), g(bins), b(bins), l(bins);
    if (image.isNull()) return result;

    const QImage src = image.convertToFormat(QImage::Format_RGBA64);
    quint64 shadowClipped = 0, highlightClipped = 0, count = 0;
    auto binOf = [bins](quint16 v) {
        return std::min(bins - 1, static_cast<int>((static_cast<quint64>(v) * bins) / 65536u));
    };

    for (int y = 0; y < src.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgba64 *>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const quint16 rv = line[x].red();
            const quint16 gv = line[x].green();
            const quint16 bv = line[x].blue();
            ++r[binOf(rv)]; ++g[binOf(gv)]; ++b[binOf(bv)];
            const quint16 y709 = static_cast<quint16>(std::clamp(
                static_cast<int>(std::lround(0.2126 * rv + 0.7152 * gv + 0.0722 * bv)), 0, 65535));
            ++l[binOf(y709)];
            if (rv <= 257 && gv <= 257 && bv <= 257) ++shadowClipped;
            if (rv >= 65278 || gv >= 65278 || bv >= 65278) ++highlightClipped;
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
