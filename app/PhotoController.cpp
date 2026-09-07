#include "app/PhotoController.h"
#include "core/color/ColorManagement.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/metadata/MetadataReader.h"
#include "core/pipeline/ImagePipeline.h"
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
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include "diagnostics/PerformanceRecorder.h"

PhotoController::PhotoController(ProcessedImageProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider) {
    QSettings settings;
    m_language = settings.value(QStringLiteral("ui/language"), QStringLiteral("zh_CN")).toString();
    if (m_language != QStringLiteral("zh_CN") && m_language != QStringLiteral("en_US")) m_language = QStringLiteral("zh_CN");
    m_statusMessage = uiText(QStringLiteral("就绪"), QStringLiteral("Ready"));
    m_gpuEnabled = !qEnvironmentVariableIsSet("JIXELLIGHT_FORCE_CPU") && settings.value("performance/gpuEnabled", true).toBool();
    initializeJobs();
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

AdjustmentState PhotoController::currentState() const { return hasImage() ? m_photos[m_currentIndex].state : AdjustmentState{}; }
AdjustmentState *PhotoController::mutableCurrentState() { return hasImage() ? &m_photos[m_currentIndex].state : nullptr; }

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
    QVariantMap payload = details;
    payload["file"] = currentFile();
    ActionTrace::instance().record(action, payload);
    markDirty();
    emit adjustmentsChanged();
    applyCurrent();
}

void PhotoController::setAdjustment(const char *name, double value, double AdjustmentState::*member) {
    auto *state = mutableCurrentState();
    if (!state || !std::isfinite(value) || qFuzzyCompare((*state).*member + 1.0, value + 1.0)) return;
    value = std::clamp(value, member == &AdjustmentState::exposure ? -5.0 : -180.0, member == &AdjustmentState::exposure ? 5.0 : 180.0);
    (*state).*member = value;
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
    (*array)[index] = value;
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
    (*curve)[index] = value;
    persistAndApply(QStringLiteral("curve_point"), {{"channel", channel}, {"point", point}, {"value", value}});
}

void PhotoController::resetCurve(int channel) {
    auto *state = mutableCurrentState();
    if (!state || channel < 0 || channel > 3) return;
    const AdjustmentState::CurveArray identity{0.0, 0.25, 0.5, 0.75, 1.0};
    if (channel == 0) state->masterCurve = identity;
    else if (channel == 1) state->redCurve = identity;
    else if (channel == 2) state->greenCurve = identity;
    else state->blueCurve = identity;
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
    m_photos.push_back(entry);
    m_importedPaths.insert(identity);
    if (m_project.isOpen()) { m_dirtyEdits.insert(entry.path, entry.state); m_saveTimer.start(); if (!m_saveMaxTimer.isActive()) m_saveMaxTimer.start(); }
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
    if (index < 0 || index >= m_photos.size() || index == m_currentIndex) return;
    enqueueEdits();
    m_currentIndex = index;
    ++m_photoEpoch;
    ++m_requestedRevision;
    m_loader->cancel(); m_prepare->cancel(); m_render->cancel();
    m_scopeJob->cancel(); m_fullScopeJob->cancel(); m_prefetch->cancel();
    m_refineTimer.stop(); m_exactTimer.stop(); m_prefetchTimer.stop();
    m_centerX = m_centerY = .5;
    m_fullSource = {}; m_previewSource = {}; m_processedPreview = {};
    m_fastSource = {}; m_gpuSource = {}; m_loadedPreview = {}; m_loadedKey.clear();
    m_sourceIsFull = false; m_currentMetadata.clear(); m_scopes = {};
    m_scopesUpdating = true; m_scopesRank = -1; m_displayPixels = {};
    m_loading = true; m_gpuActive = false; emit backendChanged();
    if (m_provider) m_provider->setImage({});
    ++m_previewRevision;
    emit currentIndexChanged(); emit adjustmentsChanged(); emit currentMetadataChanged();
    emit previewUrlChanged(); emit gpuFrameChanged(); emit scopesChanged(); emit previewGeometryChanged();
    emit activityChanged();
    ActionTrace::instance().record("select_photo", {{"index", index}, {"file", currentFile()}, {"raw", currentIsRaw()}, {"photo_epoch", qint64(m_photoEpoch)}});
    loadCurrent();
}

