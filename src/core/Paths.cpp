#include "core/Paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace bc {

QString Paths::ensure(const QString &dir) {
    QDir().mkpath(dir);
    return dir;
}

QString Paths::dataRoot() {
    // GenericDataLocation on Windows = %APPDATA%\..\Local; we want Roaming, which
    // AppDataLocation gives as %APPDATA%/<org>/<app>. Keep it short: %APPDATA%/HeartCast.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#ifdef _WIN32
    const QString roaming = qEnvironmentVariable("APPDATA", base);
    return ensure(roaming + "/HeartCast");
#else
    return ensure(base + "/HeartCast");
#endif
}

QString Paths::configFile()       { return dataRoot() + "/config.json"; }
QString Paths::sceneCollections() { return ensure(dataRoot() + "/scenes"); }
QString Paths::logs()             { return ensure(dataRoot() + "/logs"); }
QString Paths::crashes()          { return ensure(dataRoot() + "/crashes"); }
QString Paths::cache()            { return ensure(dataRoot() + "/cache"); }
QString Paths::mediaLibrary()     { return ensure(dataRoot() + "/media"); }
QString Paths::plugins()          { return ensure(dataRoot() + "/plugins"); }
QString Paths::secrets()          { return ensure(dataRoot() + "/secrets"); }

QString Paths::defaultRecordings() {
    return ensure(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/HeartCast");
}

QString Paths::appDataDir() {
    // Installed layout (mirrors OBS so libobs finds its data):
    //   <install>/bin/64bit/HeartCastStudio.exe, <install>/data/heartcast/...
    QDir d(QCoreApplication::applicationDirPath());
    if (d.cd("../../data/heartcast")) return d.absolutePath();
    return QCoreApplication::applicationDirPath() + "/data";
}

} // namespace bc
