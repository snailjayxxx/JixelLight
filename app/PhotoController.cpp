#include "app/PhotoController.h"
#include "core/color/ColorManagement.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/metadata/MetadataReader.h"
#include "core/metadata/XmpSidecar.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/StageGraph.h"
#include "core/commands/CommandRegistry.h"
#include "core/raw/RawDecoder.h"
#include "diagnostics/ActionTrace.h"
#include "diagnostics/DiagnosticBundle.h"

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QMessageBox>
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
#include "diagnostics/PerformanceRecorder.h"
#include "core/look/LookProfiles.h"

PhotoController::PhotoController(ProcessedImageProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider) {
    QSettings settings;
    m_language = settings.value(QStringLiteral("ui/language"), QStringLiteral("zh_CN")).toString();
    if (m_language != QStringLiteral("zh_CN") && m_language != QStringLiteral("en_US")) m_language = QStringLiteral("zh_CN");
    m_statusMessage = uiText(QStringLiteral("就绪"), QStringLiteral("Ready"));
    m_gpuEnabled = !qEnvironmentVariableIsSet("JIXELLIGHT_FORCE_CPU") && settings.value("performance/gpuEnabled", true).toBool();
    initializeJobs();
    initializeLookJobs();
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
    if (!hasImage()) return nullptr;
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
    const double original = double(m_loadedPreview.width()) / m_loadedPreview.height();
    const double width = std::min(1.0, aspect/original), height = std::min(1.0, original/aspect);
    setCrop((1-width)/2, (1-height)/2, width, height);
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
    if (!hasImage()) return false;
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
    const auto path = QFileDialog::getOpenFileName(nullptr,uiText("导入 XMP 到当前版本", "Import XMP into current version"),
        QFileInfo(currentFile()).absolutePath(),"XMP (*.xmp)");
    if (path.isEmpty()) return;
    const auto answer = QMessageBox::question(nullptr,uiText("应用 XMP", "Apply XMP"),
        uiText("应用到当前照片版本？\n文件中已有的评分、标签和关键词会替换当前值。JixelLight 显影可撤销；目录标注不能撤销。Adobe 显影参数不会转换。",
               "Apply to the current photo version?\nPresent ratings, labels and keywords replace current values. JixelLight Develop edits can be undone; catalog annotations cannot. Adobe Develop settings are not converted."));
    if (answer == QMessageBox::Yes && hasImage() && key == m_photos[m_currentIndex].storageKey()) importXmp(QUrl::fromLocalFile(path));
}

bool PhotoController::canUndo() const { return hasImage() && m_photos[m_currentIndex].history.canUndo(); }
bool PhotoController::canRedo() const { return hasImage() && m_photos[m_currentIndex].history.canRedo(); }
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
    if (action == "adjustment" || action == "color_mixer" || action == "curve_point" || action == "look_parameter" || action == "look_strength") {
        QVariantMap keyDetails = details;
        keyDetails.remove("value");
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
    auto *state = mutableCurrentState(); if (!state) return false;
    QString error; auto staged = *state;
    if (!CommandRegistry::execute(staged,QJsonObject::fromVariantMap(command),&error)) {
        setStatus(uiText("命令未执行：", "Command rejected: ") + error); return false;
    }
    m_photos[m_currentIndex].history.finish(); *state = std::move(staged);
    persistAndApply("command_replay",{{"command",command}}); return true;
}

void PhotoController::setAdjustment(const char *name, double value, double AdjustmentState::*member) {
    auto *state = mutableCurrentState();
    if (!state || !std::isfinite(value) || qFuzzyCompare((*state).*member + 1.0, value + 1.0)) return;
    if (!CommandRegistry::set(*state, QString::fromLatin1(name), value)) return;
    value = (*state).*member;
    persistAndApply(QStringLiteral("adjustment"), {{"parameter", QString::fromLatin1(name)}, {"value", value}});
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
    entry.timeline.importedAt = QDateTime::currentMSecsSinceEpoch();
    if (isRaw) entry.state.look.mode = "as-shot"; // New imports only; old projects remain off.
    m_photos.push_back(entry);
    m_importedPaths.insert(identity);
    markPhotoDirty(entry); m_catalogDateTimer.start();
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
    if (index == m_currentIndex) return;
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
    m_refineTimer.stop(); m_exactTimer.stop(); m_prefetchTimer.stop();
    m_centerX = m_centerY = .5;
    m_fullSource = {}; m_previewSource = {}; m_processedPreview = {};
    m_fastSource = {}; m_gpuSource = {}; m_loadedPreview = {}; m_loadedKey.clear();
    m_sourceIsFull = false; m_currentMetadata.clear(); m_scopes = {};
    m_scopesUpdating = true; m_scopesRank = -1; m_displayPixels = {};
    m_loading = hasImage(); m_preparing = false; m_gpuActive = false; emit backendChanged();
    if (m_provider) m_provider->setImage({});
    ++m_previewRevision;
    emit currentIndexChanged(); emit curationChanged(); emit adjustmentsChanged(); emit currentMetadataChanged();
    emit previewUrlChanged(); emit gpuFrameChanged(); emit scopesChanged(); emit previewGeometryChanged();
    emit activityChanged();
    ActionTrace::instance().record("select_photo", {{"index", index}, {"file", currentFile()}, {"raw", currentIsRaw()}, {"photo_epoch", qint64(m_photoEpoch)}});
    if (hasImage()) loadCurrent(); else setBusy(false);
}

