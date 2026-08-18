#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/raw/RawDecoder.h"
#include "diagnostics/ActionTrace.h"
#include "diagnostics/DiagnosticBundle.h"

#include <QFileInfo>
#include <QImageReader>
#include <QJsonObject>
#include <QSettings>
#include <QUrl>
#include <QDebug>
#include <algorithm>

PhotoController::PhotoController(ProcessedImageProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider) {
    QSettings settings;
    m_language = settings.value(QStringLiteral("ui/language"), QStringLiteral("zh_CN")).toString();
    if (m_language != QStringLiteral("zh_CN") && m_language != QStringLiteral("en_US")) m_language = QStringLiteral("zh_CN");
    m_statusMessage = uiText(QStringLiteral("就绪"), QStringLiteral("Ready"));
}

QString PhotoController::uiText(const QString &zh, const QString &en) const { return m_language == QStringLiteral("zh_CN") ? zh : en; }

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
    QVariantList out; out.reserve(m_photos.size());
    for (int i=0;i<m_photos.size();++i) {
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
QString PhotoController::previewUrl() const { return hasImage() ? QString("image://processed/current?rev=%1").arg(m_previewRevision) : QString(); }
QString PhotoController::currentFile() const { return hasImage() ? m_photos[m_currentIndex].path : QString(); }
QString PhotoController::currentFormat() const { return hasImage() ? (m_photos[m_currentIndex].raw ? QStringLiteral("RAW") : QFileInfo(currentFile()).suffix().toUpper()) : QString(); }
bool PhotoController::currentIsRaw() const { return hasImage() && m_photos[m_currentIndex].raw; }
AdjustmentState PhotoController::currentState() const { return hasImage() ? m_photos[m_currentIndex].state : AdjustmentState{}; }
AdjustmentState *PhotoController::mutableCurrentState() { return hasImage() ? &m_photos[m_currentIndex].state : nullptr; }
#define GETTER(name) double PhotoController::name() const { return currentState().name; }
GETTER(exposure) GETTER(temperature) GETTER(tint) GETTER(contrast) GETTER(highlights) GETTER(shadows) GETTER(whites) GETTER(blacks)
#undef GETTER

void PhotoController::setAdjustment(const char *name, double v, double AdjustmentState::*member) {
    auto *s = mutableCurrentState(); if (!s || qFuzzyCompare((*s).*member + 1.0, v + 1.0)) return;
    (*s).*member = v;
    ActionTrace::instance().record("adjustment", {{"parameter", name}, {"value", v}, {"file", currentFile()}});
    if (m_project.isOpen()) m_project.updateAdjustment(currentFile(), *s);
    emit adjustmentsChanged(); applyCurrent();
}
void PhotoController::setExposure(double v){setAdjustment("exposure",v,&AdjustmentState::exposure);} void PhotoController::setTemperature(double v){setAdjustment("temperature",v,&AdjustmentState::temperature);}
void PhotoController::setTint(double v){setAdjustment("tint",v,&AdjustmentState::tint);} void PhotoController::setContrast(double v){setAdjustment("contrast",v,&AdjustmentState::contrast);}
void PhotoController::setHighlights(double v){setAdjustment("highlights",v,&AdjustmentState::highlights);} void PhotoController::setShadows(double v){setAdjustment("shadows",v,&AdjustmentState::shadows);}
void PhotoController::setWhites(double v){setAdjustment("whites",v,&AdjustmentState::whites);} void PhotoController::setBlacks(double v){setAdjustment("blacks",v,&AdjustmentState::blacks);}

void PhotoController::importFiles(const QVariantList &urls) {
    int added = 0, rawAdded = 0;
    for (const QVariant &v : urls) {
        const QUrl u = v.canConvert<QUrl>() ? v.toUrl() : QUrl(v.toString());
        const QString path = u.isLocalFile() ? u.toLocalFile() : u.toString();
        if (path.isEmpty()) continue;
        const bool isRaw = RawDecoder::isRawFile(path);
        if (!isRaw) {
            QImageReader reader(path); reader.setAutoTransform(true);
            if (!reader.canRead()) { qWarning() << "Unsupported image" << path << reader.errorString(); continue; }
        }
        bool exists=false; for(const auto &p:m_photos) if(p.path==path){exists=true;break;} if(exists) continue;
        PhotoEntry entry{path, QFileInfo(path).fileName(), {}, isRaw}; m_photos.push_back(entry); ++added; if (isRaw) ++rawAdded;
        if (m_project.isOpen()) m_project.addOrUpdatePhoto(path, entry.state);
    }
    if (added) {
        ActionTrace::instance().record("import_files", {{"count", added}, {"raw_count", rawAdded}});
        emit libraryChanged(); if (m_currentIndex < 0) selectPhoto(0);
        setStatus(uiText(QStringLiteral("已导入 %1 张照片，其中 RAW %2 张").arg(added).arg(rawAdded),
                         QStringLiteral("Imported %1 image(s), %2 RAW").arg(added).arg(rawAdded)));
    } else {
        setStatus(uiText(QStringLiteral("没有可导入的照片"), QStringLiteral("No supported photos were imported")));
    }
}

void PhotoController::selectPhoto(int index) {
    if (index < 0 || index >= m_photos.size() || index == m_currentIndex) return;
    m_currentIndex = index; loadCurrent();
    ActionTrace::instance().record("select_photo", {{"index", index}, {"file", currentFile()}, {"raw", currentIsRaw()}});
    emit currentIndexChanged(); emit libraryChanged(); emit adjustmentsChanged();
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
            ActionTrace::instance().record("raw_decode_failed", {{"file",currentFile()},{"error",error}});
            return;
        }
        ActionTrace::instance().record("raw_decoded", {{"file",currentFile()},{"make",meta.make},{"model",meta.model},{"width",meta.width},{"height",meta.height},{"bits",meta.bitsPerChannel}});
        setStatus(uiText(QStringLiteral("RAW 已解码：%1 %2 · %3×%4 · %5-bit").arg(meta.make,meta.model).arg(meta.width).arg(meta.height).arg(meta.bitsPerChannel),
                         QStringLiteral("RAW decoded: %1 %2 · %3×%4 · %5-bit").arg(meta.make,meta.model).arg(meta.width).arg(meta.height).arg(meta.bitsPerChannel)));
    } else {
        QImageReader reader(currentFile()); reader.setAutoTransform(true); m_fullSource = reader.read();
        if (m_fullSource.isNull()) { setStatus(uiText(QStringLiteral("图片读取失败"), QStringLiteral("Failed to load image"))); qWarning() << reader.errorString(); return; }
    }
    m_previewSource = m_fullSource;
    if (std::max(m_previewSource.width(), m_previewSource.height()) > 2048)
        m_previewSource = m_previewSource.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    applyCurrent();
}

