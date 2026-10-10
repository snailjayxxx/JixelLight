#pragma once
#include <QStringList>

struct ImportNaming {
    // Empty retains the original leaf name. Otherwise a stem template, with
    // {name}, {seq}, {seq:1} through {seq:9}, {capture_date}, {capture_time};
    // the extension is preserved. Capture times are camera wall-clock records.
    QString pattern;
    int sequenceStart=1;
};
struct ImportNames { QStringList names; QString error, errorZh; bool needsCaptureTimes=false; };

// Pure filename planning, in selection order. No file reads or mutations.
// Invalid templates/names or intra-plan collisions reject the entire plan.
ImportNames planImportNames(const QStringList &sources,const ImportNaming &naming={},const QStringList &captureTimes={});
bool importTemplateNeedsCaptureTime(const QString &pattern);
