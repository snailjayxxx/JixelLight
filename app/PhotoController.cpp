#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/ImagePipeline.h"
#include "diagnostics/ActionTrace.h"
#include "diagnostics/DiagnosticBundle.h"

#include <QFileInfo>
#include <QImageReader>
#include <QJsonObject>
#include <QUrl>
#include <QDebug>
#include <algorithm>

PhotoController::PhotoController(ProcessedImageProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider) {}

QVariantList PhotoController::library() const {
    QVariantList out; out.reserve(m_photos.size());
    for (int i=0;i<m_photos.size();++i) {
        QVariantMap row; row["name"] = m_photos[i].name; row["path"] = m_photos[i].path; row["current"] = i == m_currentIndex;
        out.push_back(row);
    }
    return out;
}
QString PhotoController::previewUrl() const { return hasImage() ? QString("image://processed/current?rev=%1").arg(m_previewRevision) : QString(); }
QString PhotoController::currentFile() const { return hasImage() ? m_photos[m_currentIndex].path : QString(); }
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
    int added = 0;
    for (const QVariant &v : urls) {
        const QUrl u = v.canConvert<QUrl>() ? v.toUrl() : QUrl(v.toString());
        const QString path = u.isLocalFile() ? u.toLocalFile() : u.toString();
        if (path.isEmpty()) continue;
        QImageReader reader(path); reader.setAutoTransform(true);
        if (!reader.canRead()) { qWarning() << "Unsupported image" << path << reader.errorString(); continue; }
        bool exists=false; for(const auto &p:m_photos) if(p.path==path){exists=true;break;} if(exists) continue;
        PhotoEntry entry{path, QFileInfo(path).fileName(), {}}; m_photos.push_back(entry); ++added;
        if (m_project.isOpen()) m_project.addOrUpdatePhoto(path, entry.state);
    }
    if (added) {
        ActionTrace::instance().record("import_files", {{"count", added}});
        emit libraryChanged(); if (m_currentIndex < 0) selectPhoto(0);
        setStatus(QString("Imported %1 image(s)").arg(added));
    }
}

void PhotoController::selectPhoto(int index) {
    if (index < 0 || index >= m_photos.size() || index == m_currentIndex) return;
    m_currentIndex = index; loadCurrent();
    ActionTrace::instance().record("select_photo", {{"index", index}, {"file", currentFile()}});
    emit currentIndexChanged(); emit libraryChanged(); emit adjustmentsChanged();
}

void PhotoController::loadCurrent() {
    if (!hasImage()) return;
    QImageReader reader(currentFile()); reader.setAutoTransform(true); m_fullSource = reader.read();
    if (m_fullSource.isNull()) { setStatus("Failed to load image"); qWarning() << reader.errorString(); return; }
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
    if (ok) { for (const auto &p:m_photos) m_project.addOrUpdatePhoto(p.path,p.state); emit projectChanged(); setStatus("Project created: " + m_project.projectName()); }
    else setStatus("Project creation failed");
    return ok;
}

void PhotoController::resetAdjustments() { if (auto *s=mutableCurrentState()) { *s={}; if(m_project.isOpen())m_project.updateAdjustment(currentFile(),*s); ActionTrace::instance().record("reset_adjustments",{{"file",currentFile()}}); emit adjustmentsChanged(); applyCurrent(); } }
void PhotoController::copyAdjustments() { if(!hasImage())return; m_clipboard=currentState();m_hasClipboard=true;ActionTrace::instance().record("copy_adjustments",{{"file",currentFile()}});setStatus("Adjustments copied"); }
void PhotoController::pasteAdjustments() { if(!hasImage()||!m_hasClipboard)return; *mutableCurrentState()=m_clipboard;if(m_project.isOpen())m_project.updateAdjustment(currentFile(),m_clipboard);ActionTrace::instance().record("paste_adjustments",{{"file",currentFile()}});emit adjustmentsChanged();applyCurrent();setStatus("Adjustments pasted"); }
void PhotoController::syncAdjustmentsToAll() { if(!hasImage())return; const auto state=currentState(); for(auto &p:m_photos){p.state=state;if(m_project.isOpen())m_project.updateAdjustment(p.path,p.state);} ActionTrace::instance().record("sync_adjustments",{{"count",m_photos.size()}});emit libraryChanged();emit adjustmentsChanged();applyCurrent();setStatus(QString("Synced to %1 image(s)").arg(m_photos.size())); }

bool PhotoController::exportCurrent(const QUrl &destination) {
    if (!hasImage() || m_fullSource.isNull()) return false;
    QString path = destination.isLocalFile() ? destination.toLocalFile() : destination.toString(); if (path.isEmpty()) return false;
    if (!path.endsWith(".jpg", Qt::CaseInsensitive) && !path.endsWith(".jpeg", Qt::CaseInsensitive)) path += ".jpg";
    const QImage output = ImagePipeline::process(m_fullSource, currentState());
    const bool ok = output.save(path, "JPEG", 92);
    ActionTrace::instance().record("export_jpeg", {{"ok",ok},{"source",currentFile()},{"destination",path}});
    setStatus(ok ? "Exported: " + path : "JPEG export failed"); return ok;
}

QString PhotoController::reportBug() {
    ActionTrace::instance().record("bug_snapshot_requested", {{"file",currentFile()}});
    const QString path = DiagnosticBundle::create(m_processedPreview,currentFile(),projectPath(),currentState(),m_scopes.shadowClipPercent,m_scopes.highlightClipPercent);
    ActionTrace::instance().record("bug_snapshot_created", {{"path",path},{"ok",!path.isEmpty()}});
    setStatus(path.isEmpty() ? "Diagnostic bundle failed" : "Diagnostic bundle: " + path); return path;
}
void PhotoController::setStatus(const QString &message) { if(m_statusMessage==message)return;m_statusMessage=message;emit statusMessageChanged(); }
