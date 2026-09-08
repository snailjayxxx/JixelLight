#pragma once
#include "core/cache/SourceCache.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/scopes/ScopesEngine.h"
#include <QSize>

struct LoadRequest { QString path; quint64 photo = 0; };
struct PrepareRequest {
    QImage image;
    quint64 photo = 0, generation = 0;
    QSize viewport{1600, 1000};
    double zoom = 0, centerX = .5, centerY = .5;
    bool fullResolution = false;
};
struct PreparedPreview {
    QImage normal, fast, gpu;
    QSizeF displayPixels;
    bool viewportOnly = false;
    QString error;
};
struct RenderRequest {
    QImage source;
    ProcessingPlan plan;
    quint64 revision = 0;
    bool fast = false;
};
struct ScopeRequest {
    QImage image;
    ProcessingPlan plan;
    quint64 revision = 0;
    bool fullResolution = false;
};
PreparedPreview preparePreview(const PrepareRequest &request, const CancelToken &cancel);
