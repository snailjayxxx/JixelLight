#include "CopyImport.h"
#include "diagnostics/PerformanceRecorder.h"
#include "core/raw/RawDecoder.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QTemporaryDir>
#include <limits>

CopyImportResult copyImportFiles(const QStringList &sources,const QString &directory,
                                 const CancelToken &cancel,const CopyImportProgress &progress) {
    CopyImportResult result;
    auto fail=[&](const QString &error) { result.error=error; result.wasCancelled=cancelled(cancel); return result; };
    auto stopped=[&] { result.wasCancelled=true; return result; };
    try {
        if (cancelled(cancel)) return stopped();
        QFileInfo folder(directory);
        if (!folder.isDir() || !folder.isWritable() || sources.isEmpty() || sources.size()>1000)
            return fail("Choose an existing writable folder and 1–1000 source files");
        const QDir destination(folder.canonicalFilePath());
        struct Entry { QString source,target; qint64 size; QDateTime modified; };
        QVector<Entry> plan; QSet<QString> seenSources,seenNames;
        // Case-fold existing names too: a portable import must not create two
        // names that collide when the folder later moves to Windows/macOS.
        for (const auto &name : destination.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot))
            seenNames.insert(name.normalized(QString::NormalizationForm_C).toCaseFolded());
        qint64 totalBytes=0;
        for (const auto &path : sources) {
            if (cancelled(cancel)) return stopped();
            const QFileInfo info(path); const auto identity=info.canonicalFilePath();
            const auto name=info.fileName(), folded=name.normalized(QString::NormalizationForm_C).toCaseFolded();
            if (!info.isFile() || !info.isReadable() || identity.isEmpty() || seenSources.contains(identity))
                return fail("Unreadable or duplicate source: "+path);
            if (!RawDecoder::isRawFile(path)) {
                QImageReader reader(identity);
                if (!reader.canRead()) return fail("Unsupported image source: "+path);
            }
            if (name.isEmpty() || seenNames.contains(folded)) return fail("Destination name already exists or is duplicated: "+name);
            if (info.size()<0 || info.size()>std::numeric_limits<qint64>::max()-totalBytes) return fail("Import byte count is too large");
            seenSources.insert(identity); seenNames.insert(folded); totalBytes+=info.size();
            plan.push_back({identity,destination.filePath(name),info.size(),info.lastModified()});
        }
        PerformanceSpan timing("copy_import",{{"files",sources.size()},{"bytes",totalBytes}});
        qint64 copiedBytes=0;
        for (const auto &entry : plan) {
            if (cancelled(cancel)) return stopped();
            const QFileInfo current(entry.source);
            if (current.size()!=entry.size || current.lastModified()!=entry.modified || !current.isFile())
                return fail("Source changed after import preflight: "+entry.source);
            if (QFileInfo::exists(entry.target) || QFileInfo(entry.target).isSymLink()) return fail("Destination appeared during import: "+entry.target);
            QTemporaryDir stage(destination.filePath(".jixellight-import-XXXXXX"));
            if (!stage.isValid()) return fail("Cannot create temporary import directory");
            const auto stagedPath=stage.filePath("photo");
            QFile input(entry.source),output(stagedPath);
            if (!input.open(QIODevice::ReadOnly)) return fail("Cannot read source: "+entry.source+": "+input.errorString());
            if (!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return fail("Cannot create staging file: "+output.errorString());
            QCryptographicHash sourceHash(QCryptographicHash::Sha256); qint64 readBytes=0;
            while (!input.atEnd()) {
                if (cancelled(cancel)) return stopped();
                const auto bytes=input.read(1024*1024);
                if (bytes.isEmpty() || output.write(bytes)!=bytes.size()) return fail("Import read or write failed: "+entry.source);
                sourceHash.addData(bytes); readBytes+=bytes.size(); copiedBytes+=bytes.size();
                if (readBytes>entry.size) return fail("Source grew during import: "+entry.source);
                if (progress) progress(copiedBytes,totalBytes,result.completed.size(),plan.size(),"copy");
            }
            if (input.error()!=QFileDevice::NoError || readBytes!=entry.size || !output.flush()) return fail("Incomplete import copy: "+entry.source);
            input.close(); output.close();
            const QFileInfo after(entry.source);
            if (after.size()!=entry.size || after.lastModified()!=entry.modified) return fail("Source changed during import: "+entry.source);
            if (progress) progress(copiedBytes,totalBytes,result.completed.size(),plan.size(),"verify");
            QFile verify(stagedPath); if (!verify.open(QIODevice::ReadOnly)) return fail("Cannot verify staged copy");
            QCryptographicHash targetHash(QCryptographicHash::Sha256);
            while (!verify.atEnd()) {
                if (cancelled(cancel)) return stopped();
                const auto bytes=verify.read(1024*1024);
                if (bytes.isEmpty()) return fail("Cannot read staged copy for verification");
                targetHash.addData(bytes);
            }
            if (verify.error()!=QFileDevice::NoError || verify.size()!=entry.size || targetHash.result()!=sourceHash.result())
                return fail("Copied content verification failed: "+entry.source);
            verify.close();
            // Re-read the source too: mtime/size alone miss same-size edits
            // within a filesystem's timestamp resolution while copying.
            QFile confirm(entry.source); if (!confirm.open(QIODevice::ReadOnly)) return fail("Cannot recheck source: "+entry.source);
            QCryptographicHash confirmedHash(QCryptographicHash::Sha256);
            while (!confirm.atEnd()) {
                if (cancelled(cancel)) return stopped();
                const auto bytes=confirm.read(1024*1024);
                if (bytes.isEmpty()) return fail("Cannot recheck source content: "+entry.source);
                confirmedHash.addData(bytes);
            }
            if (confirm.error()!=QFileDevice::NoError || confirm.size()!=entry.size || confirmedHash.result()!=sourceHash.result())
                return fail("Source content changed during import: "+entry.source);
            confirm.close();
            if (cancelled(cancel)) return stopped();
            // All native handles are closed before same-filesystem publication.
            if (!QFile::rename(stagedPath,entry.target)) return fail("Destination exists or publication failed: "+entry.target);
            result.completed.push_back({entry.source,entry.target,QString::fromLatin1(sourceHash.result().toHex())});
            if (progress) progress(copiedBytes,totalBytes,result.completed.size(),plan.size(),"published");
        }
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
      catch (...) { return fail("Import copy failed"); }
    return result;
}