void PhotoController::loadCurrent() {
    if (!hasImage()) return;
    setBusy(true);
    setStatus(uiText(QStringLiteral("正在后台读取：%1").arg(QFileInfo(currentFile()).fileName()), QStringLiteral("Loading in background: %1").arg(QFileInfo(currentFile()).fileName())));
    m_loader->submit({currentFile(), m_photoEpoch});
}

void PhotoController::applyCurrent() {
    ++m_requestedRevision;
    m_scopesUpdating = true; m_scopesRank = -1;
    m_scopeJob->cancel(); m_fullScopeJob->cancel();
    m_exactTimer.stop();
    m_interacting = true;
    m_refineTimer.start();
    emit scopesChanged();
    scheduleRender(true);
}

bool PhotoController::createProject(const QUrl &folder, const QString &name) {
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (!flushEdits()) return false;
    const bool ok = m_project.create(path, name);
    ActionTrace::instance().record("create_project", {{"ok", ok}, {"path", path}, {"name", name}});
    if (ok) {
        for (const auto &p : m_photos) m_dirtyEdits.insert(p.path, p.state);
        enqueueEdits();
        emit projectChanged();
        setStatus(uiText(QStringLiteral("项目已创建：") + m_project.projectName(), QStringLiteral("Project created: ") + m_project.projectName()));
    } else {
        setStatus(uiText(QStringLiteral("项目创建失败"), QStringLiteral("Project creation failed")));
    }
    return ok;
}

void PhotoController::resetAdjustments() {
    if (auto *state = mutableCurrentState()) {
        *state = {};
        markDirty();
        ActionTrace::instance().record("reset_adjustments", {{"file", currentFile()}});
        emit adjustmentsChanged();
        applyCurrent();
        setStatus(uiText(QStringLiteral("调整已重置"), QStringLiteral("Adjustments reset")));
    }
}

void PhotoController::copyAdjustments() {
    if (!hasImage()) return;
    m_clipboard = currentState();
    m_hasClipboard = true;
    ActionTrace::instance().record("copy_adjustments", {{"file", currentFile()}});
    setStatus(uiText(QStringLiteral("已复制调整参数"), QStringLiteral("Adjustments copied")));
}

void PhotoController::pasteAdjustments() {
    if (!hasImage() || !m_hasClipboard) return;
    *mutableCurrentState() = m_clipboard;
    markDirty();
    ActionTrace::instance().record("paste_adjustments", {{"file", currentFile()}});
    emit adjustmentsChanged();
    applyCurrent();
    setStatus(uiText(QStringLiteral("已粘贴调整参数"), QStringLiteral("Adjustments pasted")));
}