void PhotoController::applyCurrent() {
    if (!hasImage() || m_previewSource.isNull()) return;
    m_processedPreview = ImagePipeline::process(m_previewSource, currentState());
    m_scopes = ScopesEngine::analyze(m_processedPreview, 1024);
    if (m_provider) m_provider->setImage(m_processedPreview);
    ++m_previewRevision; emit previewUrlChanged(); emit scopesChanged();
}

bool PhotoController::createProject(const QUrl &folder, const QString &name) {
    QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    const bool ok = m_project.create(path, name);
    ActionTrace::instance().record("create_project", {{"ok", ok}, {"path", path}, {"name", name}});
    if (ok) { for (const auto &p:m_photos) m_project.addOrUpdatePhoto(p.path,p.state); emit projectChanged(); setStatus(uiText(QStringLiteral("项目已创建：") + m_project.projectName(), QStringLiteral("Project created: ") + m_project.projectName())); }
    else setStatus(uiText(QStringLiteral("项目创建失败"), QStringLiteral("Project creation failed")));
    return ok;
}

void PhotoController::resetAdjustments() { if (auto *s=mutableCurrentState()) { *s={}; if(m_project.isOpen())m_project.updateAdjustment(currentFile(),*s); ActionTrace::instance().record("reset_adjustments",{{"file",currentFile()}}); emit adjustmentsChanged(); applyCurrent(); setStatus(uiText(QStringLiteral("调整已重置"),QStringLiteral("Adjustments reset"))); } }
void PhotoController::copyAdjustments() { if(!hasImage())return; m_clipboard=currentState();m_hasClipboard=true;ActionTrace::instance().record("copy_adjustments",{{"file",currentFile()}});setStatus(uiText(QStringLiteral("已复制调整参数"),QStringLiteral("Adjustments copied"))); }
void PhotoController::pasteAdjustments() { if(!hasImage()||!m_hasClipboard)return; *mutableCurrentState()=m_clipboard;if(m_project.isOpen())m_project.updateAdjustment(currentFile(),m_clipboard);ActionTrace::instance().record("paste_adjustments",{{"file",currentFile()}});emit adjustmentsChanged();applyCurrent();setStatus(uiText(QStringLiteral("已粘贴调整参数"),QStringLiteral("Adjustments pasted"))); }
void PhotoController::syncAdjustmentsToAll() { if(!hasImage())return; const auto state=currentState(); for(auto &p:m_photos){p.state=state;if(m_project.isOpen())m_project.updateAdjustment(p.path,p.state);} ActionTrace::instance().record("sync_adjustments",{{"count",m_photos.size()}});emit libraryChanged();emit adjustmentsChanged();applyCurrent();setStatus(uiText(QStringLiteral("已同步到 %1 张照片").arg(m_photos.size()),QStringLiteral("Synced to %1 image(s)").arg(m_photos.size()))); }

bool PhotoController::exportCurrent(const QUrl &destination) {
    if (!hasImage() || m_fullSource.isNull()) return false;
    QString path = destination.isLocalFile() ? destination.toLocalFile() : destination.toString(); if (path.isEmpty()) return false;
    if (!path.endsWith(".jpg", Qt::CaseInsensitive) && !path.endsWith(".jpeg", Qt::CaseInsensitive)) path += ".jpg";
    const QImage output = ImagePipeline::process(m_fullSource, currentState());
    const bool ok = output.save(path, "JPEG", 92);
    ActionTrace::instance().record("export_jpeg", {{"ok",ok},{"source",currentFile()},{"destination",path},{"source_raw",currentIsRaw()}});
    setStatus(ok ? uiText(QStringLiteral("已导出：")+path,QStringLiteral("Exported: ")+path) : uiText(QStringLiteral("JPEG 导出失败"),QStringLiteral("JPEG export failed"))); return ok;
}

QString PhotoController::reportBug() {
    ActionTrace::instance().record("bug_snapshot_requested", {{"file",currentFile()}});
    const QString path = DiagnosticBundle::create(m_processedPreview,currentFile(),projectPath(),currentState(),m_scopes.shadowClipPercent,m_scopes.highlightClipPercent);
    ActionTrace::instance().record("bug_snapshot_created", {{"path",path},{"ok",!path.isEmpty()}});
    setStatus(path.isEmpty() ? uiText(QStringLiteral("诊断包生成失败"),QStringLiteral("Diagnostic bundle failed")) : uiText(QStringLiteral("诊断包：")+path,QStringLiteral("Diagnostic bundle: ")+path)); return path;
}
void PhotoController::setStatus(const QString &message) { if(m_statusMessage==message)return;m_statusMessage=message;emit statusMessageChanged(); }
