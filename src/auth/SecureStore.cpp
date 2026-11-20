#include "auth/SecureStore.h"
#include "core/Paths.h"
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace bc {

static QString blobPath(const QString &name) {
    QString safe = name;
    safe.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    return Paths::secrets() + "/" + safe + ".bin";
}

#ifdef _WIN32
// Unchanged since the app was called HeartCast: changing it would make existing secrets unreadable.
static const char kEntropy[] = "HeartCast.Studio.v1.secret-entropy";

bool SecureStore::put(const QString &name, const QByteArray &secret) {
    DATA_BLOB in{DWORD(secret.size()), reinterpret_cast<BYTE *>(const_cast<char *>(secret.data()))};
    DATA_BLOB ent{DWORD(sizeof(kEntropy)), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"KovaLive", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    QSaveFile f(blobPath(name));
    bool ok = f.open(QIODevice::WriteOnly);
    if (ok) { f.write(reinterpret_cast<const char *>(out.pbData), out.cbData); ok = f.commit(); }
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return ok;
}

QByteArray SecureStore::get(const QString &name) {
    QFile f(blobPath(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray enc = f.readAll();
    DATA_BLOB in{DWORD(enc.size()), reinterpret_cast<BYTE *>(enc.data())};
    DATA_BLOB ent{DWORD(sizeof(kEntropy)), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QByteArray result(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

bool SecureStore::isOsProtected() { return true; }
#else
bool SecureStore::put(const QString &name, const QByteArray &secret) {
    qWarning("SecureStore: non-Windows development build — secrets are stored with file permissions only");
    QSaveFile f(blobPath(name));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(secret.toBase64());
    return f.commit();
}

QByteArray SecureStore::get(const QString &name) {
    QFile f(blobPath(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QByteArray::fromBase64(f.readAll());
}

bool SecureStore::isOsProtected() { return false; }
#endif

bool SecureStore::remove(const QString &name) { return QFile::remove(blobPath(name)); }

} // namespace bc
