#pragma once
#include "core/pipeline/ProcessingPlan.h"
#include <QImage>
#include <QVector>
#include <array>

// Optional CPU reference plots of the encoded sRGB output, before monitor ICC.
// Waveform/parade retain 1024 vertical levels; these do not replace histogram.
struct ScopePlotCounts {
    static constexpr int Levels = 1024, Columns = 256, ChromaSize = 512;
    QString mode;
    int width = 0, height = 0;
    QVector<quint64> bins;
    quint64 pixels = 0;
    explicit ScopePlotCounts(const QString &kind);
    bool add(const QImage &encoded, const CancelToken &cancel = {});
    QImage image() const;
};
struct ScopePlotRequest {
    QImage source;
    ProcessingPlan plan;
    GeometryState geometry;
    QString mode;
    quint64 revision = 0;
    bool fullResolution = false;
};
struct ScopePlotResult { QImage image; quint64 pixels = 0; QString error; };
ScopePlotResult renderScopePlot(const ScopePlotRequest &request, const CancelToken &cancel);
// Fixed order: waveform, parade, vectorscope. Shares one bounded tile render;
// counts and display ink keep exactly the individual-mode algorithms above.
std::array<ScopePlotResult,3> renderScopePlots(const ScopePlotRequest &request, const CancelToken &cancel);
// Consume a completed pre-monitor RGBA64 frame. The caller keeps its immutable
// frame lease alive through this call; no geometry or color rendering occurs.
ScopePlotResult renderEncodedScopePlot(const QImage &encoded,const QString &mode,const CancelToken &cancel = {});
std::array<ScopePlotResult,3> renderEncodedScopePlots(const QImage &encoded,const CancelToken &cancel = {});
