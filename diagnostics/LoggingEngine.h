#pragma once

#include <QString>

class LoggingEngine {
public:
    static void install();
    static QString currentLogPath();
    static QString sessionId();
};
