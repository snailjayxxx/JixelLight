#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/raw/RawDecoder.h"
#include "diagnostics/ActionTrace.h"
#include "diagnostics/DiagnosticBundle.h"

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QSettings>
#include <QUrl>
#include <algorithm>

PhotoController::PhotoController(ProcessedImageProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider) {
    QSettings settings;
    m_language = settings.value(QStringLiteral("ui/language"), QStringLiteral("zh_CN")).toString();
    if (m_language != QStringLiteral("zh_CN") && m_language != QStringLiteral("en_US")) m_language = QStringLiteral("zh_CN");
    m_statusMessage = uiText(QStringLiteral("就绪"), QStringLiteral("Ready"));
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
    return hasImage() ? QString("image://processed/current?rev=%1").arg(m_previewRevision) : QString();
}
QString PhotoController::currentFile() const { return hasImage() ? m_photos[m_currentIndex].path : QString(); }
QString PhotoController::currentFormat() const {
    return hasImage() ? (m_photos[m_currentIndex].raw ? QStringLiteral("RAW") : QFileInfo(currentFile()).suffix().toUpper()) : QString();
}
bool PhotoController::currentIsRaw() const { return hasImage() && m_photos[m_currentIndex].raw; }
QString PhotoController::pipelineDescription() const {
    if (!hasImage()) return QString();
    return currentIsRaw()
        ? QStringLiteral("RAW → Camera WB/Matrix → Linear ProPhoto RGB → Perceptual Color/HSL → Tone/RGB Curves → Display sRGB")
        : QStringLiteral("sRGB → Linear ProPhoto RGB → Perceptual Color/HSL → Tone/RGB Curves → Display sRGB");
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
    if (m_project.isOpen()) m_project.updateAdjustment(currentFile(), currentState());
    emit adjustmentsChanged();
    applyCurrent();
}

void PhotoController::setAdjustment(const char *name, double value, double AdjustmentState::*member) {
    auto *state = mutableCurrentState();
    if (!state || qFuzzyCompare((*state).*member + 1.0, value + 1.0)) return;
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
    if (!state || band < 0 || band >= AdjustmentState::ColorBandCount || component < 0 || component > 2) return;
    value = std::clamp(value, -100.0, 100.0);
    auto *array = component == 0 ? &state->hslHue : (component == 1 ? &state->hslSaturation : &state->hslLuminance);
    const std::size_t index = static_cast<std::size_t>(band);
    if (qFuzzyCompare((*array)[index] + 1.0, value + 1.0)) return;
    (*array)[index] = value;
    persistAndApply(QStringLiteral("color_mixer"), {{"band", band}, {"component", component}, {"value", value}});
}