void PhotoController::syncAdjustmentsToAll() {
    if (!hasImage()) return;
    const auto state = currentState();
    for (auto &photo : m_photos) {
        photo.state = state;
        if (m_project.isOpen()) m_dirtyEdits.insert(photo.path, photo.state);
    }
    enqueueEdits();
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
    if (!path.endsWith(".jpg", Qt::CaseInsensitive) && !path.endsWith(".jpeg", Qt::CaseInsensitive)) path += ".jpg";
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

bool PhotoController::exportAll(const QUrl &folder, const QString &colorSpaceKey, int quality) {
    if (m_photos.isEmpty() || exportBusy() || !folder.isLocalFile()) return false;
    QDir directory(folder.toLocalFile());
    if (!directory.exists()) return false;
    flushEdits();
    QVector<ExportRequest> requests;
    QSet<QString> reserved;
    for (const auto &photo : m_photos) {
        QString stem = QFileInfo(photo.path).completeBaseName() + QStringLiteral("_JixelLight");
        QString path = directory.filePath(stem + ".jpg");
        int suffix = 1;
        while (QFileInfo::exists(path) || reserved.contains(path.toCaseFolded())) path = directory.filePath(stem + QStringLiteral("_%1.jpg").arg(suffix++));
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
    if (!m_previewSource.isNull())
        capture = ImagePipeline::process(m_previewSource, currentState(), ImagePipeline::InputEncoding::LinearProPhoto);
    PerformanceRecorder::value("controller_state", QJsonObject{{"requested_revision", qint64(m_requestedRevision)}, {"scopes_revision", qint64(m_scopesRevision)}, {"scopes_mode", scopesStatus()}, {"backend", processingBackend()}, {"loading", m_loading}});
    const QString path = DiagnosticBundle::create(capture, currentFile(), projectPath(), currentState(),
        m_scopes.shadowClipPercent, m_scopes.highlightClipPercent, pipelineDescription(),
        {{"mode", scopesStatus()}, {"pixel_count", qint64(m_scopes.pixelCount)},
         {"parameter_revision", qint64(m_requestedRevision)}, {"statistics_revision", qint64(m_scopesRevision)},
         {"is_current", m_scopesRevision == m_requestedRevision}, {"viewport_preparing", m_preparing}});
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
    m_saveTimer.stop(); m_saveMaxTimer.stop(); m_refineTimer.stop(); m_exactTimer.stop(); m_prefetchTimer.stop();
    m_loader.reset(); m_prefetch.reset(); m_prepare.reset(); m_render.reset(); m_scopeJob.reset(); m_fullScopeJob.reset();
    m_exportQueue.reset();
    flushEdits();
}

void PhotoController::initializeJobs() {
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
            try { return ScopesEngine::analyzeFull(request.image, request.plan, cancel); }
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
        for (const auto &photo : m_photos) m_dirtyEdits.insert(photo.path, photo.state);
        setStatus(uiText(QStringLiteral("保存失败（编辑仍保留在内存）：%1").arg(message), QStringLiteral("Save failed (edits retained in memory): %1").arg(message)));
        ActionTrace::instance().record("project_save_failed", {{"error", message}});
    });
    connect(m_exportQueue.get(), &ExportQueue::progress, this, [this](int completed, int total, int percent, const QString &) {
        m_exportProgress = total > 0 ? (completed + percent/100.0) / total : 0;
        emit exportChanged();
    });
    connect(m_exportQueue.get(), &ExportQueue::fileFinished, this, [this](const QString &source, const QString &destination, bool ok, const QString &error) {
        ActionTrace::instance().record("export_jpeg", {{"source", source}, {"destination", destination}, {"ok", ok}, {"error", error}});
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
    if (data.fullResolution) {
        m_fullSource = data.image; m_loading = false;
        m_prefetchTimer.start();
        setStatus(uiText(QStringLiteral("已读取：%1 × %2 · 线性宽色域").arg(data.image.width()).arg(data.image.height()),
                         QStringLiteral("Loaded: %1 × %2 · linear wide gamut").arg(data.image.width()).arg(data.image.height())));
    }
    emit currentMetadataChanged(); emit activityChanged();
    prepareCurrent();
}

void PhotoController::prepareCurrent() {
    if (m_loadedPreview.isNull()) return;
    m_preparing = true;
    ++m_requestedRevision;
    m_render->cancel(); m_scopeJob->cancel(); m_fullScopeJob->cancel();
    m_scopesUpdating = true; m_scopesRank = -1;
    ++m_prepareGeneration;
    m_prepare->submit({m_loadedPreview, m_photoEpoch, m_prepareGeneration, m_viewport, m_zoom, m_centerX, m_centerY, m_sourceIsFull});
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

void PhotoController::markDirty() {
    if (!m_project.isOpen() || !hasImage()) return;
    m_dirtyEdits.insert(currentFile(), currentState());
    m_saveTimer.start();
    if (!m_saveMaxTimer.isActive()) m_saveMaxTimer.start();
}
void PhotoController::enqueueEdits() {
    m_saveTimer.stop(); m_saveMaxTimer.stop();
    if (m_dirtyEdits.isEmpty() || !m_project.isOpen()) return;
    if (m_project.updateBatch(m_dirtyEdits)) m_dirtyEdits.clear();
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
    return m_scopesLabel + (m_scopesUpdating ? uiText(QStringLiteral(" · 全分辨率更新中"), QStringLiteral(" · full resolution updating")) : QString());
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
    m_fullScopeJob->submit({m_fullSource, gpuPlan(), m_requestedRevision, true});
}
void PhotoController::prefetchNeighbor() {
    if (!hasImage() || m_loading || exportBusy()) return;
    const int next = m_currentIndex + 1 < m_photos.size() ? m_currentIndex + 1 : m_currentIndex - 1;
    if (next < 0) return;
    if (m_fullSource.sizeInBytes() * 2 > m_sourceCache->budgetBytes()) return;
    m_prefetch->submit({m_photos[next].path, m_photoEpoch});
}
