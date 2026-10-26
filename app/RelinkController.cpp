#include "app/PhotoController.h"
#include "core/cache/SourceCache.h"
#include "core/files/FileNames.h"
#include "core/raw/RawDecoder.h"
#include "core/image/ProcessedImageProvider.h"
#include "diagnostics/ActionTrace.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
ProjectDatabase::OriginalIdentity hashSource(const QString &path,const CancelToken &cancel,QString *error) {
    ProjectDatabase::OriginalIdentity identity;
    QFile file(path);
    const QFileInfo before(path);
    if (!before.isFile() || !before.isReadable() || before.size()<=0 || before.size()>2LL*1024*1024*1024 || !file.open(QIODevice::ReadOnly)) {
        if (error) *error="Candidate must be a readable photograph of at most 2 GiB";
        return identity;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 bytes=0;
    while (!file.atEnd()) {
        if (cancelled(cancel)) return {};
        const auto chunk=file.read(1024*1024);
        if (chunk.isEmpty()) { if(error)*error="Cannot read complete source"; return {}; }
        bytes+=chunk.size(); hash.addData(chunk);
    }
    const QFileInfo after(path);
    if (cancelled(cancel)) return {};
    if (bytes!=before.size() || after.size()!=before.size() || after.lastModified()!=before.lastModified()
        || after.canonicalFilePath()!=before.canonicalFilePath()) {
        if(error)*error="Source changed during verification";
        return {};
    }
    identity.sizeBytes=bytes; identity.sha256=hash.result().toHex();
    return identity;
}
bool sameIdentity(const ProjectDatabase::OriginalIdentity &a,const ProjectDatabase::OriginalIdentity &b) {
    return a.isKnown() && b.isKnown() && a.sizeBytes==b.sizeBytes && a.sha256==b.sha256;
}
}

QString PhotoController::sourceFamily(const PhotoEntry &photo) const {
    return ProjectDatabase::normalizedSourcePath(m_project.projectPath(),photo.sourceKey.isEmpty()?photo.path:photo.sourceKey);
}
QStringList PhotoController::familyVersionKeys(const QString &path) const {
    QStringList keys;
    for (const auto &photo:m_photos) if (sourceFamily(photo)==path) keys.append(photo.storageKey());
    keys.sort(); return keys;
}
bool PhotoController::sourceReadable(const PhotoEntry &photo) const {
    const QFileInfo info(photo.path);
    return info.isFile() && info.isReadable() && !info.canonicalFilePath().isEmpty();
}

