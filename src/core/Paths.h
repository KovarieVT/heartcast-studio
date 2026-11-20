#pragma once
#include <QString>

namespace bc {

// All user data lives under %APPDATA%\KovaLive (never under Program Files),
// so installer upgrades/uninstalls never wipe user configuration.
struct Paths {
    static QString dataRoot();        // %APPDATA%/KovaLive
    static QString configFile();      // config.json
    static QString sceneCollections();// scenes/
    static QString logs();            // logs/
    static QString crashes();         // crashes/
    static QString cache();           // cache/ (generated gradients etc.)
    static QString mediaLibrary();    // media/
    static QString plugins();         // plugins/ (user plugins)
    static QString secrets();         // secrets/ (DPAPI blobs)
    static QString defaultRecordings();// Videos/KovaLive
    static QString appDataDir();      // <install>/data
    static QString ensure(const QString &dir);
};

} // namespace bc
