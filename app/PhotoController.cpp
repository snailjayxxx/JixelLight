#include "app/PhotoController.h"
#include "core/color/ColorManagement.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/export/ExportNaming.h"
#include "core/library/CatalogSelection.h"
#include "core/files/FileNames.h"
#include "core/metadata/MetadataReader.h"
#include "core/metadata/XmpSidecar.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/StageGraph.h"
#include "core/pipeline/MaskCanvasGeometry.h"
#include "core/pipeline/FloatFrameFingerprint.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/cache/RenderedPreviewCache.h"
#include "core/cache/FullScopesCache.h"
#include "core/cache/AnalysisFrameCache.h"
#include "core/cache/ScopePlotCache.h"
#include "core/commands/CommandRegistry.h"
#include "core/commands/AdjustmentTransfer.h"
#include "core/raw/RawDecoder.h"
#include "diagnostics/ActionTrace.h"
#include "diagnostics/DiagnosticBundle.h"

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include "core/commands/NamedPresets.h"
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include "diagnostics/PerformanceRecorder.h"
#include "core/look/LookProfiles.h"

PhotoController::PhotoController(ProcessedImageProvider *provider,QObject *parent,std::shared_ptr<BrushRenderBudget> brushBudget)
    : QObject(parent),m_provider(provider),m_brushBudget(brushBudget?std::move(brushBudget):BrushRenderBudget::sharedDefault()) {
    connect(this,&PhotoController::currentIndexChanged,this,&PhotoController::cancelBrushStroke);
    connect(this,&PhotoController::adjustmentsChanged,this,&PhotoController::cancelBrushStroke);
    connect(this,&PhotoController::cropEditingChanged,this,&PhotoController::cancelBrushStroke);
    QSettings settings;
    m_language = settings.value(QStringLiteral("ui/language"), QStringLiteral("zh_CN")).toString();
    if (m_language != QStringLiteral("zh_CN") && m_language != QStringLiteral("en_US")) m_language = QStringLiteral("zh_CN");
    m_statusMessage = uiText(QStringLiteral("就绪"), QStringLiteral("Ready"));
    m_gpuEnabled = !qEnvironmentVariableIsSet("JIXELLIGHT_FORCE_CPU") && settings.value("performance/gpuEnabled", true).toBool();
    initializeJobs();
    initializeRelinkJobs();
    initializeLookJobs();
    m_diagnosticTimer.setSingleShot(true);
    connect(&m_diagnosticTimer, &QTimer::timeout, this, [this] {
        if (m_pendingBug) finishBugReport({{"available", false}, {"error", "GPU diagnostic readback timed out"}});
    });
}

QString PhotoController::uiText(const QString &zh, const QString &en) const {
    return m_language == QStringLiteral("zh_CN") ? zh : en;
}

void PhotoController::setLanguage(const QString &language) {
    const QString normalized = language == QStringLiteral("en_US") ? QStringLiteral("en_US") : QStringLiteral("zh_CN");
    if (m_language == normalized) return;
    m_language = normalized;
    QSettings().setValue(QStringLiteral("ui/language"), m_language);
    emit languageChanged();
    setStatus(uiText(QStringLiteral("界面语言已切换为中文"), QStringLiteral("Interface language changed to English")));
    ActionTrace::instance().record("language_changed", {{"language", m_language}});
}

QVariantList PhotoController::library() const {
    QVariantList out;
    out.reserve(m_photos.size());
    for (int i = 0; i < m_photos.size(); ++i) {
        QVariantMap row;
        row["name"] = m_photos[i].name;
        row["path"] = m_photos[i].path;
        row["available"] = m_photos[i].available;
        row["thumbnailRevision"] = qulonglong(m_photos[i].thumbnailRevision);
        row["current"] = i == m_currentIndex;
        row["index"] = i;
        row["id"] = m_photos[i].storageKey();
        row["rating"] = m_photos[i].rating;
        row["flag"] = m_photos[i].flag;
        row["virtual"] = !m_photos[i].copyKey.isEmpty();
        row["versionName"] = m_photos[i].versionName;
        row["selected"] = m_selectedPhotos.contains(i);
        row["keywords"] = m_photos[i].tags.keywords;
        row["albums"] = m_photos[i].tags.albums;
        row["label"] = m_photos[i].tags.label;
        row["captureTime"] = m_photos[i].timeline.captureTime;
        row["captureChecked"] = m_photos[i].timeline.captureChecked;
        row["captureOrder"] = m_photos[i].timeline.captureOrder();
        row["importedAt"] = m_photos[i].timeline.importedAt;
        row["editedAt"] = m_photos[i].timeline.editedAt;
        row["raw"] = m_photos[i].raw;
        row["type"] = m_photos[i].raw ? QStringLiteral("RAW") : QFileInfo(m_photos[i].path).suffix().toUpper();
        out.push_back(row);
    }
    return out;
}

QString PhotoController::previewUrl() const {
    return !m_processedPreview.isNull() ? QString("image://processed/current?rev=%1").arg(m_previewRevision) : QString();
}
QString PhotoController::currentFile() const { return hasImage() ? m_photos[m_currentIndex].path : QString(); }
QString PhotoController::currentFormat() const {
    return hasImage() ? (m_photos[m_currentIndex].raw ? QStringLiteral("RAW") : QFileInfo(currentFile()).suffix().toUpper()) : QString();
}
bool PhotoController::currentIsRaw() const { return hasImage() && m_photos[m_currentIndex].raw; }
QString PhotoController::pipelineDescription() const {
    if (!hasImage()) return QString();
    return currentIsRaw()
        ? QStringLiteral("RAW → Camera WB/Matrix → Linear ProPhoto RGB → Perceptual Color/HSL → Tone/RGB Curves → ICC sRGB Preview")
        : QStringLiteral("Input ICC (sRGB fallback) → Linear ProPhoto RGB → Perceptual Color/HSL → Tone/RGB Curves → ICC sRGB Preview");
}

AdjustmentState PhotoController::currentState() const { return hasImage() ? LookProfiles::resolveAsShot(m_photos[m_currentIndex].state, m_currentMetadata, currentIsRaw()) : AdjustmentState{}; }
AdjustmentState *PhotoController::mutableCurrentState() {
    if (!currentSourceAvailable()) return nullptr;
    auto &photo = m_photos[m_currentIndex];
    photo.history.initialize(photo.state);
    return &photo.state;
}

void PhotoController::rotatePhoto(int turns) {
    if (auto *state = mutableCurrentState()) {
        CommandRegistry::execute(*state,{{"command","geometry.rotate"},{"quarterTurns",turns}});
        persistAndApply("geometry_rotate");
    }
}
void PhotoController::flipPhoto(bool horizontal) {
    if (auto *state = mutableCurrentState()) {
        CommandRegistry::execute(*state,{{"command","geometry.flip"},{"axis",horizontal ? "horizontal" : "vertical"}});
        persistAndApply("geometry_flip");
    }
}
void PhotoController::setCrop(double x, double y, double width, double height) {
    if (!GeometryState::validCrop(x,y,width,height)) return;
    if (auto *state = mutableCurrentState()) {
        CommandRegistry::execute(*state,{{"command","geometry.crop"},{"x",x},{"y",y},{"width",width},{"height",height}});
        persistAndApply("geometry_crop");
    }
}
void PhotoController::setCropAspect(double aspect) {
    if (!hasImage() || m_loadedPreview.isNull() || !std::isfinite(aspect) || aspect <= 0) return;
    if (currentState().geometry.quarterTurns % 2) aspect = 1 / aspect;
    const auto size=currentState().geometry.correctedSize(m_loadedPreview.size());
    if (size.isEmpty()) return;
    const double original = double(size.width()) / size.height();
    const double width = std::min(1.0, aspect/original), height = std::min(1.0, original/aspect);
    setCrop((1-width)/2, (1-height)/2, width, height);
}
void PhotoController::setStraighten(double degrees) {
    if (!GeometryState::validStraighten(degrees)) return;
    auto *state=mutableCurrentState(); if (!state || state->geometry.straighten==degrees) return;
    if (!CommandRegistry::execute(*state,{{"command","geometry.straighten"},{"degrees",degrees}})) return;
    persistAndApply("geometry_straighten",{{"value",degrees}});
}
bool PhotoController::setGeometryAdjustment(const QString &parameter,double value) {
    if (m_cropEditing || !GeometryState::validCorrection(parameter,value)) return false;
    auto *state=mutableCurrentState(); if (!state) return false;
    auto staged=*state;
    if (!CommandRegistry::execute(staged,{{"command","geometry.set"},{"parameter",parameter},{"value",value}})) return false;
    if (staged.geometry.toJson()==state->geometry.toJson()) return true;
    *state=std::move(staged);
    persistAndApply("geometry_correction",{{"parameter",parameter},{"value",value}}); return true;
}
void PhotoController::resetGeometryCorrections() {
    if (m_cropEditing) return;
    if (auto *state=mutableCurrentState(); state && state->geometry.hasCorrections()) {
        CommandRegistry::execute(*state,{{"command","geometry.resetCorrections"}});
        persistAndApply("geometry_corrections_reset");
    }
}
void PhotoController::resetGeometry() {
    if (auto *state = mutableCurrentState()) { CommandRegistry::execute(*state,{{"command","geometry.reset"}}); persistAndApply("geometry_reset"); }
}

GeometryState PhotoController::previewGeometry() const {
    auto geometry = currentState().geometry;
    if (m_cropEditing) geometry.crop = QRectF(0,0,1,1);
    return geometry;
}
bool PhotoController::beginCrop() {
    if (!hasImage() || !previewReady() || m_loading || m_cropEditing) return false;
    finishInteraction(); m_cropEditing = true;
    m_zoom = 0; m_centerX = m_centerY = .5;
    emit cropEditingChanged(); prepareCurrent(); return true;
}
void PhotoController::cancelCrop() {
    if (!m_cropEditing) return;
    m_cropEditing = false; emit cropEditingChanged(); prepareCurrent();
}
bool PhotoController::applyCrop(double x, double y, double width, double height) {
    if (!m_cropEditing || !GeometryState::validCrop(x,y,width,height)) return false;
    const auto crop = currentState().geometry.orientedRect({x,y,width,height},true);
    if (!GeometryState::validCrop(crop.x(),crop.y(),crop.width(),crop.height())) return false;
    m_cropEditing = false; emit cropEditingChanged();
    setCrop(crop.x(),crop.y(),crop.width(),crop.height()); finishInteraction(); return true;
}

QVariantList PhotoController::selectedIndices() const {
    auto indices = m_selectedPhotos.values(); std::sort(indices.begin(),indices.end());
    QVariantList out; for (int index : indices) out.append(index); return out;
}
QStringList PhotoController::albumNames() const {
    QStringList names; for (const auto &photo : m_photos) names.append(photo.tags.albums);
    names.removeDuplicates(); names.sort(); return names;
}
QStringList PhotoController::currentKeywords() const { return hasImage() ? m_photos[m_currentIndex].tags.keywords : QStringList{}; }
QStringList PhotoController::currentAlbums() const { return hasImage() ? m_photos[m_currentIndex].tags.albums : QStringList{}; }
QString PhotoController::currentColorLabel() const { return hasImage() ? m_photos[m_currentIndex].tags.label : QStringLiteral("none"); }
bool PhotoController::createVirtualCopy(const QString &name) {
    if (!hasImage() || !flushEdits()) return false;
    auto copy = m_photos[m_currentIndex];
    const auto title = name.trimmed().isEmpty() ? QStringLiteral("Version %1").arg(m_photos.size()+1) : name.trimmed();
    QStringList checked{title};
    if (!CatalogTags::normalize(&checked,1) || checked.size() != 1 || checked[0] != title || !copy.state.look.error.isEmpty()) return false;
    copy.copyKey = "jixel-copy:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.versionName = title; copy.name = QFileInfo(copy.path).fileName() + " · " + title;
    copy.history = {}; copy.history.initialize(copy.state);
    copy.timeline.importedAt = QDateTime::currentMSecsSinceEpoch(); copy.timeline.editedAt = 0;
    EditHistory validated;
    if (!validated.restore(copy.history.toJson(),copy.state)) return false;
    if (m_project.isOpen() && !m_project.addVirtualCopy(copy.copyKey,copy.path,title,copy.state,copy.history,copy.tags,{copy.rating,copy.flag},copy.timeline)) {
        setStatus(uiText("无法创建虚拟副本：", "Cannot create virtual copy: ") + m_project.lastError()); return false;
    }
    m_photos.append(copy); emit libraryChanged(); selectPhoto(m_photos.size()-1);
    ActionTrace::instance().record("virtual_copy_created", {{"file",copy.path},{"photo_key",copy.copyKey},{"version",title}});
    setStatus(uiText("已创建虚拟副本：", "Virtual copy created: ") + title); return true;
}

bool PhotoController::renameCurrentVirtualCopy(const QString &name) {
    if (!hasImage() || m_photos[m_currentIndex].copyKey.isEmpty() || !flushEdits()) return false;
    const auto title = name.trimmed(); QStringList checked{title};
    if (!CatalogTags::normalize(&checked,1) || checked.size() != 1 || checked[0] != title) return false;
    auto &photo = m_photos[m_currentIndex];
    if (m_project.isOpen() && !m_project.renameVirtualCopy(photo.copyKey,title)) {
        setStatus(uiText("副本重命名失败：", "Copy rename failed: ") + m_project.lastError()); return false;
    }
    photo.versionName = title; photo.name = QFileInfo(photo.path).fileName() + " · " + title;
    emit libraryChanged();
    ActionTrace::instance().record("virtual_copy_renamed",{{"photo_key",photo.copyKey},{"version",title}});
    return true;
}
bool PhotoController::removeCurrentVirtualCopy() {
    if (!hasImage() || m_photos[m_currentIndex].copyKey.isEmpty() || !flushEdits()) return false;
    const int removed = m_currentIndex; const auto photo = m_photos[removed];
    if (m_project.isOpen() && !m_project.removeVirtualCopy(photo.copyKey)) {
        setStatus(uiText("副本删除失败：", "Copy deletion failed: ") + m_project.lastError()); return false;
    }
    QSet<int> selection;
    for (int index : m_selectedPhotos) if (index != removed) selection.insert(index > removed ? index-1 : index);
    m_dirtyEdits.remove(photo.copyKey); m_dirtyHistories.remove(photo.copyKey);
    m_dirtyTags.remove(photo.copyKey); m_dirtyCuration.remove(photo.copyKey);
    m_dirtyDates.remove(photo.copyKey);
    m_photos.removeAt(removed); m_importedPaths.clear();
    for (const auto &item : m_photos) m_importedPaths.insert(QFileInfo(item.path).canonicalFilePath());
    // The following version may occupy the same index: force a new epoch.
    m_currentIndex = -2;
    selectPhoto(std::min(removed,int(m_photos.size())-1));
    if (!selection.isEmpty()) { m_selectedPhotos = selection; emit libraryChanged(); }
    ActionTrace::instance().record("virtual_copy_removed",{{"photo_key",photo.copyKey},{"file",photo.path}});
    setStatus(uiText("已删除虚拟副本：", "Virtual copy deleted: ") + photo.versionName);
    return true;
}

