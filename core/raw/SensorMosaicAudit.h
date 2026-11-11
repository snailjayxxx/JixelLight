#pragma once
#include "core/async/LatestJob.h"
#include <QJsonObject>
#include <QRect>
#include <QtGlobal>

// Diagnostic-only reader of 16-bit LibRaw unpacked single-plane sensor codes.
// These codes are NOT white-balanced, black-subtracted, demosaiced or scene RGB.
// The caller must guarantee that the buffer is a live, tightly packed U16 RAW
// plane and cannot destroy/free it while capture() runs.
struct SensorMosaicAudit {
    struct Layout {
        int rawWidth=0,rawHeight=0;
        int pitchBytes=0;
        QRect active;
        quint32 nominalBlack=0,nominalWhite=0;
    };
    static QJsonObject capture(const quint16 *raw, const Layout &layout,
                               const CancelToken &cancel={});
};
