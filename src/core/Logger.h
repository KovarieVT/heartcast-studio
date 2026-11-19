#pragma once
#include <QString>

namespace bc {

// Routes Qt messages AND libobs blog() output into one rotating log file:
// %APPDATA%/HeartCast/logs/YYYY-MM-DD_HH-MM-SS.txt (keeps the newest 15).
// Installs a crash handler that writes a minidump on Windows.
class Logger {
public:
    static void init();
    static void installObsLogHandler();
    static QString currentLogFile();
    static void shutdown();
};

} // namespace bc