bool PhotoController::setPhotoSelection(const QVariantList &indices) {
    QSet<int> selected;
    for (const auto &value : indices) {
        bool valid = false; const int index = value.toInt(&valid);
        if (!valid || value.toDouble() != index || index < 0 || index >= m_photos.size()) return false;
        selected.insert(index);
    }
    m_selectedPhotos = selected; emit libraryChanged(); return true;
}
bool PhotoController::setSelectionRating(int rating) {
    if (rating < 0 || rating > 5 || m_selectedPhotos.isEmpty()) return false;
    for (int index : m_selectedPhotos) {
        auto &photo = m_photos[index]; photo.rating = rating;
        if (m_project.isOpen()) m_dirtyCuration.insert(photo.storageKey(), {photo.rating, photo.flag});
    }
    enqueueEdits(); emit libraryChanged(); emit curationChanged();
    ActionTrace::instance().record("selection_rating", {{"count",m_selectedPhotos.size()},{"rating",rating}}); return true;
}
bool PhotoController::setSelectionFlag(const QString &flag) {
    if ((flag != "none" && flag != "pick" && flag != "reject") || m_selectedPhotos.isEmpty()) return false;
    for (int index : m_selectedPhotos) {
        auto &photo = m_photos[index]; photo.flag = flag;
        if (m_project.isOpen()) m_dirtyCuration.insert(photo.storageKey(), {photo.rating, photo.flag});
    }
    enqueueEdits(); emit libraryChanged(); emit curationChanged();
    ActionTrace::instance().record("selection_flag", {{"count",m_selectedPhotos.size()},{"flag",flag}}); return true;
}
bool PhotoController::updateSelectedTags(const QString &operation, const QString &value) {
    if (m_selectedPhotos.isEmpty()) return false;
    QHash<int, CatalogTags> staged;
    for (int index : m_selectedPhotos) {
        auto tags = m_photos[index].tags;
        if (operation == "keywords") tags.keywords = value.split(',');
        else if (operation == "label") tags.label = value;
        else {
            const auto name = value.trimmed();
            if (name.isEmpty()) return false;
            if (operation == "album_add") tags.albums.append(name);
            else if (operation == "album_remove") tags.albums.removeAll(name);
            else return false;
        }
        if (!CatalogTags::normalize(&tags.keywords,64) || !CatalogTags::normalize(&tags.albums,32)
            || !CatalogTags::validLabel(tags.label)) return false;
        staged.insert(index,tags);
    }
    for (auto it = staged.begin(); it != staged.end(); ++it) {
        auto &photo = m_photos[it.key()]; photo.tags = it.value();
        if (m_project.isOpen()) m_dirtyTags.insert(photo.storageKey(),photo.tags);
    }
    enqueueEdits(); emit libraryChanged();
    ActionTrace::instance().record("selection_" + operation, {{"count",staged.size()}}); return true;
}
bool PhotoController::setSelectionKeywords(const QString &text) { return updateSelectedTags("keywords",text); }
bool PhotoController::setSelectionLabel(const QString &label) { return updateSelectedTags("label",label); }
bool PhotoController::addSelectionToAlbum(const QString &name) { return updateSelectedTags("album_add",name); }
bool PhotoController::removeSelectionFromAlbum(const QString &name) { return updateSelectedTags("album_remove",name); }

namespace {
QString presetFile() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("develop-presets.json");
}
}
QStringList PhotoController::presetNames() const {
    NamedPresets presets(presetFile()); QString error;
    return presets.load(&error) ? presets.names() : QStringList{};
}
bool PhotoController::saveNamedPreset(const QString &name) {
    if (!hasImage()) return false;
    NamedPresets presets(presetFile()); QString error;
    if (!presets.load(&error) || !presets.save(name.trimmed(), m_photos[m_currentIndex].state, &error)) {
        setStatus(uiText("预设保存失败：", "Preset save failed: ") + error); return false;
    }
    emit presetsChanged();
    setStatus(uiText("已保存预设：", "Preset saved: ") + name.trimmed()); return true;
}
bool PhotoController::applyNamedPreset(const QString &name) {
    if (!currentSourceAvailable()) return false;
    NamedPresets presets(presetFile()); QString error; AdjustmentState state;
    if (!presets.load(&error) || !presets.get(name, &state)) {
        setStatus(uiText("无法读取预设：", "Cannot read preset: ") + (error.isEmpty() ? name : error)); return false;
    }
    state.geometry = currentState().geometry;
    m_photos[m_currentIndex].history.finish();
    *mutableCurrentState() = state;
    persistAndApply(QStringLiteral("named_preset"));
    setStatus(uiText("已应用预设：", "Preset applied: ") + name); return true;
}
bool PhotoController::removeNamedPreset(const QString &name) {
    NamedPresets presets(presetFile()); QString error;
    if (!presets.load(&error) || !presets.remove(name, &error)) {
        setStatus(uiText("预设删除失败：", "Preset removal failed: ") + error); return false;
    }
    emit presetsChanged(); return true;
}
bool PhotoController::renameNamedPreset(const QString &name, const QString &replacement) {
    NamedPresets presets(presetFile()); QString error;
    if (!presets.load(&error) || !presets.rename(name,replacement.trimmed(),&error)) {
        setStatus(uiText("预设重命名失败：", "Preset rename failed: ")+error); return false;
    }
    emit presetsChanged(); return true;
}
bool PhotoController::replaceNamedPreset(const QString &name) {
    if (!hasImage()) return false;
    NamedPresets presets(presetFile()); QString error;
    if (!presets.load(&error) || !presets.replace(name,m_photos[m_currentIndex].state,&error)) {
        setStatus(uiText("预设更新失败：", "Preset update failed: ")+error); return false;
    }
    emit presetsChanged(); setStatus(uiText("已更新预设：", "Preset updated: ")+name); return true;
}
bool PhotoController::exportNamedPreset(const QString &name, const QUrl &destination) {
    if (!destination.isLocalFile() || isProtectedPhoto(destination.toLocalFile())) return false;
    NamedPresets presets(presetFile()); QString error;
    if (!presets.load(&error) || !presets.exportFile(name,destination.toLocalFile(),&error)) {
        setStatus(uiText("预设导出失败：", "Preset export failed: ")+error); return false;
    }
    setStatus(uiText("已导出预设：", "Preset exported: ")+name); return true;
}
bool PhotoController::importNamedPreset(const QUrl &source, const QString &replacementName) {
    if (!source.isLocalFile()) return false;
    NamedPresets presets(presetFile()); QString error;
    if (!presets.load(&error) || !presets.importFile(source.toLocalFile(),replacementName.trimmed(),&error)) {
        setStatus(uiText("预设导入失败：", "Preset import failed: ")+error); return false;
    }
    emit presetsChanged(); setStatus(uiText("预设已导入", "Preset imported")); return true;
}
void PhotoController::openPresetExportDialog(const QString &name) {
    const auto path=QFileDialog::getSaveFileName(nullptr,uiText("导出 JixelLight 预设", "Export JixelLight preset"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("preset.jixelpreset.json"),"JixelLight preset (*.jixelpreset.json)");
    if (!path.isEmpty()) exportNamedPreset(name,QUrl::fromLocalFile(path.endsWith(".jixelpreset.json",Qt::CaseInsensitive) ? path : path+".jixelpreset.json"));
}
void PhotoController::openPresetImportDialog(const QString &replacementName) {
    const auto path=QFileDialog::getOpenFileName(nullptr,uiText("导入 JixelLight 预设", "Import JixelLight preset"),{},"JixelLight preset (*.jixelpreset.json)");
    if (!path.isEmpty()) importNamedPreset(QUrl::fromLocalFile(path),replacementName);
}

bool PhotoController::exportXmp(const QUrl &destination) {
    if (!hasImage() || !destination.isLocalFile()) return false;
    const auto &photo = m_photos[m_currentIndex]; QString error;
    if (!XmpSidecar::writeNew(destination.toLocalFile(),photo.state,photo.tags,photo.rating,photo.flag,&error)) {
        setStatus(uiText("XMP 导出失败：", "XMP export failed: ")+error); return false;
    }
    setStatus(uiText("XMP 已导出：", "XMP exported: ")+destination.toLocalFile()); return true;
}
bool PhotoController::importXmp(const QUrl &source) {
    if (!hasImage() || !source.isLocalFile()) return false;
    XmpSidecar::Document document; QString error;
    if (!XmpSidecar::read(source.toLocalFile(),&document,&error)) {
        setStatus(uiText("XMP 导入失败：", "XMP import failed: ")+error); return false;
    }
    return applyParsedXmp(document);
}
bool PhotoController::applyParsedXmp(const XmpSidecar::Document &document) {
    if (!hasImage() || (document.hasAdjustments && !currentSourceAvailable())) return false;
    auto &photo = m_photos[m_currentIndex];
    photo.history.initialize(photo.state); photo.history.finish();
    if (document.hasRating) photo.rating = document.rating;
    if (document.hasFlag) photo.flag = document.flag;
    if (document.hasKeywords) photo.tags.keywords = document.tags.keywords;
    if (document.hasLabel) photo.tags.label = document.tags.label;
    if (document.hasAlbums) photo.tags.albums = document.tags.albums;
    if (m_project.isOpen()) {
        m_dirtyCuration.insert(photo.storageKey(),{photo.rating,photo.flag});
        m_dirtyTags.insert(photo.storageKey(),photo.tags);
    }
    if (document.hasAdjustments) { photo.state = document.adjustments; persistAndApply("xmp_import"); }
    enqueueEdits(); emit libraryChanged(); emit curationChanged();
    setStatus(document.warnings.isEmpty() ? uiText("XMP 已应用到当前版本", "XMP applied to current version")
        : uiText("XMP 已应用；部分外部属性未映射：", "XMP applied; some external properties were not mapped: ")+document.warnings.join("; "));
    return true;
}
void PhotoController::openXmpExportDialog() {
    if (!hasImage()) return;
    const auto key = m_photos[m_currentIndex].storageKey();
    const auto suffix = m_photos[m_currentIndex].copyKey.isEmpty() ? QString{} : "."+m_photos[m_currentIndex].copyKey.mid(11);
    const auto path = QFileDialog::getSaveFileName(nullptr,uiText("导出新的 XMP 侧车文件", "Export new XMP sidecar"),
        currentFile()+suffix+".xmp","XMP (*.xmp)");
    if (!path.isEmpty() && hasImage() && key == m_photos[m_currentIndex].storageKey())
        exportXmp(QUrl::fromLocalFile(path.endsWith(".xmp",Qt::CaseInsensitive) ? path : path+".xmp"));
}
void PhotoController::openXmpImportDialog() {
    if (!hasImage()) return;
    const auto key = m_photos[m_currentIndex].storageKey();
    const auto companion = XmpSidecar::discoverCompanion(currentFile());
    // Discovery is metadata-only. Never silently apply an external XMP to a
    // catalog: curation may be non-undoable and foreign edits are not mapped.
    if (companion.status == "ambiguous")
        setStatus(uiText("发现多个匹配的 XMP：请手动选择，避免覆盖当前目录信息",
                         "Multiple matching XMP sidecars: select one manually to avoid a curation conflict"));
    else if (companion.status == "unsafe")
        setStatus(uiText("匹配的 XMP 不安全或不可读：请检查文件后手动选择",
                         "Matching XMP sidecar is unsafe or unreadable: inspect it before selecting"));
    const QString selection=companion.status=="unique" ? companion.selectedPath
        : QFileInfo(currentFile()).absolutePath();
    const auto path = QFileDialog::getOpenFileName(nullptr,
        uiText("导入 XMP 到当前版本", "Import XMP into current version"),
        selection,"XMP (*.xmp *.XMP)");
    // QFileDialog runs a nested event loop; the selected project/photo may
    // have changed before it returns. Never preview an XMP for another version.
    if (path.isEmpty() || !hasImage()
        || key != m_photos[m_currentIndex].storageKey()) return;
    XmpSidecar::Document parsed; QString error;
    if (!XmpSidecar::read(path, &parsed, &error)) {
        setStatus(uiText("XMP 导入失败：", "XMP import failed: ") + error);
        return;
    }
    const auto &photo = m_photos[m_currentIndex];
    const auto impact = XmpSidecar::previewChanges(
        parsed, photo.state, photo.tags, photo.rating, photo.flag);
    if (!impact.any()) {
        setStatus(uiText("XMP 没有可应用的变化；Adobe 显影参数不会转换",
                         "No applicable XMP changes; Adobe Develop edits are not converted"));
        return;
    }
    QStringList fields;
    if (impact.adjustments) fields << uiText("显影参数", "Develop adjustments");
    if (impact.rating) fields << uiText("评分", "Rating");
    if (impact.flag) fields << uiText("旗标", "Flag");
    if (impact.keywords) fields << uiText("关键词", "Keywords");
    if (impact.label) fields << uiText("颜色标签", "Color label");
    if (impact.albums) fields << uiText("相册", "Albums");
    const auto priorState = photo.state.toJson();
    const auto priorTags = photo.tags.toJson();
    const int priorRating = photo.rating;
    const QString priorFlag = photo.flag;
    const QString warnings = parsed.warnings.isEmpty() ? QString{}
        : "\n\n" + uiText("外部字段提醒：", "External-field warnings: ")
          + "\n" + parsed.warnings.join("\n");
    const auto answer = QMessageBox::question(nullptr, uiText("预览 XMP 导入变化", "Preview XMP import changes"),
        uiText("即将替换当前版本中的这些字段：", "The following fields will be replaced in this version:")
        + "\n" + fields.join(uiText("、", ", "))
        + "\n\n" + uiText("只列出文件中存在且与当前值不同的字段。显影参数可撤销，目录标注不能撤销。Adobe 显影参数不会转换。确定应用吗？",
                              "Only present fields differing from the current version are listed. Develop changes are undoable; catalog annotations are not. Adobe Develop edits are not converted. Apply?")
        + warnings);
    if (answer != QMessageBox::Yes || !hasImage()
        || key != m_photos[m_currentIndex].storageKey()) return;
    const auto &current = m_photos[m_currentIndex];
    if (current.state.toJson() != priorState || current.tags.toJson() != priorTags
        || current.rating != priorRating || current.flag != priorFlag) {
        setStatus(uiText("照片在确认期间已更改；请重新检查 XMP 导入内容",
                         "The photo changed during confirmation; review the XMP import again"));
        return;
    }
    // Apply exactly the validated, previewed snapshot. A sidecar may change on
    // disk while the confirmation dialog is open; never parse it a second time.
    applyParsedXmp(parsed);
}

bool PhotoController::canUndo() const { return currentSourceAvailable() && m_photos[m_currentIndex].history.canUndo(); }
bool PhotoController::canRedo() const { return currentSourceAvailable() && m_photos[m_currentIndex].history.canRedo(); }
QVariantList PhotoController::editHistory() const {
    QVariantList out;
    if (!hasImage()) return out;
    const auto &history = m_photos[m_currentIndex].history;
    for (int i = 0; i < history.entries().size(); ++i)
        out.push_back(QVariantMap{{"action", history.entries()[i].action}, {"current", i == history.cursor()}});
    return out;
}
void PhotoController::undo() {
    if (!canUndo()) return;
    auto &photo = m_photos[m_currentIndex];
    photo.state = photo.history.undo();
    photo.timeline.editedAt = QDateTime::currentMSecsSinceEpoch(); emit libraryChanged();
    markDirty();
    ActionTrace::instance().record("edit_undo", {{"file", currentFile()}});
    emit adjustmentsChanged(); emit lookChanged(); emit historyChanged();
    applyCurrent();
}
void PhotoController::redo() {
    if (!canRedo()) return;
    auto &photo = m_photos[m_currentIndex];
    photo.state = photo.history.redo();
    photo.timeline.editedAt = QDateTime::currentMSecsSinceEpoch(); emit libraryChanged();
    markDirty();
    ActionTrace::instance().record("edit_redo", {{"file", currentFile()}});
    emit adjustmentsChanged(); emit lookChanged(); emit historyChanged();
    applyCurrent();
}

#define GETTER(name) double PhotoController::name() const { return currentState().name; }
GETTER(exposure)
GETTER(temperature)
GETTER(tint)
GETTER(contrast)
GETTER(highlights)
GETTER(shadows)
GETTER(whites)
GETTER(blacks)
GETTER(highlightRecovery)
GETTER(hue)
GETTER(saturation)
GETTER(vibrance)
GETTER(vignetteAmount)
GETTER(vignetteMidpoint)
GETTER(vignetteFeather)
#undef GETTER

QVariantList PhotoController::toVariantList(const AdjustmentState::ColorBandArray &values) {
    QVariantList out;
    out.reserve(AdjustmentState::ColorBandCount);
    for (double v : values) out.push_back(v);
    return out;
}
QVariantList PhotoController::toVariantList(const AdjustmentState::CurveArray &values) {
    QVariantList out;
    out.reserve(AdjustmentState::CurvePointCount);
    for (double v : values) out.push_back(v);
    return out;
}
QVariantList PhotoController::blackWhiteMix() const { return toVariantList(currentState().bwMix); }
QVariantList PhotoController::toVariantList(const AdjustmentState::GradingArray &values) {
    QVariantList out; out.reserve(AdjustmentState::GradingRegionCount);
    for (double v:values) out.push_back(v);
    return out;
}
QVariantList PhotoController::gradingHue() const { return toVariantList(currentState().gradingHue); }
QVariantList PhotoController::gradingSaturation() const { return toVariantList(currentState().gradingSaturation); }
QVariantList PhotoController::hslHue() const { return toVariantList(currentState().hslHue); }
QVariantList PhotoController::hslSaturation() const { return toVariantList(currentState().hslSaturation); }
QVariantList PhotoController::hslLuminance() const { return toVariantList(currentState().hslLuminance); }
QVariantList PhotoController::masterCurve() const { return toVariantList(currentState().masterCurve); }
QVariantList PhotoController::redCurve() const { return toVariantList(currentState().redCurve); }
QVariantList PhotoController::greenCurve() const { return toVariantList(currentState().greenCurve); }
QVariantList PhotoController::blueCurve() const { return toVariantList(currentState().blueCurve); }

void PhotoController::persistAndApply(const QString &action, const QVariantMap &details) {
    if (!hasImage()) return;
    QString mergeKey;
    if (action == "adjustment" || action == "brush_layer_exposure" || action == "vignette_adjustment" || action == "linear_mask_adjustment" || action == "radial_mask_adjustment" || action == "radial_mask_canvas_center" || action == "color_mixer" || action == "black_white_mix" || action == "color_grading_control" || action == "color_grading_wheel" || action == "color_grading_balance" || action == "curve_point" || action == "look_parameter" || action == "look_strength" || action == "geometry_straighten" || action == "geometry_correction") {
        QVariantMap keyDetails = details;
        keyDetails.remove("value");
        if (action=="color_grading_wheel") { keyDetails.remove("hue"); keyDetails.remove("saturation"); }
        mergeKey = action + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(keyDetails)).toJson(QJsonDocument::Compact));
    }
    auto &photo = m_photos[m_currentIndex];
    if (photo.history.record(photo.state, action, mergeKey)) {
        photo.timeline.editedAt = QDateTime::currentMSecsSinceEpoch();
        if (mergeKey.isEmpty()) emit libraryChanged();
    }
    emit historyChanged();
    QVariantMap payload = details;
    payload["file"] = currentFile();
    payload["photo_key"] = m_photos[m_currentIndex].storageKey();
    ActionTrace::instance().record(action, payload);
    markDirty();
    emit adjustmentsChanged();
    applyCurrent();
}

