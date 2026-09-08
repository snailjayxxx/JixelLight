#include "diagnostics/CrashReporter.h"
#include "diagnostics/LoggingEngine.h"
#include <QFile>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#ifdef Q_OS_WIN
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
namespace {
int markerFd=-1;
void writeMarker(const char *text,unsigned length) {
    if(markerFd<0) return;
#ifdef Q_OS_WIN
    (void)_write(markerFd,text,length);
#else
    (void)::write(markerFd,text,length);
#endif
}
void signalHandler(int sig) {
    // No Qt allocation, file open, mutex, date formatting or log queue access
    // inside a fatal signal handler. The descriptor was opened at startup.
    switch(sig) {
    case SIGSEGV: writeMarker("signal:SIGSEGV\n",15); break;
    case SIGABRT: writeMarker("signal:SIGABRT\n",15); break;
    case SIGFPE: writeMarker("signal:SIGFPE\n",14); break;
    case SIGILL: writeMarker("signal:SIGILL\n",14); break;
    default: writeMarker("fatal-signal\n",13); break;
    }
    std::signal(sig,SIG_DFL); std::raise(sig); std::_Exit(128+sig);
}
}
void CrashReporter::install() {
    const QString path=LoggingEngine::currentLogPath()+".crash";
#ifdef Q_OS_WIN
    markerFd=_wopen(reinterpret_cast<const wchar_t *>(path.utf16()),_O_CREAT|_O_WRONLY|_O_APPEND|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
    markerFd=::open(QFile::encodeName(path).constData(),O_CREAT|O_WRONLY|O_APPEND,0600);
#endif
    std::set_terminate([] { writeMarker("std::terminate\n",15); std::abort(); });
    std::signal(SIGABRT,signalHandler);std::signal(SIGSEGV,signalHandler);
    std::signal(SIGFPE,signalHandler);std::signal(SIGILL,signalHandler);
}
