#pragma once
#include <QStringList>

struct ImportNaming {
    // Empty retains the original leaf name. Otherwise a stem template, with
    // {name}, {seq}, or {seq:1} through {seq:9}; the extension is preserved.
    QString pattern;
    int sequenceStart=1;
};
struct ImportNames { QStringList names; QString error; };

// Pure filename planning, in selection order. No file reads or mutations.
// Invalid templates/names or intra-plan collisions reject the entire plan.
ImportNames planImportNames(const QStringList &sources,const ImportNaming &naming={});