bool PhotoController::executeEditCommand(const QVariantMap &command) {
    if (!currentSourceAvailable()) return false;
    QString error; auto staged = m_photos[m_currentIndex].state;
    const CommandRegistry::LookContext context{currentIsRaw(),m_currentMetadata};
    if (!CommandRegistry::executeBatch(staged,{QJsonObject::fromVariantMap(command)},&error,&context)) {
        setStatus(uiText("命令未执行：", "Command rejected: ") + error); return false;
    }
    auto *state=mutableCurrentState(); if(!state)return false;
    m_photos[m_currentIndex].history.finish(); *state = std::move(staged);
    persistAndApply("command_replay",{{"command",command}}); return true;
}

bool PhotoController::executeEditCommands(const QVariantList &commands) {
    if (!currentSourceAvailable()) return false;
    if (commands.isEmpty()) return true;
    if (commands.size()>10000) {
        setStatus(uiText("命令批次超过 10000 条。", "Command batch exceeds 10000 entries.")); return false;
    }
    const auto batch=QJsonArray::fromVariantList(commands);
    QString error; auto staged=m_photos[m_currentIndex].state;
    const CommandRegistry::LookContext context{currentIsRaw(),m_currentMetadata};
    if (!CommandRegistry::executeBatch(staged,batch,&error,&context)) {
        setStatus(uiText("命令批次未执行：", "Command batch rejected: ")+error); return false;
    }
    auto *state=mutableCurrentState(); if(!state)return false;
    m_photos[m_currentIndex].history.finish(); *state=std::move(staged);
    persistAndApply("command_batch",{{"commands",commands}}); return true;
}

void PhotoController::setAdjustment(const char *name, double value, double AdjustmentState::*member) {
    auto *state = mutableCurrentState();
    if (!state || !std::isfinite(value) || qFuzzyCompare((*state).*member + 1.0, value + 1.0)) return;
    if (!CommandRegistry::set(*state, QString::fromLatin1(name), value)) return;
    value = (*state).*member;
    persistAndApply(QString::fromLatin1(name).startsWith("vignette") ? QStringLiteral("vignette_adjustment") : QStringLiteral("adjustment"),
        {{"parameter", QString::fromLatin1(name)}, {"value", value}});
}

void PhotoController::setExposure(double v) { setAdjustment("exposure", v, &AdjustmentState::exposure); }
void PhotoController::setTemperature(double v) { setAdjustment("temperature", v, &AdjustmentState::temperature); }
void PhotoController::setTint(double v) { setAdjustment("tint", v, &AdjustmentState::tint); }
void PhotoController::setContrast(double v) { setAdjustment("contrast", v, &AdjustmentState::contrast); }
void PhotoController::setHighlights(double v) { setAdjustment("highlights", v, &AdjustmentState::highlights); }
void PhotoController::setShadows(double v) { setAdjustment("shadows", v, &AdjustmentState::shadows); }
void PhotoController::setWhites(double v) { setAdjustment("whites", v, &AdjustmentState::whites); }
void PhotoController::setBlacks(double v) { setAdjustment("blacks", v, &AdjustmentState::blacks); }
void PhotoController::setHighlightRecovery(double v) { setAdjustment("highlightRecovery", std::clamp(v, 0.0, 100.0), &AdjustmentState::highlightRecovery); }
void PhotoController::setHue(double v) { setAdjustment("hue", std::clamp(v, -180.0, 180.0), &AdjustmentState::hue); }
void PhotoController::setSaturation(double v) { setAdjustment("saturation", std::clamp(v, -100.0, 100.0), &AdjustmentState::saturation); }
void PhotoController::setVibrance(double v) { setAdjustment("vibrance", std::clamp(v, -100.0, 100.0), &AdjustmentState::vibrance); }
void PhotoController::setVignetteAmount(double v) { setAdjustment("vignetteAmount",v,&AdjustmentState::vignetteAmount); }
void PhotoController::setVignetteMidpoint(double v) { setAdjustment("vignetteMidpoint",v,&AdjustmentState::vignetteMidpoint); }
void PhotoController::setVignetteFeather(double v) { setAdjustment("vignetteFeather",v,&AdjustmentState::vignetteFeather); }
void PhotoController::resetVignette() {
    auto *state=mutableCurrentState(); if (!state || (state->vignetteAmount==0 && state->vignetteMidpoint==.5 && state->vignetteFeather==1)) return;
    m_photos[m_currentIndex].history.finish(); CommandRegistry::execute(*state,{{"command","vignette.reset"}});
    persistAndApply("vignette_reset");
}

bool PhotoController::setLinearMaskEnabled(bool enabled) {
    auto *state=mutableCurrentState();
    if (!state) return false;
    if (state->linearMaskEnabled==enabled) return true;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","mask.linear.enable"},{"enabled",enabled}})) return false;
    persistAndApply("linear_mask_toggle",{{"enabled",enabled}});
    return true;
}
bool PhotoController::setLinearMaskParameter(const QString &parameter,double value) {
    auto *state=mutableCurrentState();
    if (!state || !std::isfinite(value)) return false;
    auto revised=*state;
    if (!CommandRegistry::execute(revised,{{"command","mask.linear.set"},
                                           {"parameter",parameter},{"value",value}})) return false;
    if (revised.toJson()==state->toJson()) return true;
    *state=std::move(revised);
    persistAndApply("linear_mask_adjustment",{{"parameter",parameter},{"value",value}});
    return true;
}
void PhotoController::resetLinearMask() {
    auto *state=mutableCurrentState();
    if (!state || !state->toJson().contains("linearMask")) return;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","mask.linear.reset"}})) return;
    persistAndApply("linear_mask_reset");
}
QPointF PhotoController::maskCanvasPoint(double x,double y,double width,double height) const {
    if (!hasImage() || !previewReady()) return MaskCanvasGeometry::invalidPoint();
    return MaskCanvasGeometry::imageAt({x,y},{width,height},m_previewFrameRect);
}

bool PhotoController::setRadialMaskCenter(double x,double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x<0 || x>1 || y<0 || y>1) return false;
    auto *state=mutableCurrentState();
    if (!state) return false;
    auto candidate=*state;
    if (!CommandRegistry::execute(candidate,{{"command","mask.radial.set"},
                                             {"parameter","centerX"},{"value",x}})
        || !CommandRegistry::execute(candidate,{{"command","mask.radial.set"},
                                                {"parameter","centerY"},{"value",y}})) return false;
    if (candidate.toJson()==state->toJson()) return true;
    *state=std::move(candidate);
    persistAndApply("radial_mask_canvas_center");
    return true;
}

bool PhotoController::setRadialMaskEnabled(bool enabled) {
    auto *state=mutableCurrentState(); if (!state) return false;
    if (state->radialMaskEnabled==enabled) return true;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","mask.radial.enable"},{"enabled",enabled}})) return false;
    persistAndApply("radial_mask_toggle",{{"enabled",enabled}});
    return true;
}
bool PhotoController::setRadialMaskInverted(bool inverted) {
    auto *state=mutableCurrentState(); if (!state) return false;
    if (state->radialMaskInverted==inverted) return true;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","mask.radial.invert"},{"inverted",inverted}})) return false;
    persistAndApply("radial_mask_invert",{{"inverted",inverted}});
    return true;
}
bool PhotoController::setRadialMaskParameter(const QString &parameter,double value) {
    auto *state=mutableCurrentState(); if (!state || !std::isfinite(value)) return false;
    auto changed=*state;
    if (!CommandRegistry::execute(changed,{{"command","mask.radial.set"},
                                           {"parameter",parameter},{"value",value}})) return false;
    if (changed.toJson()==state->toJson()) return true;
    *state=std::move(changed);
    persistAndApply("radial_mask_adjustment",{{"parameter",parameter},{"value",value}});
    return true;
}
void PhotoController::resetRadialMask() {
    auto *state=mutableCurrentState(); if (!state || !state->toJson().contains("radialMask")) return;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","mask.radial.reset"}})) return;
    persistAndApply("radial_mask_reset");
}
bool PhotoController::replaceBrushMaskDraft(const QJsonObject &document) {
    auto *state=mutableCurrentState();
    if (!state) return false;
    BrushMaskDocument::Document nextDraft;
    if (!document.isEmpty()) {
        const QJsonObject wrapper{{"brushMasks",document}};
        if (!AdjustmentState::validBrushMaskJson(wrapper)
            || !BrushMaskDocument::fromJson(QJsonValue(document),&nextDraft)
            || std::any_of(nextDraft.layers.begin(),nextDraft.layers.end(),[](const auto &layer){return layer.enabled;}))
            return false; // unchanged editor/photo/history on failure
    }
    auto next=*state;
    next.brushMasks=std::move(nextDraft);
    if (next.toJson()==state->toJson()) return true;
    m_photos[m_currentIndex].history.finish();
    *state=std::move(next);
    // Existing history/save path. Source bytes are never written. Current
    // rendering intentionally ignores disabled drafts; this is NOT painting.
    persistAndApply("brush_draft_replace",
        {{"layer_count",int(state->brushMasks.layers.size())},{"rendering",QStringLiteral("disabled")}});
    return true;
}
void PhotoController::setBlackWhite(bool enabled) {
    auto *state=mutableCurrentState(); if (!state || state->blackWhite==enabled) return;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","bw.enable"},{"enabled",enabled}})) return;
    persistAndApply("black_white_mode",{{"enabled",enabled}});
}
void PhotoController::setBlackWhiteMix(int band,double value) {
    auto *state=mutableCurrentState();
    if (!state || band<0 || band>=AdjustmentState::ColorBandCount || !std::isfinite(value)) return;
    value=std::clamp(value,-100.0,100.0);
    if (qFuzzyCompare(state->bwMix[std::size_t(band)]+1.0,value+1.0)) return;
    if (!CommandRegistry::execute(*state,{{"command","bw.set"},{"band",band},{"value",value}})) return;
    persistAndApply("black_white_mix",{{"band",band},{"value",value}});
}
void PhotoController::resetBlackWhite() {
    auto *state=mutableCurrentState(); if (!state) return;
    if (!state->blackWhite && std::all_of(state->bwMix.begin(),state->bwMix.end(),[](double v){return v==0;})) return;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","bw.reset"}})) return;
    persistAndApply("black_white_reset");
}

void PhotoController::setGradingEnabled(bool enabled) {
    auto *state=mutableCurrentState();
    if (!state || state->gradingEnabled==enabled) return;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","grading.enable"},{"enabled",enabled}})) return;
    persistAndApply("color_grading_mode",{{"enabled",enabled}});
}
void PhotoController::setGradingControl(int region,int component,double value) {
    auto *state=mutableCurrentState();
    if (!state || region<0 || region>=AdjustmentState::GradingRegionCount
        || component<0 || component>1 || !std::isfinite(value)) return;
    value=std::clamp(value,0.0,component==0?360.0:100.0);
    auto *array=component==0?&state->gradingHue:&state->gradingSaturation;
    if (qFuzzyCompare((*array)[std::size_t(region)]+1.0,value+1.0)) return;
    const QString name=component==0?QStringLiteral("hue"):QStringLiteral("saturation");
    if (!CommandRegistry::execute(*state,{{"command","grading.set"},{"region",region},
                                         {"component",name},{"value",value}})) return;
    persistAndApply("color_grading_control",{{"region",region},{"component",component},{"value",value}});
}
void PhotoController::setGradingWheel(int region,double hue,double saturation) {
    auto *state=mutableCurrentState();
    if (!state || region<0 || region>=AdjustmentState::GradingRegionCount
        || !std::isfinite(hue) || !std::isfinite(saturation)) return;
    hue=std::clamp(hue,0.0,360.0);
    saturation=std::clamp(saturation,0.0,100.0);
    if (qFuzzyCompare(state->gradingHue[std::size_t(region)]+1.0,hue+1.0)
        && qFuzzyCompare(state->gradingSaturation[std::size_t(region)]+1.0,saturation+1.0)) return;
    auto next=*state;
    if (!CommandRegistry::execute(next,{{"command","grading.set"},{"region",region},
                                         {"component","hue"},{"value",hue}})
        || !CommandRegistry::execute(next,{{"command","grading.set"},{"region",region},
                                            {"component","saturation"},{"value",saturation}})) return;
    *state=std::move(next);
    persistAndApply("color_grading_wheel",{{"region",region},{"hue",hue},{"saturation",saturation}});
}
void PhotoController::setGradingBalance(double value) {
    auto *state=mutableCurrentState();
    if (!state || !std::isfinite(value)) return;
    value=std::clamp(value,-100.0,100.0);
    if (qFuzzyCompare(state->gradingBalance+1.0,value+1.0)) return;
    if (!CommandRegistry::execute(*state,{{"command","grading.balance"},{"value",value}})) return;
    persistAndApply("color_grading_balance",{{"value",value}});
}
void PhotoController::resetColorGrading() {
    auto *state=mutableCurrentState();
    if (!state) return;
    if (!state->toJson().contains("colorGrading")) return;
    m_photos[m_currentIndex].history.finish();
    if (!CommandRegistry::execute(*state,{{"command","grading.reset"}})) return;
    persistAndApply("color_grading_reset");
}

void PhotoController::setColorMix(int band, int component, double value) {
    auto *state = mutableCurrentState();
    if (!state || !std::isfinite(value) || band < 0 || band >= AdjustmentState::ColorBandCount || component < 0 || component > 2) return;
    value = std::clamp(value, -100.0, 100.0);
    auto *array = component == 0 ? &state->hslHue : (component == 1 ? &state->hslSaturation : &state->hslLuminance);
    const std::size_t index = static_cast<std::size_t>(band);
    if (qFuzzyCompare((*array)[index] + 1.0, value + 1.0)) return;
    const QString componentName = component == 0 ? "hue" : component == 1 ? "saturation" : "luminance";
    CommandRegistry::execute(*state,{{"command","hsl.set"},{"band",band},{"component",componentName},{"value",value}});
    persistAndApply(QStringLiteral("color_mixer"), {{"band", band}, {"component", component}, {"value", value}});
}

void PhotoController::setCurvePoint(int channel, int point, double value) {
    auto *state = mutableCurrentState();
    if (!state || !std::isfinite(value) || channel < 0 || channel > 3 || point < 0 || point >= AdjustmentState::CurvePointCount) return;
    value = std::clamp(value, 0.0, 1.0);
    AdjustmentState::CurveArray *curve = &state->masterCurve;
    if (channel == 1) curve = &state->redCurve;
    else if (channel == 2) curve = &state->greenCurve;
    else if (channel == 3) curve = &state->blueCurve;
    const std::size_t index = static_cast<std::size_t>(point);
    if (qFuzzyCompare((*curve)[index] + 1.0, value + 1.0)) return;
    const QString channelName = channel == 0 ? "master" : channel == 1 ? "red" : channel == 2 ? "green" : "blue";
    CommandRegistry::execute(*state,{{"command","curve.set"},{"channel",channelName},{"point",point},{"value",value}});
    persistAndApply(QStringLiteral("curve_point"), {{"channel", channel}, {"point", point}, {"value", value}});
}

void PhotoController::resetCurve(int channel) {
    auto *state = mutableCurrentState();
    if (!state || channel < 0 || channel > 3) return;
    const QString channelName = channel == 0 ? "master" : channel == 1 ? "red" : channel == 2 ? "green" : "blue";
    CommandRegistry::execute(*state,{{"command","curve.reset"},{"channel",channelName}});
    persistAndApply(QStringLiteral("curve_reset"), {{"channel", channel}});
}

