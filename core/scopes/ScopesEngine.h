#pragma once

#include <QImage>
#include <QVariantList>

struct ScopesResult {
    QVariantList red;
    QVariantList green;
    QVariantList blue;
    QVariantList luma;
    double shadowClipPercent = 0.0;
    double highlightClipPercent = 0.0;
};

class ScopesEngine {
public:
    static ScopesResult analyze(const QImage &image, int bins = 1024);
};
