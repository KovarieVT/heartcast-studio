#include "auth/OAuth.h"
#include "auth/SecureStore.h"
#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QMutex>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrlQuery>

namespace bc {

// ---------------------------------------------------------------- TokenSet
QJsonObject TokenSet::toJson() const {
    return {{"access_token", accessToken}, {"refresh_token", refreshToken}, {"scope", scope},
            {"expires_at", expiresAt.toString(Qt::ISODate)}};
}
TokenSet TokenSet::fromJson(const QJsonObject &o) {
    TokenSet t;
    t.accessToken = o.value("access_token").toString();
    t.refreshToken = o.value("refresh_token").toString();
    t.scope = o.value("scope").toVariant().toString();
    t.expiresAt = QDateTime::fromString(o.value("expires_at").toString(), Qt::ISODate);
    return t;
}
TokenSet TokenSet::fromTokenResponse(const QJsonObject &o) {
    TokenSet t;
    t.accessToken = o.value("access_token").toString();
    t.refreshToken = o.value("refresh_token").toString();
    const QJsonValue sc = o.value("scope");
    if (sc.isArray()) { QStringList l; for (auto v : sc.toArray()) l << v.toString(); t.scope = l.join(' '); }
    else t.scope = sc.toString();
    const int exp = o.value("expires_in").toInt(3600);
    t.expiresAt = QDateTime::currentDateTimeUtc().addSecs(exp);
    return t;
}
bool TokenSet::save(const QString &name) const {
    return SecureStore::put(name, QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}
TokenSet TokenSet::load(const QString &name) {
    const QByteArray raw = SecureStore::get(name);
    return raw.isEmpty() ? TokenSet{} : fromJson(QJsonDocument::fromJson(raw).object());
}

// ---------------------------------------------------------------- credentials
static QJsonObject devFile() {
    QFile f(Paths::dataRoot() + "/developer.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
static const char *fileKey(DevCredentials::Key k) {
    switch (k) {
    case DevCredentials::Key::TwitchClientId: return "twitchClientId";
    case DevCredentials::Key::GoogleClientId: return "googleClientId";
    case DevCredentials::Key::GoogleClientSecret: return "googleClientSecret";
    }
    return "";
}
static QString storeName(DevCredentials::Key k) { return QStringLiteral("developer-") + QLatin1String(fileKey(k)); }

QString DevCredentials::envName(Key k) {
    switch (k) {
    case Key::TwitchClientId: return QStringLiteral("HEARTCAST_TWITCH_CLIENT_ID");
    case Key::GoogleClientId: return QStringLiteral("HEARTCAST_GOOGLE_CLIENT_ID");
    case Key::GoogleClientSecret: return QStringLiteral("HEARTCAST_GOOGLE_CLIENT_SECRET");
    }
    return {};
}

// Values are cached after the first lookup (Helix calls read the Client ID on every
// request; DPAPI decryption + a file read each time would be wasteful). set() clears it.
static QMutex s_credMutex;
static QHash<int, QPair<QString, DevCredentials::Source>> s_credCache;

static QPair<QString, DevCredentials::Source> resolve(DevCredentials::Key k) {
    QMutexLocker lock(&s_credMutex);
    const auto it = s_credCache.constFind(int(k));
    if (it != s_credCache.constEnd()) return *it;
    QPair<QString, DevCredentials::Source> r{qEnvironmentVariable(qPrintable(DevCredentials::envName(k))).trimmed(),
                                             DevCredentials::Source::Environment};
    if (r.first.isEmpty()) r = {QString::fromUtf8(SecureStore::get(storeName(k))).trimmed(), DevCredentials::Source::App};
    if (r.first.isEmpty()) r = {devFile().value(QLatin1String(fileKey(k))).toString().trimmed(), DevCredentials::Source::File};
    if (r.first.isEmpty()) r.second = DevCredentials::Source::None;
    s_credCache.insert(int(k), r);
    return r;
}

QString DevCredentials::get(Key k) { return resolve(k).first; }
DevCredentials::Source DevCredentials::source(Key k) { return resolve(k).second; }

bool DevCredentials::set(Key k, const QString &value) {
    const QString v = value.trimmed();
    bool ok = true;
    if (v.isEmpty()) SecureStore::remove(storeName(k));
    else ok = SecureStore::put(storeName(k), v.toUtf8());
    QMutexLocker lock(&s_credMutex);
    s_credCache.remove(int(k));
    return ok;
}

void DevCredentials::reload() { QMutexLocker lock(&s_credMutex); s_credCache.clear(); }

bool DevCredentials::ready(const QString &platformId) {
    if (platformId == QLatin1String("twitch")) return !twitchClientId().isEmpty();
    if (platformId == QLatin1String("youtube")) return !googleClientId().isEmpty() && !googleClientSecret().isEmpty();
    return true;   // platforms without OAuth need nothing
}

static QNetworkRequest formRequest(const QString &url) {
    QNetworkRequest r{QUrl(url)};
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    r.setTransferTimeout(20000);
    return r;
}

// ---------------------------------------------------------------- device flow
DeviceCodeFlow::DeviceCodeFlow(QNetworkAccessManager *nam, QString deviceUrl, QString tokenUrl, QString clientId,
                               QStringList scopes, QObject *parent)
    : QObject(parent), m_nam(nam), m_deviceUrl(std::move(deviceUrl)), m_tokenUrl(std::move(tokenUrl)),
      m_clientId(std::move(clientId)), m_scopes(std::move(scopes)) {
    connect(&m_poll, &QTimer::timeout, this, &DeviceCodeFlow::poll);
}

void DeviceCodeFlow::start() {
    if (m_clientId.isEmpty()) { Q_EMIT failed(tr("No Twitch Client ID yet. Add one in Settings → Advanced → Developer credentials.")); return; }
    QUrlQuery q;
    q.addQueryItem("client_id", m_clientId);
    q.addQueryItem("scopes", m_scopes.join(' '));
    q.addQueryItem("scope", m_scopes.join(' '));
    QNetworkReply *rep = m_nam->post(formRequest(m_deviceUrl), q.toString(QUrl::FullyEncoded).toUtf8());
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        if (rep->error() != QNetworkReply::NoError || !o.contains("device_code")) {
            const QString msg = o.value("message").toString(rep->errorString());
            QString hint;
            if (msg.contains("client", Qt::CaseInsensitive))
                hint = tr(" — check the Client ID in Settings → Advanced → Developer credentials. The Twitch app's Client Type must be Public.");
            Q_EMIT failed(tr("Authorization request failed: %1").arg(msg) + hint);
            return;
        }
        m_deviceCode = o.value("device_code").toString();
        m_intervalMs = qMax(1, o.value("interval").toInt(5)) * 1000;
        m_deadline = QDateTime::currentDateTimeUtc().addSecs(o.value("expires_in").toInt(1800));
        const QString url = o.value("verification_uri").toString();
        Q_EMIT userCodeReady(o.value("user_code").toString(), url);
        QDesktopServices::openUrl(QUrl(url));
        m_poll.start(m_intervalMs);
    });
}

void DeviceCodeFlow::cancel() { m_poll.stop(); }

void DeviceCodeFlow::poll() {
    if (QDateTime::currentDateTimeUtc() > m_deadline) { m_poll.stop(); Q_EMIT failed(tr("The sign-in code expired. Please try again.")); return; }
    QUrlQuery q;
    q.addQueryItem("client_id", m_clientId);
    q.addQueryItem("scopes", m_scopes.join(' '));
    q.addQueryItem("device_code", m_deviceCode);
    q.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    QNetworkReply *rep = m_nam->post(formRequest(m_tokenUrl), q.toString(QUrl::FullyEncoded).toUtf8());
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        if (o.contains("access_token")) { m_poll.stop(); Q_EMIT succeeded(TokenSet::fromTokenResponse(o)); return; }
        const QString msg = o.value("message").toString() + o.value("error").toString();
        if (msg.contains("pending")) return;                       // user hasn't approved yet
        if (msg.contains("slow_down")) { m_intervalMs += 5000; m_poll.setInterval(m_intervalMs); return; }
        m_poll.stop();
        if (msg.contains("denied")) { Q_EMIT failed(tr("Access was denied in the browser.")); return; }
        // A Confidential Twitch app can't finish the device flow without a secret.
        if (msg.contains("secret", Qt::CaseInsensitive) || msg.contains("client", Qt::CaseInsensitive)) {
            Q_EMIT failed(tr("Sign-in failed: %1 — in the Twitch Developer Console, the app's Client Type must be Public.").arg(msg));
            return;
        }
        Q_EMIT failed(tr("Sign-in failed: %1").arg(msg));
    });
}

