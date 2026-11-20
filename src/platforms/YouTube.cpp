#include "platforms/YouTube.h"
#include "auth/SecureStore.h"
#include "core/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>

namespace bc {

static const char *kTokenName = "oauth.youtube";
static const char *kTokenUrl = "https://oauth2.googleapis.com/token";
static const char *kApi = "https://www.googleapis.com/youtube/v3";

YouTubeAccount::YouTubeAccount(QNetworkAccessManager *nam, QObject *parent) : PlatformAccount(nam, parent) {
    m_tokens = TokenSet::load(kTokenName);
    m_channelTitle = Settings::instance().s("accounts.youtube.channelTitle");
    m_channelId = Settings::instance().s("accounts.youtube.channelId");
    m_chatTimer.setSingleShot(true);
    connect(&m_chatTimer, &QTimer::timeout, this, &YouTubeAccount::pollChat);
}

QString YouTubeAccount::privacy() const {
    const QString p = Settings::instance().s("accounts.youtube.privacy");
    return p.isEmpty() ? "public" : p;
}

void YouTubeAccount::connectAccount() {
    if (m_flow) m_flow->deleteLater();
    m_flow = new LoopbackPkceFlow(m_nam, "https://accounts.google.com/o/oauth2/v2/auth", kTokenUrl,
                                  DevCredentials::googleClientId(), DevCredentials::googleClientSecret(),
                                  {"https://www.googleapis.com/auth/youtube"}, this);
    connect(m_flow, &LoopbackPkceFlow::browserOpened, this, [this] { Q_EMIT status(tr("Finish signing in to Google in your browser…")); });
    connect(m_flow, &LoopbackPkceFlow::failed, this, &YouTubeAccount::error);
    connect(m_flow, &LoopbackPkceFlow::succeeded, this, [this](const TokenSet &t) {
        m_tokens = t;
        m_tokens.save(kTokenName);
        loadChannel([this](bool ok) {
            if (ok) Q_EMIT status(tr("Connected to YouTube: %1").arg(m_channelTitle));
            else Q_EMIT error(tr("Signed in, but this Google account has no YouTube channel (or live streaming is not enabled)."));
            Q_EMIT connectionChanged();
        });
    });
    m_flow->start();
}

void YouTubeAccount::disconnectAccount() {
    stopChat();
    if (m_tokens.valid()) {
        QNetworkRequest r(QUrl("https://oauth2.googleapis.com/revoke?token=" + m_tokens.refreshToken));
        r.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        auto *rep = m_nam->post(r, QByteArray());
        connect(rep, &QNetworkReply::finished, rep, &QObject::deleteLater);
    }
    m_tokens = {};
    SecureStore::remove(kTokenName);
    m_channelTitle.clear(); m_channelId.clear();
    auto &s = Settings::instance();
    s.set("accounts.youtube.channelTitle", QString());
    s.set("accounts.youtube.channelId", QString());
    s.set("accounts.youtube.streamId", QString());
    s.save();
    Q_EMIT connectionChanged();
}

void YouTubeAccount::refreshIfNeeded(const std::function<void(bool)> &done) {
    if (!m_tokens.valid()) { done(false); return; }
    if (!m_tokens.expiringSoon()) { done(true); return; }
    refreshToken(m_nam, kTokenUrl, DevCredentials::googleClientId(), DevCredentials::googleClientSecret(),
                 m_tokens.refreshToken, [this, done](bool ok, const TokenSet &t, const QString &err) {
        if (!ok) { Q_EMIT error(tr("YouTube session expired — please reconnect. (%1)").arg(err)); done(false); return; }
        m_tokens = t; m_tokens.save(kTokenName); done(true);
    });
}

void YouTubeAccount::api(const QByteArray &verb, const QString &pq, const QJsonObject &body,
                         const std::function<void(int, const QJsonObject &)> &cb) {
    refreshIfNeeded([=, this](bool ok) {
        if (!ok) { cb(401, {}); return; }
        QNetworkRequest r(QUrl(QString(kApi) + pq));
        r.setRawHeader("Authorization", "Bearer " + m_tokens.accessToken.toUtf8());
        r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        r.setTransferTimeout(20000);
        QNetworkReply *rep = m_nam->sendCustomRequest(r, verb, body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(rep, &QNetworkReply::finished, this, [rep, cb] {
            rep->deleteLater();
            cb(rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), QJsonDocument::fromJson(rep->readAll()).object());
        });
    });
}

static QString apiError(int st, const QJsonObject &o) {
    const QString m = o.value("error").toObject().value("message").toString();
    return m.isEmpty() ? QObject::tr("HTTP %1").arg(st) : m;
}

void YouTubeAccount::loadChannel(const std::function<void(bool)> &done) {
    api("GET", "/channels?part=snippet&mine=true", {}, [this, done](int st, const QJsonObject &o) {
        const auto items = o.value("items").toArray();
        if (st != 200 || items.isEmpty()) { done(false); return; }
        const QJsonObject ch = items.first().toObject();
        m_channelId = ch.value("id").toString();
        m_channelTitle = ch.value("snippet").toObject().value("title").toString();
        auto &s = Settings::instance();
        s.set("accounts.youtube.channelId", m_channelId);
        s.set("accounts.youtube.channelTitle", m_channelTitle);
        s.save();
        done(true);
    });
}

void YouTubeAccount::ensureStream(const std::function<void(bool, const QString &, const QString &, const QString &, const QString &)> &cb) {
    // Reuse one persistent liveStream (stable stream key) if it still exists.
    const QString saved = Settings::instance().s("accounts.youtube.streamId");
    auto create = [this, cb] {
        QJsonObject body{{"snippet", QJsonObject{{"title", "KovaLive!"}}},
                         {"cdn", QJsonObject{{"ingestionType", "rtmp"}, {"resolution", "variable"}, {"frameRate", "variable"}}},
                         {"contentDetails", QJsonObject{{"isReusable", true}}}};
        api("POST", "/liveStreams?part=snippet,cdn,contentDetails", body, [this, cb](int st, const QJsonObject &o) {
            if (st != 200) { cb(false, {}, {}, {}, apiError(st, o)); return; }
            const QJsonObject ing = o.value("cdn").toObject().value("ingestionInfo").toObject();
            Settings::instance().set("accounts.youtube.streamId", o.value("id").toString());
            Settings::instance().save();
            cb(true, o.value("id").toString(), ing.value("ingestionAddress").toString(), ing.value("streamName").toString(), {});
        });
    };
    if (saved.isEmpty()) { create(); return; }
    api("GET", "/liveStreams?part=cdn&id=" + saved, {}, [create, cb](int st, const QJsonObject &o) {
        const auto items = o.value("items").toArray();
        if (st != 200 || items.isEmpty()) { create(); return; }
        const QJsonObject it = items.first().toObject();
        const QJsonObject ing = it.value("cdn").toObject().value("ingestionInfo").toObject();
        cb(true, it.value("id").toString(), ing.value("ingestionAddress").toString(), ing.value("streamName").toString(), {});
    });
}

void YouTubeAccount::fetchStreamTarget(const QString &title, const TargetCb &cb) {
    ensureStream([this, title, cb](bool ok, const QString &streamId, const QString &server, const QString &key, const QString &err) {
        if (!ok) { cb(false, {}, {}, tr("YouTube: %1 (is live streaming enabled on the channel?)").arg(err)); return; }
        QJsonObject body{
            {"snippet", QJsonObject{{"title", title.isEmpty() ? tr("Live with KovaLive!") : title},
                                    {"scheduledStartTime", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}},
            {"status", QJsonObject{{"privacyStatus", privacy()}, {"selfDeclaredMadeForKids", false}}},
            {"contentDetails", QJsonObject{{"enableAutoStart", true}, {"enableAutoStop", true}, {"latencyPreference", "low"}}}};
        api("POST", "/liveBroadcasts?part=snippet,status,contentDetails", body,
            [this, streamId, server, key, cb](int st, const QJsonObject &o) {
            if (st != 200) { cb(false, {}, {}, tr("YouTube broadcast creation failed: %1").arg(apiError(st, o))); return; }
            m_broadcastId = o.value("id").toString();
            m_liveChatId = o.value("snippet").toObject().value("liveChatId").toString();
            m_pageToken.clear();
            api("POST", "/liveBroadcasts/bind?part=id,contentDetails&id=" + m_broadcastId + "&streamId=" + streamId, {},
                [server, key, cb](int st2, const QJsonObject &o2) {
                if (st2 != 200) { cb(false, {}, {}, tr("YouTube bind failed: %1").arg(apiError(st2, o2))); return; }
                cb(true, server, key, {});
                });
            });
    });
}

void YouTubeAccount::updateStreamInfo(const QString &title, const QString &category, const QString &description,
                                      const std::function<void(bool, const QString &)> &cb) {
    if (m_broadcastId.isEmpty()) {   // nothing live yet: remember for the next broadcast
        Settings::instance().set("accounts.youtube.nextTitle", title);
        cb(true, {});
        return;
    }
    QJsonObject snippet{{"title", title}, {"description", description}, {"categoryId", category.isEmpty() ? "20" : category}};
    api("PUT", "/videos?part=snippet", QJsonObject{{"id", m_broadcastId}, {"snippet", snippet}}, [cb](int st, const QJsonObject &o) {
        cb(st == 200, st == 200 ? QString() : apiError(st, o));
    });
}

void YouTubeAccount::fetchViewerCount(const std::function<void(int)> &cb) {
    if (m_broadcastId.isEmpty()) { cb(-1); return; }
    api("GET", "/videos?part=liveStreamingDetails&id=" + m_broadcastId, {}, [cb](int st, const QJsonObject &o) {
        const auto items = o.value("items").toArray();
        if (st != 200 || items.isEmpty()) { cb(-1); return; }
        const QJsonValue v = items.first().toObject().value("liveStreamingDetails").toObject().value("concurrentViewers");
        cb(v.isUndefined() ? -1 : v.toVariant().toInt());
    });
}

void YouTubeAccount::startChat() {
    m_chatWanted = true;
    if (!m_liveChatId.isEmpty()) { pollChat(); return; }
    // Find our active broadcast's chat (e.g. stream started elsewhere).
    api("GET", "/liveBroadcasts?part=snippet&broadcastStatus=active&broadcastType=all", {}, [this](int st, const QJsonObject &o) {
        const auto items = o.value("items").toArray();
        if (st == 200 && !items.isEmpty()) {
            m_broadcastId = items.first().toObject().value("id").toString();
            m_liveChatId = items.first().toObject().value("snippet").toObject().value("liveChatId").toString();
            pollChat();
        } else if (m_chatWanted) {
            m_chatTimer.start(15000);   // not live yet — check again shortly
        }
    });
}

void YouTubeAccount::stopChat() { m_chatWanted = false; m_chatTimer.stop(); }

void YouTubeAccount::pollChat() {
    if (!m_chatWanted) return;
    if (m_liveChatId.isEmpty()) { startChat(); return; }
    QString pq = "/liveChat/messages?part=snippet,authorDetails&maxResults=200&liveChatId=" + m_liveChatId;
    if (!m_pageToken.isEmpty()) pq += "&pageToken=" + m_pageToken;
    api("GET", pq, {}, [this](int st, const QJsonObject &o) {
        if (st == 403 || st == 404) { m_liveChatId.clear(); m_chatTimer.start(15000); return; }   // chat ended
        const bool firstPage = m_pageToken.isEmpty();
        m_pageToken = o.value("nextPageToken").toString();
        for (const auto v : o.value("items").toArray()) {
            const QJsonObject it = v.toObject(), sn = it.value("snippet").toObject(), au = it.value("authorDetails").toObject();
            const QString type = sn.value("type").toString();
            if (firstPage && type == "textMessageEvent") continue;   // skip backlog on first connect
            if (type == "textMessageEvent") {
                ChatMessage m;
                m.platform = "youtube"; m.id = it.value("id").toString();
                m.user = au.value("displayName").toString(); m.userId = au.value("channelId").toString();
                m.text = sn.value("displayMessage").toString();
                if (au.value("isChatOwner").toBool()) m.badges << "broadcaster";
                if (au.value("isChatModerator").toBool()) m.badges << "moderator";
                if (au.value("isChatSponsor").toBool()) m.badges << "member";
                m.fromMod = au.value("isChatOwner").toBool() || au.value("isChatModerator").toBool();
                Q_EMIT chatMessage(m);
            } else if (!firstPage && (type == "superChatEvent" || type == "superStickerEvent")) {
                StreamEvent e; e.platform = "youtube"; e.type = "donation"; e.user = au.value("displayName").toString();
                const QJsonObject d = sn.value(type == "superChatEvent" ? "superChatDetails" : "superStickerDetails").toObject();
                e.amount = d.value("amountMicros").toVariant().toDouble() / 1e6;
                e.message = d.value("amountDisplayString").toString() + " " + d.value("userComment").toString();
                Q_EMIT streamEvent(e);
            } else if (!firstPage && (type == "newSponsorEvent" || type == "memberMilestoneChatEvent")) {
                StreamEvent e; e.platform = "youtube"; e.type = "membership"; e.user = au.value("displayName").toString();
                Q_EMIT streamEvent(e);
            } else if (type == "messageDeletedEvent") {
                Q_EMIT chatMessageDeleted("youtube", sn.value("messageDeletedDetails").toObject().value("deletedMessageId").toString());
            }
        }
        m_chatTimer.start(qBound(2000, o.value("pollingIntervalMillis").toInt(5000), 30000));
    });
}

void YouTubeAccount::sendChat(const QString &text) {
    if (m_liveChatId.isEmpty() || text.trimmed().isEmpty()) return;
    QJsonObject body{{"snippet", QJsonObject{{"liveChatId", m_liveChatId}, {"type", "textMessageEvent"},
                                             {"textMessageDetails", QJsonObject{{"messageText", text}}}}}};
    api("POST", "/liveChat/messages?part=snippet", body, [this](int st, const QJsonObject &o) {
        if (st != 200) Q_EMIT error(tr("YouTube chat send failed: %1").arg(apiError(st, o)));
    });
}

void YouTubeAccount::moderate(ModAction a, const ChatMessage &t, int seconds, bool) {
    if (a == ModAction::Delete) {
        api("DELETE", "/liveChat/messages?id=" + t.id, {}, [this](int st, const QJsonObject &o) {
            st == 204 ? Q_EMIT status(tr("Message deleted")) : Q_EMIT error(apiError(st, o));
        });
    } else if (a == ModAction::Timeout || a == ModAction::Ban) {
        QJsonObject sn{{"liveChatId", m_liveChatId}, {"type", a == ModAction::Ban ? "permanent" : "temporary"},
                       {"bannedUserDetails", QJsonObject{{"channelId", t.userId}}}};
        if (a == ModAction::Timeout) sn["banDurationSeconds"] = qMax(10, seconds);
        api("POST", "/liveChat/bans?part=snippet", QJsonObject{{"snippet", sn}}, [this, t, a](int st, const QJsonObject &o) {
            st == 200 ? Q_EMIT status(a == ModAction::Ban ? tr("%1 banned").arg(t.user) : tr("%1 timed out").arg(t.user))
                      : Q_EMIT error(apiError(st, o));
        });
    }
}

} // namespace bc