bool PhotoController::importPath(const QString &path, bool notifyImmediately) {
    if (path.isEmpty()) return false;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        qWarning() << "Import file does not exist" << path;
        ActionTrace::instance().record("import_rejected", {{"file", path}, {"reason", "not_found"}});
        if (notifyImmediately) setStatus(uiText(QStringLiteral("文件不存在：%1").arg(path), QStringLiteral("File not found: %1").arg(path)));
        return false;
    }

    const QString identity = info.canonicalFilePath();
    if (m_importedPaths.contains(identity)) {
        if (notifyImmediately) setStatus(uiText(QStringLiteral("照片已经在图片库中：%1").arg(info.fileName()), QStringLiteral("Already in library: %1").arg(info.fileName())));
        return false;
    }

    const bool isRaw = RawDecoder::isRawFile(path);
    if (!isRaw) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (!reader.canRead()) {
            qWarning() << "Unsupported image" << path << reader.errorString();
            ActionTrace::instance().record("import_rejected", {{"file", path}, {"reason", reader.errorString()}});
            if (notifyImmediately) setStatus(uiText(QStringLiteral("无法读取照片：%1").arg(info.fileName()), QStringLiteral("Cannot read image: %1").arg(info.fileName())));
            return false;
        }
    }

    PhotoEntry entry{info.absoluteFilePath(), info.fileName(), {}, isRaw};
    entry.sourceKey=entry.path;
    entry.timeline.importedAt = QDateTime::currentMSecsSinceEpoch();
    if (isRaw) entry.state.look.mode = "as-shot"; // New imports only; old projects remain off.
    m_photos.push_back(entry);
    m_importedPaths.insert(identity);
    markPhotoDirty(entry); m_catalogDateTimer.start();
    queueSourceIdentity(entry.path);
    ActionTrace::instance().record("import_file", {{"file", entry.path}, {"raw", isRaw}});

    if (notifyImmediately) {
        emit libraryChanged();
        if (m_currentIndex < 0) selectPhoto(m_photos.size() - 1);
        setStatus(uiText(QStringLiteral("已导入：%1").arg(entry.name), QStringLiteral("Imported: %1").arg(entry.name)));
    }
    return true;
}

void PhotoController::finishImportBatch(int added, int rawAdded) {
    if (added <= 0) {
        setStatus(uiText(QStringLiteral("没有导入新的照片"), QStringLiteral("No new photos were imported")));
        return;
    }
    emit libraryChanged();
    if (m_currentIndex < 0 && !m_photos.isEmpty()) selectPhoto(0);
    ActionTrace::instance().record("import_files", {{"count", added}, {"raw_count", rawAdded}});
    setStatus(uiText(QStringLiteral("已导入 %1 张照片，其中 RAW %2 张").arg(added).arg(rawAdded),
                     QStringLiteral("Imported %1 image(s), %2 RAW").arg(added).arg(rawAdded)));
}

void PhotoController::openImportDialog() {
    QSettings settings;
    const QString startDir = settings.value(QStringLiteral("ui/lastImportDir")).toString();
    const QString filter = uiText(
        QStringLiteral("支持的照片与 RAW (*.arw *.cr2 *.cr3 *.crw *.nef *.nrw *.raf *.rw2 *.orf *.dng *.pef *.srw *.rwl *.3fr *.erf *.kdc *.mos *.mrw *.x3f *.iiq *.raw *.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp);;RAW (*.arw *.cr2 *.cr3 *.crw *.nef *.nrw *.raf *.rw2 *.orf *.dng *.pef *.srw *.rwl *.3fr *.erf *.kdc *.mos *.mrw *.x3f *.iiq *.raw);;普通图片 (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp);;所有文件 (*)"),
        QStringLiteral("Supported photos and RAW (*.arw *.cr2 *.cr3 *.crw *.nef *.nrw *.raf *.rw2 *.orf *.dng *.pef *.srw *.rwl *.3fr *.erf *.kdc *.mos *.mrw *.x3f *.iiq *.raw *.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp);;RAW (*.arw *.cr2 *.cr3 *.crw *.nef *.nrw *.raf *.rw2 *.orf *.dng *.pef *.srw *.rwl *.3fr *.erf *.kdc *.mos *.mrw *.x3f *.iiq *.raw);;Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp);;All files (*)"));

    ActionTrace::instance().record("import_dialog_opened");
    const QStringList files = QFileDialog::getOpenFileNames(nullptr,
        uiText(QStringLiteral("导入 RAW / 照片"), QStringLiteral("Import RAW / Photos")), startDir, filter);

    if (files.isEmpty()) {
        ActionTrace::instance().record("import_dialog_cancelled");
        setStatus(uiText(QStringLiteral("已取消导入"), QStringLiteral("Import cancelled")));
        return;
    }

    settings.setValue(QStringLiteral("ui/lastImportDir"), QFileInfo(files.first()).absolutePath());
    int added = 0;
    int rawAdded = 0;
    for (const QString &path : files) {
        const bool raw = RawDecoder::isRawFile(path);
        if (importPath(path, false)) {
            ++added;
            if (raw) ++rawAdded;
        }
    }
    finishImportBatch(added, rawAdded);
}

QVariantMap PhotoController::previewImportNames(const QVariantList &urls,const QString &pattern,int sequenceStart) {
    QStringList sources;
    for (const auto &value : urls) {
        const auto url=value.canConvert<QUrl>() ? value.toUrl() : QUrl(value.toString());
        if (!url.isLocalFile()) return {{"valid",false},{"error",uiText("复制导入仅支持本地文件", "Copy import requires local files")},{"rows",QVariantList{}}};
        sources.push_back(url.toLocalFile());
    }
    const ImportNaming naming{pattern,sequenceStart};
    auto plan=planImportNames(sources,naming); QStringList captureTimes;
    if (plan.needsCaptureTimes) {
        QHash<QString,QString> missing;
        for (const auto &path : sources) {
            const auto stamp=FileNames::sourceStamp(path);
            if (stamp.isEmpty()) return {{"valid",false},{"pending",false},{"rows",QVariantList{}},
                {"error",uiText("无法读取源文件：", "Cannot read source: ")+QFileInfo(path).fileName()}};
            if (const auto *time=m_importNameTimes.object(stamp)) {
                captureTimes.push_back(*time); PerformanceRecorder::count("import_name_preview_cache_hits");
            } else { missing.insert(stamp,QFileInfo(path).canonicalFilePath()); captureTimes.push_back(QString()); }
        }
        if (!missing.isEmpty()) {
            if (missing!=m_importNamePending) { m_importNamePending=missing; m_importNameDatesJob->submit({missing}); }
            return {{"valid",false},{"pending",true},{"rows",QVariantList{}},{"error",uiText("读取拍摄时间…", "Reading capture times…")}};
        }
        plan=planImportNames(sources,naming,captureTimes);
    } else if (!m_importNamePending.isEmpty()) { m_importNameDatesJob->cancel(); m_importNamePending.clear(); }
    QVariantList rows;
    if (plan.error.isEmpty()) for (int i=0;i<sources.size();++i)
        rows.push_back(QVariantMap{{"source",QFileInfo(sources[i]).fileName()},{"destination",plan.names[i]}});
    return {{"valid",plan.error.isEmpty()},{"pending",false},{"error",uiText(plan.errorZh,plan.error)},
            {"rows",rows},{"captureTimes",captureTimes}};
}
void PhotoController::cancelImportNamePreview() {
    m_importNameDatesJob->cancel(); m_importNamePending.clear(); m_importNameTimes.clear();
    ++m_importNameRevision; emit importNamePreviewChanged();
}
bool PhotoController::copyImport(const QVariantList &urls,const QUrl &directory,const QString &pattern,int sequenceStart) {
    if (copyImportBusy() || !directory.isLocalFile() || urls.isEmpty() || urls.size()>1000) return false;
    QStringList sources;
    for (const auto &value : urls) {
        const auto url=value.canConvert<QUrl>() ? value.toUrl() : QUrl(value.toString());
        if (!url.isLocalFile()) { setStatus(uiText("复制导入仅支持本地文件", "Copy import requires local files")); return false; }
        sources.push_back(url.toLocalFile());
    }
    const ImportNaming naming{pattern,sequenceStart};
    const auto preview=previewImportNames(urls,pattern,sequenceStart);
    if (!preview["valid"].toBool()) { setStatus(uiText("复制名称计划错误：", "Copy name plan error: ")+preview["error"].toString()); return false; }
    const auto expected=preview["captureTimes"].toStringList();
    const auto names=planImportNames(sources,naming,expected);
    if (!m_copyImportQueue->start(sources,directory.toLocalFile(),naming,expected)) return false;
    m_copyImportProgress=0; m_copyImportStatus=uiText("检查复制计划…", "Checking copy plan…"); emit copyImportChanged();
    ActionTrace::instance().record("copy_import_started",{{"count",sources.size()},{"directory",directory.toLocalFile()},
        {"pattern",pattern},{"sequence_start",sequenceStart},{"names",QJsonArray::fromStringList(names.names)}});
    return true;
}
void PhotoController::openCopyImportDialog() {
    if (copyImportBusy()) return;
    QSettings settings;
    const auto files=QFileDialog::getOpenFileNames(nullptr,uiText("选择要复制并导入的照片", "Choose photos to copy and import"),
        settings.value("ui/lastImportDir").toString(),uiText("照片与 RAW (*)", "Photos and RAW (*)"));
    if (files.isEmpty()) return;
    settings.setValue("ui/lastImportDir",QFileInfo(files.first()).absolutePath());
    QVariantList urls; for (const auto &file : files) urls.push_back(QUrl::fromLocalFile(file));
    emit copyImportRequested(urls);
}
void PhotoController::cancelCopyImport() { m_copyImportQueue->cancel(); }
bool PhotoController::prepareToClose() {
    m_copyImportQueue->stopAndCollect();
    return flushEdits();
}

bool PhotoController::importFile(const QUrl &url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    return importPath(path, true);
}

void PhotoController::importFiles(const QVariantList &urls) {
    int added = 0, rawAdded = 0;
    for (const QVariant &v : urls) {
        const QUrl u = v.canConvert<QUrl>() ? v.toUrl() : QUrl(v.toString());
        const QString path = u.isLocalFile() ? u.toLocalFile() : u.toString();
        const bool raw = RawDecoder::isRawFile(path);
        if (importPath(path, false)) { ++added; if (raw) ++rawAdded; }
    }
    finishImportBatch(added, rawAdded);
}

void PhotoController::selectPhoto(int index) {
    if (index < -1 || index >= m_photos.size() || (index == -1 && !m_photos.isEmpty())) return;
    m_selectedPhotos = index < 0 ? QSet<int>{} : QSet<int>{index}; emit libraryChanged();
    if (index == m_currentIndex && (!hasImage() || m_photos[index].available==sourceReadable(m_photos[index]))) return;
    if (m_cropEditing) { m_cropEditing = false; emit cropEditingChanged(); }
    enqueueEdits();
    if (hasImage()) m_photos[m_currentIndex].history.finish();
    m_currentIndex = index;
    if (hasImage()) m_photos[index].history.initialize(m_photos[index].state);
    emit historyChanged();
    ++m_photoEpoch;
    resetReference();
    ++m_requestedRevision;
    m_loader->cancel(); m_prepare->cancel(); m_render->cancel();
    m_scopeJob->cancel(); m_fullScopeJob->cancel(); m_prefetch->cancel();
    m_plotTimer.stop(); m_plotJob->cancel(); m_scopePlot={}; ++m_plotImageId; m_plotSubmittedRevision=0;
    if (m_provider) m_provider->setScopePlot({});
    emit scopePlotChanged();
    m_refineTimer.stop(); m_exactTimer.stop(); m_prefetchTimer.stop();
    m_centerX = m_centerY = .5;
    m_fullSource = {}; m_previewSource = {}; m_processedPreview = {};
    m_fastSource = {}; m_gpuSource = {}; m_loadedPreview = {}; m_loadedKey.clear();
    m_sourceIsFull = false; m_currentMetadata.clear(); m_scopes = {};
    m_scopesUpdating = true; m_scopesRank = -1; m_displayPixels = {};
    if(hasImage()) {
        const bool available=sourceReadable(m_photos[index]);
        if(available!=m_photos[index].available) {m_photos[index].available=available;emit libraryChanged();}
    }
    m_loading = currentSourceAvailable(); m_preparing = false; m_gpuActive = false; emit backendChanged();
    m_scopesLabel.clear();
    if (m_provider) m_provider->setImage({});
    ++m_previewRevision;
    emit currentIndexChanged(); emit curationChanged(); emit adjustmentsChanged(); emit currentMetadataChanged();
    emit previewUrlChanged(); emit gpuFrameChanged(); emit scopesChanged(); emit previewGeometryChanged();
    emit activityChanged();
    ActionTrace::instance().record("select_photo", {{"index", index}, {"file", currentFile()}, {"raw", currentIsRaw()}, {"photo_epoch", qint64(m_photoEpoch)}});
    if (currentSourceAvailable()) loadCurrent();
    else {
        m_scopesUpdating=false; setBusy(false);
        if(hasImage())setStatus(uiText("源文件缺失或不可读，版本、历史和标签已保留。请重新关联源文件。", "Source is missing or unreadable. Versions, history and tags are retained. Relink the source."));
    }
}

void PhotoController::loadCurrent() {
    if (!currentSourceAvailable()) return;
    setBusy(true);
    setStatus(uiText(QStringLiteral("正在后台读取：%1").arg(QFileInfo(currentFile()).fileName()), QStringLiteral("Loading in background: %1").arg(QFileInfo(currentFile()).fileName())));
    m_loader->submit({currentFile(), m_photoEpoch});
}

void PhotoController::applyCurrent() {
    // Another Develop action, including Undo/Redo, abandons an uncommitted box.
    if (m_cropEditing) { m_cropEditing = false; emit cropEditingChanged(); }
    cancelCalibration();
    ++m_requestedRevision;
    m_plotTimer.stop(); m_plotJob->cancel(); m_plotSubmittedRevision=0; emit scopePlotChanged();
    m_scopesUpdating = true; m_scopesRank = -1;
    m_scopeJob->cancel(); m_fullScopeJob->cancel();
    m_exactTimer.stop();
    m_interacting = true;
    m_refineTimer.start();
    emit scopesChanged();
    if (m_preparedGeometry != previewGeometry().toJson()) prepareCurrent();
    else scheduleRender(true);
}

bool PhotoController::createProject(const QUrl &folder, const QString &name) {
    if (copyImportBusy()) { setStatus(uiText("请等待复制导入完成，或先取消复制", "Wait for copy import to finish, or cancel it first")); return false; }
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (!flushEdits()) return false;
    const bool ok = m_project.create(path, name);
    ActionTrace::instance().record("create_project", {{"ok", ok}, {"path", path}, {"name", name}});
    if (ok) {
        cancelRelink(); ++m_catalogEpoch; m_identityJob->cancel(); m_identityPending.clear(); m_identityEstablished.clear();
        m_catalogDatesJob->cancel(); m_catalogDateTimer.start();
        // A new catalog stores absolute source keys; an opened legacy catalog
        // keeps its original relative keys until it is explicitly copied here.
        for (auto &photo : m_photos) {
            photo.sourceKey=photo.path;
            if (photo.copyKey.isEmpty()) photo.originalKey = photo.path;
        }
        for (const auto &p : m_photos) {
            if (!p.copyKey.isEmpty() && !m_project.addVirtualCopy(p.copyKey,p.path,p.versionName,p.state,p.history,p.tags,{p.rating,p.flag},p.timeline)) {
                setStatus(uiText("虚拟副本未能保存：", "Virtual copy save failed: ") + m_project.lastError()); return false;
            }
            m_dirtyEdits.insert(p.storageKey(), p.state);
            auto history = p.history; history.initialize(p.state);
            m_dirtyHistories.insert(p.storageKey(), history);
            m_dirtyCuration.insert(p.storageKey(), {p.rating, p.flag});
            m_dirtyTags.insert(p.storageKey(),p.tags);
            m_dirtyDates.insert(p.storageKey(),p.timeline);
        }
        enqueueEdits();
        for(const auto &photo:m_photos)if(photo.identity.isKnown() && !m_identityEstablished.contains(photo.path)) {
            if(!m_project.recordOriginalIdentity(photo.path,photo.identity)) {
                setStatus(uiText("原片内容身份未能复制：", "Source identity could not be copied: ")+m_project.lastError());return false;
            }
            m_identityEstablished.insert(photo.path);
        }
        for(const auto &photo:m_photos)if(sourceReadable(photo))queueSourceIdentity(photo.path);
        emit projectChanged();
        setStatus(uiText(QStringLiteral("项目已创建：") + m_project.projectName(), QStringLiteral("Project created: ") + m_project.projectName()));
    } else {
        setStatus(uiText(QStringLiteral("项目创建失败"), QStringLiteral("Project creation failed")));
    }
    return ok;
}