// ---------------------------------------------------------------- PKCE loopback
LoopbackPkceFlow::LoopbackPkceFlow(QNetworkAccessManager *nam, QString authUrl, QString tokenUrl, QString clientId,
                                   QString clientSecret, QStringList scopes, QObject *parent)
    : QObject(parent), m_nam(nam), m_authUrl(std::move(authUrl)), m_tokenUrl(std::move(tokenUrl)),
      m_clientId(std::move(clientId)), m_clientSecret(std::move(clientSecret)), m_scopes(std::move(scopes)) {
    connect(&m_server, &QTcpServer::newConnection, this, &LoopbackPkceFlow::onConnection);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { cancel(); Q_EMIT failed(tr("Sign-in timed out.")); });
}

QByteArray LoopbackPkceFlow::makeVerifier() {
    QByteArray raw(48, 0);
    for (auto &c : raw) c = char(QRandomGenerator::system()->bounded(256));
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray LoopbackPkceFlow::challengeFor(const QByteArray &verifier) {
    return QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

void LoopbackPkceFlow::start() {
    if (m_clientId.isEmpty() || m_clientSecret.isEmpty()) { Q_EMIT failed(tr("No Google Client ID / secret yet. Add them in Settings → Advanced → Developer credentials.")); return; }
    if (!m_server.listen(QHostAddress::LocalHost, 0)) { Q_EMIT failed(tr("Could not open local sign-in listener.")); return; }
    m_redirect = QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    m_verifier = makeVerifier();
    m_state = makeVerifier().left(24);
    QUrl url(m_authUrl);
    QUrlQuery q;
    q.addQueryItem("client_id", m_clientId);
    q.addQueryItem("redirect_uri", m_redirect);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("scope", m_scopes.join(' '));
    q.addQueryItem("code_challenge", QString::fromLatin1(challengeFor(m_verifier)));
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state", QString::fromLatin1(m_state));
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent");
    url.setQuery(q);
    m_timeout.start(5 * 60 * 1000);
    QDesktopServices::openUrl(url);
    Q_EMIT browserOpened(url);
}

void LoopbackPkceFlow::cancel() { m_timeout.stop(); m_server.close(); }

void LoopbackPkceFlow::onConnection() {
    QTcpSocket *sock = m_server.nextPendingConnection();
    if (!sock) return;
    connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
        const QByteArray req = sock->readAll();
        const QByteArray line = req.left(req.indexOf("\r\n"));          // GET /?code=…&state=… HTTP/1.1
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() < 2) { sock->close(); return; }
        const QUrlQuery q(QUrl(QString::fromLatin1(parts[1])).query());
        const bool ok = q.queryItemValue("state").toLatin1() == m_state && q.hasQueryItem("code");
        const QByteArray html = ok
            ? "<html><body style='font-family:sans-serif;background:#FFDCEB;color:#514852;text-align:center;padding-top:80px'>"
              "<h1>&#9825; You're connected!</h1><p>You can close this tab and return to HeartCast Studio.</p></body></html>"
            : "<html><body><h1>Sign-in failed</h1><p>Please return to HeartCast Studio and try again.</p></body></html>";
        sock->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(html.size()) + "\r\n\r\n" + html);
        sock->disconnectFromHost();
        sock->deleteLater();
        m_server.close();
        m_timeout.stop();
        if (!ok) { Q_EMIT failed(q.hasQueryItem("error") ? tr("Access was denied: %1").arg(q.queryItemValue("error")) : tr("State mismatch — sign-in rejected for safety.")); return; }
        exchange(q.queryItemValue("code", QUrl::FullyDecoded));
    });
}

