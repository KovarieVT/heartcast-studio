#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QVersionNumber>

namespace bc {

struct UpdateInfo {
    QVersionNumber version;
    QString notes, url, sha256;
    qint64 size = 0;
    bool requireSignature = true;
};

// Secure updater:
//  1. Fetches a JSON feed over HTTPS (URL baked at build time via HEARTCAST_UPDATE_FEED,
//     or Settings "updates.feed"). No feed → updates are reported as unavailable.
//  2. Downloads the installer over HTTPS, verifies SHA-256 from the feed AND (on
//     Windows) a valid Authenticode signature before it is ever executed.
//  3. Keeps the previous installer in %APPDATA%/HeartCast/updates for rollback.
class Updater : public QObject {
    Q_OBJECT
public:
    static Updater &instance();
    static QString feedUrl();
    void check(bool userInitiated);
    void downloadAndInstall(const UpdateInfo &info);

    static bool isNewer(const QString &remote, const QString &local);   // for tests

Q_SIGNALS:
    void updateAvailable(const UpdateInfo &info);
    void upToDate();
    void checkFailed(const QString &reason);
    void downloadProgress(qint64 received, qint64 total);
    void readyToInstall(const QString &installerPath);
    void installFailed(const QString &reason);

private:
    Updater() = default;
    QNetworkAccessManager m_nam;
};

} // namespace bc
