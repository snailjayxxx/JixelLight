#include "diagnostics/DiagnosticBundle.h"
#include "diagnostics/PerformanceRecorder.h"
#include "diagnostics/ActionTrace.h"
#include "diagnostics/LoggingEngine.h"
#include "diagnostics/ZipStoreWriter.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSysInfo>

#ifndef JIXELLIGHT_VERSION
#define JIXELLIGHT_VERSION "dev"
#endif
#ifndef JIXELLIGHT_GIT_COMMIT
#define JIXELLIGHT_GIT_COMMIT "unknown"
#endif

QString DiagnosticBundle::create(const QImage &preview, const QString &currentFile,
                                 const QString &projectPath, const AdjustmentState &state,
                                 double shadowClip, double highlightClip,
                                 const QString &pipelineDescription, const QJsonObject &scopeContext) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/JixelLight_Diagnostic_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".zip";
    const bool logsFlushed = LoggingEngine::flush();
    ZipStoreWriter zip(path);
    if (!zip.open()) return {};

    QJsonObject manifest{
        {"app", "JixelLight"}, {"version", JIXELLIGHT_VERSION}, {"git_commit", JIXELLIGHT_GIT_COMMIT},
        {"session", LoggingEngine::sessionId()}, {"created_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"os", QSysInfo::prettyProductName()}, {"cpu_arch", QSysInfo::currentCpuArchitecture()},
        {"kernel", QSysInfo::kernelType() + " " + QSysInfo::kernelVersion()},
        {"current_file", currentFile}, {"project_path", projectPath}, {"adjustments", state.toJson()},
        {"processing_graph", pipelineDescription},
        {"working_space", pipelineDescription.contains(QStringLiteral("RAW")) ? QStringLiteral("Linear ProPhoto RGB") : QStringLiteral("sRGB input -> Linear ProPhoto RGB")},
        {"display_output", QStringLiteral("sRGB")},
        {"scopes", QJsonObject{{"bins",1024},{"source",QStringLiteral("current display result")},{"shadow_clip_percent",shadowClip},{"highlight_clip_percent",highlightClip}}}
    };
    auto scopes = manifest["scopes"].toObject();
    scopes["source"] = QStringLiteral("sRGB output-referred; mode and revision below");
    for (auto it=scopeContext.constBegin(); it!=scopeContext.constEnd(); ++it) scopes.insert(it.key(), it.value());
    manifest["scopes"] = scopes;
    manifest["logs_flushed"] = logsFlushed;
    manifest["log_lines_dropped"] = qint64(LoggingEngine::droppedLines());
    manifest["preview_capture"] = QStringLiteral("CPU reference of current parameters; not a GPU screen capture");
    bool written = zip.addFile("manifest.json", QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    written &= zip.addFile("actions.json", QJsonDocument(ActionTrace::instance().snapshot()).toJson(QJsonDocument::Indented));

    written &= zip.addFile("performance.json", QJsonDocument(PerformanceRecorder::snapshot()).toJson(QJsonDocument::Indented));

    if (!preview.isNull()) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        written &= preview.save(&buffer, "PNG");
        written &= zip.addFile("current_preview.png", png);
    }
    QFile log(LoggingEngine::currentLogPath());
    if (log.open(QIODevice::ReadOnly)) written &= zip.addFile("session.log", log.readAll());
    const bool closed = zip.close();
    if (!written || !closed) { QFile::remove(path); return {}; }
    return path;
}