void PhotoController::initializeRelinkJobs() {
    m_relinkJob=std::make_unique<LatestJob<RelinkRequest,RelinkResult>>(
        [](const RelinkRequest &request,const CancelToken &cancel) {
            RelinkResult result;
            result.identity=hashSource(request.candidatePath,cancel,&result.error);
            if (!result.identity.isKnown() || cancelled(cancel)) return result;
            if ((request.expected.isKnown() && !sameIdentity(request.expected,result.identity))
                || (request.confirming && !sameIdentity(request.candidateIdentity,result.identity))) {
                result.error="Candidate content does not match the verified source identity"; return result;
            }
            // A decode is required even when the old cache key, size and mtime
            // happen to match. Neither validation pass reads or publishes caches.
            result.source=loadSourceFresh(request.candidatePath,cancel);
            if (cancelled(cancel)) return RelinkResult{};
            if (result.source.image.isNull() || !result.source.fullResolution) {
                result.error=result.source.error.isEmpty()?QStringLiteral("Cannot decode candidate photograph"):result.source.error;
                return result;
            }
            const auto after=hashSource(request.candidatePath,cancel,&result.error);
            if (!sameIdentity(result.identity,after)) {
                result.source={};
                if(result.error.isEmpty())result.error="Candidate changed while being decoded";
                return result;
            }
            result.verifiedKey=result.source.key;
            if (!request.confirming) result.source={}; // Don't retain a RAW frame while a dialog is open.
            return result;
        },[this](const RelinkRequest &request,RelinkResult result) {
            if (m_closing || !m_relinkRequest || request.id!=m_relinkRequest->id || request.epoch!=m_catalogEpoch) return;
            m_relinkBusy=false;
            if (familyVersionKeys(request.sourcePath)!=request.versionKeys) result.error="Source versions changed; reopen relink";
            if (!result.identity.isKnown() && result.error.isEmpty()) result.error="Source verification failed";
            if(result.error.isEmpty() && result.verifiedKey!=SourceCache::fileKey(request.candidatePath))result.error="Candidate changed after verification; reopen relink";
            if (!result.error.isEmpty()) {
                m_relinkPreview["status"]="error"; m_relinkPreview["error"]=result.error;
                setStatus(uiText("源文件校验失败：", "Source verification failed: ")+result.error);
                emit relinkChanged(); return;
            }
            if (!request.confirming) {
                m_relinkRequest->candidateIdentity=result.identity;
                m_relinkPreview["status"]="ready";
                m_relinkPreview["identityKnown"]=request.expected.isKnown();
                m_relinkPreview["sha256"]=QString::fromLatin1(result.identity.sha256);
                m_relinkPreview["sizeBytes"]=result.identity.sizeBytes;
                emit relinkChanged(); return;
            }
            if (exportBusy() || copyImportBusy() || !flushEdits()) {
                m_relinkPreview["status"]="error"; m_relinkPreview["error"]="Finish active file operations and retry relink";
                emit relinkChanged(); return;
            }
            if(result.verifiedKey!=SourceCache::fileKey(request.candidatePath)) {
                m_relinkPreview["status"]="error";m_relinkPreview["error"]="Candidate changed before catalog commit; reopen relink";
                emit relinkChanged();return;
            }
            ProjectDatabase::RelinkRequest change;
            change.sourceKey=request.sourceKey; change.candidatePath=request.candidatePath;
            change.candidateIdentity=result.identity; change.acknowledgeUnknownIdentity=request.acknowledgeUnknown;
            if (!m_project.relinkSource(change)) {
                m_relinkPreview["status"]="error"; m_relinkPreview["error"]=m_project.lastError();
                setStatus(uiText("重新关联未保存：", "Relink was not saved: ")+m_project.lastError());
                emit relinkChanged(); return;
            }
            // Only a committed transaction may alter the live family/caches.
            m_loader->cancel(); m_prefetch->cancel(); m_prepare->cancel(); m_render->cancel();
            m_scopeJob->cancel(); m_fullScopeJob->cancel(); m_plotJob->cancel();
            ++m_catalogEpoch; m_catalogDatesJob->cancel(); m_catalogDateTimer.stop();
            m_identityJob->cancel(); m_identityPending.clear();
            m_sourceCache->invalidatePath(request.candidatePath);
            result.source.key=SourceCache::fileKey(request.candidatePath);
            m_sourceCache->put(result.source);
            const auto thumbnail=++m_thumbnailRevision;
            for (auto &photo:m_photos) if (sourceFamily(photo)==request.sourcePath) {
                photo.path=request.candidatePath; photo.sourceKey=request.candidatePath;
                if (photo.copyKey.isEmpty()) photo.originalKey=request.candidatePath;
                photo.name=QFileInfo(photo.path).fileName()+(photo.copyKey.isEmpty()?QString():QStringLiteral(" · ")+photo.versionName);
                photo.raw=RawDecoder::isRawFile(photo.path); photo.available=true; photo.thumbnailRevision=thumbnail;
            }
            m_identityEstablished.insert(request.candidatePath);
            m_importedPaths.clear();
            for(const auto &photo:m_photos) {
                const auto canonical=QFileInfo(photo.path).canonicalFilePath();
                if(!canonical.isEmpty())m_importedPaths.insert(canonical);
            }
            const auto selection=m_selectedPhotos; const auto current=m_currentIndex;
            m_currentIndex=-2; selectPhoto(current); m_selectedPhotos=selection;
            m_catalogDateTimer.start();
            m_relinkPreview["status"]="complete";
            m_relinkRequest.reset();
            emit libraryChanged(); emit projectChanged(); emit relinkChanged();
            ActionTrace::instance().record("source_relinked",{{"versions",request.versionKeys.size()},{"verified_identity",request.expected.isKnown()}});
            setStatus(uiText("已重新关联源文件及其全部版本。", "Source and all its versions relinked."));
        });
    m_identityJob=std::make_unique<LatestJob<IdentityRequest,QHash<QString,ProjectDatabase::OriginalIdentity>>>(
        [](const IdentityRequest &request,const CancelToken &cancel) {
            QHash<QString,ProjectDatabase::OriginalIdentity> result;
            for (const auto &path:request.paths) {
                if(cancelled(cancel))return result;
                const auto identity=hashSource(path,cancel,nullptr);
                if(identity.isKnown())result.insert(path,identity);
            }
            return result;
        },[this](const IdentityRequest &request,QHash<QString,ProjectDatabase::OriginalIdentity> result) {
            if(m_closing || request.epoch!=m_catalogEpoch || !m_project.isOpen())return;
            // Imports may still exist only in the controller's dirty maps.
            // Publish those rows before recording a background identity.
            // A failed save keeps pending paths for a later explicit retry.
            if(!flushEdits())return;
            for(const auto &path:request.paths)m_identityPending.remove(path);
            for(auto it=result.cbegin();it!=result.cend();++it) {
                const auto photo=std::find_if(m_photos.cbegin(),m_photos.cend(),[&](const auto &p){return p.path==it.key();});
                if(photo!=m_photos.cend() && m_project.recordOriginalIdentity(photo->sourceKey.isEmpty()?photo->path:photo->sourceKey,it.value()))
                    m_identityEstablished.insert(it.key());
            }
            startSourceIdentities();
        });
}