bool PhotoController::openProject(const QUrl &url) {
    if (copyImportBusy()) { setStatus(uiText("请等待复制导入完成，或先取消复制", "Wait for copy import to finish, or cancel it first")); return false; }
    if (!url.isLocalFile() || !flushEdits()) return false;
    QVector<ProjectDatabase::SavedPhoto> saved;
    const QString path = url.toLocalFile();
    if (!m_project.open(path, &saved)) {
        const QString error = m_project.lastError();
        setStatus(uiText(QStringLiteral("无法打开项目：") + error, QStringLiteral("Cannot open project: ") + error));
        ActionTrace::instance().record("project_open_failed", {{"path", path}, {"error", error}});
        return false;
    }

    // All callbacks are generation-guarded: no in-flight task from the old
    // project may publish a preview or scopes for this project's first photo.
    cancelRelink(); m_identityJob->cancel(); m_identityPending.clear(); m_identityEstablished.clear();
    m_plotTimer.stop(); m_plotJob->cancel(); m_scopePlot={}; ++m_plotImageId; m_plotSubmittedRevision=0;
    if (m_provider) m_provider->setScopePlot({});
    ++m_photoEpoch;
    ++m_requestedRevision;
    ++m_catalogEpoch; m_catalogDatesJob->cancel(); m_catalogDateTimer.stop();
    m_loader->cancel(); m_prepare->cancel(); m_render->cancel();
    m_scopeJob->cancel(); m_fullScopeJob->cancel(); m_prefetch->cancel();
    m_refineTimer.stop(); m_exactTimer.stop(); m_prefetchTimer.stop();
    m_saveTimer.stop(); m_saveMaxTimer.stop();
    m_dirtyEdits.clear(); m_dirtyHistories.clear();
    m_dirtyCuration.clear(); m_dirtyTags.clear(); m_dirtyDates.clear(); m_selectedPhotos.clear();
    m_currentIndex = -1;
    m_fullSource = {}; m_previewSource = {}; m_processedPreview = {};
    m_fastSource = {}; m_gpuSource = {}; m_loadedPreview = {}; m_loadedKey.clear();
    m_currentMetadata.clear(); m_scopes = {}; m_scopesRank = -1;
    m_sourceIsFull = false; m_loading = false; m_rendering = false; m_gpuActive = false;
    m_scopesUpdating = true; m_displayPixels = {};
    m_referenceFiles.clear();
    resetReference();
    if (m_provider) m_provider->setImage({});
    ++m_previewRevision;
    m_photos.clear();
    emit historyChanged();
    m_importedPaths.clear();

    int missing = 0;
    for (const auto &record : saved) {
        const QFileInfo rawPath(record.path);
        const QFileInfo info(rawPath.isAbsolute() ? record.path : QDir(m_project.projectPath()).filePath(record.path));
        const bool available=info.isFile() && info.isReadable() && !info.canonicalFilePath().isEmpty();
        if (!available) ++missing;
        const QString identity = info.canonicalFilePath();
        const QString absolute = info.absoluteFilePath();
        m_photos.push_back({absolute, info.fileName() + (record.copyKey.isEmpty() ? QString() : " · " + record.versionName), record.adjustments,
                            RawDecoder::isRawFile(absolute), record.rating, record.flag, record.history, record.tags, record.copyKey, record.versionName, record.copyKey.isEmpty() ? record.path : QString(),record.timeline});
        auto &photo=m_photos.last();
        photo.available=available; photo.sourceKey=record.sourceKey.isEmpty()?record.path:record.sourceKey;
        photo.identity=record.identity;
        if(record.identity.isKnown())m_identityEstablished.insert(absolute);
        if (record.copyKey.isEmpty() && !identity.isEmpty()) m_importedPaths.insert(identity);
    }
    for (const auto &photo : m_photos) {
        const auto canonical=QFileInfo(photo.path).canonicalFilePath();
        if(!canonical.isEmpty())m_importedPaths.insert(canonical);
    }
    m_catalogDateTimer.start();
    QSettings().setValue(QStringLiteral("ui/lastProjectDir"), path);
    emit projectChanged(); emit libraryChanged(); emit currentIndexChanged(); emit curationChanged();
    emit currentMetadataChanged(); emit previewUrlChanged(); emit gpuFrameChanged();
    emit scopesChanged(); emit adjustmentsChanged(); emit backendChanged();
    emit scopePlotChanged();
    emit previewGeometryChanged(); emit activityChanged();
    if (!m_photos.isEmpty()) selectPhoto(0);

    setStatus(uiText(QStringLiteral("已打开项目：%1 · %2 个照片/版本 · %3 个版本的源文件缺失")
                          .arg(m_project.projectName()).arg(m_photos.size()).arg(missing),
                     QStringLiteral("Opened project: %1 · %2 photos/versions · %3 versions with missing sources")
                          .arg(m_project.projectName()).arg(m_photos.size()).arg(missing)));
    ActionTrace::instance().record("project_opened",
        {{"project", path}, {"available", m_photos.size()-missing}, {"missing", missing}});
    return true;
}

void PhotoController::setRating(int rating) {
    if (!hasImage()) return;
    rating = std::clamp(rating, 0, 5);
    auto &photo = m_photos[m_currentIndex];
    if (photo.rating == rating) return;
    photo.rating = rating;
    if (m_project.isOpen()) {
        m_dirtyCuration.insert(photo.storageKey(), {photo.rating, photo.flag});
        m_saveTimer.start();
        if (!m_saveMaxTimer.isActive()) m_saveMaxTimer.start();
    }
    ActionTrace::instance().record("photo_rating", {{"file", photo.path}, {"rating", rating}});
    emit libraryChanged(); emit curationChanged();
}
void PhotoController::setFlag(const QString &flag) {
    if (!hasImage() || (flag != QStringLiteral("none") && flag != QStringLiteral("pick") && flag != QStringLiteral("reject"))) return;
    auto &photo = m_photos[m_currentIndex];
    if (photo.flag == flag) return;
    photo.flag = flag;
    if (m_project.isOpen()) {
        m_dirtyCuration.insert(photo.storageKey(), {photo.rating, photo.flag});
        m_saveTimer.start();
        if (!m_saveMaxTimer.isActive()) m_saveMaxTimer.start();
    }
    ActionTrace::instance().record("photo_flag", {{"file", photo.path}, {"flag", flag}});
    emit libraryChanged(); emit curationChanged();
}

void PhotoController::resetAdjustments() {
    if (auto *state = mutableCurrentState()) {
        CommandRegistry::execute(*state,{{"command","develop.reset"}});
        persistAndApply(QStringLiteral("reset_adjustments"));
        setStatus(uiText(QStringLiteral("调整已重置"), QStringLiteral("Adjustments reset")));
    }
}

void PhotoController::copyAdjustments() {
    if (!hasImage()) return;
    m_clipboard = m_photos[m_currentIndex].state;
    m_clipboardName = m_photos[m_currentIndex].name;
    m_hasClipboard = true;
    emit adjustmentClipboardChanged();
    ActionTrace::instance().record("copy_adjustments", {{"file", currentFile()}});
    setStatus(uiText(QStringLiteral("已复制调整参数"), QStringLiteral("Adjustments copied")));
}

void PhotoController::pasteAdjustments() {
    pasteAdjustmentGroups(AdjustmentTransfer::allGroups());
}

bool PhotoController::pasteAdjustmentGroups(const QStringList &groups) {
    if (!currentSourceAvailable() || !m_hasClipboard || !AdjustmentTransfer::validGroups(groups)) return false;
    auto *state=mutableCurrentState();
    AdjustmentTransfer::apply(*state,m_clipboard,groups);
    m_photos[m_currentIndex].history.finish();
    persistAndApply(QStringLiteral("paste_adjustments"),{{"groups",groups},{"source_name",m_clipboardName}});
    setStatus(uiText(QStringLiteral("已粘贴调整参数"), QStringLiteral("Adjustments pasted")));
    return true;
}

void PhotoController::syncAdjustmentsToAll() {
    syncAdjustmentGroups(AdjustmentTransfer::allGroups(),false);
}

int PhotoController::syncAdjustmentGroups(const QStringList &groups,bool selectedOnly) {
    if (!currentSourceAvailable() || !AdjustmentTransfer::validGroups(groups)) return -1;
    for(int i=0;i<m_photos.size();++i)
        if(i!=m_currentIndex && (!selectedOnly || m_selectedPhotos.contains(i)) && !m_photos[i].available)return -1;
    const auto source=m_photos[m_currentIndex].state;
    const auto sourceKey=m_photos[m_currentIndex].storageKey();
    QStringList changedKeys; int targets=0;
    for (int i=0;i<m_photos.size();++i) {
        if (i==m_currentIndex || (selectedOnly && !m_selectedPhotos.contains(i))) continue;
        ++targets; auto &photo=m_photos[i];
        auto state=photo.state; AdjustmentTransfer::apply(state,source,groups);
        photo.history.initialize(photo.state);
        photo.history.finish();
        if (!photo.history.record(state,QStringLiteral("sync_adjustments"))) continue;
        photo.state=std::move(state); photo.timeline.editedAt=QDateTime::currentMSecsSinceEpoch();
        markPhotoDirty(photo);
        changedKeys.append(photo.storageKey());
    }
    ActionTrace::instance().record("sync_adjustments",QVariantMap{{"source_key",sourceKey},{"groups",groups},
        {"selected_only",selectedOnly},{"targets",targets},{"changed_keys",changedKeys},{"count",changedKeys.size()}});
    if (!changedKeys.isEmpty()) emit libraryChanged();
    setStatus(uiText("同步完成：更新 %1 / %2 个目标版本", "Sync complete: updated %1 / %2 target version(s)").arg(changedKeys.size()).arg(targets));
    // A failed write remains pending via the existing per-photo retry path.
    // Keep its save-error status visible instead of replacing it with success.
    if (m_project.isOpen()) flushEdits();
    return int(changedKeys.size());
}

bool PhotoController::exportCurrent(const QUrl &destination, const QString &colorSpaceKey, int quality) {
    if (!currentSourceAvailable() || !sourceReadable(m_photos[m_currentIndex]) || exportBusy() || m_relinkBusy) return false;
    QString path = destination.isLocalFile() ? destination.toLocalFile() : destination.toString();
    if (path.isEmpty()) return false;
    if (!QStringList{"jpg","jpeg","png","tif","tiff","webp"}.contains(QFileInfo(path).suffix().toLower())) path += ".jpg";
    if (isProtectedPhoto(path)) { setStatus(uiText("不能覆盖原图或参考图。", "Cannot overwrite an original or reference photograph.")); return false; }
    const auto target = ColorManagement::fromKey(colorSpaceKey);
    // Protect every imported original, not merely the current photograph.
    // canonicalFilePath also detects a symlink/alternate spelling of a source.
    const QFileInfo destinationInfo(path);
    for (const auto &photo : m_photos) {
        const QFileInfo original(photo.path);
        if (destinationInfo.absoluteFilePath() == original.absoluteFilePath()
            || (!destinationInfo.canonicalFilePath().isEmpty()
                && destinationInfo.canonicalFilePath() == original.canonicalFilePath())) {
            setStatus(uiText(QStringLiteral("不能覆盖原始照片。请选择新的文件名。"), QStringLiteral("Cannot overwrite an imported original. Choose a new filename.")));
            return false;
        }
    }
    flushEdits();
    quality = std::clamp(quality, 1, 100);
    const bool queued = m_exportQueue->start({{currentFile(), path, m_fullSource, currentState(), target, quality}});
    if (queued) {
        m_exportProgress = 0;
        QSettings().setValue("export/colorSpace", ColorManagement::key(target));
        QSettings().setValue("export/jpegQuality", quality);
        ActionTrace::instance().record("export_queued", {{"file", currentFile()}, {"destination", path}, {"quality", quality}, {"output_space", ColorManagement::key(target)}});
        setStatus(uiText(QStringLiteral("导出已开始，可继续编辑。"), QStringLiteral("Export started; editing remains available.")));
        emit exportChanged();
    }
    return queued; // Accepted, not a claim that the file has already been written.
}

QVariantMap PhotoController::exportNamePreview(const QString &pattern, int sequenceStart, const QString &format) const {
    QVector<ExportNameSource> sources; sources.reserve(m_photos.size());
    for (const auto &photo : m_photos) sources.push_back({photo.path,photo.versionName,photo.timeline.captureTime});
    const auto plan=planExportNames(sources,{pattern,sequenceStart},format);
    return {{"valid",plan.error.isEmpty()},{"names",plan.names.mid(0,3)},{"count",m_photos.size()},
            {"error",uiText(plan.errorZh,plan.error)}};
}

QStringList PhotoController::selectedVersionKeys() const {
    QStringList keys;
    for (qsizetype i=0;i<m_photos.size();++i)
        if (m_selectedPhotos.contains(int(i))) keys.append(m_photos[i].storageKey());
    return keys;
}

QVector<PhotoController::PhotoEntry> PhotoController::resolveExportVersions(const QStringList &keys, QString *error) const {
    if (error) error->clear();
    // The shared catalog resolver treats an empty request as all versions.
    // This explicit-selection API must reject it instead of widening scope.
    if (keys.isEmpty()) {
        if (error) *error=uiText("请选择至少一个照片或版本。", "Choose at least one photo or version.");
        return {};
    }
    if (keys.size()>1000) {
        if (error) *error=uiText("每次最多导出 1000 个所选照片或版本。", "Choose at most 1000 photos or versions for one export.");
        return {};
    }
    QStringList catalogKeys; catalogKeys.reserve(m_photos.size());
    for (const auto &photo:m_photos) catalogKeys.append(photo.storageKey());
    const auto selection=selectCatalogVersions(catalogKeys,keys);
    if (!selection.error.isEmpty()) {
        if (error) *error=uiText("导出选择无效或版本已移除。请重新选择并打开导出。",
                                "Export selection is invalid or a version was removed. Select versions and reopen export.");
        return {};
    }
    QVector<PhotoEntry> resolved; resolved.reserve(selection.indices.size());
    for (const auto index:selection.indices) resolved.append(m_photos[index]);
    return resolved;
}

QVariantMap PhotoController::exportVersionNamePreview(const QStringList &keys, const QString &pattern,
                                                      int sequenceStart, const QString &format) const {
    QString error;
    const auto resolved=resolveExportVersions(keys,&error);
    if (!error.isEmpty()) return {{"valid",false},{"names",QStringList{}},{"count",0},{"error",error}};
    QVector<ExportNameSource> sources; sources.reserve(resolved.size());
    for (const auto &photo:resolved) sources.append({photo.path,photo.versionName,photo.timeline.captureTime});
    const auto plan=planExportNames(sources,{pattern,sequenceStart},format);
    return {{"valid",plan.error.isEmpty()},{"names",plan.names.mid(0,3)},{"count",resolved.size()},
            {"error",uiText(plan.errorZh,plan.error)}};
}

bool PhotoController::exportVersions(const QStringList &keys, const QUrl &folder, const QString &colorSpaceKey,
                                     int quality, const QString &format, const QString &pattern, int sequenceStart) {
    if (exportBusy() || m_relinkBusy || !folder.isLocalFile()) return false;
    QString error;
    const auto resolved=resolveExportVersions(keys,&error);
    if (!error.isEmpty()) { setStatus(error); return false; }
    for(const auto &photo:resolved)if(!photo.available || !sourceReadable(photo)) {
        setStatus(uiText("所选版本包含缺失或不可读的源文件，请先重新关联。", "Selected versions include a missing or unreadable source. Relink it first.")); return false;
    }
    QDir directory(folder.toLocalFile());
    if (!directory.exists()) return false;
    // One resolved catalog-order snapshot drives both names and requests.
    // Neither a later selection nor an index shift can redirect the batch.
    QVector<ExportNameSource> sources; sources.reserve(resolved.size());
    for (const auto &photo:resolved) sources.append({photo.path,photo.versionName,photo.timeline.captureTime});
    const auto names=planExportNames(sources,{pattern,sequenceStart},format,
        directory.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot));
    if (!names.error.isEmpty()) { setStatus(uiText(names.errorZh,names.error)); return false; }
    for (const auto &name:names.names) {
        const auto path=directory.filePath(name);
        if (isProtectedPhoto(path)) {
            setStatus(uiText("不能覆盖原图或参考图。", "Cannot overwrite an original or reference photograph."));
            return false;
        }
        const QFileInfo target(path);
        if (target.exists() || target.isSymLink()) {
            setStatus(uiText("目标文件已存在，请选择新的文件名。", "Destination already exists. Choose a new filename."));
            return false;
        }
    }
    if (!flushEdits()) return false;
    QVector<ExportRequest> requests; requests.reserve(resolved.size());
    QStringList exportKeys; exportKeys.reserve(resolved.size());
    const auto target=ColorManagement::fromKey(colorSpaceKey);
    quality=std::clamp(quality,1,100);
    for (qsizetype i=0;i<resolved.size();++i) {
        const auto &photo=resolved[i];
        requests.append({photo.path,directory.filePath(names.names[i]),{},photo.state,target,quality,true});
        exportKeys.append(photo.storageKey());
    }
    const bool queued=m_exportQueue->start(std::move(requests));
    if (queued) {
        m_exportProgress=0;
        ActionTrace::instance().record("selected_export_queued",{{"count",resolved.size()},{"version_keys",QJsonArray::fromStringList(exportKeys)}});
        setStatus(uiText("所选版本导出已开始，可继续编辑。", "Selected-version export started; editing remains available."));
        emit exportChanged();
    }
    return queued;
}

