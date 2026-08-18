#include "diagnostics/CrashReporter.h"
#include "diagnostics/LoggingEngine.h"

#include <QDateTime>
#include <QFile>
#include <csignal>
#include <cstdlib>
#include <exception>

namespace {
void writeMarker(const char *kind) {
    QFile f(LoggingEngine::currentLogPath() + ".crash");
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        f.write(QString("%1 %2\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), QString::fromLatin1(kind)).toUtf8());
}
void signalHandler(int sig) {
    writeMarker(qPrintable(QString("signal:%1").arg(sig)));
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
}

void CrashReporter::install() {
    std::set_terminate([] { writeMarker("std::terminate"); std::abort(); });
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);
}