void LoopbackPkceFlow::exchange(const QString &code) {
    QUrlQuery q;
    q.addQueryItem("client_id", m_clientId);
    if (!m_clientSecret.isEmpty()) q.addQueryItem("client_secret", m_clientSecret);
    q.addQueryItem("code", code);
    q.addQueryItem("code_verifier", QString::fromLatin1(m_verifier));
    q.addQueryItem("grant_type", "authorization_code");
    q.addQueryItem("redirect_uri", m_redirect);
    QNetworkReply *rep = m_nam->post(formRequest(m_tokenUrl), q.toString(QUrl::FullyEncoded).toUtf8());
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        if (o.contains("access_token")) Q_EMIT succeeded(TokenSet::fromTokenResponse(o));
        else Q_EMIT failed(tr("Token exchange failed: %1").arg(o.value("error_description").toString(o.value("error").toString(rep->errorString()))));
    });
}

void refreshToken(QNetworkAccessManager *nam, const QString &tokenUrl, const QString &clientId,
                  const QString &clientSecret, const QString &refresh,
                  const std::function<void(bool, const TokenSet &, const QString &)> &done) {
    QUrlQuery q;
    q.addQueryItem("client_id", clientId);
    if (!clientSecret.isEmpty()) q.addQueryItem("client_secret", clientSecret);
    q.addQueryItem("grant_type", "refresh_token");
    q.addQueryItem("refresh_token", refresh);
    QNetworkReply *rep = nam->post(formRequest(tokenUrl), q.toString(QUrl::FullyEncoded).toUtf8());
    QObject::connect(rep, &QNetworkReply::finished, rep, [rep, refresh, done] {
        rep->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        if (!o.contains("access_token")) { done(false, {}, o.value("message").toString(o.value("error").toString(rep->errorString()))); return; }
        TokenSet t = TokenSet::fromTokenResponse(o);
        if (t.refreshToken.isEmpty()) t.refreshToken = refresh;   // Google keeps the old refresh token
        done(true, t, {});
    });
}

} // namespace bc