bool PhotoController::exportAll(const QUrl &folder, const QString &colorSpaceKey, int quality, const QString &format,
                                const QString &pattern, int sequenceStart) {
    if (m_photos.isEmpty() || exportBusy() || m_relinkBusy || !folder.isLocalFile()) return false;
    for(const auto &photo:m_photos)if(!photo.available || !sourceReadable(photo)) {
        setStatus(uiText("目录包含缺失或不可读的源文件，请先重新关联或只导出可用版本。", "Catalog contains a missing or unreadable source. Relink it or export available selected versions.")); return false;
    }
    QDir directory(folder.toLocalFile());
    if (!directory.exists()) return false;
    QVector<ExportNameSource> sources; sources.reserve(m_photos.size());
    for (const auto &photo : m_photos) sources.push_back({photo.path,photo.versionName,photo.timeline.captureTime});
    const auto names=planExportNames(sources,{pattern,sequenceStart},format,
        directory.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot));
    if (!names.error.isEmpty()) { setStatus(uiText(names.errorZh,names.error)); return false; }
    if (!flushEdits()) return false;
    QVector<ExportRequest> requests;
    for (qsizetype i=0;i<m_photos.size();++i) {
        const auto &photo=m_photos[i];
        requests.push_back({photo.path,directory.filePath(names.names[i]),{},photo.state,
            ColorManagement::fromKey(colorSpaceKey),std::clamp(quality,1,100),true});
    }
    const bool queued = m_exportQueue->start(std::move(requests));
    if (queued) { m_exportProgress = 0; emit exportChanged(); }
    return queued;
}
void PhotoController::cancelExport() { m_exportQueue->cancel(); }

PhotoController::BugSnapshot PhotoController::freezeBugSnapshot() const {
    BugSnapshot snapshot;
    snapshot.revision = m_requestedRevision; snapshot.photoEpoch = m_photoEpoch;
    snapshot.state = currentState();
    snapshot.file = currentFile(); snapshot.project = projectPath(); snapshot.pipeline = pipelineDescription();
    snapshot.prepare.image = m_fullSource.isNull() ? m_loadedPreview : m_fullSource;
    snapshot.prepare.viewport = m_viewport;
    snapshot.prepare.zoom = m_zoom; snapshot.prepare.centerX = m_centerX; snapshot.prepare.centerY = m_centerY;
    snapshot.prepare.fullResolution = m_sourceIsFull; snapshot.prepare.geometry = snapshot.state.geometry;
    snapshot.plan = gpuPlan(true); snapshot.visiblePlan = gpuPlan();
    snapshot.gpuSource = gpuSource(); snapshot.gpuFrameRect = m_previewFrameRect;
    snapshot.shadowClip = m_scopes.shadowClipPercent; snapshot.highlightClip = m_scopes.highlightClipPercent;
    snapshot.scopeContext = {{"mode", scopesStatus()}, {"pixel_count", qint64(m_scopes.pixelCount)},
        {"parameter_revision", qint64(snapshot.revision)}, {"statistics_revision", qint64(m_scopesRevision)},
        {"is_current", m_scopesRevision == snapshot.revision}, {"viewport_preparing", m_preparing}};
    auto &values = snapshot.performanceValues;
    values["stage_dependencies"] = StageGraph::describe(m_loadedKey, snapshot.prepare, snapshot.state, currentIsRaw(), rawBaseExposureStops());
    if (m_renderCache) values["render_cache"] = m_renderCache->snapshot();
    if (m_fullScopesCache) values["full_scopes_cache"] = m_fullScopesCache->snapshot();
    if (m_analysisFrameCache) values["analysis_frame_cache"] = m_analysisFrameCache->snapshot();
    if (m_scopePlotCache) values["scope_plot_cache"] = m_scopePlotCache->snapshot();
    values["controller_state"] = QJsonObject{{"requested_revision", qint64(snapshot.revision)},
        {"scopes_revision", qint64(m_scopesRevision)}, {"scopes_mode", scopesStatus()},
        {"backend", processingBackend()}, {"loading", m_loading}, {"photo_epoch", qint64(snapshot.photoEpoch)}};
    values["scope_plot"] = QJsonObject{{"mode", m_scopeMode}, {"revision", qint64(m_plotRevision)},
        {"pixels", qint64(m_scopePlot.pixels)}, {"full_resolution", m_plotFull}, {"is_current", scopePlotCurrent()},
        {"error", m_scopePlot.error}, {"encoding", "sRGB encoded, before monitor ICC"}};
    values["look_context"] = QJsonObject::fromVariantMap({{"asShot", sonyLook()}, {"current", lookState()},
        {"reference", m_referenceInfo}, {"calibration", m_calibrationReport}});
    return snapshot;
}

QString PhotoController::createBugReport(const BugSnapshot &snapshot, QJsonObject gpuOutput) {
    // Both the CPU oracle and the bundle metadata use the initial request,
    // even if a photo, viewport, or parameter changes while the GPU responds.
    const auto diagnosticPreview = preparePreview(snapshot.prepare, {});
    auto diagnosticPlan = snapshot.plan; diagnosticPlan.setFrameRect(diagnosticPreview.frameRect);
    ImagePipeline::DiagnosticResult colorCapture;
    if(BrushMaskTileRaster::anyEnabled(diagnosticPlan.state.brushMasks)) {
        QString error;
        if(qint64(diagnosticPreview.normal.width())*diagnosticPreview.normal.height()*16<=FloatFrameFingerprint::BudgetBytes) {
            auto admitted=BrushRenderRequest::createPrepared(diagnosticPreview.normal,diagnosticPlan,
                BrushRenderRequest::Layout::Whole,{},&error,m_brushBudget);
            if(admitted)colorCapture.image=admitted->render({},&error).sharedImage();
        }else error="Admitted diagnostic frame exceeds the existing readback budget";
        colorCapture.stages={{"available",false},{"reason","Per-stage brush trace is unavailable"},
            {"final_frame_admitted",!colorCapture.image.isNull()},{"error",error}};
    }else colorCapture=ImagePipeline::diagnoseWithPlan(diagnosticPreview.normal,diagnosticPlan);
    const QImage capture = colorCapture.image;
    QElapsedTimer hashTimer; hashTimer.start();
    QJsonObject gpuContext{{"source_cache_key", QString::number(snapshot.gpuSource.cacheKey())},
        {"frame_rect", QJsonArray{snapshot.gpuFrameRect.x(), snapshot.gpuFrameRect.y(), snapshot.gpuFrameRect.width(), snapshot.gpuFrameRect.height()}},
        {"source", FloatFrameFingerprint::capture(snapshot.gpuSource)}};
    if (gpuOutput.value("available").toBool()) {
        // Frozen CPU oracle now consumes the identical FP32 source, without
        // clipping values above one before the shared color kernel.
        QImage reference;
        if(BrushMaskTileRaster::anyEnabled(snapshot.visiblePlan.state.brushMasks)) {
            if(snapshot.gpuSource.sizeInBytes()<=FloatFrameFingerprint::BudgetBytes) {
                auto admitted=BrushRenderRequest::createPrepared(snapshot.gpuSource,snapshot.visiblePlan,
                    BrushRenderRequest::Layout::Whole,{},nullptr,m_brushBudget);
                if(admitted)reference=admitted->render().sharedImage();
            }
        }else reference=ImagePipeline::processWithPlan(snapshot.gpuSource,snapshot.visiblePlan);
        gpuContext["cpu_reference"] = FloatFrameFingerprint::capture(ImagePipeline::floatSource(reference));
        gpuContext["reference_storage"] = "FP32 source -> CPU scene color kernel -> RGBA64 display quantization -> FP32 reference";
    }
    gpuOutput["request_id"] = QString::number(snapshot.request);
    const QJsonObject stageOutputs{{"schema",2},{"engine",ProcessingPlan::EngineVersion},
        {"parameter_revision",qint64(snapshot.revision)},{"photo_epoch",qint64(snapshot.photoEpoch)},
        {"source_is_full_resolution",snapshot.prepare.fullResolution},
        {"source",StageGraph::outputFingerprint(snapshot.prepare.image)},
        {"prepared_preview",StageGraph::outputFingerprint(diagnosticPreview.normal)},
        {"prepared_frame_rect",QJsonArray{diagnosticPreview.frameRect.x(),diagnosticPreview.frameRect.y(),diagnosticPreview.frameRect.width(),diagnosticPreview.frameRect.height()}},
        {"cpu_srgb_output",StageGraph::outputFingerprint(capture)},
        {"color_stages",colorCapture.stages},
        {"gpu_working_output",gpuOutput},{"gpu_context",gpuContext},
        {"monitor_icc","excluded; hashes precede screen presentation"},
        {"note","RGBA64/proxy and CPU color boundaries; explicit GPU final working-output fingerprint only; float RAW and internal GPU boundaries remain unavailable"}};
    PerformanceRecorder::sample("diagnostic_stage_hash_ms",hashTimer.nsecsElapsed()/1e6,{{"source_bytes",qint64(snapshot.prepare.image.sizeInBytes())}});
    for (auto it = snapshot.performanceValues.begin(); it != snapshot.performanceValues.end(); ++it)
        PerformanceRecorder::value(it.key(), it.value());
    const QString path = DiagnosticBundle::create(capture, snapshot.file, snapshot.project, snapshot.state,
        snapshot.shadowClip, snapshot.highlightClip, snapshot.pipeline, snapshot.scopeContext, stageOutputs);
    ActionTrace::instance().record("bug_snapshot_created", {{"path", path}, {"ok", !path.isEmpty()}});
    setStatus(path.isEmpty() ? uiText(QStringLiteral("诊断包生成失败"), QStringLiteral("Diagnostic bundle failed"))
                             : uiText(QStringLiteral("诊断包：") + path, QStringLiteral("Diagnostic bundle: ") + path));
    return path;
}

QString PhotoController::reportBug() {
    ActionTrace::instance().record("bug_snapshot_requested", {{"file", currentFile()}, {"pipeline", pipelineDescription()}});
    return createBugReport(freezeBugSnapshot(), {{"available", false}, {"error", "Synchronous report uses the CPU reference; GPU readback requires an asynchronous request"}});
}

quint64 PhotoController::diagnosticRequestId() const {
    return m_pendingBug && m_pendingBug->requestGpu ? m_pendingBug->request : 0;
}
quint64 PhotoController::diagnosticRevision() const { return m_pendingBug ? m_pendingBug->revision : 0; }
qint64 PhotoController::diagnosticSourceKey() const { return m_pendingBug ? m_pendingBug->gpuSource.cacheKey() : 0; }

quint64 PhotoController::startBugReport(bool showDialog) {
    if (m_closing || m_diagnosticBusy) return 0;
    m_pendingBug = std::make_unique<BugSnapshot>(freezeBugSnapshot());
    m_pendingBug->request = ++m_nextDiagnosticRequest;
    m_pendingBug->requestGpu = m_gpuEnabled && !m_loading && !m_preparing && !m_pendingBug->gpuSource.isNull();
    const quint64 request = m_pendingBug->request;
    m_diagnosticBusy = true; m_diagnosticDialog = showDialog;
    ActionTrace::instance().record("bug_snapshot_requested", {{"file", m_pendingBug->file}, {"request", QString::number(request)}});
    const QPointer<PhotoController> guard(this);
    emit diagnosticChanged();
    if (!guard || !m_pendingBug) return request;
    if (m_pendingBug->requestGpu) {
        setStatus(uiText(QStringLiteral("正在采集诊断快照…"), QStringLiteral("Capturing diagnostic snapshot…")));
        if (!guard || !m_pendingBug) return request;
        m_diagnosticTimer.start(3000);
        emit gpuFrameChanged();
    } else finishBugReport({{"available", false}, {"error", "GPU preview is disabled or not ready for this snapshot"}});
    return request;
}

qulonglong PhotoController::requestBugReport() { return startBugReport(false); }

void PhotoController::gpuDiagnosticReady(quint64 request, QJsonObject output) {
    if (m_closing || !m_pendingBug || request != m_pendingBug->request) return;
    if (output.value("available").toBool()
        && (output.value("parameter_revision").toInteger(-1) != qint64(m_pendingBug->revision)
            || output.value("source_cache_key").toString() != QString::number(m_pendingBug->gpuSource.cacheKey())))
        output = {{"available", false}, {"error", "GPU response does not match the frozen revision and source"}};
    finishBugReport(std::move(output));
}

void PhotoController::finishBugReport(QJsonObject gpuOutput) {
    if (m_closing || !m_pendingBug) return;
    m_diagnosticTimer.stop();
    const auto snapshot = std::move(m_pendingBug);
    const bool dialog = m_diagnosticDialog;
    const QPointer<PhotoController> guard(this);
    const QString path = createBugReport(*snapshot, std::move(gpuOutput));
    if (!guard) return;
    m_diagnosticBusy = false; m_diagnosticDialog = false;
    emit diagnosticChanged();
    if (!guard) return;
    emit diagnosticFinished(path);
    if (guard && dialog) showBugReport(path);
}

void PhotoController::reportBugWithDialog() {
    startBugReport(true);
}

void PhotoController::showBugReport(const QString &path) {
    if (path.isEmpty()) {
        QMessageBox::critical(nullptr,
            uiText(QStringLiteral("JixelLight 诊断"), QStringLiteral("JixelLight Diagnostics")),
            uiText(QStringLiteral("诊断包生成失败。请查看日志文件。"), QStringLiteral("Failed to create diagnostic bundle. Please check the log file.")));
        return;
    }
    QMessageBox::information(nullptr,
        uiText(QStringLiteral("诊断包已生成"), QStringLiteral("Diagnostic bundle created")),
        uiText(QStringLiteral("Bug 诊断包已经保存到：\n\n%1\n\n请把这个 ZIP 发给我，我可以根据日志和操作记录定位问题。").arg(path),
               QStringLiteral("The diagnostic ZIP was saved to:\n\n%1\n\nSend this ZIP to me so I can inspect the logs and action trace.").arg(path)));
}

