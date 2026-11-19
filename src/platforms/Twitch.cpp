#include "platforms/Twitch.h"
#include "core/Settings.h"
#include "auth/SecureStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>

namespace bc {

static const char *kTokenName = "oauth.twitch";
static const char *kTokenUrl = "https://id.twitch.tv/oauth2/token";

QStringList TwitchAccount::scopes() {
    return {"channel:read:stream_key", "channel:manage:broadcast", "chat:read", "chat:edit",
            "moderator:read:followers", "channel:read:subscriptions", "bits:read", "channel:read:redemptions",
            "moderator:manage:banned_users", "moderator:manage:chat_messages", "moderator:manage:chat_settings",
            "user:manage:blocked_users"};
}

TwitchAccount::TwitchAccount(QNetworkAccessManager *nam, QObject *parent) : PlatformAccount(nam, parent) {
    m_tokens = TokenSet::load(kTokenName);
    m_userId = Settings::instance().s("accounts.twitch.userId");
    m_login = Settings::instance().s("accounts.twitch.login");
    connect(&m_irc, &QWebSocket::textMessageReceived, this, &TwitchAccount::onIrc);
    connect(&m_irc, &QWebSocket::connected, this, [this] {
        m_irc.sendTextMessage("CAP REQ :twitch.tv/tags twitch.tv/commands");
        m_irc.sendTextMessage("PASS oauth:" + m_tokens.accessToken);
        m_irc.sendTextMessage("NICK " + m_login.toLower());
        m_irc.sendTextMessage("JOIN #" + m_login.toLower());
        Q_EMIT status(tr("Twitch chat connected"));
    });
    connect(&m_irc, &QWebSocket::disconnected, this, [this] {
        if (m_chatWanted) QTimer::singleShot(3000, this, [this] { if (m_chatWanted) startChat(); });   // auto-reconnect
    });
    connect(&m_eventSub, &QWebSocket::textMessageReceived, this, &TwitchAccount::onEventSub);
    connect(&m_eventSub, &QWebSocket::disconnected, this, [this] {
        if (m_chatWanted) QTimer::singleShot(5000, this, [this] { if (m_chatWanted) startEventSub(); });
    });
}

void TwitchAccount::connectAccount() {
    if (m_flow) m_flow->deleteLater();
    m_flow = new DeviceCodeFlow(m_nam, "https://id.twitch.tv/oauth2/device", kTokenUrl,
                                DevCredentials::twitchClientId(), scopes(), this);
    connect(m_flow, &DeviceCodeFlow::userCodeReady, this, &TwitchAccount::userCodeNeeded);
    connect(m_flow, &DeviceCodeFlow::failed, this, &TwitchAccount::error);
    connect(m_flow, &DeviceCodeFlow::succeeded, this, [this](const TokenSet &t) {
        m_tokens = t;
        m_tokens.save(kTokenName);
        validate([this](bool ok) {
            if (ok) Q_EMIT status(tr("Connected to Twitch as %1 ♡").arg(m_login));
            Q_EMIT connectionChanged();
        });
    });
    m_flow->start();
}

void TwitchAccount::disconnectAccount() {
    stopChat();
    if (m_tokens.valid()) {   // revoke token server-side
        QUrlQuery q; q.addQueryItem("client_id", DevCredentials::twitchClientId()); q.addQueryItem("token", m_tokens.accessToken);
        QNetworkRequest r(QUrl("https://id.twitch.tv/oauth2/revoke"));
        r.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        auto *rep = m_nam->post(r, q.toString(QUrl::FullyEncoded).toUtf8());
        connect(rep, &QNetworkReply::finished, rep, &QObject::deleteLater);
    }
    m_tokens = {};
    SecureStore::remove(kTokenName);
    m_userId.clear(); m_login.clear();
    Settings::instance().set("accounts.twitch.userId", QString());
    Settings::instance().set("accounts.twitch.login", QString());
    Settings::instance().save();
    Q_EMIT connectionChanged();
}

void TwitchAccount::validate(const std::function<void(bool)> &done) {
    QNetworkRequest r(QUrl("https://id.twitch.tv/oauth2/validate"));
    r.setRawHeader("Authorization", "OAuth " + m_tokens.accessToken.toUtf8());
    auto *rep = m_nam->get(r);
    connect(rep, &QNetworkReply::finished, this, [this, rep, done] {
        rep->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        if (!o.contains("user_id")) { done(false); return; }
        m_userId = o.value("user_id").toString();
        m_login = o.value("login").toString();
        auto &s = Settings::instance();
        s.set("accounts.twitch.userId", m_userId);
        s.set("accounts.twitch.login", m_login);
        s.save();
        done(true);
    });
}

void TwitchAccount::refreshIfNeeded(const std::function<void(bool)> &done) {
    if (!m_tokens.valid()) { done(false); return; }
    if (!m_tokens.expiringSoon()) { done(true); return; }
    refreshToken(m_nam, kTokenUrl, DevCredentials::twitchClientId(), {}, m_tokens.refreshToken,
                 [this, done](bool ok, const TokenSet &t, const QString &err) {
        if (!ok) { Q_EMIT error(tr("Twitch session expired — please reconnect. (%1)").arg(err)); done(false); return; }
        m_tokens = t; m_tokens.save(kTokenName); done(true);
    });
}

void TwitchAccount::helix(const QByteArray &verb, const QString &path, const QJsonObject &body,
                          const std::function<void(int, const QJsonObject &)> &cb, bool retried) {
    refreshIfNeeded([=, this](bool ok) {
        if (!ok) { cb(401, {}); return; }
        QNetworkRequest r(QUrl("https://api.twitch.tv/helix" + path));
        r.setRawHeader("Client-Id", DevCredentials::twitchClientId().toUtf8());
        r.setRawHeader("Authorization", "Bearer " + m_tokens.accessToken.toUtf8());
        r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        r.setTransferTimeout(15000);
        const QByteArray data = body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
        QNetworkReply *rep = m_nam->sendCustomRequest(r, verb, data);
        connect(rep, &QNetworkReply::finished, this, [=, this] {
            rep->deleteLater();
            const int st = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
            if (st == 401 && !retried) {   // token revoked/expired early → force refresh once
                m_tokens.expiresAt = QDateTime::currentDateTimeUtc();
                helix(verb, path, body, cb, true);
                return;
            }
            cb(st, o);
        });
    });
}

void TwitchAccount::fetchStreamTarget(const QString &, const TargetCb &cb) {
    helix("GET", "/streams/key?broadcaster_id=" + m_userId, {}, [cb](int st, const QJsonObject &o) {
        const auto data = o.value("data").toArray();
        if (st != 200 || data.isEmpty()) { cb(false, {}, {}, QObject::tr("Could not read the Twitch stream key (HTTP %1).").arg(st)); return; }
        cb(true, "rtmp://live.twitch.tv/app", data.first().toObject().value("stream_key").toString(), {});
    });
}

void TwitchAccount::updateStreamInfo(const QString &title, const QString &category, const QString &,
                                     const std::function<void(bool, const QString &)> &cb) {
    auto patch = [this, title, cb](const QString &gameId) {
        QJsonObject body{{"title", title}};
        if (!gameId.isEmpty()) body["game_id"] = gameId;
        helix("PATCH", "/channels?broadcaster_id=" + m_userId, body, [cb](int st, const QJsonObject &o) {
            cb(st == 204, st == 204 ? QString() : o.value("message").toString(QObject::tr("HTTP %1").arg(st)));
        });
    };
    if (category.trimmed().isEmpty()) { patch({}); return; }
    helix("GET", "/search/categories?first=1&query=" + QUrl::toPercentEncoding(category), {}, [patch](int, const QJsonObject &o) {
        const auto d = o.value("data").toArray();
        patch(d.isEmpty() ? QString() : d.first().toObject().value("id").toString());
    });
}

void TwitchAccount::fetchViewerCount(const std::function<void(int)> &cb) {
    helix("GET", "/streams?user_id=" + m_userId, {}, [cb](int st, const QJsonObject &o) {
        const auto d = o.value("data").toArray();
        cb(st == 200 && !d.isEmpty() ? d.first().toObject().value("viewer_count").toInt() : -1);
    });
}

// ---------------------------------------------------------------- chat (IRC over WebSocket)
void TwitchAccount::startChat() {
    if (!isConnected()) return;
    m_chatWanted = true;
    refreshIfNeeded([this](bool ok) {
        if (!ok) return;
        if (m_irc.state() == QAbstractSocket::UnconnectedState) m_irc.open(QUrl("wss://irc-ws.chat.twitch.tv:443"));
        if (m_eventSub.state() == QAbstractSocket::UnconnectedState) startEventSub();
    });
}

void TwitchAccount::stopChat() {
    m_chatWanted = false;
    m_irc.close();
    m_eventSub.close();
}

void TwitchAccount::sendChat(const QString &text) {
    if (m_irc.state() == QAbstractSocket::ConnectedState && !text.trimmed().isEmpty())
        m_irc.sendTextMessage("PRIVMSG #" + m_login.toLower() + " :" + text.simplified());
}

static QString unescapeTag(QString v) {
    return v.replace("\\s", " ").replace("\\:", ";").replace("\\\\", "\\").replace("\\r", "").replace("\\n", "");
}

ChatMessage TwitchAccount::parseIrcPrivmsg(const QString &line) {
    ChatMessage m;
    m.platform = "twitch";
    QString rest = line;
    QHash<QString, QString> tags;
    if (rest.startsWith('@')) {
        const int sp = rest.indexOf(' ');
        for (const QString &kv : rest.mid(1, sp - 1).split(';')) {
            const int eq = kv.indexOf('=');
            tags.insert(kv.left(eq), unescapeTag(kv.mid(eq + 1)));
        }
        rest = rest.mid(sp + 1);
    }
    // :nick!nick@nick.tmi.twitch.tv PRIVMSG #chan :message
    const int msgIdx = rest.indexOf(" :", rest.indexOf("PRIVMSG"));
    m.text = msgIdx >= 0 ? rest.mid(msgIdx + 2) : QString();
    if (m.text.startsWith("\x01" "ACTION ")) { m.text = m.text.mid(8); m.text.chop(1); }
    m.user = tags.value("display-name");
    if (m.user.isEmpty()) m.user = rest.mid(1, rest.indexOf('!') - 1);
    m.userId = tags.value("user-id");
    m.id = tags.value("id");
    m.color = tags.value("color");
    for (const QString &b : tags.value("badges").split(',', Qt::SkipEmptyParts)) m.badges << b.section('/', 0, 0);
    m.fromMod = m.badges.contains("moderator") || m.badges.contains("broadcaster");
    return m;
}

void TwitchAccount::onIrc(const QString &frame) {
    for (const QString &line : frame.split("\r\n", Qt::SkipEmptyParts)) {
        if (line.startsWith("PING")) { m_irc.sendTextMessage("PONG :tmi.twitch.tv"); continue; }
        if (line.contains(" PRIVMSG #")) { Q_EMIT chatMessage(parseIrcPrivmsg(line)); continue; }
        if (line.contains(" CLEARMSG #")) {
            const int i = line.indexOf("target-msg-id=");
            if (i >= 0) Q_EMIT chatMessageDeleted("twitch", line.mid(i + 14).section(QRegularExpression("[; ]"), 0, 0));
            continue;
        }
        if (line.contains("Login authentication failed")) Q_EMIT error(tr("Twitch chat login failed — reconnect your account."));
    }
}

// ---------------------------------------------------------------- EventSub (WebSocket transport)
void TwitchAccount::startEventSub() { m_eventSub.open(QUrl("wss://eventsub.wss.twitch.tv/ws")); }

void TwitchAccount::subscribe(const QString &type, const QString &version, const QJsonObject &condition) {
    QJsonObject body{{"type", type}, {"version", version}, {"condition", condition},
                     {"transport", QJsonObject{{"method", "websocket"}, {"session_id", m_eventSessionId}}}};
    helix("POST", "/eventsub/subscriptions", body, [this, type](int st, const QJsonObject &o) {
        if (st != 202 && st != 409) qWarning("EventSub %s failed: %d %s", qPrintable(type), st, qPrintable(o.value("message").toString()));
    });
}

void TwitchAccount::onEventSub(const QString &frame) {
    const QJsonObject o = QJsonDocument::fromJson(frame.toUtf8()).object();
    const QString kind = o.value("metadata").toObject().value("message_type").toString();
    const QJsonObject payload = o.value("payload").toObject();
    if (kind == "session_welcome") {
        m_eventSessionId = payload.value("session").toObject().value("id").toString();
        const QJsonObject bc{{"broadcaster_user_id", m_userId}};
        subscribe("channel.follow", "2", QJsonObject{{"broadcaster_user_id", m_userId}, {"moderator_user_id", m_userId}});
        subscribe("channel.subscribe", "1", bc);
        subscribe("channel.subscription.gift", "1", bc);
        subscribe("channel.subscription.message", "1", bc);
        subscribe("channel.cheer", "1", bc);
        subscribe("channel.raid", "1", QJsonObject{{"to_broadcaster_user_id", m_userId}});
        subscribe("channel.channel_points_custom_reward_redemption.add", "1", bc);
    } else if (kind == "session_reconnect") {
        m_eventSub.open(QUrl(payload.value("session").toObject().value("reconnect_url").toString()));
    } else if (kind == "notification") {
        const QString type = payload.value("subscription").toObject().value("type").toString();
        const QJsonObject ev = payload.value("event").toObject();
        StreamEvent e;
        e.platform = "twitch";
        e.user = ev.value("user_name").toString(ev.value("from_broadcaster_user_name").toString());
        if (type == "channel.follow") e.type = "follow";
        else if (type == "channel.subscribe") { if (ev.value("is_gift").toBool()) return; e.type = "subscribe"; }
        else if (type == "channel.subscription.message") { e.type = "subscribe"; e.amount = ev.value("cumulative_months").toInt(); e.message = ev.value("message").toObject().value("text").toString(); }
        else if (type == "channel.subscription.gift") { e.type = "gift"; e.amount = ev.value("total").toInt(); if (ev.value("is_anonymous").toBool()) e.user = tr("Anonymous"); }
        else if (type == "channel.cheer") { e.type = "cheer"; e.amount = ev.value("bits").toInt(); e.message = ev.value("message").toString(); if (ev.value("is_anonymous").toBool()) e.user = tr("Anonymous"); }
        else if (type == "channel.raid") { e.type = "raid"; e.amount = ev.value("viewers").toInt(); }
        else if (type == "channel.channel_points_custom_reward_redemption.add") { e.type = "redemption"; e.message = ev.value("reward").toObject().value("title").toString(); }
        else return;
        Q_EMIT streamEvent(e);
    }
}

// ---------------------------------------------------------------- moderation (Helix)
void TwitchAccount::moderate(ModAction a, const ChatMessage &t, int seconds, bool enable) {
    const QString q = "?broadcaster_id=" + m_userId + "&moderator_id=" + m_userId;
    auto report = [this](const QString &what) {
        return [this, what](int st, const QJsonObject &o) {
            if (st >= 200 && st < 300) Q_EMIT status(what);
            else Q_EMIT error(tr("Twitch moderation failed: %1").arg(o.value("message").toString(QString::number(st))));
        };
    };
    switch (a) {
    case ModAction::Delete:
        helix("DELETE", "/moderation/chat" + q + "&message_id=" + t.id, {}, report(tr("Message deleted")));
        break;
    case ModAction::Timeout:
    case ModAction::Ban: {
        QJsonObject d{{"user_id", t.userId}, {"reason", "HeartCast moderation"}};
        if (a == ModAction::Timeout) d["duration"] = qBound(1, seconds, 1209600);
        helix("POST", "/moderation/bans" + q, QJsonObject{{"data", d}},
              report(a == ModAction::Ban ? tr("%1 banned").arg(t.user) : tr("%1 timed out").arg(t.user)));
        break;
    }
    case ModAction::Block:
        helix("PUT", "/users/blocks?target_user_id=" + t.userId, {}, report(tr("%1 blocked").arg(t.user)));
        break;
    case ModAction::SlowMode:
        helix("PATCH", "/chat/settings" + q, QJsonObject{{"slow_mode", enable}, {"slow_mode_wait_time", enable ? QJsonValue(qBound(3, seconds, 120)) : QJsonValue()}},
              report(enable ? tr("Slow mode on") : tr("Slow mode off")));
        break;
    case ModAction::FollowerOnly:
        helix("PATCH", "/chat/settings" + q, QJsonObject{{"follower_mode", enable}, {"follower_mode_duration", enable ? QJsonValue(qMax(0, seconds / 60)) : QJsonValue()}},
              report(enable ? tr("Follower-only mode on") : tr("Follower-only mode off")));
        break;
    case ModAction::SubscriberOnly:
        helix("PATCH", "/chat/settings" + q, QJsonObject{{"subscriber_mode", enable}},
              report(enable ? tr("Subscriber-only mode on") : tr("Subscriber-only mode off")));
        break;
    }
}

} // namespace bc
