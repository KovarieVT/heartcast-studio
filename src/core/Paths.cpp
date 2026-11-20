#include "core/Paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace bc {

QString Paths::ensure(const QString &dir) {
    QDir().mkpath(dir);
    return dir;
}

// KovaLive! was previously called HeartCast Studio. The first time KovaLive's data
// folder is needed, copy the old one over (settings, scenes, encrypted keys, media)
// so nothing has to be set up again. The old folder is left untouched.
static void copyTree(const QString &from, const QString &to) {
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString rel = QDir(from).relativeFilePath(src);
        if (rel.startsWith("logs") || rel.startsWith("crashes") || rel == "running.lock") continue;
        const QString dst = to + "/" + rel;
        if (it.fileInfo().isDir()) QDir().mkpath(dst);
        else { QDir().mkpath(QFileInfo(dst).absolutePath()); QFile::copy(src, dst); }
    }
}

QString Paths::dataRoot() {
    // GenericDataLocation on Windows = %APPDATA%\..\Local; we want Roaming, which
    // AppDataLocation gives as %APPDATA%/<org>/<app>. Keep it short: %APPDATA%/KovaLive.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#ifdef _WIN32
    const QString parent = qEnvironmentVariable("APPDATA", base);
#else
    const QString parent = base;
#endif
    const QString root = parent + "/KovaLive";
    static bool checked = false;
    if (!checked) {
        checked = true;
        const QString legacy = parent + "/HeartCast";
        if (!QFileInfo::exists(root) && QFileInfo(legacy + "/config.json").exists()) copyTree(legacy, root);
    }
    return ensure(root);
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
    return ensure(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/KovaLive");
}

QString Paths::appDataDir() {
    // Installed layout (mirrors OBS so libobs finds its data):
    //   <install>/bin/64bit/KovaLive.exe, <install>/data/kovalive/...
    // Development builds get a copy in <build>/data (see CMakeLists.txt).
    const QString env = qEnvironmentVariable("KOVALIVE_DATA_DIR");
    if (!env.isEmpty()) return env;
    QDir d(QCoreApplication::applicationDirPath());
    if (d.cd("../../data/kovalive")) return d.absolutePath();
    QDir up(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 3; ++i) {
        if (QFileInfo::exists(up.filePath("data/stingers"))) return up.filePath("data");
        if (!up.cdUp()) break;
    }
    return QCoreApplication::applicationDirPath() + "/data";
}

} // namespace bc