void PhotoController::loadCurrent() {
    if (!hasImage()) return;
    setBusy(true);
    setStatus(uiText(QStringLiteral("正在后台读取：%1").arg(QFileInfo(currentFile()).fileName()), QStringLiteral("Loading in background: %1").arg(QFileInfo(currentFile()).fileName())));
    m_loader->submit({currentFile(), m_photoEpoch});
}

void PhotoController::applyCurrent() {
    // Another Develop action, including Undo/Redo, abandons an uncommitted box.
    if (m_cropEditing) { m_cropEditing = false; emit cropEditingChanged(); }
    cancelCalibration();
    ++m_requestedRevision;
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
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (!flushEdits()) return false;
    const bool ok = m_project.create(path, name);
    ActionTrace::instance().record("create_project", {{"ok", ok}, {"path", path}, {"name", name}});
    if (ok) {
        // A new catalog stores absolute source keys; an opened legacy catalog
        // keeps its original relative keys until it is explicitly copied here.
        for (auto &photo : m_photos) if (photo.copyKey.isEmpty()) photo.originalKey = photo.path;
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
        emit projectChanged();
        setStatus(uiText(QStringLiteral("项目已创建：") + m_project.projectName(), QStringLiteral("Project created: ") + m_project.projectName()));
    } else {
        setStatus(uiText(QStringLiteral("项目创建失败"), QStringLiteral("Project creation failed")));
    }
    return ok;
}

bool PhotoController::openProject(const QUrl &url) {
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
        if (!info.isFile() || !info.isReadable()) { ++missing; continue; }
        const QString identity = info.canonicalFilePath();
        if (identity.isEmpty() || (record.copyKey.isEmpty() && m_importedPaths.contains(identity))) continue;
        const QString absolute = info.absoluteFilePath();
        m_photos.push_back({absolute, info.fileName() + (record.copyKey.isEmpty() ? QString() : " · " + record.versionName), record.adjustments,
                            RawDecoder::isRawFile(absolute), record.rating, record.flag, record.history, record.tags, record.copyKey, record.versionName, record.copyKey.isEmpty() ? record.path : QString(),record.timeline});
        if (record.copyKey.isEmpty()) m_importedPaths.insert(identity);
    }
    for (const auto &photo : m_photos) m_importedPaths.insert(QFileInfo(photo.path).canonicalFilePath());
    m_catalogDateTimer.start();
    QSettings().setValue(QStringLiteral("ui/lastProjectDir"), path);
    emit projectChanged(); emit libraryChanged(); emit currentIndexChanged(); emit curationChanged();
    emit currentMetadataChanged(); emit previewUrlChanged(); emit gpuFrameChanged();
    emit scopesChanged(); emit adjustmentsChanged(); emit backendChanged();
    emit previewGeometryChanged(); emit activityChanged();
    if (!m_photos.isEmpty()) selectPhoto(0);

    setStatus(uiText(QStringLiteral("已打开项目：%1 · %2 张照片 · %3 个缺失文件保留在原目录中")
                          .arg(m_project.projectName()).arg(m_photos.size()).arg(missing),
                     QStringLiteral("Opened project: %1 · %2 photos · %3 missing files kept in catalog")
                          .arg(m_project.projectName()).arg(m_photos.size()).arg(missing)));
    ActionTrace::instance().record("project_opened",
        {{"project", path}, {"available", m_photos.size()}, {"missing", missing}});
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
    m_hasClipboard = true;
    ActionTrace::instance().record("copy_adjustments", {{"file", currentFile()}});
    setStatus(uiText(QStringLiteral("已复制调整参数"), QStringLiteral("Adjustments copied")));
}