void PhotoController::queueSourceIdentity(const QString &path) {
    if (!m_project.isOpen() || m_identityEstablished.contains(path))return;
    m_identityPending.insert(path); startSourceIdentities();
}
void PhotoController::startSourceIdentities() {
    if(m_closing || !m_project.isOpen() || m_identityPending.isEmpty() || m_identityJob->busy())return;
    auto paths=m_identityPending.values(); paths.sort(); m_identityJob->submit({m_catalogEpoch,paths});
}
void PhotoController::cancelRelink() {
    if(m_relinkJob)m_relinkJob->cancel();
    ++m_nextRelinkId; m_relinkRequest.reset(); m_relinkBusy=false; m_relinkPreview.clear(); emit relinkChanged();
}
bool PhotoController::beginRelink(const QString &versionKey,const QUrl &candidate,const QString &catalogToken) {
    if(!catalogToken.isEmpty() && catalogToken!=relinkCatalogToken())return false;
    if(!m_project.isOpen() || !candidate.isLocalFile() || exportBusy() || copyImportBusy())return false;
    const auto entry=std::find_if(m_photos.cbegin(),m_photos.cend(),[&](const auto &p){return p.storageKey()==versionKey;});
    if(entry==m_photos.cend())return false;
    const QFileInfo info(candidate.toLocalFile());
    if(!info.isFile() || !info.isReadable() || info.canonicalFilePath().isEmpty())return false;
    cancelRelink();
    auto request=std::make_unique<RelinkRequest>();
    request->id=++m_nextRelinkId; request->epoch=m_catalogEpoch;
    request->sourceKey=entry->sourceKey.isEmpty()?entry->path:entry->sourceKey;
    request->sourcePath=sourceFamily(*entry); request->candidatePath=info.canonicalFilePath();
    request->versionKeys=familyVersionKeys(request->sourcePath);
    if(!m_project.originalIdentity(request->sourceKey,&request->expected))return false;
    m_relinkPreview={{"requestId",QString::number(request->id)},{"status","validating"},{"oldPath",entry->path},
                     {"candidatePath",request->candidatePath},{"versionCount",request->versionKeys.size()},
                     {"identityKnown",request->expected.isKnown()},{"error",QString()}};
    m_relinkBusy=true; m_relinkRequest=std::move(request);
    m_relinkJob->submit(*m_relinkRequest); emit relinkChanged(); return true;
}
bool PhotoController::confirmRelink(const QString &requestId,bool acknowledgeUnknownIdentity) {
    if(!m_relinkRequest || m_relinkBusy || requestId!=QString::number(m_relinkRequest->id)
        || m_relinkPreview.value("status").toString()!="ready" || m_relinkRequest->epoch!=m_catalogEpoch
        || familyVersionKeys(m_relinkRequest->sourcePath)!=m_relinkRequest->versionKeys
        || (!m_relinkRequest->expected.isKnown() && !acknowledgeUnknownIdentity)
        || exportBusy() || copyImportBusy() || !flushEdits())return false;
    m_relinkRequest->confirming=true; m_relinkRequest->acknowledgeUnknown=acknowledgeUnknownIdentity;
    m_relinkBusy=true; m_relinkPreview["status"]="confirming";
    m_relinkJob->submit(*m_relinkRequest); emit relinkChanged(); return true;
}
