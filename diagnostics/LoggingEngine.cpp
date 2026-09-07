#include "diagnostics/LoggingEngine.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QCoreApplication>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace {
class Sink final {
public:
    explicit Sink(QString path) : m_thread([this,path=std::move(path)] { run(path); }) {}
    ~Sink() {
        { std::lock_guard lock(m_mutex); m_stop=true; }
        m_wake.notify_all();
        if(m_thread.joinable()) m_thread.join();
    }
    void push(QByteArray line, bool urgent) {
        { std::lock_guard lock(m_mutex);
          if(m_queue.size()>=4096) { m_queue.pop_front(); ++m_dropped; }
          m_queue.push_back({++m_issued,std::move(line)}); m_urgent|=urgent; }
        if(urgent) m_wake.notify_one();
    }
    bool flush() {
        std::unique_lock lock(m_mutex);
        const auto target=m_issued;
        m_urgent=true; m_wake.notify_one();
        return m_flushed.wait_for(lock,std::chrono::seconds(3),[&] { return m_written>=target; }) && !m_failed;
    }
    qulonglong dropped() const { std::lock_guard lock(m_mutex); return m_dropped; }
private:
    struct Line { quint64 sequence; QByteArray data; };
    void run(const QString &path) {
        QFile file(path);
        bool failed=!file.open(QIODevice::WriteOnly|QIODevice::Append);
        if(failed) std::fputs("JixelLight: cannot open diagnostic log file\n",stderr);
        for(;;) {
            std::deque<Line> batch;
            bool stop=false;
            { std::unique_lock lock(m_mutex);
              m_wake.wait_for(lock,std::chrono::milliseconds(100),[&] { return m_stop || m_urgent; });
              batch.swap(m_queue); m_urgent=false; stop=m_stop; }
            quint64 last=0;
            QByteArray bytes;
            for(const auto &line:batch) { bytes.append(line.data); last=line.sequence; }
            if(!bytes.isEmpty() && (failed || file.write(bytes)!=bytes.size())) failed=true;
            if(!failed && !bytes.isEmpty() && !file.flush()) failed=true;
            { std::lock_guard lock(m_mutex); if(last) m_written=last; m_failed=failed; }
            m_flushed.notify_all();
            if(stop) break;
        }
    }
    mutable std::mutex m_mutex;
    std::condition_variable m_wake,m_flushed;
    std::deque<Line> m_queue;
    quint64 m_issued=0,m_written=0,m_dropped=0;
    bool m_stop=false,m_urgent=false,m_failed=false;
    std::thread m_thread;
};
std::mutex stateMutex;
std::shared_ptr<Sink> sink;
QString logPath,session;
QtMessageHandler previous=nullptr;
bool echo=false;
void handler(QtMsgType type,const QMessageLogContext &context,const QString &message) {
    const char *level=type==QtDebugMsg?"DEBUG":type==QtWarningMsg?"WARN":type==QtCriticalMsg?"CRITICAL":type==QtFatalMsg?"FATAL":"INFO";
    const QByteArray line=QString("%1 [%2] [tid:%3] %4 (%5:%6)\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),QString::fromLatin1(level))
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
        .arg(message,QString::fromUtf8(context.file?context.file:"?")).arg(context.line).toUtf8();
    std::shared_ptr<Sink> current;
    { std::lock_guard lock(stateMutex); current=sink; }
    const bool urgent=type==QtCriticalMsg || type==QtFatalMsg;
    if(current) current->push(line,urgent);
    if(echo || type==QtWarningMsg || urgent) std::fputs(line.constData(),stderr);
    if(type==QtFatalMsg) { if(current) current->flush(); std::abort(); }
}
}
void LoggingEngine::install() {
    std::lock_guard lock(stateMutex);
    if(sink) return;
    session=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs";
    QDir().mkpath(directory);
    logPath=directory+"/JixelLight_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+".log";
    echo=qEnvironmentVariableIsSet("JIXELLIGHT_LOG_STDERR");
    sink=std::make_shared<Sink>(logPath);
    previous=qInstallMessageHandler(handler);
    qAddPostRoutine(&LoggingEngine::shutdown);
}
bool LoggingEngine::flush() {
    std::shared_ptr<Sink> current;
    { std::lock_guard lock(stateMutex); current=sink; }
    return !current || current->flush();
}
void LoggingEngine::shutdown() {
    std::shared_ptr<Sink> current;
    { std::lock_guard lock(stateMutex);
      if(!sink) return;
      qInstallMessageHandler(previous); current=std::move(sink); }
    current->flush();
}
qulonglong LoggingEngine::droppedLines() {
    std::shared_ptr<Sink> current;
    { std::lock_guard lock(stateMutex); current=sink; }
    return current?current->dropped():0;
}
QString LoggingEngine::currentLogPath() { std::lock_guard lock(stateMutex); return logPath; }
QString LoggingEngine::sessionId() { std::lock_guard lock(stateMutex); return session; }
