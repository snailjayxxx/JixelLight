#include "MoveImport.h"
#include "core/project/ProjectDatabase.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <cmath>

namespace {
constexpr qint64 MaxJournalBytes=2*1024*1024;
bool digest(const QString &text){static const QRegularExpression pattern("^[0-9a-f]{64}$");return pattern.match(text).hasMatch();}
QString clean(const QString &path){return QDir::fromNativeSeparators(QDir::cleanPath(path));}
QString family(const QString &project,const QString &path){const auto lexical=ProjectDatabase::normalizedSourcePath(project,path);const auto canonical=QFileInfo(lexical).canonicalFilePath();return ProjectDatabase::normalizedSourcePath(project,canonical.isEmpty()?lexical:canonical);}
bool localPath(const QString &path){return !path.isEmpty()&&path.size()<=32768&&!path.contains(QChar(0))&&QDir::isAbsolutePath(path)&&clean(path)==path;}
bool regular(const QString &path){const QFileInfo file(path);return file.isFile()&&file.isReadable()&&!file.isSymLink()&&!file.canonicalFilePath().isEmpty();}
bool matches(const QString &path,qint64 size,const QString &expected,const CancelToken &cancel){
    if(cancelled(cancel)||!regular(path))return false;
    const QFileInfo before(path);if(before.size()!=size)return false;
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;QCryptographicHash hash(QCryptographicHash::Sha256);qint64 read=0;
    while(!file.atEnd()){if(cancelled(cancel))return false;const auto bytes=file.read(1024*1024);if(bytes.isEmpty())return false;hash.addData(bytes);read+=bytes.size();if(read>size)return false;}
    const QFileInfo after(path);return file.error()==QFileDevice::NoError&&read==size&&after.size()==size
        &&before.canonicalFilePath()==after.canonicalFilePath()&&before.lastModified()==after.lastModified()
        &&QString::fromLatin1(hash.result().toHex())==expected;
}
QJsonObject encode(const MoveImportBatch &batch){
    QJsonArray files;for(const auto &file:batch.files)files.append(QJsonObject{{"source",file.copy.source},{"destination",file.copy.destination},{"sha256",file.copy.sha256},
        {"bytes",file.bytes},{"state",file.state},{"trash_path",file.trashPath}});
    return {{"schema",1},{"project",batch.project},{"directory",batch.directory},{"requested",batch.requested},{"phase",batch.phase},
        {"error",batch.error},{"cancelled",batch.wasCancelled},{"files",files}};
}
bool store(const MoveImportBatch &batch){
    if(batch.journal.isEmpty()||QFileInfo(batch.journal).isSymLink())return false;
    const auto data=QJsonDocument(encode(batch)).toJson(QJsonDocument::Indented);if(data.size()>MaxJournalBytes)return false;
    QSaveFile file(batch.journal);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();
}
bool keys(const QJsonObject &object,const QStringList &expected){const auto actual=object.keys();return QSet<QString>(actual.cbegin(),actual.cend())==QSet<QString>(expected.cbegin(),expected.cend());}
bool savedCopies(const MoveImportBatch &batch,QString *error,int selected=-1){
    ProjectDatabase reader;QVector<ProjectDatabase::SavedPhoto> photos;
    if(!reader.readSnapshot(batch.project,&photos)){*error="Cannot verify the saved move-import catalog";return false;}
    for(int index=0;index<batch.files.size();++index){
        if(selected>=0&&selected!=index)continue;const auto &file=batch.files[index];
        const auto target=family(batch.project,file.copy.destination),source=family(batch.project,file.copy.source);bool saved=false;
        for(const auto &photo:photos){
            const auto path=family(batch.project,photo.path),sourcePath=family(batch.project,photo.sourceKey.isEmpty()?photo.path:photo.sourceKey);
            if(file.state!="trashed"&&(path==source||sourcePath==source)){*error="Source is already referenced by this catalog; use source relinking";return false;}
            if(path==target&&sourcePath==target&&photo.copyKey.isEmpty()&&photo.identity.isKnown()&&photo.identity.sizeBytes==file.bytes
                &&photo.identity.sha256==file.copy.sha256.toLatin1())saved=true;
        }
        if(!saved){*error="Verified destination identity is not yet saved in the catalog";return false;}
    }
    return true;
}
}
int MoveImportBatch::retired()const{int count=0;for(const auto &file:files)count+=file.state=="trashed";return count;}
MoveImportBatch readMoveImport(const QString &path,QString *error){
    if(error)error->clear();MoveImportBatch batch;
    const auto fail=[&](const QString &why){if(error)*error=why;batch.error=why;return batch;};
    const QFileInfo info(path);if(!info.isFile()||info.isSymLink()||info.size()<=0||info.size()>MaxJournalBytes||info.fileName()!="move.json")return fail("Invalid move journal");
    const auto folder=info.dir();if(!folder.dirName().startsWith(".jixellight-move-")||QFileInfo(folder.absolutePath()).isSymLink())return fail("Invalid move journal directory");
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return fail("Cannot read move journal");const auto bytes=file.read(MaxJournalBytes+1);
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject()||bytes.size()>MaxJournalBytes)return fail("Malformed move journal");
    const auto object=document.object();
    if(!keys(object,{"schema","project","directory","requested","phase","error","cancelled","files"})||!object["schema"].isDouble()||object["schema"].toDouble()!=1
        ||!object["project"].isString()||!object["directory"].isString()||!object["requested"].isDouble()||!object["phase"].isString()
        ||!object["error"].isString()||!object["cancelled"].isBool()||!object["files"].isArray())return fail("Unsupported move journal schema");
    batch.journal=info.canonicalFilePath();batch.project=object["project"].toString();batch.directory=object["directory"].toString();batch.requested=object["requested"].toInt(-1);
    batch.phase=object["phase"].toString();batch.error=object["error"].toString();batch.wasCancelled=object["cancelled"].toBool();
    if(!localPath(batch.project)||!localPath(batch.directory)||batch.requested<1||batch.requested>1000||object["requested"].toDouble()!=batch.requested
        ||family(batch.project,folder.absolutePath()+"/..")!=family(batch.project,batch.directory)
        ||!QStringList{"copying","copied","copy-incomplete","retiring","retained","done"}.contains(batch.phase))return fail("Invalid move journal plan");
    const auto entries=object["files"].toArray();if(entries.size()>batch.requested)return fail("Too many move journal files");QSet<QString> sources,targets;
    for(const auto &value:entries){
        if(!value.isObject())return fail("Invalid move journal file");const auto row=value.toObject();
        if(!keys(row,{"source","destination","sha256","bytes","state","trash_path"})||!row["source"].isString()||!row["destination"].isString()
            ||!row["sha256"].isString()||!row["bytes"].isDouble()||!row["state"].isString()||!row["trash_path"].isString())return fail("Invalid move journal file fields");
        MoveImportFile entry{{row["source"].toString(),row["destination"].toString(),row["sha256"].toString()},row["bytes"].toInteger(-1),row["state"].toString(),row["trash_path"].toString()};
        if(!localPath(entry.copy.source)||!localPath(entry.copy.destination)||!digest(entry.copy.sha256)||entry.bytes<0||row["bytes"].toDouble()!=double(entry.bytes)
            ||(!entry.trashPath.isEmpty()&&!localPath(entry.trashPath))||!QStringList{"copied","retiring","retained","trashed","review"}.contains(entry.state)
            ||family(batch.project,QFileInfo(entry.copy.destination).absolutePath())!=family(batch.project,batch.directory))return fail("Invalid move journal file plan");
        const auto source=family(batch.project,entry.copy.source),target=family(batch.project,entry.copy.destination);
        if(source==target||sources.contains(source)||targets.contains(target))return fail("Duplicated move source or destination");sources.insert(source);targets.insert(target);batch.files.push_back(std::move(entry));
    }
    for(const auto &source:sources)if(targets.contains(source))return fail("Move destinations overlap sources");
    batch.recordDigest=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return batch;
}
MoveImportBatch prepareMoveImport(const QStringList &sources,const QString &directory,const QString &project,const CancelToken &cancel,
    const CopyImportProgress &progress,const ImportNaming &naming,const QStringList &captureTimes){
    MoveImportBatch batch;batch.requested=sources.size();batch.project=QFileInfo(project).canonicalFilePath();batch.directory=QFileInfo(directory).canonicalFilePath();
    const auto fail=[&](const QString &error){batch.error=error;batch.wasCancelled=cancelled(cancel);return batch;};
    if(cancelled(cancel))return fail("Move import cancelled");
    if(batch.requested<1||batch.requested>1000||!QFileInfo(batch.directory).isDir()||!QFileInfo(batch.directory).isWritable())return fail("Choose an existing writable destination and1–1000 files");
    ProjectDatabase reader;QVector<ProjectDatabase::SavedPhoto> saved;if(batch.project.isEmpty()||!reader.readSnapshot(batch.project,&saved))return fail("Save a project before move import");
    QSet<QString> catalogSources;for(const auto &photo:saved){catalogSources.insert(family(batch.project,photo.path));catalogSources.insert(family(batch.project,photo.sourceKey.isEmpty()?photo.path:photo.sourceKey));}
    for(const auto &source:sources){
        if(!regular(source))return fail("Move requires readable regular source files; source symlinks are not retired");
        if(catalogSources.contains(family(batch.project,QFileInfo(source).canonicalFilePath())))return fail("Source is already referenced by this catalog; use source relinking");
    }
    QTemporaryDir journal(QDir(batch.directory).filePath(".jixellight-move-XXXXXX"));if(!journal.isValid())return fail("Cannot create move recovery directory");
    batch.journal=journal.filePath("move.json");QLockFile lock(batch.journal+".lock");if(!lock.tryLock(0))return fail("Move recovery directory is busy");
    if(!store(batch))return fail("Cannot preserve the initial move recovery journal");journal.setAutoRemove(false);
    const auto result=copyImportFiles(sources,batch.directory,cancel,progress,naming,captureTimes,[&](const CopyImportFile &copy){
        batch.files.push_back({copy,QFileInfo(copy.destination).size(),"copied",{}});return store(batch);
    });
    batch.error=result.error;batch.wasCancelled=result.wasCancelled;batch.phase=(!result.error.isEmpty()||result.wasCancelled)?"copy-incomplete":"copied";
    if(!store(batch)){if(batch.error.isEmpty())batch.error="Cannot preserve completed move copies; sources retained";}
    else batch.recordDigest=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(encode(batch)).toJson(QJsonDocument::Indented),QCryptographicHash::Sha256).toHex());
    return batch;
}
MoveImportBatch retireMoveImport(const QString &journal,const CancelToken &cancel,const CopyImportProgress &progress,const MoveImportTrash &trash,const QString &expectedDigest){
    QString parse;auto batch=readMoveImport(journal,&parse);if(!parse.isEmpty())return batch;
    // Validate before creating any sibling lock, then reread under the lock.
    const auto canonical=batch.journal;QLockFile lock(canonical+".lock");
    if(!lock.tryLock(0)){batch.error="Move recovery journal is busy";return batch;}
    batch=readMoveImport(canonical,&parse);if(!parse.isEmpty())return batch;
    if(!expectedDigest.isEmpty()&&batch.recordDigest!=expectedDigest){batch.error="Move recovery record changed after review; inspect it again";return batch;}
    const auto fail=[&](const QString &why){batch.error=why;batch.wasCancelled=cancelled(cancel);batch.phase="retained";store(batch);return batch;};
    if(batch.files.isEmpty())return fail("No verified copies in this move recovery journal");
    if(cancelled(cancel))return fail("Move source retirement cancelled; sources retained");
    if(!savedCopies(batch,&parse))return fail(parse);
    for(const auto &file:batch.files)if(file.state=="review")return fail("A raced source needs manual recovery review; remaining sources retained");
    // Verify the complete selected set before the first recoverable retirement.
    for(const auto &file:batch.files){if(!matches(file.copy.destination,file.bytes,file.copy.sha256,cancel))return fail("Saved move destination changed or is unreadable");
        if(file.state!="trashed"&&!matches(file.copy.source,file.bytes,file.copy.sha256,cancel))return fail("Move source changed or is missing; remaining sources retained");}
    batch.phase="retiring";batch.error.clear();batch.wasCancelled=false;if(!store(batch))return fail("Cannot preserve source-retirement intent");
    for(int index=0;index<batch.files.size();++index){
        auto &file=batch.files[index];if(file.state=="trashed")continue;
        if(cancelled(cancel))return fail("Move source retirement cancelled; remaining sources retained");
        if(!savedCopies(batch,&parse,index))return fail(parse);
        if(!matches(file.copy.destination,file.bytes,file.copy.sha256,cancel)||!matches(file.copy.source,file.bytes,file.copy.sha256,cancel))return fail("Move bytes changed before source retirement");
        file.state="retiring";if(!store(batch))return fail("Cannot record source-retirement intent");
        QString trashed;const bool retired=trash?trash(file.copy.source,&trashed):QFile::moveToTrash(file.copy.source,&trashed);
        if(!retired){file.state="retained";return fail("Cannot move source to the system trash; verified copy and source retained");}
        file.state="trashed";file.trashPath=clean(trashed);if(trashed.isEmpty())file.trashPath.clear();
        if(QFileInfo::exists(file.copy.source)||QFileInfo(file.copy.source).isSymLink()){
            return fail("Source path changed during trash operation; recovery record retained");}
        if(!file.trashPath.isEmpty()&&!matches(file.trashPath,file.bytes,file.copy.sha256,{})){
            // A raced replacement is recoverable; never overwrite a new source.
            const bool restored=!QFileInfo::exists(file.copy.source)&&!QFileInfo(file.copy.source).isSymLink()&&QFile::rename(file.trashPath,file.copy.source);
            file.state=restored?"retained":"review";if(restored)file.trashPath.clear();return fail("Trashed source bytes differ; attempted safe source restoration");}
        file.state="trashed";if(!store(batch))return fail("Source is in system trash; cannot update its recovery journal");
        if(progress)progress(batch.retired(),batch.files.size(),batch.retired(),batch.files.size(),"trash");
    }
    batch.phase="done";batch.error.clear();if(!store(batch))batch.error="Move complete but final recovery record could not be written";return batch;
}
