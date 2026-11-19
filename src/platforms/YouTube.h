#pragma once
#include "auth/OAuth.h"
#include "platforms/Platform.h"
#include <QPointer>
#include <QTimer>

namespace bc {

// YouTube Live: Google OAuth (installed-app loopback + PKCE), YouTube Data API v3:
// creates a liveBroadcast + reusable liveStream, binds them, returns the ingest
// URL/key; polls live chat (respecting pollingIntervalMillis) and maps Super Chats
// and memberships to stream events.
class YouTubeAccount : public PlatformAccount {
    Q_OBJECT
public:
    explicit YouTubeAccount(QNetworkAccessManager *nam, QObject *parent = nullptr);
    QString platformId() const override { return "youtube"; }
    bool isConnected() const override { return m_tokens.valid() && !m_channelTitle.isEmpty(); }
    QString accountName() const override { return m_channelTitle; }
    void connectAccount() override;
    void disconnectAccount() override;
    void refreshIfNeeded(const std::function<void(bool)> &done) override;
    void fetchStreamTarget(const QString &title, const TargetCb &cb) override;
    void updateStreamInfo(const QString &title, const QString &category, const QString &description,
                          const std::function<void(bool, const QString &)> &cb) override;
    void fetchViewerCount(const std::function<void(int)> &cb) override;
    void startChat() override;
    void stopChat() override;
    void sendChat(const QString &text) override;
    bool supports(ModAction a) const override { return a == ModAction::Delete || a == ModAction::Timeout || a == ModAction::Ban; }
    void moderate(ModAction a, const ChatMessage &target, int seconds, bool enable = true) override;

    QString privacy() const;   // public | unlisted | private (Settings "accounts.youtube.privacy")

private:
    void api(const QByteArray &verb, const QString &pathAndQuery, const QJsonObject &body,
             const std::function<void(int, const QJsonObject &)> &cb);
    void loadChannel(const std::function<void(bool)> &done);
    void pollChat();
    void ensureStream(const std::function<void(bool, const QString &id, const QString &server, const QString &key, const QString &err)> &cb);

    TokenSet m_tokens;
    QString m_channelTitle, m_channelId, m_broadcastId, m_liveChatId, m_pageToken;
    QPointer<LoopbackPkceFlow> m_flow;
    QTimer m_chatTimer;
    bool m_chatWanted = false;
};

} // namespace bc
