#include "app/PhotoController.h"
#include "app/XmpMergeReviewDialog.h"
#include "core/metadata/XmpCapture.h"
#include "diagnostics/ActionTrace.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QProgressDialog>
#include <QThread>

namespace {
ProjectDatabase::OriginalIdentity verifiedOriginal(const QString &path,const CancelToken &cancel,QString *error) {
    ProjectDatabase::OriginalIdentity identity;
    QFile file(path);const QFileInfo before(path);
    if(!before.isFile()||!before.isReadable()||before.size()<=0||before.size()>2LL*1024*1024*1024||!file.open(QIODevice::ReadOnly)) {
        *error="Original must be a readable photograph of at most 2 GiB";return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);qint64 bytes=0;
    while(!file.atEnd()) {
        if(cancelled(cancel))return {};
        const auto chunk=file.read(1024*1024);
        if(chunk.isEmpty()){*error="Cannot verify the complete original";return {};}
        bytes+=chunk.size();hash.addData(chunk);
    }
    const QFileInfo after(path);
    if(cancelled(cancel))return {};
    if(bytes!=before.size()||after.size()!=before.size()||after.lastModified()!=before.lastModified()
       ||after.canonicalFilePath()!=before.canonicalFilePath()) {
        *error="Original changed during verification";return {};
    }
    identity.sizeBytes=bytes;identity.sha256=hash.result().toHex();return identity;
}
}

