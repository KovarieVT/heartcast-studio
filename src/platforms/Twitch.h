#pragma once
#include "auth/OAuth.h"
#include "platforms/Platform.h"
#include <QPointer>
#include <QWebSocket>

namespace bc {

// Twitch: OAuth device-code flow (public client), Helix API, IRC-over-WebSocket chat,
// EventSub-over-WebSocket for follows / subs / gifts / cheers / raids / redemptions.
class TwitchAccount : public PlatformAccount {
    Q_OBJECT
public:
    explicit TwitchAccount(QNetworkAccessManager *nam, QObject *parent = nullptr);
    QString platformId() const override { return "twitch"; }
    bool isConnected() const override { return m_tokens.valid() && !m_userId.isEmpty(); }
    QString accountName() const override { return m_login; }
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
    bool supports(ModAction) const override { return true; }
    void moderate(ModAction a, const ChatMessage &target, int seconds, bool enable = true) override;

    static QStringList scopes();
    // exposed for tests
    static ChatMessage parseIrcPrivmsg(const QString &line);

private:
    void validate(const std::function<void(bool)> &done);
    void helix(const QByteArray &verb, const QString &path, const QJsonObject &body,
               const std::function<void(int status, const QJsonObject &)> &cb, bool retried = false);
    void onIrc(const QString &frame);
    void startEventSub();
    void onEventSub(const QString &frame);
    void subscribe(const QString &type, const QString &version, const QJsonObject &condition);

    TokenSet m_tokens;
    QString m_userId, m_login;
    QPointer<DeviceCodeFlow> m_flow;
    QWebSocket m_irc, m_eventSub;
    QString m_eventSessionId;
    bool m_chatWanted = false;
};

} // namespace bc
