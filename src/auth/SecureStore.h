#pragma once
#include <QByteArray>
#include <QString>

namespace bc {

// OS-protected secret storage for OAuth tokens and stream keys.
//  - Windows: DPAPI (CryptProtectData, CURRENT_USER scope) with app-specific
//    entropy; blobs written to %APPDATA%/KovaLive/secrets/<name>.bin.
//    Only the same Windows user on the same machine can decrypt.
//  - Other OSes (development builds only): file with 0600 permissions and a
//    loud warning — never used for release builds.
class SecureStore {
public:
    static bool put(const QString &name, const QByteArray &secret);
    static QByteArray get(const QString &name);
    static bool remove(const QString &name);
    static bool isOsProtected();
};

} // namespace bc
