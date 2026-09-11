#include "core/raw/RawContainerMetadata.h"

#include <QFile>
#include <QPoint>
#include <QSet>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace {
constexpr quint16 kTiffMagic = 42;
constexpr quint16 kTagSubIfds = 330;
constexpr quint16 kTagBlackLevel = 50714;
constexpr quint16 kTagWhiteLevel = 50717;
constexpr quint16 kTagDefaultCropOrigin = 50719;
constexpr quint16 kTagDefaultCropSize = 50720;
constexpr quint16 kTagActiveArea = 50829;

struct Entry {
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    QByteArray valueField;
};

class Reader {
public:
    explicit Reader(const QString &path) : file(path) {}

    RawContainerInfo run() {
        if (!file.open(QIODevice::ReadOnly) || file.size() < 8 || file.size() > (qint64(1) << 40)) return {};
        const QByteArray header = file.read(8);
        if (header.size() != 8) return {};
        if (header[0] == 'I' && header[1] == 'I') little = true;
        else if (header[0] == 'M' && header[1] == 'M') little = false;
        else return {};
        if (u16(header.constData() + 2) != kTiffMagic) return {};
        const quint32 root = u32(header.constData() + 4);
        scanIfd(root, 0);
        if (cropOrigin && cropSize) {
            qint64 x = cropOrigin->x(), y = cropOrigin->y();
            if (activeArea) { x += activeArea->x(); y += activeArea->y(); }
            if (x >= 0 && y >= 0 && cropSize->width() > 0 && cropSize->height() > 0
                && x <= std::numeric_limits<int>::max() && y <= std::numeric_limits<int>::max()) {
                info.defaultCrop = QRect(int(x), int(y), cropSize->width(), cropSize->height());
                info.cropSource = QStringLiteral("TIFF DefaultCropOrigin/DefaultCropSize");
            }
        }
        return info;
    }

private:
    QFile file;
    bool little = true;
    QSet<quint32> visited;
    RawContainerInfo info;
    std::optional<QPoint> cropOrigin;
    std::optional<QSize> cropSize;
    std::optional<QPoint> activeArea;

    quint16 u16(const char *p) const {
        const auto *b = reinterpret_cast<const uchar *>(p);
        return little ? quint16(b[0] | (quint16(b[1]) << 8)) : quint16((quint16(b[0]) << 8) | b[1]);
    }
    quint32 u32(const char *p) const {
        const auto *b = reinterpret_cast<const uchar *>(p);
        return little ? quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24)
                      : (quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | quint32(b[3]);
    }
    qint32 i32(const char *p) const { return qint32(u32(p)); }

