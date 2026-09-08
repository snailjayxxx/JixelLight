#pragma once
#include <QString>

class LoggingEngine {
public:
    static void install();
    static bool flush();
    static void shutdown();
    static qulonglong droppedLines();
    static QString currentLogPath();
    static QString sessionId();
};