void PhotoController::pasteAdjustments() {
    if (!hasImage() || !m_hasClipboard) return;
    *mutableCurrentState() = m_clipboard;
    persistAndApply(QStringLiteral("paste_adjustments"));
    setStatus(uiText(QStringLiteral("已粘贴调整参数"), QStringLiteral("Adjustments pasted")));
}

void PhotoController::syncAdjustmentsToAll() {
    if (!hasImage()) return;
    const auto state = m_photos[m_currentIndex].state;
    for (auto &photo : m_photos) {
        photo.history.initialize(photo.state);
        photo.history.finish();
        photo.state = state;
        if (photo.history.record(state, QStringLiteral("sync_adjustments"))) photo.timeline.editedAt = QDateTime::currentMSecsSinceEpoch();
        markPhotoDirty(photo);
    }
    enqueueEdits();
    emit historyChanged();
    ActionTrace::instance().record("sync_adjustments", {{"count", m_photos.size()}});
    emit libraryChanged();
    emit adjustmentsChanged();
    applyCurrent();
    setStatus(uiText(QStringLiteral("已同步到 %1 张照片").arg(m_photos.size()), QStringLiteral("Synced to %1 image(s)").arg(m_photos.size())));
}

bool PhotoController::exportCurrent(const QUrl &destination, const QString &colorSpaceKey, int quality) {
    if (!hasImage() || exportBusy()) return false;
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

bool PhotoController::exportAll(const QUrl &folder, const QString &colorSpaceKey, int quality, const QString &format) {
    const auto normalized=format.toLower();
    if (!QStringList{"png","jpeg","jpg","tif","tiff","webp"}.contains(normalized)) return false;
    const QString extension = normalized=="jpeg" ? ".jpg" : normalized=="tiff" ? ".tif" : "."+normalized;
    if (m_photos.isEmpty() || exportBusy() || !folder.isLocalFile()) return false;
    QDir directory(folder.toLocalFile());
    if (!directory.exists()) return false;
    flushEdits();
    QVector<ExportRequest> requests;
    QSet<QString> reserved;
    for (const auto &photo : m_photos) {
        QString stem = QFileInfo(photo.path).completeBaseName() + QStringLiteral("_JixelLight");
        QString path = directory.filePath(stem + extension);
        int suffix = 1;
        while (QFileInfo::exists(path) || reserved.contains(path.toCaseFolded())) path = directory.filePath(stem + QStringLiteral("_%1").arg(suffix++) + extension);
        reserved.insert(path.toCaseFolded());
        requests.push_back({photo.path, path, {}, photo.state, ColorManagement::fromKey(colorSpaceKey), std::clamp(quality, 1, 100)});
    }
    const bool queued = m_exportQueue->start(std::move(requests));
    if (queued) { m_exportProgress = 0; emit exportChanged(); }
    return queued;
}
void PhotoController::cancelExport() { m_exportQueue->cancel(); }

QString PhotoController::reportBug() {
    ActionTrace::instance().record("bug_snapshot_requested", {{"file", currentFile()}, {"pipeline", pipelineDescription()}});
    // Explicit, one-off reference capture. Never label a previous revision's
    // asynchronously displayed pixels as the current parameters.
    QImage capture;
    PrepareRequest stageRequest;
    stageRequest.image = m_fullSource.isNull() ? m_loadedPreview : m_fullSource;
    stageRequest.viewport = m_viewport;
    stageRequest.zoom = m_zoom; stageRequest.centerX = m_centerX; stageRequest.centerY = m_centerY;
    stageRequest.fullResolution = m_sourceIsFull;
    stageRequest.geometry = currentState().geometry;
    // Geometry may still be preparing asynchronously; capture the current
    // snapshot rather than labeling the previous viewport as this revision.
    const auto diagnosticPreview = preparePreview(stageRequest, {});
    if (!diagnosticPreview.normal.isNull()) capture = ImagePipeline::processWithPlan(diagnosticPreview.normal, gpuPlan());
    QElapsedTimer hashTimer; hashTimer.start();
    const QJsonObject stageOutputs{{"schema",1},{"engine",ProcessingPlan::EngineVersion},
        {"parameter_revision",qint64(m_requestedRevision)},{"source_is_full_resolution",m_sourceIsFull},
        {"source",StageGraph::outputFingerprint(stageRequest.image)},
        {"prepared_preview",StageGraph::outputFingerprint(diagnosticPreview.normal)},
        {"cpu_srgb_output",StageGraph::outputFingerprint(capture)},
        {"monitor_icc","excluded; hashes precede screen presentation"},
        {"note","actual RGBA64/proxy boundaries; not per-color-kernel or float-RAW stage hashes"}};
    PerformanceRecorder::sample("diagnostic_stage_hash_ms",hashTimer.nsecsElapsed()/1e6,{{"source_bytes",qint64(stageRequest.image.sizeInBytes())}});
    PerformanceRecorder::value("stage_dependencies", StageGraph::describe(m_loadedKey, stageRequest, currentState(), currentIsRaw(), rawBaseExposureStops()));
    PerformanceRecorder::value("controller_state", QJsonObject{{"requested_revision", qint64(m_requestedRevision)}, {"scopes_revision", qint64(m_scopesRevision)}, {"scopes_mode", scopesStatus()}, {"backend", processingBackend()}, {"loading", m_loading}});
    PerformanceRecorder::value("look_context", QJsonObject::fromVariantMap({{"asShot",sonyLook()},{"current",lookState()},{"reference",m_referenceInfo},{"calibration",m_calibrationReport}}));
    const QString path = DiagnosticBundle::create(capture, currentFile(), projectPath(), currentState(),
        m_scopes.shadowClipPercent, m_scopes.highlightClipPercent, pipelineDescription(),
        {{"mode", scopesStatus()}, {"pixel_count", qint64(m_scopes.pixelCount)},
         {"parameter_revision", qint64(m_requestedRevision)}, {"statistics_revision", qint64(m_scopesRevision)},
         {"is_current", m_scopesRevision == m_requestedRevision}, {"viewport_preparing", m_preparing}},stageOutputs);
    ActionTrace::instance().record("bug_snapshot_created", {{"path", path}, {"ok", !path.isEmpty()}});
    setStatus(path.isEmpty() ? uiText(QStringLiteral("诊断包生成失败"), QStringLiteral("Diagnostic bundle failed"))
                             : uiText(QStringLiteral("诊断包：") + path, QStringLiteral("Diagnostic bundle: ") + path));
    return path;
}

void PhotoController::reportBugWithDialog() {
    const QString path = reportBug();
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
    m_catalogDateTimer.stop(); m_catalogDatesJob.reset();
    m_referenceJob.reset(); m_calibrationJob.reset();
    m_saveTimer.stop(); m_saveMaxTimer.stop(); m_refineTimer.stop(); m_exactTimer.stop(); m_prefetchTimer.stop();
    m_loader.reset(); m_prefetch.reset(); m_prepare.reset(); m_render.reset(); m_scopeJob.reset(); m_fullScopeJob.reset();
    m_exportQueue.reset();
    flushEdits();
}

void PhotoController::initializeJobs() {
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
        QStringList paths; for (const auto &photo : m_photos) if (!photo.timeline.captureChecked) paths.append(photo.path);
        paths.removeDuplicates(); paths.sort();
        if (!paths.isEmpty()) m_catalogDatesJob->submit({m_catalogEpoch,paths});
    });
    m_sourceCache = std::make_shared<SourceCache>();
    m_exportQueue = std::make_unique<ExportQueue>(m_sourceCache);
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
            m_displayPixels = result.displayPixels; m_viewportOnly = result.viewportOnly;
            emit previewGeometryChanged(); emit activityChanged();
            ++m_requestedRevision;
            m_scopesUpdating = true; m_scopesRank = -1;
            m_interacting = false;
            scheduleRender(false);
            if (m_exactScopes && !m_fullSource.isNull()) m_exactTimer.start();
        });
    m_render = std::make_unique<LatestJob<RenderRequest, QImage>>(
        [](const RenderRequest &request, const CancelToken &cancel) {
            try { return ImagePipeline::processWithPlan(request.source, request.plan, cancel); }
            catch (...) { return QImage{}; }
        }, [this](const RenderRequest &request, QImage image) {
            if (request.revision != m_requestedRevision || m_gpuActive) {
                PerformanceRecorder::count("preview_results_discarded");
                return;
            }
            if (image.isNull()) { setBusy(false); setStatus(uiText(QStringLiteral("预览处理失败。"), QStringLiteral("Preview processing failed."))); return; }
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
    m_fullScopeJob = std::make_unique<LatestJob<ScopeRequest, ScopesResult>>(
        [](const ScopeRequest &request, const CancelToken &cancel) {
            try { return ScopesEngine::analyzeFull(request.geometry.apply(request.image), request.plan, cancel); }
            catch (...) { return ScopesResult{}; }
        }, [this](const ScopeRequest &request, ScopesResult scopes) {
            acceptScopes(request.revision, scopes, 1, uiText(QStringLiteral("全分辨率统计"), QStringLiteral("Full-resolution statistics")));
        });
    for (QTimer *timer : {&m_saveTimer, &m_saveMaxTimer, &m_refineTimer, &m_exactTimer, &m_prefetchTimer}) timer->setSingleShot(true);
    m_saveTimer.setInterval(300); m_saveMaxTimer.setInterval(1000);
    m_refineTimer.setInterval(120); m_exactTimer.setInterval(600); m_prefetchTimer.setInterval(350);
    connect(&m_saveTimer, &QTimer::timeout, this, &PhotoController::enqueueEdits);
    connect(&m_saveMaxTimer, &QTimer::timeout, this, &PhotoController::enqueueEdits);
    connect(&m_refineTimer, &QTimer::timeout, this, &PhotoController::finishInteraction);
    connect(&m_exactTimer, &QTimer::timeout, this, &PhotoController::requestFullScopes);
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
    });
}