void PhotoController::setStatus(const QString &message) {
    if (m_statusMessage == message) return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

PhotoController::~PhotoController() {
    m_closing = true;
    m_budgetRetryTimer.stop();
    m_relinkJob.reset(); m_identityJob.reset();
    m_importNameDatesJob.reset();
    m_copyImportQueue.reset();
    m_plotTimer.stop(); m_plotJob.reset();
    m_catalogDateTimer.stop(); m_catalogDatesJob.reset();
    m_referenceJob.reset(); m_calibrationJob.reset();
    m_saveTimer.stop(); m_saveMaxTimer.stop(); m_refineTimer.stop(); m_exactTimer.stop(); m_prefetchTimer.stop();
    m_loader.reset(); m_prefetch.reset(); m_prepare.reset(); m_render.reset(); m_scopeJob.reset(); m_fullScopeJob.reset();
    m_exportQueue.reset();
    flushEdits();
}

void PhotoController::initializeJobs() {
    m_importNameDatesJob=std::make_unique<LatestJob<ImportNameDateRequest,QHash<QString,QString>>>(
        [](const ImportNameDateRequest &request,const CancelToken &cancel) {
            QHash<QString,QString> result;
            for (auto it=request.sources.begin();it!=request.sources.end();++it) {
                if (cancelled(cancel)) return result;
                if (FileNames::sourceStamp(it.value())!=it.key()) continue;
                PerformanceRecorder::count("import_name_preview_metadata_reads");
                const auto time=PhotoTimeline::cameraTime(MetadataReader::read(it.value()).value("captureTime").toString());
                if (FileNames::sourceStamp(it.value())==it.key()) result.insert(it.key(),time);
            }
            return result;
        },[this](const ImportNameDateRequest &request,QHash<QString,QString> result) {
            if (m_closing || request.sources!=m_importNamePending) return;
            for (auto it=request.sources.begin();it!=request.sources.end();++it)
                if (FileNames::sourceStamp(it.value())==it.key()) m_importNameTimes.insert(it.key(),new QString(result.value(it.key())));
            m_importNamePending.clear(); ++m_importNameRevision; emit importNamePreviewChanged();
        });
    m_copyImportQueue=std::make_unique<CopyImportQueue>();
    connect(m_copyImportQueue.get(),&CopyImportQueue::progress,this,[this](qint64 copied,qint64 total,int completed,int files,const QString &stage) {
        m_copyImportProgress=total>0 ? std::clamp(double(copied)/total,0.0,1.0) : 0;
        m_copyImportStatus=(stage=="verify" ? uiText("校验副本", "Verifying copy") : uiText("复制导入", "Copy import"))
            +QString(" · %1/%2").arg(completed).arg(files); emit copyImportChanged();
    });
    connect(m_copyImportQueue.get(),&CopyImportQueue::finished,this,[this](const CopyImportResult &result) {
        if (m_closing) return;
        int added=0,raw=0;
        for (const auto &file : result.completed) {
            if (importPath(file.destination,false)) { ++added; if (RawDecoder::isRawFile(file.destination)) ++raw; }
            ActionTrace::instance().record("copy_import_published",{{"source",file.source},{"destination",file.destination},{"sha256",file.sha256}});
        }
        if (added) finishImportBatch(added,raw);
        m_copyImportProgress=result.error.isEmpty() && !result.wasCancelled ? 1 : m_copyImportProgress;
        m_copyImportStatus=uiText("复制完成 %1 个，导入 %2 张", "Copied %1 file(s), imported %2 image(s)").arg(result.completed.size()).arg(added);
        if (result.wasCancelled) m_copyImportStatus+=uiText(" · 已取消，保留已完成副本", " · cancelled; completed copies retained");
        if (!result.error.isEmpty()) m_copyImportStatus+=uiText(" · 错误：", " · error: ")+(m_language=="zh_CN" && !result.errorZh.isEmpty()?result.errorZh:result.error);
        setStatus(m_copyImportStatus); emit copyImportChanged();
        ActionTrace::instance().record("copy_import_finished",{{"copied",result.completed.size()},{"imported",added},{"cancelled",result.wasCancelled},{"error",result.error}});
    });
    m_plotTimer.setSingleShot(true); m_plotTimer.setInterval(250);
    connect(&m_plotTimer,&QTimer::timeout,this,&PhotoController::requestScopePlot);
    m_analysisFrameCache=std::make_shared<AnalysisFrameCache>();
    m_scopePlotCache=std::make_shared<ScopePlotCache>(16*1024*1024,m_analysisFrameCache,m_brushBudget);
    const auto plotCache=m_scopePlotCache;
    m_plotJob=std::make_unique<LatestJob<ScopePlotRequest,ScopePlotResult>>(
        [plotCache](const ScopePlotRequest &request,const CancelToken &cancel) { return plotCache->render(request,cancel); },
        [this](const ScopePlotRequest &request,ScopePlotResult result) {
            if (m_closing || request.revision!=m_requestedRevision || request.mode!=m_scopeMode || request.fullResolution!=m_exactScopes || m_preparing) return;
            if (result.image.isNull() && result.error.isEmpty()) result.error="Scope plot rendering failed";
            if(result.retryableBudget)deferBudgetJob(2,request.revision);
            else m_deferredBudgetJobs[2]={};
            if(result.image.isNull()&&!result.error.isEmpty()) {result.image=m_scopePlot.image;result.pixels=m_scopePlot.pixels;}
            m_scopePlot=std::move(result); m_plotRevision=request.revision; m_plotFull=request.fullResolution; ++m_plotImageId;
            if (m_provider) m_provider->setScopePlot(m_scopePlot.image);
            emit scopePlotChanged();
        });
    m_catalogDatesJob = std::make_unique<LatestJob<CatalogDateRequest,QHash<QString,QString>>>(
        [](const CatalogDateRequest &request, const CancelToken &cancel) {
            QHash<QString,QString> result;
            for (const auto &path : request.paths) {
                if (cancelled(cancel)) return result;
                result.insert(path,MetadataReader::read(path).value("captureTime").toString());
            }
            return result;
        }, [this](const CatalogDateRequest &request, QHash<QString,QString> result) {
            if (m_closing || request.epoch != m_catalogEpoch) return;
            bool changed = false;
            for (auto &photo : m_photos) if (result.contains(photo.path)) {
                const auto captured = PhotoTimeline::cameraTime(result.value(photo.path));
                if (photo.timeline.captureChecked && photo.timeline.captureTime == captured) continue;
                photo.timeline.captureChecked = true; photo.timeline.captureTime = captured;
                markPhotoDirty(photo); changed = true;
            }
            if (changed) emit libraryChanged();
        });
    m_catalogDateTimer.setSingleShot(true); m_catalogDateTimer.setInterval(50);
    connect(&m_catalogDateTimer,&QTimer::timeout,this,[this] {
        QStringList paths; for (const auto &photo : m_photos) if (photo.available && !photo.timeline.captureChecked) paths.append(photo.path);
        paths.removeDuplicates(); paths.sort();
        if (!paths.isEmpty()) m_catalogDatesJob->submit({m_catalogEpoch,paths});
    });
    m_sourceCache = std::make_shared<SourceCache>();
    m_renderCache = std::make_shared<RenderedPreviewCache>(64*1024*1024,m_brushBudget);
    m_fullScopesCache = std::make_shared<FullScopesCache>(4*1024*1024,m_analysisFrameCache,m_brushBudget);
    m_exportQueue = std::make_unique<ExportQueue>(m_sourceCache,nullptr,m_brushBudget);
    const auto cache = m_sourceCache;
    m_loader = std::make_unique<LatestJob<LoadRequest, SourceData>>(
        [this, cache](const LoadRequest &request, const CancelToken &cancel) {
            return loadSource(*cache, request.path, cancel, [this, request, cancel](SourceData data) {
                if (cancelled(cancel)) return;
                QMetaObject::invokeMethod(this, [this, request, data=std::move(data)]() mutable {
                    if (!m_closing) acceptSource(request.photo, std::move(data));
                }, Qt::QueuedConnection);
            });
        }, [this](const LoadRequest &request, SourceData data) { acceptSource(request.photo, std::move(data)); });
    m_prefetch = std::make_unique<LatestJob<LoadRequest, SourceData>>(
        [cache](const LoadRequest &request, const CancelToken &cancel) { return loadSource(*cache, request.path, cancel); },
        [](const LoadRequest &, SourceData) {});
    m_prepare = std::make_unique<LatestJob<PrepareRequest, PreparedPreview>>(preparePreview,
        [this](const PrepareRequest &request, PreparedPreview result) {
            if (request.photo != m_photoEpoch || request.generation != m_prepareGeneration) return;
            m_preparing = false;
            if (result.normal.isNull()) {
                setBusy(false);
                if (!result.error.isEmpty()) setStatus(result.error);
                return;
            }
            m_previewSource = result.normal; m_fastSource = result.fast; m_gpuSource = result.gpu;
            m_previewFrameRect=result.frameRect;
            m_displayPixels = result.displayPixels; m_viewportOnly = result.viewportOnly;
            emit previewGeometryChanged(); emit activityChanged();
            ++m_requestedRevision;
            m_scopesUpdating = true; m_scopesRank = -1;
            m_interacting = false;
            scheduleRender(false);
            if (m_exactScopes && !m_fullSource.isNull()) m_exactTimer.start();
        });
    const auto renderCache = m_renderCache;
    m_render = std::make_unique<LatestJob<RenderRequest, RenderedFrame>>(
        [renderCache](const RenderRequest &request, const CancelToken &cancel) {
            RenderedFrame result;BrushAdmissionFailure reason=BrushAdmissionFailure::None;
            try {result.image=renderCache->render(request.source,request.plan,cancel,&result.error,&reason);}
            catch (...) {result.error="Preview rendering failed";}
            result.retryableBudget=reason==BrushAdmissionFailure::BudgetUnavailable;return result;
        }, [this](const RenderRequest &request, RenderedFrame result) {
            if (request.revision != m_requestedRevision || m_gpuActive) {
                PerformanceRecorder::count("preview_results_discarded");
                return;
            }
            if (result.image.isNull()) {
                setBusy(false);
                if(result.error.isEmpty())result.error="Preview rendering failed";
                setStatus(result.retryableBudget?uiText("图像内存正被其他任务使用，暂时保留旧预览。","Image memory is busy; retaining the previous preview."):
                    uiText("预览处理失败：","Preview processing failed: ")+result.error);
                ScopesResult failed;failed.error=result.error.isEmpty()?QString("Preview rendering failed"):result.error;
                if(result.retryableBudget)deferBudgetJob(0,request.revision);else m_deferredBudgetJobs[0]={};
                failed.retryableBudget=result.retryableBudget;acceptScopes(request.revision,failed,0,{});
                return;
            }
            m_deferredBudgetJobs[0]={};auto image=std::move(result.image);
            m_processedPreview = image;
            if (m_provider) m_provider->setImage(image);
            ++m_previewRevision;
            emit previewUrlChanged();
            completeFrame(request.revision);
            // Publishing pixels never waits for histogram computation.
            m_scopeJob->submit({image, request.plan, request.revision, false});
        });
    m_scopeJob = std::make_unique<LatestJob<ScopeRequest, ScopesResult>>(
        [](const ScopeRequest &request, const CancelToken &cancel) { return ScopesEngine::analyze(request.image, 1024, cancel); },
        [this](const ScopeRequest &request, ScopesResult scopes) {
            acceptScopes(request.revision, scopes, 0, m_viewportOnly ? uiText(QStringLiteral("视区预览统计"), QStringLiteral("Viewport preview")) : uiText(QStringLiteral("预览统计"), QStringLiteral("Preview statistics")));
        });
    const auto fullScopesCache=m_fullScopesCache;
    m_fullScopeJob = std::make_unique<LatestJob<ScopeRequest, ScopesResult>>(
        [fullScopesCache](const ScopeRequest &request, const CancelToken &cancel) {
            try { return fullScopesCache->analyze(request, cancel); }
            catch (...) { return ScopesResult{}; }
        }, [this](const ScopeRequest &request, ScopesResult scopes) {
            if(!scopes.pixelCount&&scopes.error.isEmpty())scopes.error="Full-resolution statistics failed";
            acceptScopes(request.revision, scopes, 1, uiText(QStringLiteral("全分辨率统计"), QStringLiteral("Full-resolution statistics")));
        });
    for (QTimer *timer : {&m_saveTimer, &m_saveMaxTimer, &m_refineTimer, &m_exactTimer, &m_prefetchTimer}) timer->setSingleShot(true);
    m_saveTimer.setInterval(300); m_saveMaxTimer.setInterval(1000);
    m_refineTimer.setInterval(120); m_exactTimer.setInterval(600); m_prefetchTimer.setInterval(350);
    connect(&m_saveTimer, &QTimer::timeout, this, &PhotoController::enqueueEdits);
    connect(&m_saveMaxTimer, &QTimer::timeout, this, &PhotoController::enqueueEdits);
    connect(&m_refineTimer, &QTimer::timeout, this, &PhotoController::finishInteraction);
    connect(&m_exactTimer, &QTimer::timeout, this, &PhotoController::requestFullScopes);
    m_budgetRetryTimer.setSingleShot(true);m_budgetRetryTimer.setInterval(5000);
    connect(&m_budgetRetryTimer,&QTimer::timeout,this,[this]{retryBudgetJobs();});
    connect(&m_prefetchTimer, &QTimer::timeout, this, &PhotoController::prefetchNeighbor);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { flushEdits(); });
    connect(&m_project, &ProjectDatabase::writeFailed, this, [this](const QString &message) {
        // Retain a recoverable in-memory copy after an asynchronous SQL failure.
        for (const auto &photo : m_photos) {
            m_dirtyEdits.insert(photo.storageKey(), photo.state);
            auto history = photo.history; history.initialize(photo.state);
            m_dirtyHistories.insert(photo.storageKey(),history);
            m_dirtyTags.insert(photo.storageKey(),photo.tags);
            m_dirtyDates.insert(photo.storageKey(),photo.timeline);
            m_dirtyCuration.insert(photo.storageKey(), {photo.rating, photo.flag});
        }
        setStatus(uiText(QStringLiteral("保存失败（编辑仍保留在内存）：%1").arg(message), QStringLiteral("Save failed (edits retained in memory): %1").arg(message)));
        ActionTrace::instance().record("project_save_failed", {{"error", message}});
    });
    connect(m_exportQueue.get(), &ExportQueue::progress, this, [this](int completed, int total, int percent, const QString &) {
        m_exportProgress = total > 0 ? (completed + percent/100.0) / total : 0;
        emit exportChanged();
    });
    connect(m_exportQueue.get(), &ExportQueue::fileFinished, this, [this](const QString &source, const QString &destination, bool ok, const QString &error) {
        ActionTrace::instance().record("export_image", {{"source", source}, {"destination", destination}, {"ok", ok}, {"error", error}});
        if (!ok) qWarning() << "Export failed" << source << error;
    });
    connect(m_exportQueue.get(), &ExportQueue::finished, this, [this](int succeeded, int failed, bool stopped) {
        if (!stopped && !failed) m_exportProgress = 1;
        setStatus(uiText(QStringLiteral("导出%1：成功 %2，失败 %3").arg(stopped ? QStringLiteral("已取消") : QStringLiteral("结束")).arg(succeeded).arg(failed),
                         QStringLiteral("Export %1: %2 succeeded, %3 failed").arg(stopped ? QStringLiteral("cancelled") : QStringLiteral("finished")).arg(succeeded).arg(failed)));
        emit exportChanged(); emit exportFinished(succeeded, failed, stopped);
        retryBudgetJobs(true);
    });
}

void PhotoController::acceptSource(quint64 photo, SourceData data) {
    if (photo != m_photoEpoch || m_closing) { PerformanceRecorder::count("source_results_discarded"); return; }
    if (!data.error.isEmpty()) {
        if(hasImage()) {
            m_photos[m_currentIndex].available=false;
            ++m_photoEpoch; ++m_requestedRevision;
            m_loader->cancel(); m_prepare->cancel(); m_render->cancel();
            m_scopeJob->cancel(); m_fullScopeJob->cancel(); m_plotJob->cancel();
            m_refineTimer.stop(); m_exactTimer.stop(); m_prefetchTimer.stop(); m_plotTimer.stop();
            m_fullSource={};m_loadedPreview={};m_previewSource={};m_processedPreview={};m_gpuSource={};m_fastSource={};
            m_scopes={};m_scopesLabel.clear();m_scopePlot={};m_preparing=false;m_gpuActive=false;m_sourceIsFull=false;m_currentMetadata.clear();
            if(m_provider){m_provider->setImage({});m_provider->setScopePlot({});}
            ++m_previewRevision;
            emit libraryChanged();emit currentIndexChanged();emit previewUrlChanged();emit gpuFrameChanged();
            emit scopesChanged();emit scopePlotChanged();emit backendChanged();
            emit currentMetadataChanged();
        }
        m_loading = false; setBusy(false); emit activityChanged();
        setStatus(uiText(QStringLiteral("读取失败：%1").arg(data.error), QStringLiteral("Load failed: %1").arg(data.error)));
        ActionTrace::instance().record("source_load_failed", {{"file", currentFile()}, {"error", data.error}});
        return;
    }
    if (data.image.isNull()) { m_loading=false;setBusy(false);emit activityChanged();setStatus(uiText(QStringLiteral("图像工作任务未返回有效结果。"),QStringLiteral("Image worker returned no valid result.")));return; }
    if (data.placeholder) {
        if (!m_previewSource.isNull() || !m_fullSource.isNull()) return;
        m_processedPreview = data.image;
        m_displayPixels = data.image.size().scaled(m_viewport, Qt::KeepAspectRatio);
        if (m_provider) m_provider->setImage(data.image);
        ++m_previewRevision;
        m_scopesLabel = uiText(QStringLiteral("相机占位图：不参与统计"), QStringLiteral("Camera placeholder: no statistics"));
        emit previewUrlChanged(); emit previewGeometryChanged(); emit scopesChanged();
        return;
    }
    if (m_sourceIsFull && !data.fullResolution) return;
    if (data.key == m_loadedKey && m_loadedPreview.cacheKey() == data.image.cacheKey() && m_sourceIsFull == data.fullResolution) return;
    m_loadedKey = data.key; m_loadedPreview = data.image; m_sourceIsFull = data.fullResolution;
    m_currentMetadata = data.metadata;
    updateCaptureTime(currentFile(),data.metadata.value("captureTime").toString());
    if (data.fullResolution) {
        m_fullSource = data.image; m_loading = false;
        queueSourceIdentity(currentFile());
        m_prefetchTimer.start();
        setStatus(uiText(QStringLiteral("已读取：%1 × %2 · 线性宽色域").arg(data.image.width()).arg(data.image.height()),
                         QStringLiteral("Loaded: %1 × %2 · linear wide gamut").arg(data.image.width()).arg(data.image.height())));
    }
    emit currentMetadataChanged(); emit activityChanged();
    if (m_showReference && m_referenceImage.isNull() && !m_referenceBusy) requestReference();
    prepareCurrent();
}

