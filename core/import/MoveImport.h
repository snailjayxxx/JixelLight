#pragma once
#include "CopyImport.h"

struct MoveImportFile {
    CopyImportFile copy;
    qint64 bytes=-1;
    QString state="copied",trashPath;
};
struct MoveImportBatch {
    QString journal,project,directory,recordDigest;
    QVector<MoveImportFile> files;
    int requested=0;
    QString phase="copying",error;
    bool wasCancelled=false;
    int retired() const;
};
// Copies and SHA-verifies first. This function never retires a source.
MoveImportBatch prepareMoveImport(const QStringList &,const QString &directory,const QString &project,
    const CancelToken &cancel={},const CopyImportProgress &progress={},const ImportNaming &naming={},const QStringList &captureTimes={});
// Read-only, bounded journal parsing. Loading never resumes work automatically.
MoveImportBatch readMoveImport(const QString &journal,QString *error=nullptr);
// Only saved catalog identities and current verified destination/source bytes
// admit source retirement. Trash failure retains the source; never unlink it.
using MoveImportTrash=std::function<bool(const QString &,QString *)>;
MoveImportBatch retireMoveImport(const QString &journal,const CancelToken &cancel={},
    const CopyImportProgress &progress={},const MoveImportTrash &trash={},const QString &expectedDigest={});
