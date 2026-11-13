#include "PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "diagnostics/PerformanceRecorder.h"
#include "diagnostics/ActionTrace.h"
#include "core/commands/CommandRegistry.h"
#include <QPointer>
#include <QThread>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <algorithm>

void PhotoController::initializeSoftProof() {
    QSettings saved;m_softProofTarget=saved.value("softProof/target","srgb").toString();m_softProofPath=saved.value("softProof/profilePath").toString();
    m_softProofSettings.intent=SoftProof::Intent(std::clamp(saved.value("softProof/intent",1).toInt(),0,3));
    m_softProofSettings.simulatePaper=saved.value("softProof/paper",false).toBool();m_softProofSettings.gamutWarning=saved.value("softProof/gamut",false).toBool();m_softProofSettings.blackPointCompensation=saved.value("softProof/blackPoint",true).toBool();
    if(!m_softProofPath.isEmpty()){QString error;const auto profile=SoftProof::Profile::fromFile(m_softProofPath,&error);if(profile.valid()&&profile.key()==saved.value("softProof/profileKey").toString())m_softProofCustom=profile;}
    if(m_softProofTarget=="custom"&&m_softProofCustom.valid())m_softProofProfile=m_softProofCustom;
    else{if(!ColorManagement::keys().contains(m_softProofTarget))m_softProofTarget="srgb";m_softProofProfile=SoftProof::Profile::fromOutputSpace(ColorManagement::fromKey(m_softProofTarget));}
    m_softProofDelay.setSingleShot(true);m_softProofDeadline.setSingleShot(true);m_softProofDeadline.setInterval(60000);
    m_softProofRenderer=std::make_shared<SoftProofPreviewRenderer>(m_brushBudget,m_renderCache);
    const auto renderer=m_softProofRenderer;
    m_softProofJob=std::make_unique<LatestJob<SoftProofPreviewRequest,SoftProofPreviewResult>>(
        [renderer](const SoftProofPreviewRequest &request,const CancelToken &cancel){try{return renderer->render(request,cancel);}catch(...){SoftProofPreviewResult result;result.error="Soft proof worker failed";return result;}},
        [this](const SoftProofPreviewRequest &request,SoftProofPreviewResult result){
            if(m_closing||!m_softProofEnabled||brushDrawing()||request.generation!=m_softProofGeneration||request.photo!=m_photoEpoch||request.preparation!=m_prepareGeneration||request.revision!=m_requestedRevision||request.source.cacheKey()!=m_previewSource.cacheKey()||request.gpuStagingKey!=m_gpuSource.cacheKey()||m_preparing)return;
            m_softProofDeadline.stop();
            if(result.image.isNull()){
                m_softProofError=result.error;m_softProofFacts={{"available",false},{"error",result.error},{"retryable_budget",result.retryableBudget}};
                m_softProofState=result.retryableBudget?"waiting":"failed";if(result.retryableBudget)m_softProofDelay.start(500);emit softProofChanged();return;
            }
            const auto id=QString("proof-live/%1/%2/%3").arg(request.generation).arg(request.revision).arg(++m_softProofDisplaySerial);
            if(!m_provider||!m_provider->setProofPreview(id,result.image)){m_softProofState="failed";m_softProofError="Proof presentation unavailable";emit softProofChanged();return;}
            m_softProofId=id;m_softProofState="ready";m_softProofError.clear();m_softProofFacts=result.facts;
            m_softProofFacts["parameter_revision"]=QString::number(request.revision);m_softProofFacts["photo_epoch"]=QString::number(request.photo);m_softProofFacts["preparation_generation"]=QString::number(request.preparation);m_softProofFacts["generation"]=QString::number(request.generation);
            PerformanceRecorder::count("soft_proof_frames_published");emit softProofChanged();
        });
    connect(&m_softProofDelay,&QTimer::timeout,this,&PhotoController::submitSoftProof);
    connect(&m_softProofDeadline,&QTimer::timeout,this,[this]{m_softProofJob->cancel();++m_softProofGeneration;m_softProofState="timeout";m_softProofId.clear();if(m_provider)m_provider->clearProofPreview();emit softProofChanged();});
    connect(this,&PhotoController::gpuFrameChanged,this,[this]{scheduleSoftProof();});
    connect(this,&PhotoController::brushDrawingChanged,this,[this]{scheduleSoftProof(true);});
    connect(this,&PhotoController::languageChanged,this,[this]{emit softProofChanged();});
}
void PhotoController::saveSoftProofSettings(){QSettings saved;saved.setValue("softProof/target",m_softProofTarget);saved.setValue("softProof/profilePath",m_softProofPath);saved.setValue("softProof/profileKey",m_softProofCustom.key());saved.setValue("softProof/intent",int(m_softProofSettings.intent));saved.setValue("softProof/paper",m_softProofSettings.simulatePaper);saved.setValue("softProof/gamut",m_softProofSettings.gamutWarning);saved.setValue("softProof/blackPoint",m_softProofSettings.blackPointCompensation);}
void PhotoController::scheduleSoftProof(bool force) {
    if(m_closing||!m_softProofJob)return;
    const auto signature=QString("%1/%2/%3/%4/%5/%6/%7").arg(m_softProofEnabled).arg(brushDrawing()).arg(m_preparing).arg(m_photoEpoch).arg(m_prepareGeneration).arg(m_requestedRevision).arg(m_previewSource.cacheKey());
    if(!force&&signature==m_softProofSignature)return;m_softProofSignature=signature;
    m_softProofDelay.stop();m_softProofDeadline.stop();m_softProofJob->cancel();++m_softProofGeneration;m_softProofId.clear();m_softProofError.clear();m_softProofFacts={};if(m_provider)m_provider->clearProofPreview();
    if(!m_softProofEnabled){m_softProofState="off";m_softProofRenderer->clearCachedSource();}
    else if(brushDrawing()){m_softProofState="paused";}
    else if(m_previewSource.isNull()||m_preparing||m_loading||!hasImage()){m_softProofState="source";m_softProofRenderer->clearCachedSource();}
    else{m_softProofState="updating";m_softProofDelay.start(150);}
    emit softProofChanged();
}
void PhotoController::submitSoftProof() {
    if(m_closing||!m_softProofEnabled||brushDrawing()||m_preparing||m_loading||m_previewSource.isNull()||!m_softProofProfile.valid())return;
    SoftProofPreviewRequest request{m_previewSource,gpuPlan(false,ColorManagement::OutputSpace::ProPhotoRgb),m_softProofProfile,m_softProofSettings,m_photoEpoch,m_prepareGeneration,m_requestedRevision,m_softProofGeneration,m_gpuSource.cacheKey()};
    m_softProofState="updating";m_softProofDeadline.start();m_softProofJob->submit(std::move(request));emit softProofChanged();
}
void PhotoController::setSoftProofEnabled(bool value){executeProofCommand({{"command","proof.enable"},{"enabled",value}});}
bool PhotoController::setSoftProofTarget(const QString &key){
    if(QThread::currentThread()!=thread()||m_closing)return false;
    // The existing UI chooser stays explicit; command dispatch never opens it.
    if(key=="custom"&&!m_softProofCustom.valid()) {QPointer<PhotoController> owner(this);chooseSoftProofProfile();return owner&&m_softProofTarget=="custom";}
    return executeProofCommand({{"command","proof.target"},{"target",key}});
}
bool PhotoController::loadSoftProofProfile(const QUrl &source){return source.isLocalFile()&&executeProofCommand({{"command","proof.profile"},{"source",source.toLocalFile()}});}
bool PhotoController::setSoftProofIntent(int value){return executeProofCommand({{"command","proof.intent"},{"intent",value}});}
void PhotoController::setSoftProofPaper(bool value){executeProofCommand({{"command","proof.paper"},{"enabled",value}});}
void PhotoController::setSoftProofGamutWarning(bool value){executeProofCommand({{"command","proof.gamut-warning"},{"enabled",value}});}
void PhotoController::setSoftProofBlackPoint(bool value){executeProofCommand({{"command","proof.black-point"},{"enabled",value}});}
bool PhotoController::executeProofCommand(const QJsonObject &command) {
    if(QThread::currentThread()!=thread()||m_closing)return false;
    CommandRegistry::Proof request;
    if(!CommandRegistry::decodeProof(command,&request)){setStatus(uiText("软打样命令参数无效", "Invalid soft proof command parameters"));return false;}
    auto canonical=command;
    if(request.operation==CommandRegistry::ProofOperation::Profile)canonical["source"]=QFileInfo(request.text).absoluteFilePath();
    const QVariantMap details{{"command",canonical},{"presentation_only",true},{"completion","presentation-request-accepted"}};
    bool accepted=true;
    switch(request.operation) {
    case CommandRegistry::ProofOperation::Enable:setSoftProofEnabledImpl(request.enabled);break;
    case CommandRegistry::ProofOperation::Target:accepted=setSoftProofTargetImpl(request.text);break;
    case CommandRegistry::ProofOperation::Profile:accepted=loadSoftProofProfileImpl(QUrl::fromLocalFile(canonical["source"].toString()));break;
    case CommandRegistry::ProofOperation::Intent:accepted=setSoftProofIntentImpl(request.intent);break;
    case CommandRegistry::ProofOperation::Paper:setSoftProofPaperImpl(request.enabled);break;
    case CommandRegistry::ProofOperation::GamutWarning:setSoftProofGamutWarningImpl(request.enabled);break;
    case CommandRegistry::ProofOperation::BlackPoint:setSoftProofBlackPointImpl(request.enabled);break;
    case CommandRegistry::ProofOperation::Refresh:scheduleSoftProof(true);break;
    }
    // Presentation callbacks can destroy the owner; only frozen locals follow.
    if(accepted)ActionTrace::instance().record("proof_command",details);
    return accepted;
}
void PhotoController::setSoftProofEnabledImpl(bool value){if(value==m_softProofEnabled)return;m_softProofEnabled=value;scheduleSoftProof(true);}
bool PhotoController::setSoftProofTargetImpl(const QString &key){if(key=="custom"){if(!m_softProofCustom.valid())return false;m_softProofProfile=m_softProofCustom;}else{if(!ColorManagement::keys().contains(key))return false;QString error;const auto profile=SoftProof::Profile::fromOutputSpace(ColorManagement::fromKey(key),&error);if(!profile.valid())return false;m_softProofProfile=profile;}m_softProofTarget=key;saveSoftProofSettings();scheduleSoftProof(true);return true;}
void PhotoController::chooseSoftProofProfile(){
    if(QThread::currentThread()!=thread()||m_closing)return;
    QPointer<PhotoController> owner(this);
    const auto path=QFileDialog::getOpenFileName(nullptr,uiText("选择软打样 ICC","Choose proof ICC"),QFileInfo(m_softProofPath).absolutePath(),"ICC (*.icc *.icm)");
    if(owner&&!path.isEmpty())loadSoftProofProfile(QUrl::fromLocalFile(path));
}
bool PhotoController::loadSoftProofProfileImpl(const QUrl &url){if(!url.isLocalFile())return false;QString error;const auto profile=SoftProof::Profile::fromFile(url.toLocalFile(),&error);if(!profile.valid()){setStatus(uiText("无法读取所选 ICC 配置。","Cannot read the selected ICC profile."));return false;}m_softProofCustom=profile;m_softProofProfile=profile;m_softProofTarget="custom";m_softProofPath=QFileInfo(url.toLocalFile()).absoluteFilePath();saveSoftProofSettings();scheduleSoftProof(true);return true;}
bool PhotoController::setSoftProofIntentImpl(int value){if(value<0||value>3)return false;if(value==int(m_softProofSettings.intent))return true;m_softProofSettings.intent=SoftProof::Intent(value);saveSoftProofSettings();scheduleSoftProof(true);return true;}
void PhotoController::setSoftProofPaperImpl(bool value){if(value==m_softProofSettings.simulatePaper)return;m_softProofSettings.simulatePaper=value;saveSoftProofSettings();scheduleSoftProof(true);}
void PhotoController::setSoftProofGamutWarningImpl(bool value){if(value==m_softProofSettings.gamutWarning)return;m_softProofSettings.gamutWarning=value;saveSoftProofSettings();scheduleSoftProof(true);}
void PhotoController::setSoftProofBlackPointImpl(bool value){if(value==m_softProofSettings.blackPointCompensation)return;m_softProofSettings.blackPointCompensation=value;saveSoftProofSettings();scheduleSoftProof(true);}
void PhotoController::refreshSoftProof(){executeProofCommand({{"command","proof.refresh"}});}
void PhotoController::refreshSoftProofDisplay(){if(m_softProofId.isEmpty()||!m_provider)return;const auto id=QString("proof-live/%1/%2/%3").arg(m_softProofGeneration).arg(m_requestedRevision).arg(++m_softProofDisplaySerial);if(m_provider->renameProofPreview(id)){m_softProofId=id;emit softProofChanged();}}
QString PhotoController::softProofStatus() const {
    if(m_softProofState=="off")return uiText("软打样未启用","Soft proof is off");
    if(m_softProofState=="paused")return uiText("笔刷绘制时显示编辑画面，结束后恢复软打样。","Showing the edit while painting; proof resumes afterwards.");
    if(m_softProofState=="source")return uiText("正在等待照片预览…","Waiting for the photo preview…");
    if(m_softProofState=="waiting")return uiText("等待图像内存空闲…","Waiting for image memory…");
    if(m_softProofState=="updating")return uiText("正在生成软打样…","Preparing soft proof…");
    if(m_softProofState=="timeout")return uiText("软打样超时，请重试。","Soft proof timed out; try again.");
    if(m_softProofState=="failed")return uiText("无法生成软打样，请尝试其他 ICC 或重新生成。","Cannot generate proof; try another ICC or refresh.");
    return uiText("软打样：","Soft proof: ")+m_softProofProfile.description()+QString(" · %1 × %2").arg(m_softProofFacts["proof_width"].toInt()).arg(m_softProofFacts["proof_height"].toInt());
}
QVariantMap PhotoController::softProofDiagnostics() const {auto facts=m_softProofFacts;facts["enabled"]=m_softProofEnabled;facts["available"]=softProofReady();facts["state"]=m_softProofState;facts["presentation_only"]=true;facts["target"]=m_softProofTarget;facts["profile_key"]=m_softProofProfile.key();facts["monitor_lut_key"]=m_displayColorLutKey;facts["error"]=m_softProofError;return facts.toVariantMap();}