void PhotoController::prepareCurrent() {
    cancelBrushStroke();
    if (m_loadedPreview.isNull()) return;
    m_preparing = true;
    m_plotTimer.stop(); m_plotJob->cancel(); m_plotSubmittedRevision=0; emit scopePlotChanged();
    ++m_requestedRevision;
    m_render->cancel(); m_scopeJob->cancel(); m_fullScopeJob->cancel();
    m_scopesUpdating = true; m_scopesRank = -1;
    ++m_prepareGeneration;
    m_preparedGeometry = previewGeometry().toJson();
    m_prepare->submit({m_loadedPreview, m_photoEpoch, m_prepareGeneration, m_viewport, m_cropEditing ? 0 : m_zoom, m_centerX, m_centerY, m_sourceIsFull, previewGeometry()});
    emit gpuFrameChanged();
    setBusy(true);
}

void PhotoController::scheduleRender(bool fast) {
    if (m_previewSource.isNull() || m_preparing) return;
    setBusy(true);
    emit scopePlotChanged();
    m_renderClock.restart();
    emit gpuFrameChanged();
    if (m_gpuEnabled && m_gpuActive) { m_render->cancel(); return; }
    m_render->submit({fast ? m_fastSource : m_previewSource, gpuPlan(), m_requestedRevision, fast});
}

void PhotoController::finishInteraction() {
    cancelBrushStroke();
    if (hasImage()) m_photos[m_currentIndex].history.finish();
    emit libraryChanged();
    m_refineTimer.stop();
    m_interacting = false;
    enqueueEdits();
    if (m_exportQueue) m_exportQueue->setInteractive(false);
    if (!m_previewSource.isNull() && !(m_gpuEnabled && m_gpuActive)) {
        // A distinct revision prevents an unfinished low-resolution frame from
        // replacing the final quality frame after the mouse has been released.
        ++m_requestedRevision;
        m_scopesUpdating = true; m_scopesRank = -1;
        scheduleRender(false);
    }
    if (m_exactScopes && !m_fullSource.isNull()) m_exactTimer.start();
}

void PhotoController::completeFrame(quint64 revision) {
    if (revision != m_requestedRevision) return;
    if (m_renderClock.isValid()) PerformanceRecorder::sample("request_to_result_ms", m_renderClock.nsecsElapsed()/1e6, {{"backend", processingBackend()}, {"revision", qint64(revision)}});
    setBusy(false);
    if (m_scopeMode!="histogram" && m_plotSubmittedRevision!=m_requestedRevision && !m_plotTimer.isActive()) m_plotTimer.start();
    emit scopePlotChanged();
}
void PhotoController::setBusy(bool busy) {
    if (m_exportQueue) m_exportQueue->setInteractive(busy || m_interacting);
    if (m_rendering == busy) return;
    m_rendering = busy; emit activityChanged();
}

void PhotoController::setViewport(double width, double height, double dpr, double zoom, double centerX, double centerY) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(dpr) || !std::isfinite(zoom) || !std::isfinite(centerX) || !std::isfinite(centerY)) return;
    dpr = std::clamp(dpr, .5, 4.0); zoom = std::clamp(zoom, 0.0, 8.0);
    const QSize size(int(std::clamp(width * dpr, 64.0, 8192.0)), int(std::clamp(height * dpr, 64.0, 8192.0)));
    centerX = std::clamp(centerX, 0.0, 1.0); centerY = std::clamp(centerY, 0.0, 1.0);
    if (size == m_viewport && zoom == m_zoom && centerX == m_centerX && centerY == m_centerY && dpr == m_devicePixelRatio) return;
    m_viewport = size; m_devicePixelRatio = dpr; m_zoom = zoom; m_centerX = centerX; m_centerY = centerY;
    // Invalidate old viewport results immediately, before preparation finishes.
    ++m_requestedRevision;
    m_render->cancel(); m_scopeJob->cancel(); m_fullScopeJob->cancel();
    m_scopesUpdating = true; m_scopesRank = -1;
    prepareCurrent();
    emit scopesChanged();
}

void PhotoController::updateCaptureTime(const QString &path, const QString &time) {
    const auto captured = PhotoTimeline::cameraTime(time); bool changed = false;
    for (auto &photo : m_photos) if (photo.path == path && (!photo.timeline.captureChecked || photo.timeline.captureTime != captured)) {
        photo.timeline.captureChecked = true; photo.timeline.captureTime = captured;
        markPhotoDirty(photo); changed = true;
    }
    if (changed) emit libraryChanged();
}
void PhotoController::markDirty() {
    if (hasImage()) markPhotoDirty(m_photos[m_currentIndex]);
}
void PhotoController::markPhotoDirty(const PhotoEntry &photo) {
    if (!m_project.isOpen()) return;
    const auto key = photo.storageKey();
    m_dirtyEdits.insert(key,photo.state);
    auto history = photo.history; history.initialize(photo.state);
    m_dirtyHistories.insert(key,history); m_dirtyDates.insert(key,photo.timeline);
    m_saveTimer.start();
    if (!m_saveMaxTimer.isActive()) m_saveMaxTimer.start();
}
void PhotoController::enqueueEdits() {
    m_saveTimer.stop(); m_saveMaxTimer.stop();
    if (!m_project.isOpen()) return;
    if (!m_dirtyEdits.isEmpty() && m_project.updateBatch(m_dirtyEdits, m_dirtyHistories,m_dirtyDates)) { m_dirtyEdits.clear(); m_dirtyHistories.clear(); m_dirtyDates.clear(); }
    if (!m_dirtyCuration.isEmpty() && m_project.updateCurationBatch(m_dirtyCuration)) m_dirtyCuration.clear();
    if (!m_dirtyTags.isEmpty() && m_project.updateTagsBatch(m_dirtyTags)) m_dirtyTags.clear();
}
bool PhotoController::flushEdits() {
    enqueueEdits();
    if (!m_project.isOpen()) return true;
    const bool ok = m_project.flush();
    if (!ok && !m_closing) setStatus(uiText(QStringLiteral("项目未能保存：%1").arg(m_project.lastError()), QStringLiteral("Project save failed: %1").arg(m_project.lastError())));
    return ok;
}

QString PhotoController::processingBackend() const {
    if (m_gpuEnabled && m_gpuActive) return m_backendName;
    return uiText(QStringLiteral("CPU 并行"), QStringLiteral("CPU parallel"));
}
void PhotoController::setGpuEnabled(bool enabled) {
    if (qEnvironmentVariableIsSet("JIXELLIGHT_FORCE_CPU")) enabled = false;
    if (enabled == m_gpuEnabled) return;
    m_gpuEnabled = enabled; m_gpuActive = false;
    QSettings().setValue("performance/gpuEnabled", enabled);
    emit backendChanged();
    ++m_requestedRevision; m_scopesRank = -1;
    scheduleRender(false);
}
void PhotoController::gpuPresented(quint64 revision, const QString &backend) {
    if (!m_gpuEnabled || revision != m_requestedRevision || m_gpuSource.isNull()) return;
    const bool changed = !m_gpuActive || m_backendName != backend;
    m_gpuActive = true; m_backendName = backend;
    m_render->cancel();
    if (changed) emit backendChanged();
    completeFrame(revision);
}
void PhotoController::gpuFailed(const QString &message) {
    if (!m_gpuEnabled) return;
    m_gpuEnabled = false; m_gpuActive = false;
    emit backendChanged();
    ActionTrace::instance().record("gpu_fallback", {{"error", message}});
    setStatus(uiText(QStringLiteral("GPU 不可用，已回退 CPU：%1").arg(message), QStringLiteral("GPU unavailable; using CPU: %1").arg(message)));
    ++m_requestedRevision;
    scheduleRender(false);
}
void PhotoController::gpuScopes(quint64 revision, const QByteArray &counts, quint64 pixels) {
    if (!m_gpuEnabled || revision != m_requestedRevision) return;
    const auto scopes = ScopesEngine::fromGpu(counts, pixels);
    acceptScopes(revision, scopes, 0, m_viewportOnly ? uiText(QStringLiteral("GPU 视区预览"), QStringLiteral("GPU viewport preview")) : uiText(QStringLiteral("GPU 预览统计"), QStringLiteral("GPU preview statistics")));
}
void PhotoController::acceptScopes(quint64 revision, const ScopesResult &scopes, int rank, const QString &label) {
    if (revision != m_requestedRevision) return;
    if(!scopes.pixelCount) {
        if(scopes.error.isEmpty())return;
        if(m_scopesErrorRevision!=revision||rank>=m_scopesErrorRank) {
            m_scopesError=scopes.error;m_scopesErrorRevision=revision;m_scopesErrorRank=rank;m_scopesUpdating=false;
        }
        if(rank==1) {
            if(scopes.retryableBudget)deferBudgetJob(1,revision);else m_deferredBudgetJobs[1]={};
        }
        emit scopesChanged();return;
    }
    if(rank<m_scopesRank)return;
    const bool keepError=m_scopesErrorRevision==revision&&!m_scopesError.isEmpty()&&rank<m_scopesErrorRank;
    if(!keepError){m_scopesError.clear();m_scopesErrorRank=-1;}
    if(rank==1)m_deferredBudgetJobs[1]={};
    m_scopes = scopes; m_scopesRevision = revision; m_scopesRank = rank; m_scopesLabel = label;
    m_scopesUpdating = m_exactScopes && rank < 1&&!keepError;
    emit scopesChanged();
}
QString PhotoController::scopesStatus() const {
    if(m_scopesErrorRevision==m_requestedRevision&&!m_scopesError.isEmpty())
        return m_deferredBudgetJobs[std::size_t(m_scopesErrorRank==1?1:0)].pending
            ?uiText("统计暂不可用：图像内存正被其他任务使用。","Statistics unavailable: image memory is busy.")
            :uiText("统计计算失败：","Statistics failed: ")+m_scopesError;
    if (m_previewSource.isNull()) return m_scopesLabel.isEmpty() ? uiText(QStringLiteral("尚无统计"), QStringLiteral("No statistics")) : m_scopesLabel;
    if (m_scopesRevision != m_requestedRevision) return uiText(QStringLiteral("更新中（旧统计）"), QStringLiteral("Updating (previous statistics)"));
    return m_scopesLabel + (m_cropEditing ? uiText(" · 裁剪编辑全图", " · uncropped crop preview") : QString())
        + (m_scopesUpdating ? uiText(QStringLiteral(" · 全分辨率更新中"), QStringLiteral(" · full resolution updating")) : QString());
}
void PhotoController::setExactScopes(bool enabled) {
    if (m_exactScopes == enabled) return;
    m_exactScopes = enabled;
    m_deferredBudgetJobs[1]={};m_deferredBudgetJobs[2]={};
    if(m_scopesErrorRank==1){m_scopesError.clear();m_scopesErrorRank=-1;}
    m_plotJob->cancel(); m_scopePlot={}; m_plotSubmittedRevision=0; emit scopePlotChanged();
    if (m_scopeMode!="histogram") m_plotTimer.start();
    m_fullScopeJob->cancel(); m_exactTimer.stop();
    m_scopesUpdating = enabled && m_scopesRank < 1;
    emit scopesChanged();
    if (enabled) requestFullScopes();
}
void PhotoController::requestFullScopes() {
    if (!m_exactScopes || m_fullSource.isNull() || m_interacting) return;
    // Preparation publishes a new revision and restarts the exact timer.
    // Starting now would compute geometry/statistics for an obsolete revision,
    // then cancel and repeat them as soon as the prepared preview arrives.
    if (m_preparing) {
        PerformanceRecorder::count("full_scopes_deferred_while_preparing");
        return;
    }
    if(m_scopesErrorRank==1){m_scopesError.clear();m_scopesErrorRank=-1;}
    m_scopesUpdating=true;emit scopesChanged();
    m_fullScopeJob->submit({m_fullSource, gpuPlan(true), m_requestedRevision, true, previewGeometry()});
}
void PhotoController::deferBudgetJob(int job,quint64 revision) {
    auto &state=m_deferredBudgetJobs[std::size_t(job)];
    if(state.revision!=revision)state={revision,0,false};
    state.pending=true;
    if(!m_closing&&state.attempts<8&&!m_budgetRetryTimer.isActive())m_budgetRetryTimer.start();
}
void PhotoController::retryBudgetJobs(bool afterExport) {
    m_budgetRetryTimer.stop();if(m_closing)return;
    bool later=false;
    for(int i=0;i<3;++i) {
        auto &state=m_deferredBudgetJobs[std::size_t(i)];
        if(!state.pending)continue;
        if(state.revision!=m_requestedRevision){state={};continue;}
        if(afterExport)state.attempts=0;
        if(state.attempts>=8)continue;
        if(m_preparing||m_interacting||(i==2&&m_rendering)){later=true;continue;}
        state.pending=false;++state.attempts;
        if(i==0){if(!m_gpuActive&&!m_previewSource.isNull())scheduleRender(false);}
        else if(i==1){if(m_exactScopes)requestFullScopes();}
        else if(m_scopeMode!="histogram")requestScopePlot();
    }
    if(later)m_budgetRetryTimer.start();
}
QString PhotoController::scopePlotUrl() const {
    return m_scopePlot.image.isNull() ? QString{} : QString("image://processed/scopes/%1").arg(m_plotImageId);
}
QString PhotoController::scopePlotStatus() const {
    if (!hasImage()) return uiText("尚无统计", "No statistics");
    if(m_scopePlot.retryableBudget&&m_plotRevision==m_requestedRevision&&m_plotFull==m_exactScopes)
        return uiText("示波器暂不可用：图像内存正被其他任务使用。","Scope unavailable: image memory is busy.");
    if (!m_scopePlot.error.isEmpty() && m_plotRevision==m_requestedRevision && m_plotFull==m_exactScopes)
        return uiText("CPU 示波器失败：", "CPU scope failed: ")+m_scopePlot.error;
    if (m_scopeMode=="histogram") return {};
    if (!scopePlotCurrent()) return uiText("CPU 示波器更新中（旧结果）", "CPU scope updating (previous result)");
    return uiText("CPU · ","CPU · ")+(m_plotFull ? uiText("全分辨率", "Full resolution") : uiText("当前视区预览", "Current viewport preview"))
        +QString(" · %1 px").arg(m_scopePlot.pixels)+(m_cropEditing ? uiText(" · 裁剪编辑全图", " · uncropped crop preview") : QString());
}
void PhotoController::setScopeMode(const QString &mode) {
    if (!QStringList{"histogram","waveform","parade","vectorscope"}.contains(mode) || mode==m_scopeMode) return;
    m_deferredBudgetJobs[2]={};
    m_scopeMode=mode; m_plotTimer.stop(); m_plotJob->cancel(); m_scopePlot={}; ++m_plotImageId; m_plotSubmittedRevision=0;
    if (m_provider) m_provider->setScopePlot({});
    emit scopePlotChanged();
    if (mode!="histogram") m_plotTimer.start();
}
void PhotoController::requestScopePlot() {
    if (m_closing || m_scopeMode=="histogram" || !hasImage() || m_preparing || m_rendering) return;
    ScopePlotRequest request;
    request.source=m_exactScopes ? m_fullSource : m_previewSource;
    if (request.source.isNull()) return;
    request.geometry=m_exactScopes ? previewGeometry() : GeometryState{};
    request.plan=gpuPlan(m_exactScopes); request.mode=m_scopeMode; request.revision=m_requestedRevision; request.fullResolution=m_exactScopes;
    m_plotSubmittedRevision=request.revision;
    m_plotJob->submit(std::move(request));
}
void PhotoController::prefetchNeighbor() {
    if (!hasImage() || m_loading || exportBusy()) return;
    const int next = m_currentIndex + 1 < m_photos.size() ? m_currentIndex + 1 : m_currentIndex - 1;
    if (next < 0 || !m_photos[next].available) return;
    if (m_fullSource.sizeInBytes() * 2 > m_sourceCache->budgetBytes()) return;
    m_prefetch->submit({m_photos[next].path, m_photoEpoch});
}
