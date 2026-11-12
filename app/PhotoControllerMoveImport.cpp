#include "PhotoController.h"
#include "diagnostics/ActionTrace.h"
#include "core/raw/RawDecoder.h"
#include <QDir>
#include <algorithm>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>

namespace {
QString localizedMoveError(const QString &error,const QString &language){
    if(language!="zh_CN")return error;
    const auto has=[&](const char *key){return error.contains(QLatin1String(key),Qt::CaseInsensitive);};
    if(has("changed after review")||has("inspect it again"))return "恢复记录已发生变化，请重新核对后继续。";
    if(has("regular source")||has("source symlinks"))return "来源无法读取，或是符号链接；请选择可读取的原始文件。";
    if(has("already referenced")||has("outside the current catalog"))return "所选来源已加入当前项目，请使用重新定位功能。";
    if(has("source is in system trash")||has("Move complete"))return "原文件已移至废纸篓，但恢复记录未能更新，请保留副本并核对记录。";
    if(has("source path changed")||has("Trashed source bytes differ"))return "文件在操作期间发生变化，请核对原位置、废纸篓与恢复记录。";
    if(has("trash"))return "无法将原文件移至系统废纸篓；副本及未处理的原文件已保留。";
    if(has("catalog")||has("project"))return "项目尚未完成保存或内容身份校验，未继续移动原文件。";
    if(has("destination")&&has("exist"))return "目标名称已被占用，原文件已保留，请选择其他目标或名称。";
    if(has("destination"))return "副本发生变化或无法读取，未继续移动原文件。";
    if(has("source")&&has("changed"))return "来源发生变化或无法读取，剩余原文件已保留。";
    if(has("cancelled"))return "已取消，副本及未处理的原文件已保留。";
    if(has("busy"))return "恢复记录正在被其他操作使用，请稍后重试。";
    if(has("journal")||has("record"))return "恢复记录无法读取或保存，请核对目标文件夹和恢复记录。";
    return "移动未完成，请核对恢复记录；副本及未处理的原文件已保留。";
}
}

