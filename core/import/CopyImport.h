#pragma once
#include "core/async/LatestJob.h"
#include "ImportNaming.h"
#include <QStringList>
#include <QVector>
#include <functional>

struct CopyImportFile { QString source, destination, sha256; };
struct CopyImportResult { QVector<CopyImportFile> completed; QString error; bool wasCancelled=false; QString errorZh; };
using CopyImportProgress=std::function<void(qint64 copiedBytes,qint64 totalBytes,int completed,int total,const QString &stage)>;

// Existing directory, validated filename plan, whole-plan preflight. Never removes
// sources or replaces a destination; completed copies survive a later failure.
CopyImportResult copyImportFiles(const QStringList &sources,const QString &directory,
                                 const CancelToken &cancel={},const CopyImportProgress &progress={},
                                 const ImportNaming &naming={},const QStringList &expectedCaptureTimes={});