void PhotoController::acceptSource(quint64 photo, SourceData data) {
    if (photo != m_photoEpoch || m_closing) { PerformanceRecorder::count("source_results_discarded"); return; }
    if (!data.error.isEmpty()) {
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
        m_prefetchTimer.start();
        setStatus(uiText(QStringLiteral("已读取：%1 × %2 · 线性宽色域").arg(data.image.width()).arg(data.image.height()),
                         QStringLiteral("Loaded: %1 × %2 · linear wide gamut").arg(data.image.width()).arg(data.image.height())));
    }
    emit currentMetadataChanged(); emit activityChanged();
    if (m_showReference && m_referenceImage.isNull() && !m_referenceBusy) requestReference();
    prepareCurrent();
}

void PhotoController::prepareCurrent() {
    if (m_loadedPreview.isNull()) return;
    m_preparing = true;
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
    m_renderClock.restart();
    emit gpuFrameChanged();
    if (m_gpuEnabled && m_gpuActive) { m_render->cancel(); return; }
    m_render->submit({fast ? m_fastSource : m_previewSource, gpuPlan(), m_requestedRevision, fast});
}

void PhotoController::finishInteraction() {
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
    if (revision != m_requestedRevision || !scopes.pixelCount || rank < m_scopesRank) return;
    m_scopes = scopes; m_scopesRevision = revision; m_scopesRank = rank; m_scopesLabel = label;
    m_scopesUpdating = m_exactScopes && rank < 1;
    emit scopesChanged();
}
QString PhotoController::scopesStatus() const {
    if (m_previewSource.isNull()) return m_scopesLabel.isEmpty() ? uiText(QStringLiteral("尚无统计"), QStringLiteral("No statistics")) : m_scopesLabel;
    if (m_scopesRevision != m_requestedRevision) return uiText(QStringLiteral("更新中（旧统计）"), QStringLiteral("Updating (previous statistics)"));
    return m_scopesLabel + (m_cropEditing ? uiText(" · 裁剪编辑全图", " · uncropped crop preview") : QString())
        + (m_scopesUpdating ? uiText(QStringLiteral(" · 全分辨率更新中"), QStringLiteral(" · full resolution updating")) : QString());
}
void PhotoController::setExactScopes(bool enabled) {
    if (m_exactScopes == enabled) return;
    m_exactScopes = enabled;
    m_fullScopeJob->cancel(); m_exactTimer.stop();
    m_scopesUpdating = enabled && m_scopesRank < 1;
    emit scopesChanged();
    if (enabled) requestFullScopes();
}
void PhotoController::requestFullScopes() {
    if (!m_exactScopes || m_fullSource.isNull() || m_interacting) return;
    m_fullScopeJob->submit({m_fullSource, gpuPlan(), m_requestedRevision, true, previewGeometry()});
}
void PhotoController::prefetchNeighbor() {
    if (!hasImage() || m_loading || exportBusy()) return;
    const int next = m_currentIndex + 1 < m_photos.size() ? m_currentIndex + 1 : m_currentIndex - 1;
    if (next < 0) return;
    if (m_fullSource.sizeInBytes() * 2 > m_sourceCache->budgetBytes()) return;
    m_prefetch->submit({m_photos[next].path, m_photoEpoch});
}