void PhotoController::setCurvePoint(int channel, int point, double value) {
    auto *state = mutableCurrentState();
    if (!state || channel < 0 || channel > 3 || point < 0 || point >= AdjustmentState::CurvePointCount) return;
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

    for (const auto &p : m_photos) {
        if (QFileInfo(p.path).absoluteFilePath() == info.absoluteFilePath()) {
            if (notifyImmediately) setStatus(uiText(QStringLiteral("照片已经在图片库中：%1").arg(info.fileName()), QStringLiteral("Already in library: %1").arg(info.fileName())));
            return false;
        }
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
    if (m_project.isOpen()) m_project.addOrUpdatePhoto(entry.path, entry.state);
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
    m_currentIndex = index;
    loadCurrent();
    ActionTrace::instance().record("select_photo", {{"index", index}, {"file", currentFile()}, {"raw", currentIsRaw()}});
    emit currentIndexChanged();
    emit libraryChanged();
    emit adjustmentsChanged();
}

void PhotoController::loadCurrent() {
    if (!hasImage()) return;
    m_fullSource = {};
    if (currentIsRaw()) {
        QString error;
        RawMetadata meta;
        m_fullSource = RawDecoder::decode(currentFile(), &error, &meta);
        if (m_fullSource.isNull()) {
            setStatus(uiText(QStringLiteral("RAW 解码失败：%1").arg(error), QStringLiteral("RAW decode failed: %1").arg(error)));
            qWarning() << "RAW decode failed" << currentFile() << error;
            ActionTrace::instance().record("raw_decode_failed", {{"file", currentFile()}, {"error", error}});
            return;
        }
        ActionTrace::instance().record("raw_decoded", {
            {"file", currentFile()}, {"make", meta.make}, {"model", meta.model},
            {"width", meta.width}, {"height", meta.height}, {"bits", meta.bitsPerChannel},
            {"working_space", meta.workingSpace}, {"demosaic", meta.demosaic},
            {"camera_matrix", meta.cameraMatrixEnabled}, {"camera_wb", meta.cameraWhiteBalanceEnabled},
            {"highlight_blend", meta.highlightBlendEnabled}
        });
        setStatus(uiText(
            QStringLiteral("RAW 已解码：%1 %2 · %3×%4 · %5-bit · %6").arg(meta.make, meta.model).arg(meta.width).arg(meta.height).arg(meta.bitsPerChannel).arg(meta.workingSpace),
            QStringLiteral("RAW decoded: %1 %2 · %3×%4 · %5-bit · %6").arg(meta.make, meta.model).arg(meta.width).arg(meta.height).arg(meta.bitsPerChannel).arg(meta.workingSpace)));
    } else {
        QImageReader reader(currentFile());
        reader.setAutoTransform(true);
        m_fullSource = reader.read();
        if (m_fullSource.isNull()) {
            setStatus(uiText(QStringLiteral("图片读取失败"), QStringLiteral("Failed to load image")));
            qWarning() << reader.errorString();
            return;
        }
    }

    m_previewSource = m_fullSource;
    if (std::max(m_previewSource.width(), m_previewSource.height()) > 2048)
        m_previewSource = m_previewSource.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    applyCurrent();
}

void PhotoController::applyCurrent() {
    if (!hasImage() || m_previewSource.isNull()) return;
    const auto encoding = currentIsRaw() ? ImagePipeline::InputEncoding::LinearProPhoto : ImagePipeline::InputEncoding::SRgb;
    m_processedPreview = ImagePipeline::process(m_previewSource, currentState(), encoding);
    m_scopes = ScopesEngine::analyze(m_processedPreview, 1024);
    if (m_provider) m_provider->setImage(m_processedPreview);
    ++m_previewRevision;
    emit previewUrlChanged();
    emit scopesChanged();
}

bool PhotoController::createProject(const QUrl &folder, const QString &name) {
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    const bool ok = m_project.create(path, name);
    ActionTrace::instance().record("create_project", {{"ok", ok}, {"path", path}, {"name", name}});
    if (ok) {
        for (const auto &p : m_photos) m_project.addOrUpdatePhoto(p.path, p.state);
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
        if (m_project.isOpen()) m_project.updateAdjustment(currentFile(), *state);
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
    if (m_project.isOpen()) m_project.updateAdjustment(currentFile(), m_clipboard);
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
        if (m_project.isOpen()) m_project.updateAdjustment(photo.path, photo.state);
    }
    ActionTrace::instance().record("sync_adjustments", {{"count", m_photos.size()}});
    emit libraryChanged();
    emit adjustmentsChanged();
    applyCurrent();
    setStatus(uiText(QStringLiteral("已同步到 %1 张照片").arg(m_photos.size()), QStringLiteral("Synced to %1 image(s)").arg(m_photos.size())));
}

bool PhotoController::exportCurrent(const QUrl &destination) {
    if (!hasImage() || m_fullSource.isNull()) return false;
    QString path = destination.isLocalFile() ? destination.toLocalFile() : destination.toString();
    if (path.isEmpty()) return false;
    if (!path.endsWith(".jpg", Qt::CaseInsensitive) && !path.endsWith(".jpeg", Qt::CaseInsensitive)) path += ".jpg";
    const auto encoding = currentIsRaw() ? ImagePipeline::InputEncoding::LinearProPhoto : ImagePipeline::InputEncoding::SRgb;
    const QImage output = ImagePipeline::process(m_fullSource, currentState(), encoding);
    const bool ok = output.save(path, "JPEG", 92);
    ActionTrace::instance().record("export_jpeg", {{"ok", ok}, {"source", currentFile()}, {"destination", path}, {"source_raw", currentIsRaw()}, {"pipeline", pipelineDescription()}});
    setStatus(ok ? uiText(QStringLiteral("已导出：") + path, QStringLiteral("Exported: ") + path)
                 : uiText(QStringLiteral("JPEG 导出失败"), QStringLiteral("JPEG export failed")));
    return ok;
}

QString PhotoController::reportBug() {
    ActionTrace::instance().record("bug_snapshot_requested", {{"file", currentFile()}, {"pipeline", pipelineDescription()}});
    const QString path = DiagnosticBundle::create(m_processedPreview, currentFile(), projectPath(), currentState(),
                                                  m_scopes.shadowClipPercent, m_scopes.highlightClipPercent,
                                                  pipelineDescription());
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
