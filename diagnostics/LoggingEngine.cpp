#include "diagnostics/LoggingEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QUuid>
#include <QThread>
#include <cstdio>
#include <cstdlib>

namespace {
QMutex g_mutex;
QString g_logPath;
QString g_sessionId;
QtMessageHandler g_previous = nullptr;

void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
    const char *level = "INFO";
    if (type == QtDebugMsg) level = "DEBUG";
    else if (type == QtWarningMsg) level = "WARN";
    else if (type == QtCriticalMsg) level = "CRITICAL";
    else if (type == QtFatalMsg) level = "FATAL";

    const QString line = QString("%1 [%2] [tid:%3] %4 (%5:%6)\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), level)
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
        .arg(msg, QString::fromUtf8(ctx.file ? ctx.file : "?"))
        .arg(ctx.line);
    {
        QMutexLocker lock(&g_mutex);
        QFile f(g_logPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) f.write(line.toUtf8());
    }
    std::fputs(line.toUtf8().constData(), stderr);
    if (type == QtFatalMsg) std::abort();
}
}

void LoggingEngine::install() {
    g_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(base + "/logs");
    g_logPath = base + "/logs/JixelLight_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".log";
    g_previous = qInstallMessageHandler(handler);
    Q_UNUSED(g_previous);
}

QString LoggingEngine::currentLogPath() { QMutexLocker lock(&g_mutex); return g_logPath; }
QString LoggingEngine::sessionId() { QMutexLocker lock(&g_mutex); return g_sessionId; }
