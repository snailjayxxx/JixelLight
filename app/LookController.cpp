#include "app/PhotoController.h"
#include "core/look/LookProfiles.h"
#include "core/look/SonyLookMetadata.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/image/ProcessedImageProvider.h"
#include "diagnostics/ActionTrace.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDir>
#include <cmath>

QVariantMap PhotoController::lookState() const {
    const auto look=currentState().look;
    QVariantMap p; for(auto i=look.parameters.cbegin();i!=look.parameters.cend();++i)p[i.key()]=i.value();
    QVariantMap out{{"mode",look.mode},{"code",look.code},{"strength",look.strength},{"parameters",p},
        {"active",LookProfiles::active(look)},{"error",look.error},
        {"quality",look.lut?"reference-or-imported-LUT":"uncalibrated-approximation"}};
    if(look.lut){out["lutDigest"]=look.lut->digest;out["lutTitle"]=look.lut->title;out["evidence"]=look.lut->evidence.toVariantMap();}
    return out;
}
QVariantList PhotoController::lookCatalog() const {return LookProfiles::catalog();}
QString PhotoController::cameraReferenceUrl() const {
    return m_referenceImage.isNull()?QString():QString("image://processed/reference/%1").arg(m_referenceRevision);
}
QVariantMap PhotoController::referenceHistogram() const {
    return {{"red",m_referenceScopes.red},{"green",m_referenceScopes.green},{"blue",m_referenceScopes.blue},
        {"luma",m_referenceScopes.luma},{"pixels",qulonglong(m_referenceScopes.pixelCount)}};
}
void PhotoController::initializeLookJobs() {
    connect(this,&PhotoController::adjustmentsChanged,this,&PhotoController::lookChanged);
    connect(this,&PhotoController::currentMetadataChanged,this,&PhotoController::lookChanged);
    m_referenceJob=std::make_unique<LatestJob<CameraReferenceRequest,CameraReferenceResult>>(
        CameraReference::load,[this](const CameraReferenceRequest &request,CameraReferenceResult result){
            if(m_closing||request.photo!=m_photoEpoch)return;
            m_referenceBusy=false;m_referenceImage=result.image;m_referenceInfo=result.info;m_referenceScopes=result.scopes;
            m_referenceInfo["error"]=result.error;
            if(!result.info.value("file").toString().isEmpty()) m_referenceFiles.insert(QFileInfo(result.info["file"].toString()).absoluteFilePath());
            if(m_provider)m_provider->setReference(m_referenceImage);
            ++m_referenceRevision;emit referenceChanged();
            if(!result.error.isEmpty())setStatus(uiText("参考图：", "Reference: ")+result.error);
        });
    m_calibrationJob=std::make_unique<LatestJob<LookCalibrationRequest,LookCalibrationResult>>(
        calibrateLook,[this](const LookCalibrationRequest &request,LookCalibrationResult result){
            if(m_closing)return;
            m_calibrationBusy=false;
            if(request.photo!=m_photoEpoch||request.revision!=m_requestedRevision){emit calibrationChanged();return;}
            m_calibrationReport=result.report;m_calibrationReport["error"]=result.error;
            emit calibrationChanged();
            if(!result.lut){setStatus(uiText("外观拟合未应用：", "Match not applied: ")+result.error);return;}
            auto *state=mutableCurrentState();if(!state)return;
            LookState look;look.mode="calibrated";look.code=sonyLook().value("code").toString();look.lut=result.lut;
            state->look=look;
            persistAndApply("reference_look_applied",{{"sha256",result.lut->digest},{"scope","this-image-only"}});
            setStatus(uiText("已应用本照片参考拟合，可继续编辑；不是索尼官方显影或通用相机配置。", "Applied image-specific reference match; editable, not Sony rendering or a universal camera profile."));
        });
}
void PhotoController::cancelCalibration() {
    if(m_calibrationJob)m_calibrationJob->cancel();
    if(m_calibrationBusy){m_calibrationBusy=false;emit calibrationChanged();}
}
void PhotoController::resetReference() {
    cancelCalibration();if(m_referenceJob)m_referenceJob->cancel();
    m_referenceImage={};m_referenceInfo.clear();m_referenceScopes={};m_manualReferencePath.clear();
    m_calibrationReport.clear();m_referenceBusy=false;++m_referenceRevision;
    if(m_provider)m_provider->setReference({});emit referenceChanged();emit calibrationChanged();
}
void PhotoController::requestReference() {
    if(!hasImage()||!m_referenceJob)return;
    cancelCalibration();m_referenceBusy=true;emit referenceChanged();
    m_referenceJob->submit({currentFile(),m_manualReferencePath,m_currentMetadata,m_photoEpoch});
}
void PhotoController::setShowCameraReference(bool show) {
    if(m_showReference==show)return;m_showReference=show;emit referenceChanged();
    if(show&&m_referenceImage.isNull()&&!m_referenceBusy)requestReference();
}
void PhotoController::setLookMode(const QString &mode) {
    auto *state=mutableCurrentState();if(!state)return;
    if(mode=="as-shot") {
        if(!currentIsRaw()){setStatus(uiText("JPEG/TIFF 已包含外观，不会自动重复套用。", "JPEG/TIFF already contains rendered color; no automatic double application."));return;}
        state->look=LookState{};state->look.mode="as-shot";
    } else if(mode=="off") state->look.mode="off";
    else if(mode=="manual") {
        if(state->look.mode=="as-shot")state->look=currentState().look;
        state->look.mode=state->look.lut?"calibrated":"manual";
        if(!state->look.lut&&SonyLookMetadata::normalize(state->look.code).isEmpty())state->look.code="ST";
    } else return;
    persistAndApply("look_mode",{{"mode",mode}});
}
void PhotoController::setLookCode(const QString &code) {
    auto *state=mutableCurrentState();const auto normalized=SonyLookMetadata::normalize(code);
    if(!state||normalized.isEmpty())return;
    state->look={};state->look.mode="manual";state->look.code=normalized;
    persistAndApply("look_preset",{{"code",normalized},{"quality","approximation"}});
}
void PhotoController::setLookStrength(double strength) {
    auto *state=mutableCurrentState();if(!state||!std::isfinite(strength))return;
    state->look.strength=std::clamp(strength,0.0,1.0);persistAndApply("look_strength");
}
void PhotoController::setLookParameter(const QString &name,double value) {
    auto *state=mutableCurrentState();if(!state||!std::isfinite(value)||!SonyLookMetadata::parameterNames().contains(name))return;
    if(state->look.mode=="as-shot")state->look=currentState().look;
    state->look.mode=state->look.lut?"calibrated":"manual";
    if(!state->look.lut&&state->look.code.isEmpty())state->look.code="ST";
    const auto range=SonyLookMetadata::parameterRange(name);
    state->look.parameters[name]=std::clamp(std::round(value),double(range.first),double(range.second));
    persistAndApply("look_parameter",{{"name",name},{"value",value}});
}
bool PhotoController::loadReference(const QUrl &url) {
    if(!hasImage()||!url.isLocalFile()||!QFileInfo(url.toLocalFile()).isFile())return false;
    m_manualReferencePath=url.toLocalFile();m_referenceFiles.insert(QFileInfo(m_manualReferencePath).absoluteFilePath());
    m_referenceImage={};if(m_provider)m_provider->setReference({});m_showReference=true;requestReference();return true;
}
void PhotoController::openReferenceDialog() {
    const auto path=QFileDialog::getOpenFileName(nullptr,uiText("选择同次拍摄的未裁剪 JPEG 参考图", "Choose an uncropped JPEG of the same shot"),QFileInfo(currentFile()).absolutePath(),"JPEG (*.jpg *.jpeg *.JPG *.JPEG)");
    if(!path.isEmpty())loadReference(QUrl::fromLocalFile(path));
}
bool PhotoController::calibrateFromReference() {
    if(!currentIsRaw()||m_fullSource.isNull()||m_referenceImage.isNull()||m_referenceBusy||m_calibrationBusy)return false;
    // Retain immutable pixel snapshots. Never mutate the RAW or overwrite user edits.
    m_calibrationBusy=true;m_calibrationReport.clear();emit calibrationChanged();
    auto provenance=m_referenceInfo;provenance["sourceIdentity"]=m_loadedKey;
    provenance["camera"]=m_currentMetadata.value("model");provenance["asShot"]=sonyLook();
    m_calibrationJob->submit({m_fullSource,m_referenceImage,m_photoEpoch,m_requestedRevision,provenance});return true;
}
bool PhotoController::isProtectedPhoto(const QString &path) const {
    const QFileInfo destination(path);auto protectedPath=[&](const QString &p){const QFileInfo src(p);
        return destination.absoluteFilePath()==src.absoluteFilePath()||(!destination.canonicalFilePath().isEmpty()&&destination.canonicalFilePath()==src.canonicalFilePath());};
    for(const auto &p:m_photos)if(protectedPath(p.path))return true;
    for(const auto &p:m_referenceFiles)if(protectedPath(p))return true;
    return false;
}
bool PhotoController::loadLookProfile(const QUrl &url) {
    if(!hasImage()||!url.isLocalFile())return false;
    QFile file(url.toLocalFile());if(!file.open(QIODevice::ReadOnly)||file.size()>4*1024*1024||file.size()<=0){setStatus(uiText("外观文件不可读或超过 4 MB。", "Look file is unreadable or exceeds 4 MB."));return false;}
    const QByteArray bytes=file.readAll();LookState look;QString error;
    if(QFileInfo(file).suffix().compare("cube",Qt::CaseInsensitive)==0){look.lut=LookLut::fromCube(bytes,&error);look.mode="calibrated";}
    else {
        QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);
        if(parse.error!=QJsonParseError::NoError||!doc.isObject()||doc.object()["format"]!="JixelLightLook"||doc.object()["version"].toInt()!=1)error="Invalid JixelLight look profile";
        else {look=LookState::fromJson(doc.object()["look"].toObject());error=look.error;
            if(look.mode=="as-shot")error="Profile must contain a materialized look, not an unresolved as-shot request";}
    }
    if(error.isEmpty() && look.lut) {
        const auto kind=look.lut->evidence.value("kind").toString();
        const auto fittedEngine=look.lut->evidence.value("engineVersion").toString();
        const bool fitted = kind=="image-specific-fit" || kind=="multi-scene-empirical-fit";
        if(fitted && fittedEngine != QLatin1String(ProcessingPlan::EngineVersion))
            error="This fitted look was created for a different RAW rendering engine; refit or regenerate it.";
    }
    if(error.isEmpty()&&!LookProfiles::active(look))error="Profile has no supported active look";
    if(!error.isEmpty()){setStatus(uiText("外观未导入：", "Look not imported: ")+error);return false;}
    mutableCurrentState()->look=std::move(look);persistAndApply("look_profile_imported");return true;
}
bool PhotoController::saveLookProfile(const QUrl &url) {
    if(!hasImage()||!url.isLocalFile())return false;
    auto look=currentState().look;if(!LookProfiles::active(look))return false;
    if(look.mode=="as-shot")look.mode="manual";
    QString path=url.toLocalFile();if(!path.endsWith(".jlook.json",Qt::CaseInsensitive))path+=".jlook.json";
    if(isProtectedPhoto(path))return false;
    const QByteArray json=QJsonDocument(QJsonObject{{"format","JixelLightLook"},{"version",1},{"look",look.toJson()},
        {"notice","Independent approximation or image-specific fit; not a Sony profile. LUTs use display sRGB."}}).toJson();
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(json)!=json.size()||!file.commit()){
        setStatus(uiText("外观保存失败：", "Look save failed: ")+file.errorString());return false;}
    setStatus(uiText("外观已保存：", "Look saved: ")+path);return true;
}
void PhotoController::openLookProfileDialog() {
    const auto path=QFileDialog::getOpenFileName(nullptr,uiText("导入外观（LUT 必须是显示 sRGB，非 Log/HDR）", "Import look (LUT must be display sRGB, not Log/HDR)"),{},"Looks (*.jlook.json *.cube)");
    if(!path.isEmpty())loadLookProfile(QUrl::fromLocalFile(path));
}
void PhotoController::saveLookProfileDialog() {
    const auto path=QFileDialog::getSaveFileName(nullptr,uiText("保存独立外观", "Save independent look"),"JixelLight.jlook.json","JixelLight Look (*.jlook.json)");
    if(!path.isEmpty())saveLookProfile(QUrl::fromLocalFile(path));
}
