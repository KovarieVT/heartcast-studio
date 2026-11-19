#include "updater/Updater.h"
#include "BuildInfo.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "plugins/PluginManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>

#ifndef HEARTCAST_UPDATE_FEED
#define HEARTCAST_UPDATE_FEED ""
#endif

namespace bc {

Updater &Updater::instance() {
    static Updater u;
    return u;
}

QString Updater::feedUrl() {
    const QString s = Settings::instance().s("updates.feed");
    return s.isEmpty() ? QStringLiteral(HEARTCAST_UPDATE_FEED) : s;
}

bool Updater::isNewer(const QString &remote, const QString &local) {
    return QVersionNumber::fromString(remote) > QVersionNumber::fromString(local);
}

void Updater::check(bool userInitiated) {
    const QString url = feedUrl();
    if (url.isEmpty() || !url.startsWith("https://")) {
        if (userInitiated) Q_EMIT checkFailed(tr("This build has no secure (HTTPS) update feed configured."));
        return;
    }
    QNetworkRequest r{QUrl(url)};
    r.setTransferTimeout(15000);
    auto *rep = m_nam.get(r);
    connect(rep, &QNetworkReply::finished, this, [this, rep, userInitiated] {
        rep->deleteLater();
        if (rep->error() != QNetworkReply::NoError) { if (userInitiated) Q_EMIT checkFailed(rep->errorString()); return; }
        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        UpdateInfo info;
        info.version = QVersionNumber::fromString(o.value("version").toString());
        info.notes = o.value("notes").toString();
        info.url = o.value("url").toString();
        info.sha256 = o.value("sha256").toString().toLower();
        info.size = o.value("size").toVariant().toLongLong();
        if (info.version.isNull() || !info.url.startsWith("https://") || info.sha256.size() != 64) {
            if (userInitiated) Q_EMIT checkFailed(tr("Update feed is malformed or insecure; ignoring it."));
            return;
        }
        if (isNewer(info.version.toString(), BC_APP_VERSION)) Q_EMIT updateAvailable(info);
        else if (userInitiated) Q_EMIT upToDate();
    });
}

void Updater::downloadAndInstall(const UpdateInfo &info) {
    const QString dir = Paths::ensure(Paths::dataRoot() + "/updates");
    const QString target = dir + QStringLiteral("/HeartCastStudio-%1-Setup.exe").arg(info.version.toString());
    QNetworkRequest r{QUrl(info.url)};
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *rep = m_nam.get(r);
    auto *file = new QSaveFile(target, rep);
    file->open(QIODevice::WriteOnly);
    auto *hash = new QCryptographicHash(QCryptographicHash::Sha256);
    connect(rep, &QNetworkReply::downloadProgress, this, &Updater::downloadProgress);
    connect(rep, &QNetworkReply::readyRead, rep, [rep, file, hash] { const QByteArray b = rep->readAll(); hash->addData(b); file->write(b); });
    connect(rep, &QNetworkReply::finished, this, [this, rep, file, hash, info, target, dir] {
        rep->deleteLater();
        const QString got = QString::fromLatin1(hash->result().toHex());
        delete hash;
        if (rep->error() != QNetworkReply::NoError) { file->cancelWriting(); Q_EMIT installFailed(rep->errorString()); return; }
        if (got != info.sha256) { file->cancelWriting(); Q_EMIT installFailed(tr("Downloaded file failed SHA-256 verification — not installing.")); return; }
        if (!file->commit()) { Q_EMIT installFailed(tr("Could not save the installer.")); return; }
#ifdef _WIN32
        if (info.requireSignature && PluginManager::authenticodeSigner(target).isEmpty()) {
            QFile::remove(target);
            Q_EMIT installFailed(tr("Installer is not Authenticode-signed — refusing to run it."));
            return;
        }
#endif
        // Rollback strategy: keep the two newest installers.
        auto old = QDir(dir).entryInfoList({"*-Setup.exe"}, QDir::Files, QDir::Time);
        for (int i = 2; i < old.size(); ++i) QFile::remove(old[i].absoluteFilePath());
        Q_EMIT readyToInstall(target);
    });
}

} // namespace bc