bool PhotoController::xmpMergeBusy() const {return bool(m_xmpMerge);}
QString PhotoController::xmpMergeOperationId() const {return m_xmpMerge?m_xmpMerge->operationId:QString{};}
XmpCapture::Target PhotoController::xmpMergeTarget() const {
    if(!hasImage()||!m_project.isOpen())return {};
    const auto &photo=m_photos[m_currentIndex];
    return {m_project.projectPath(),photo.sourceKey.isEmpty()?photo.path:photo.sourceKey,
        photo.storageKey(),photo.copyKey,QString::fromLatin1(photo.identity.sha256),m_xmpOwner,
        m_catalogEpoch,m_photoEpoch,m_xmpSequence};
}
QJsonObject PhotoController::xmpMergeGuiSnapshot() const {
    if(!hasImage())return {};
    const auto &p=m_photos[m_currentIndex];
    return {{"project",m_project.projectPath()},{"catalog_epoch",QString::number(m_catalogEpoch)},
        {"photo_epoch",QString::number(m_photoEpoch)},{"source",p.sourceKey},{"path",p.path},{"storage",p.storageKey()},
        {"copy",p.copyKey},{"original_sha256",QString::fromLatin1(p.identity.sha256)},
        {"original_bytes",QString::number(p.identity.sizeBytes)},{"develop",p.state.toJson()},
        {"history",p.history.toJson()},{"tags",p.tags.toJson()},{"rating",p.rating},{"flag",p.flag},
        {"timeline",p.timeline.toJson()}};
}
bool PhotoController::xmpMergeCurrent() const {
    return !m_closing&&m_xmpMerge&&hasImage()&&m_xmpMerge->target==xmpMergeTarget()
        &&m_xmpMerge->originalPath==currentFile()&&m_xmpMerge->guiBefore==xmpMergeGuiSnapshot();
}
void PhotoController::showXmpMergeProgress(const QString &message,bool cancelable) {
    if(!m_xmpMerge)return;
    if(m_xmpMerge->progress) {
        m_xmpMerge->progress->setLabelText(message);
        if(!cancelable)m_xmpMerge->progress->setCancelButton(nullptr);
        m_xmpMerge->progress->show();return;
    }
    auto *progress=new QProgressDialog(message,cancelable?uiText("取消","Cancel"):QString{},0,0,nullptr);
    progress->setObjectName("xmp-merge-progress");progress->setWindowTitle(uiText("导入 XMP","Import XMP"));
    progress->setWindowModality(Qt::ApplicationModal);progress->setMinimumDuration(0);
    progress->setAutoClose(false);progress->setAutoReset(false);
    if(!cancelable)progress->setCancelButton(nullptr);
    m_xmpMerge->progress=progress;
    connect(progress,&QProgressDialog::canceled,this,[this] {cancelXmpMerge();});
    progress->show();
}
void PhotoController::finishXmpMerge(const QString &message,bool applyDeferredMetadata) {
    if(!m_xmpMerge)return;
    const auto deferred=m_xmpMerge->deferredCaptureTimes;
    const auto target=m_xmpMerge->target;QPointer<PhotoController> owner(this);
    if(m_xmpSourceJob)m_xmpSourceJob->cancel();
    if(m_xmpMerge->dialog) {
        QObject::disconnect(m_xmpMerge->dialog.data(),nullptr,this,nullptr);
        m_xmpMerge->dialog->reject();m_xmpMerge->dialog->deleteLater();
    }
    if(m_xmpMerge->progress) {
        QObject::disconnect(m_xmpMerge->progress.data(),nullptr,this,nullptr);
        m_xmpMerge->progress->close();m_xmpMerge->progress->deleteLater();
    }
    m_xmpMerge.reset();++m_xmpSequence;setStatus(message);
    if(!owner)return;
    emit xmpMergeChanged();if(!owner)return;
    emit historyChanged();if(!owner)return;
    emit activityChanged();if(!owner)return;
    // Only a successful durable reload may create new queued metadata saves.
    // Cancellation/refusal never requeues an old UI row over a changed catalog.
    if(applyDeferredMetadata&&!xmpMergeBusy()&&hasImage()&&m_project.projectPath()==target.projectLocator
       &&m_catalogEpoch==target.catalogEpoch&&m_photoEpoch==target.photoEpoch
       &&m_photos[m_currentIndex].storageKey()==target.storageKey)
        for(auto i=deferred.constBegin();i!=deferred.constEnd();++i)updateCaptureTime(i.key(),i.value());
}
bool PhotoController::cancelXmpMerge() {
    if(QThread::currentThread()!=thread()||m_closing)return false;
    if(!m_xmpMerge)return true;
    return executeOperationCancel({{"command","xmp.cancel"},{"operation_id",xmpMergeOperationId()}});
}
bool PhotoController::cancelXmpMergeImpl() {
    if(!m_xmpMerge)return true;
    if(m_xmpMerge->phase==XmpMergePhase::Committing||m_xmpMerge->phase==XmpMergePhase::ReloadRequired)return false;
    finishXmpMerge(uiText("XMP 导入已取消，未应用所选字段。","XMP import cancelled; selected fields were not applied."));return true;
}
void PhotoController::shutdownXmpMerge() {
    // Teardown may join the existing bounded job; ordinary interaction never
    // waits for a pending CPU future. Preserve the barrier through flushEdits:
    // an already authorized durable commit must not be followed by old UI rows.
    if(m_xmpMerge) {
        if(m_xmpMerge->dialog){QObject::disconnect(m_xmpMerge->dialog.data(),nullptr,this,nullptr);delete m_xmpMerge->dialog.data();}
        if(m_xmpMerge->progress){QObject::disconnect(m_xmpMerge->progress.data(),nullptr,this,nullptr);delete m_xmpMerge->progress.data();}
    }
    m_xmpSourceJob.reset();
}
void PhotoController::initializeXmpMerge() {
    m_xmpSourceJob=std::make_unique<LatestJob<XmpSourceRequest,XmpSourceResult>>(
        [](const XmpSourceRequest &request,const CancelToken &cancel) {
            XmpSourceResult result;result.workerThread=QThread::currentThread()!=request.ownerThread;
            if(!result.workerThread){result.error="Original and sidecar verification requires a CPU worker";return result;}
            result.actualOriginal=verifiedOriginal(request.capture.protectedPhotoPath,cancel,&result.error);
            if(cancelled(cancel))return result;
            if(!result.actualOriginal.isKnown()||result.actualOriginal!=request.original) {
                if(result.error.isEmpty())result.error="Original differs from its verified catalog identity";return result;
            }
            if(request.verifyOnly)result.sidecar=request.alreadyCaptured;
            else {auto capture=XmpCapture::capture(request.capture,cancel);result.sidecar=std::move(capture.sidecar);result.error=capture.error;}
            if(!cancelled(cancel)&&result.error.isEmpty()&&result.sidecar.valid())
                ActionTrace::instance().record("xmp_merge_source_verified",{{"project",request.capture.target.projectLocator},
                    {"storage_key",request.capture.target.storageKey},{"source_identity",QString::fromLatin1(result.actualOriginal.sha256)},
                    {"original_bytes",QString::number(result.actualOriginal.sizeBytes)},{"sidecar_sha256",QString::fromLatin1(result.sidecar.sha256())},
                    {"phase",request.verifyOnly?"precommit":"review"},{"sequence",QString::number(request.capture.target.sequence)},
                    {"controller",request.capture.target.controller.toString(QUuid::WithoutBraces)},
                    {"catalog_epoch",QString::number(request.capture.target.catalogEpoch)},{"photo_epoch",QString::number(request.capture.target.photoEpoch)},
                    {"worker_thread",QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()),16)}});
            return result;
        },[this](const XmpSourceRequest &request,XmpSourceResult result) {
            if(m_closing||!m_xmpMerge||request.capture.target!=m_xmpMerge->target)return;
            if(!xmpMergeCurrent()){finishXmpMerge(uiText("版本已更改，请重新检查 XMP。","The version changed; review the XMP again."));return;}
            if(!result.workerThread||!result.error.isEmpty()||!result.sidecar.valid()
                ||result.actualOriginal!=m_xmpMerge->original||result.sidecar.target()!=m_xmpMerge->target) {
                finishXmpMerge(uiText("XMP 未导入：","XMP not imported: ")+result.error);return;
            }
            if(request.verifyOnly) {commitXmpMergeAfterVerification();return;}
            m_xmpMerge->sidecar=std::move(result.sidecar);
            const auto target=m_xmpMerge->target;
            if(!m_project.reviewCapturedXmp(m_xmpMerge->sidecar,this,[this,target](ProjectDatabase::XmpReviewRecord record,QString error) {
                if(m_closing||!m_xmpMerge||m_xmpMerge->target!=target)return;
                if(!xmpMergeCurrent()||!record.valid||!error.isEmpty()) {
                    finishXmpMerge(uiText("XMP 检查失败：","XMP review failed: ")+error);return;
                }
                const auto &photo=m_photos[m_currentIndex];
                if(record.photo.adjustments.toJson()!=photo.state.toJson()||record.photo.history.toJson()!=photo.history.toJson()
                   ||record.photo.tags.toJson()!=photo.tags.toJson()||record.photo.rating!=photo.rating||record.photo.flag!=photo.flag
                   ||record.photo.timeline.toJson()!=photo.timeline.toJson()) {
                    finishXmpMerge(uiText("目录与当前版本不一致，请重新打开项目后检查。","The catalog differs from this version; reopen the project before reviewing."));return;
                }
                showXmpMergeReview(std::move(record));
            }))finishXmpMerge(uiText("目录不可用，XMP 未导入。","Catalog unavailable; XMP was not imported."));
        });
}
bool PhotoController::requestXmpMerge(const QUrl &source) {
    return source.isLocalFile()&&executeXmpCommand({{"command","xmp.review"},{"source",source.toLocalFile()}});
}
bool PhotoController::requestXmpMergeImpl(const QUrl &source) {
    if(QThread::currentThread()!=thread()||m_closing||xmpMergeBusy()||!hasImage()||!source.isLocalFile()||!m_project.isOpen())return false;
    if(m_brushGesture||m_brushRecovery||m_cropEditing||m_loading||m_preparing||m_calibrationBusy||m_relinkBusy||copyImportBusy()) {
        setStatus(uiText("请先完成当前操作，再检查 XMP。","Finish the current operation before reviewing XMP."));return false;
    }
    if(!m_photos[m_currentIndex].available||!m_photos[m_currentIndex].identity.isKnown()) {
        setStatus(uiText("原图尚未完成验证或不可用，请稍后重试。","The original is unavailable or not yet verified; try again after verification."));return false;
    }
    // Ending an interaction emits synchronous Qt signals. Keep no PhotoEntry
    // reference across those callbacks, and never retarget this import to the
    // photo selected by a reentrant observer.
    const auto before=xmpMergeGuiSnapshot();QPointer<PhotoController> owner(this);
    finishInteraction();if(!owner)return false;
    if(before!=xmpMergeGuiSnapshot()||m_closing||xmpMergeBusy())return false;
    enqueueEdits();if(!owner)return false;
    if(before!=xmpMergeGuiSnapshot()||m_closing||xmpMergeBusy())return false;
    if(!m_dirtyEdits.isEmpty()||!m_dirtyHistories.isEmpty()||!m_dirtyCuration.isEmpty()||!m_dirtyTags.isEmpty()||!m_dirtyDates.isEmpty()) {
        setStatus(uiText("请先解决项目保存问题，再检查 XMP。","Resolve pending project saves before reviewing XMP."));return false;
    }
    ++m_xmpSequence;const auto target=xmpMergeTarget();if(!target.valid())return false;
    auto session=std::make_unique<XmpMergeSession>();session->target=target;session->originalPath=currentFile();
    session->original=m_photos[m_currentIndex].identity;session->guiBefore=xmpMergeGuiSnapshot();
    const XmpSourceRequest request{{target,source.toLocalFile(),session->originalPath},session->original,{},thread(),false};
    m_xmpMerge=std::move(session);
    ActionTrace::instance().record("xmp_review_started",{{"operation_id",xmpMergeOperationId()},{"project",target.projectLocator},{"version_key",target.storageKey}});
    const auto current=[owner,target] {return owner&&owner->m_xmpMerge&&owner->m_xmpMerge->target==target
        &&owner->m_xmpMerge->phase==XmpMergePhase::Reading&&owner->xmpMergeCurrent();};
    emit xmpMergeChanged();if(!current())return false;
    emit historyChanged();if(!current())return false;
    emit activityChanged();if(!current())return false;
    showXmpMergeProgress(uiText("正在后台验证原图与读取侧车文件…","Verifying original and reading sidecar in background…"),true);
    if(!current())return false;
    m_xmpSourceJob->submit(request);
    return true;
}
void PhotoController::showXmpMergeReview(ProjectDatabase::XmpReviewRecord record) {
    if(!xmpMergeCurrent())return;
    if(m_xmpMerge->progress){QObject::disconnect(m_xmpMerge->progress.data(),nullptr,this,nullptr);m_xmpMerge->progress->close();m_xmpMerge->progress->deleteLater();m_xmpMerge->progress=nullptr;}
    m_xmpMerge->review=std::move(record);m_xmpMerge->phase=XmpMergePhase::Reviewing;
    auto *dialog=new XmpMergeReviewDialog(m_xmpMerge->review.plan,m_language=="en_US",m_xmpMerge->sidecar.document().warnings,nullptr);
    dialog->setWindowModality(Qt::ApplicationModal);m_xmpMerge->dialog=dialog;
    connect(dialog,&QDialog::rejected,this,[this] {cancelXmpMerge();});
    connect(dialog,&QDialog::accepted,this,[this] {
        if(!xmpMergeCurrent()||m_xmpMerge->phase!=XmpMergePhase::Reviewing){cancelXmpMerge();return;}
        auto *review=static_cast<XmpMergeReviewDialog*>(m_xmpMerge->dialog.data());const auto choices=review->resolutions();
        if(!choices){cancelXmpMerge();return;}
        m_xmpMerge->choices=*choices;m_xmpMerge->phase=XmpMergePhase::Verifying;
        const auto target=m_xmpMerge->target;QPointer<PhotoController> owner(this);
        const XmpSourceRequest request{{target,m_xmpMerge->sidecar.locator(),m_xmpMerge->originalPath},m_xmpMerge->original,m_xmpMerge->sidecar,thread(),true};
        showXmpMergeProgress(uiText("正在重新验证原图…","Rechecking original before saving…"),true);
        if(owner&&m_xmpMerge&&m_xmpMerge->target==target&&m_xmpMerge->phase==XmpMergePhase::Verifying&&xmpMergeCurrent())
            m_xmpSourceJob->submit(request);
    });
    dialog->show();
}
void PhotoController::commitXmpMergeAfterVerification() {
    if(!xmpMergeCurrent()||m_xmpMerge->phase!=XmpMergePhase::Verifying)return;
    m_xmpMerge->phase=XmpMergePhase::Committing;
    QPointer<PhotoController> owner(this);
    const auto target=m_xmpMerge->target;
    showXmpMergeProgress(uiText("正在保存所选 XMP 字段…","Saving selected XMP fields…"),false);
    if(!owner||!m_xmpMerge||m_xmpMerge->target!=target||!xmpMergeCurrent())return;
    if(!m_project.commitCapturedXmp(m_xmpMerge->sidecar,m_xmpMerge->review,m_xmpMerge->choices,this,
        [this,target](bool durable,ProjectDatabase::XmpReviewRecord saved,QString error) {
            if(m_closing||!m_xmpMerge||m_xmpMerge->target!=target)return;
            if(!durable) {finishXmpMerge(uiText("XMP 未应用：","XMP not applied: ")+error);return;}
            if(!xmpMergeCurrent()||!saved.valid||saved.target!=target) {
                // A committed transaction is not an uncommitted retry. Keep
                // edits/old-row saves blocked; normal close flushes SQL only.
                m_xmpMerge->phase=XmpMergePhase::ReloadRequired;
                if(m_xmpMerge->progress){m_xmpMerge->progress->close();m_xmpMerge->progress->deleteLater();m_xmpMerge->progress=nullptr;}
                setStatus(uiText("XMP 已保存，但界面无法安全重载。请关闭应用并重新打开项目。","XMP was saved but could not safely reload. Close the app and reopen the project.")+error);
                emit xmpMergeChanged();return;
            }
            auto &photo=m_photos[m_currentIndex];photo.state=saved.photo.adjustments;photo.history=saved.photo.history;
            photo.tags=saved.photo.tags;photo.rating=saved.photo.rating;photo.flag=saved.photo.flag;photo.timeline=saved.photo.timeline;
            const auto key=photo.storageKey();m_dirtyEdits.remove(key);m_dirtyHistories.remove(key);m_dirtyTags.remove(key);m_dirtyCuration.remove(key);m_dirtyDates.remove(key);
            ActionTrace::instance().record("xmp_merge_durable",{{"photo_key",key},{"sidecar_sha256",QString::fromLatin1(saved.sidecarSha256)},{"catalog_epoch",QString::number(m_catalogEpoch)},{"photo_epoch",QString::number(m_photoEpoch)}});
            finishXmpMerge(uiText("所选 XMP 字段已保存。显影可撤销，目录标注不可撤销。","Selected XMP fields saved. Develop is undoable; catalog annotations are not."),true);
            emit libraryChanged();emit curationChanged();emit adjustmentsChanged();emit historyChanged();emit lookChanged();applyCurrent();
        }))finishXmpMerge(uiText("目录不可用，XMP 未应用。","Catalog unavailable; XMP was not applied."));
    else {
        ActionTrace::instance().record("xmp_merge_commit_queued",{{"project",m_project.projectPath()},
            {"storage_key",target.storageKey},{"sequence",QString::number(target.sequence)},
            {"sidecar_sha256",QString::fromLatin1(m_xmpMerge->sidecar.sha256())}});
        // The durable callback is separate. Publish this phase after the
        // existing SQL writer has accepted the authorized transaction.
        emit xmpMergeChanged();
    }
}