bool PhotoController::moveImport(const QVariantList &urls,const QUrl &directory,const QString &pattern,int sequenceStart){
    if(copyImportBusy()||xmpMergeBusy()||exportBusy()||m_relinkBusy||!m_project.isOpen()||!directory.isLocalFile()||urls.isEmpty()||urls.size()>1000||!flushEdits())return false;
    QStringList sources;for(const auto &value:urls){const auto url=value.canConvert<QUrl>()?value.toUrl():QUrl(value.toString());if(!url.isLocalFile())return false;sources.push_back(url.toLocalFile());}
    const auto preview=previewImportNames(urls,pattern,sequenceStart);if(!preview["valid"].toBool()){setStatus(preview["error"].toString());return false;}
    m_moveImportProject=m_project.projectPath();
    if(!m_moveImportQueue->start(sources,directory.toLocalFile(),m_moveImportProject,{pattern,sequenceStart},preview["captureTimes"].toStringList()))return false;
    m_moveImportFacts={{"phase","copying"},{"project",m_moveImportProject},{"requested",sources.size()}};
    m_copyImportProgress=0;m_copyImportStatus=uiText("检查移动导入计划…","Checking move import plan…");emit copyImportChanged();
    ActionTrace::instance().record("move_import_started",{{"count",sources.size()},{"directory",directory.toLocalFile()},{"project",m_moveImportProject}});return true;
}
void PhotoController::openMoveImportDialog(){
    if(copyImportBusy())return;
    if(!m_project.isOpen()){setStatus(uiText("请先保存项目，再移动导入。","Save a project before move import."));return;}
    QSettings saved;const auto files=QFileDialog::getOpenFileNames(nullptr,uiText("选择要移动并导入的新照片","Choose new photos to move and import"),saved.value("ui/lastImportDir").toString(),uiText("照片与 RAW (*)","Photos and RAW (*)"));
    if(files.isEmpty())return;saved.setValue("ui/lastImportDir",QFileInfo(files.first()).absolutePath());QVariantList urls;for(const auto &file:files)urls.push_back(QUrl::fromLocalFile(file));emit moveImportRequested(urls);
}
bool PhotoController::resumeMoveImport(const QString &journal){
    if(copyImportBusy()||xmpMergeBusy()||exportBusy()||m_relinkBusy||!m_project.isOpen()||!flushEdits())return false;
    const QUrl url(journal);const auto path=url.isLocalFile()?url.toLocalFile():journal;if(!QDir::isAbsolutePath(path))return false;
    m_moveImportProject=m_project.projectPath();if(!m_moveRecoveryView["valid"].toBool()||QFileInfo(path).absoluteFilePath()!=m_moveRecoveryPath)return false;
    if(!m_moveImportQueue->resume(path,m_moveRecoveryView["recordDigest"].toString()))return false;
    m_moveImportFacts={{"phase","reviewing"},{"project",m_moveImportProject},{"journal",path}};
    m_copyImportProgress=0;m_copyImportStatus=uiText("核对移动恢复记录…","Checking move recovery record…");emit copyImportChanged();return true;
}
void PhotoController::inspectMoveImportRecovery(const QString &journal){
    const QUrl url(journal);const auto path=url.isLocalFile()?url.toLocalFile():journal;
    m_moveRecoveryPreview->cancel();m_moveRecoveryPath=QDir::isAbsolutePath(path)?QFileInfo(path).absoluteFilePath():QString();
    m_moveRecoveryView={{"valid",false},{"pending",!m_moveRecoveryPath.isEmpty()}};emit moveImportRecoveryChanged();
    if(!m_moveRecoveryPath.isEmpty())m_moveRecoveryPreview->submit(m_moveRecoveryPath);
}
void PhotoController::cancelMoveImportRecoveryPreview(){if(!m_moveRecoveryPreview)return;m_moveRecoveryPreview->cancel();m_moveRecoveryPath.clear();m_moveRecoveryView={};emit moveImportRecoveryChanged();}
QVariantMap PhotoController::moveImportDiagnostics()const{auto facts=m_moveImportFacts;facts["busy"]=moveImportBusy();facts["retirement_backend"]=m_moveImportTrash?"custom-service":"system-trash";return facts.toVariantMap();}
void PhotoController::initializeMoveImport(){
    m_moveImportJournal=QSettings().value("moveImport/lastJournal").toString();m_moveImportQueue=std::make_unique<MoveImportQueue>(nullptr,m_moveImportTrash);
    m_moveRecoveryPreview=std::make_unique<LatestJob<QString,MoveImportBatch>>(
        [](const QString &path,const CancelToken &cancel){if(cancelled(cancel))return MoveImportBatch{};QString error;auto batch=readMoveImport(path,&error);if(!error.isEmpty())batch.journal.clear();return batch;},
        [this](const QString &path,MoveImportBatch batch){
            if(m_closing||path!=m_moveRecoveryPath)return;
            const bool project=m_project.isOpen()&&QFileInfo(m_project.projectPath()).canonicalFilePath()==batch.project;
            QVariantList rows;for(const auto &file:batch.files)rows.push_back(QVariantMap{{"source",file.copy.source},{"destination",file.copy.destination},{"state",file.state}});
            m_moveRecoveryView={{"valid",!batch.journal.isEmpty()&&!batch.files.isEmpty()&&project},{"pending",false},{"project",batch.project},{"directory",batch.directory},
                {"recordDigest",batch.recordDigest},{"rows",rows},{"retired",batch.retired()},
                {"error",batch.journal.isEmpty()?uiText("无法读取移动恢复记录。","Cannot read move recovery record."):!project?uiText("请先打开该记录所属项目。","Open the project belonging to this record first."):QString()}};
            emit moveImportRecoveryChanged();
        });
    connect(this,&PhotoController::projectChanged,this,[this]{cancelMoveImportRecoveryPreview();});
    connect(m_moveImportQueue.get(),&MoveImportQueue::progress,this,[this](qint64 done,qint64 total,int completed,int files,const QString &stage){
        m_copyImportProgress=total>0?std::clamp(double(done)/total,0.,1.):0;
        m_copyImportStatus=(stage=="trash"?uiText("将已保存来源移至废纸篓","Moving saved sources to trash"):stage=="verify"?uiText("校验移动副本","Verifying move copies"):uiText("正在复制照片","Copying photos"))
            +QString(" · %1/%2").arg(completed).arg(files);emit copyImportChanged();
    });
    connect(m_moveImportQueue.get(),&MoveImportQueue::copiesReady,this,[this](const MoveImportBatch &batch){
        if(m_closing){m_moveImportQueue->catalogReady(false);return;}
        m_moveImportJournal=batch.journal;if(!batch.journal.isEmpty())QSettings().setValue("moveImport/lastJournal",batch.journal);
        bool durable=!batch.journal.isEmpty()&&!batch.files.isEmpty()&&m_project.isOpen()&&QFileInfo(m_project.projectPath()).canonicalFilePath()==batch.project&&m_project.projectPath()==m_moveImportProject;
        int added=0,raw=0;
        if(durable)for(const auto &file:batch.files){
            const QFileInfo destination(file.copy.destination);
            if(!m_importedPaths.contains(destination.canonicalFilePath())){
                if(!importPath(file.copy.destination,false)){durable=false;break;}++added;raw+=RawDecoder::isRawFile(file.copy.destination);
            }
            if(!flushEdits()||!m_project.recordOriginalIdentity(file.copy.destination,{file.bytes,file.copy.sha256.toLatin1()})){durable=false;break;}
        }
        if(durable)durable=flushEdits();if(added)finishImportBatch(added,raw);
        ActionTrace::instance().record("move_import_catalog_handoff",{{"journal",batch.journal},{"copied",batch.files.size()},{"imported",added},{"durable",durable},
            {"retirement_permitted",m_moveImportQueue->retirementPermitted()}});
        m_moveImportQueue->catalogReady(durable);emit copyImportChanged();
    });
    connect(m_moveImportQueue.get(),&MoveImportQueue::finished,this,[this](const MoveImportBatch &batch){
        if(m_closing)return;m_moveImportJournal=batch.journal;if(!batch.journal.isEmpty())QSettings().setValue("moveImport/lastJournal",batch.journal);
        m_moveImportFacts={{"phase",batch.phase},{"project",batch.project},{"journal",batch.journal},{"requested",batch.requested},{"copied",batch.files.size()},{"retired",batch.retired()},{"cancelled",batch.wasCancelled},{"error",batch.error}};
        const bool complete=batch.phase=="done"&&batch.error.isEmpty();if(complete)m_copyImportProgress=1;
        m_copyImportStatus=uiText("校验副本：%1 · 移至废纸篓的原文件：%2","Verified copies: %1 · originals in trash: %2").arg(batch.files.size()).arg(batch.retired());
        if(batch.wasCancelled)m_copyImportStatus+=uiText(" · 已取消，保留副本及未处理来源"," · cancelled; copies and remaining sources retained");
        if(!batch.error.isEmpty())m_copyImportStatus+=uiText(" · 未完成，其余来源保留；请检查恢复记录："," · incomplete; remaining sources retained; check recovery record: ")+localizedMoveError(batch.error,m_language);
        if(!batch.journal.isEmpty())m_copyImportStatus+=uiText(" · 恢复记录："," · recovery: ")+batch.journal;
        setStatus(m_copyImportStatus);emit copyImportChanged();
        ActionTrace::instance().record("move_import_finished",{{"journal",batch.journal},{"copied",batch.files.size()},{"retired",batch.retired()},{"phase",batch.phase},{"error",batch.error},{"cancelled",batch.wasCancelled}});
    });
}