    int typeSize(quint16 type) const {
        switch (type) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: case 13: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
        }
    }

    QByteArray entryData(const Entry &entry) {
        const int unit = typeSize(entry.type);
        if (!unit || !entry.count || entry.count > 64) return {};
        const quint64 bytes = quint64(unit) * entry.count;
        if (bytes > 512) return {};
        if (bytes <= 4) return entry.valueField.left(int(bytes));
        if (entry.valueField.size() != 4) return {};
        const quint32 offset = u32(entry.valueField.constData());
        if (quint64(offset) + bytes > quint64(file.size())) return {};
        if (!file.seek(offset)) return {};
        return file.read(qint64(bytes));
    }

    QVector<double> numbers(const Entry &entry) {
        const QByteArray bytes = entryData(entry);
        const int unit = typeSize(entry.type);
        if (bytes.isEmpty() || !unit || bytes.size() < qint64(unit) * entry.count) return {};
        QVector<double> out;
        out.reserve(int(entry.count));
        for (quint32 i = 0; i < entry.count; ++i) {
            const char *p = bytes.constData() + qint64(i) * unit;
            double value = 0;
            switch (entry.type) {
            case 1: case 7: value = uchar(*p); break;
            case 3: value = u16(p); break;
            case 4: case 13: value = u32(p); break;
            case 8: value = qint16(u16(p)); break;
            case 9: value = i32(p); break;
            case 5: {
                const quint32 denominator = u32(p + 4);
                if (!denominator) return {};
                value = double(u32(p)) / denominator;
                break;
            }
            case 10: {
                const qint32 denominator = i32(p + 4);
                if (!denominator) return {};
                value = double(i32(p)) / denominator;
                break;
            }
            default: return {};
            }
            if (!std::isfinite(value)) return {};
            out.push_back(value);
        }
        return out;
    }

    std::optional<QPoint> pairPoint(const Entry &entry) {
        const auto values = numbers(entry);
        if (values.size() < 2) return std::nullopt;
        const qint64 x = std::llround(values[0]), y = std::llround(values[1]);
        if (x < 0 || y < 0 || x > std::numeric_limits<int>::max() || y > std::numeric_limits<int>::max()) return std::nullopt;
        return QPoint(int(x), int(y));
    }

    std::optional<QSize> pairSize(const Entry &entry) {
        const auto values = numbers(entry);
        if (values.size() < 2) return std::nullopt;
        const qint64 w = std::llround(values[0]), h = std::llround(values[1]);
        if (w <= 0 || h <= 0 || w > std::numeric_limits<int>::max() || h > std::numeric_limits<int>::max()) return std::nullopt;
        return QSize(int(w), int(h));
    }

    void consume(const Entry &entry) {
        if (entry.tag == kTagDefaultCropOrigin) cropOrigin = pairPoint(entry);
        else if (entry.tag == kTagDefaultCropSize) cropSize = pairSize(entry);
        else if (entry.tag == kTagWhiteLevel) {
            const auto values = numbers(entry);
            if (!values.isEmpty()) {
                const qint64 v = std::llround(*std::max_element(values.cbegin(), values.cend()));
                if (v > 0 && v <= std::numeric_limits<int>::max()) info.whiteLevel = int(v);
            }
        } else if (entry.tag == kTagBlackLevel) {
            const auto values = numbers(entry);
            if (!values.isEmpty()) {
                double total = 0; for (double v : values) total += v;
                const qint64 avg = std::llround(total / values.size());
                if (avg >= 0 && avg <= std::numeric_limits<int>::max()) info.blackLevel = int(avg);
            }
        } else if (entry.tag == kTagActiveArea) {
            const auto values = numbers(entry);
            if (values.size() >= 4) {
                const qint64 top = std::llround(values[0]), left = std::llround(values[1]);
                if (top >= 0 && left >= 0 && top <= std::numeric_limits<int>::max() && left <= std::numeric_limits<int>::max())
                    activeArea = QPoint(int(left), int(top));
            }
        }
    }

    void scanIfd(quint32 offset, int depth) {
        if (!offset || depth > 5 || visited.contains(offset) || quint64(offset) + 2 > quint64(file.size())) return;
        visited.insert(offset);
        if (!file.seek(offset)) return;
        const QByteArray countBytes = file.read(2);
        if (countBytes.size() != 2) return;
        const quint16 count = u16(countBytes.constData());
        if (count > 4096 || quint64(offset) + 2 + quint64(count) * 12 + 4 > quint64(file.size())) return;
        const QByteArray table = file.read(qint64(count) * 12 + 4);
        if (table.size() != qint64(count) * 12 + 4) return;

        QVector<quint32> childOffsets;
        for (quint16 i = 0; i < count; ++i) {
            const char *p = table.constData() + qint64(i) * 12;
            Entry entry;
            entry.tag = u16(p);
            entry.type = u16(p + 2);
            entry.count = u32(p + 4);
            entry.valueField = QByteArray(p + 8, 4);
            consume(entry);
            if (entry.tag == kTagSubIfds) {
                const auto values = numbers(entry);
                for (double value : values) {
                    const qint64 child = std::llround(value);
                    if (child > 0 && child <= std::numeric_limits<quint32>::max()) childOffsets.push_back(quint32(child));
                }
            }
        }
        const quint32 next = u32(table.constData() + qint64(count) * 12);
        for (quint32 child : childOffsets) scanIfd(child, depth + 1);
        if (next) scanIfd(next, depth);
    }
};
}

RawContainerInfo RawContainerMetadata::read(const QString &path) {
    return Reader(path).run();
}
