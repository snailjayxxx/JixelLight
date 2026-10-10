#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QImage>
#include <QIODevice>
#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <QtEndian>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// Explicit diagnostics only. Preserve IEEE-754 bits (including signed zero and
// NaN payloads), stream visible rows, and describe finite ranges without clipping.
struct FloatFrameFingerprint {
    static constexpr qint64 BudgetBytes = 64 * 1024 * 1024;
    static QJsonObject capture(const QImage &image, qint64 budget = BudgetBytes) {
        if (image.isNull() || image.format() != QImage::Format_RGBA32FPx4)
            return {{"available", false}, {"error", "Expected a nonempty RGBA32FPx4 frame"}};
        const qint64 bytes = qint64(image.width()) * image.height() * 16;
        if (budget < 0 || bytes > budget)
            return {{"available", false}, {"error", "Diagnostic frame exceeds readback budget"}};
        QByteArray descriptor;
        QDataStream stream(&descriptor, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << QStringLiteral("rgba-fp32-le-v1") << QString::fromLatin1(ProcessingPlan::EngineVersion)
               << image.width() << image.height();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(descriptor);
        std::array<double, 4> minimum, maximum;
        minimum.fill(std::numeric_limits<double>::infinity());
        maximum.fill(-std::numeric_limits<double>::infinity());
        std::array<qint64, 4> nonfinite{};
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
        QByteArray row(image.width() * 16, Qt::Uninitialized);
#endif
        for (int y = 0; y < image.height(); ++y) {
            const auto *pixels = image.constScanLine(y);
            for (int x = 0; x < image.width(); ++x) for (int c = 0; c < 4; ++c) {
                float value;
                std::memcpy(&value, pixels + (x * 4 + c) * 4, 4);
                if (std::isfinite(value)) {
                    minimum[c] = std::min(minimum[c], double(value));
                    maximum[c] = std::max(maximum[c], double(value));
                } else ++nonfinite[c];
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
                quint32 bits;
                std::memcpy(&bits, pixels + (x * 4 + c) * 4, 4);
                qToLittleEndian(bits, row.data() + (x * 4 + c) * 4);
#endif
            }
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
            hash.addData(row);
#else
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(pixels), qsizetype(image.width()) * 16));
#endif
        }
        QJsonArray ranges, invalid;
        for (int c = 0; c < 4; ++c) {
            ranges.append(QJsonArray{std::isfinite(minimum[c]) ? QJsonValue(minimum[c]) : QJsonValue(),
                                     std::isfinite(maximum[c]) ? QJsonValue(maximum[c]) : QJsonValue()});
            invalid.append(nonfinite[c]);
        }
        return {{"available", true}, {"pixel_sha256", QString::fromLatin1(hash.result().toHex())},
                {"width", image.width()}, {"height", image.height()}, {"pixels", qint64(image.width()) * image.height()},
                {"bytes", bytes}, {"pixel_order", "IEEE-754 binary32 RGBA, little-endian; row padding excluded"},
                {"ranges_rgba", ranges}, {"nonfinite_rgba", invalid}};
    }
};
