#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <functional>

namespace bc {

struct TokenSet {
    QString accessToken, refreshToken, scope;
    QDateTime expiresAt;
    bool valid() const { return !accessToken.isEmpty(); }
    bool expiringSoon() const { return expiresAt.isValid() && QDateTime::currentDateTimeUtc().secsTo(expiresAt) < 300; }
    QJsonObject toJson() const;
    static TokenSet fromJson(const QJsonObject &o);
    static TokenSet fromTokenResponse(const QJsonObject &o);
    bool save(const QString &secretName) const;      // → SecureStore (DPAPI)
    static TokenSet load(const QString &secretName);
};

// Developer credentials are NEVER compiled into release binaries.
// Order: environment variable → entered in HeartCast (Settings → Advanced → Developer
// credentials; stored with SecureStore/DPAPI) → %APPDATA%/HeartCast/developer.json.
// Client IDs are public identifiers; a "client secret" is only used for Google's
// *installed app* clients, which Google documents as non-confidential.
struct DevCredentials {
    enum class Key { TwitchClientId, GoogleClientId, GoogleClientSecret };
    enum class Source { None, Environment, App, File };

    static QString get(Key k);
    static Source source(Key k);
    static QString envName(Key k);
    static bool set(Key k, const QString &value);      // empty value removes the in-app entry
    static bool ready(const QString &platformId);     // "twitch" / "youtube": everything that sign-in needs is present
    static void reload();                              // forget cached values (e.g. developer.json edited)

    static QString twitchClientId() { return get(Key::TwitchClientId); }
    static QString googleClientId() { return get(Key::GoogleClientId); }
    static QString googleClientSecret() { return get(Key::GoogleClientSecret); }
};

// RFC 8628 device authorization grant — used for Twitch (public client, no secret).
// The user approves in their normal browser; we never see their password.
class DeviceCodeFlow : public QObject {
    Q_OBJECT
public:
    DeviceCodeFlow(QNetworkAccessManager *nam, QString deviceUrl, QString tokenUrl, QString clientId,
                   QStringList scopes, QObject *parent = nullptr);
    void start();
    void cancel();

Q_SIGNALS:
    void userCodeReady(const QString &userCode, const QString &verificationUrl);
    void succeeded(const TokenSet &tokens);
    void failed(const QString &error);

private:
    void poll();
    QNetworkAccessManager *m_nam;
    QString m_deviceUrl, m_tokenUrl, m_clientId, m_deviceCode;
    QStringList m_scopes;
    QTimer m_poll;
    QDateTime m_deadline;
    int m_intervalMs = 5000;
};

// RFC 8252 loopback redirect + RFC 7636 PKCE — used for Google/YouTube.
// Opens the system browser on the official consent page; a one-shot local
// HTTP listener on 127.0.0.1:<random> receives the code; state is verified.
class LoopbackPkceFlow : public QObject {
    Q_OBJECT
public:
    LoopbackPkceFlow(QNetworkAccessManager *nam, QString authUrl, QString tokenUrl, QString clientId,
                     QString clientSecret, QStringList scopes, QObject *parent = nullptr);
    void start();
    void cancel();

    // exposed for unit tests
    static QByteArray makeVerifier();
    static QByteArray challengeFor(const QByteArray &verifier);

Q_SIGNALS:
    void browserOpened(const QUrl &url);
    void succeeded(const TokenSet &tokens);
    void failed(const QString &error);

private:
    void onConnection();
    void exchange(const QString &code);
    QNetworkAccessManager *m_nam;
    QString m_authUrl, m_tokenUrl, m_clientId, m_clientSecret, m_redirect;
    QStringList m_scopes;
    QByteArray m_verifier, m_state;
    QTcpServer m_server;
    QTimer m_timeout;
};

// Refreshes an OAuth token (refresh_token grant).
void refreshToken(QNetworkAccessManager *nam, const QString &tokenUrl, const QString &clientId,
                  const QString &clientSecret, const QString &refresh,
                  const std::function<void(bool ok, const TokenSet &, const QString &err)> &done);

} // namespace bc
