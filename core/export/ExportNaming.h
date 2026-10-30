#pragma once
#include <QStringList>
#include <QVector>

struct ExportNameSource { QString path, versionName, captureTime; };
struct ExportNaming { QString pattern; int sequenceStart=1; };
struct ExportNames { QStringList names; QString error, errorZh; };

// Pure, whole-batch planning in catalog order. Empty pattern keeps the GUI's
// name_JixelLight convention with numbered collision avoidance. A custom
// stem accepts {name}, {version}, {seq}, {seq:1}..{seq:9}, {capture_date} and
// {capture_time}; its extension comes only from the chosen output format.
// Occupied names include files, directories and symlinks. Custom collisions
// reject the whole plan. Capture time is the recorded camera wall clock.
ExportNames planExportNames(const QVector<ExportNameSource> &sources, const ExportNaming &naming={},
                            const QString &format=QStringLiteral("jpeg"), const QStringList &occupied={});
bool exportTemplateNeedsCaptureTime(const QString &pattern);
