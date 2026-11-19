#pragma once
#include "engine/Engine.h"
#include "platforms/Platform.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>

namespace bc {

// Owns platform accounts, streaming destinations, unified chat (with local
// keyword/spam filtering) and the event stream that drives alerts & widgets.
class PlatformHub : public QObject {
    Q_OBJECT
public:
    static PlatformHub &instance();
    void init();

    PlatformAccount *account(const QString &platformId) const { return m_accounts.value(platformId); }
    QList<PlatformAccount *> accounts() const { return m_accounts.values(); }
    QNetworkAccessManager *network() { return &m_nam; }

    // ---- destinations (persisted in Settings "destinations"; keys in SecureStore) ----
    QVector<Destination> destinations() const;
    void saveDestination(const Destination &d, const QString &streamKey = QString());
    void removeDestination(const QString &id);
    bool hasStreamKey(const Destination &d) const;

    // Go live on all enabled destinations. For API platforms with a connected
    // account, fresh server/key are fetched first (e.g. YouTube broadcast creation).
    void goLive(const QString &title, const std::function<void(bool ok, const QString &msg)> &done);
    void endLive();

    // ---- chat ----
    void startChat();
    void stopChat();
    void sendChat(const QString &platform, const QString &text);   // platform "all" = broadcast
    void moderate(ModAction a, const ChatMessage &m, int seconds = 600, bool enable = true);
    QList<ChatMessage> recentChat() const { return m_chat; }
    QStringList blockedWords() const;
    void setBlockedWords(const QStringList &w);

    // ---- events ----
    void emitTestEvent(const QString &type);
    QList<StreamEvent> recentEvents() const { return m_events; }
    int sessionCount(const QString &type) const { return m_sessionCounts.value(type); }
    double sessionAmount(const QString &type) const { return m_sessionAmounts.value(type); }
    QString latest(const QString &type) const { return m_latest.value(type); }
    void resetSession();
    int totalViewers() const;            // sum over platforms reporting a count, -1 if none
    QHash<QString, int> viewers() const { return m_viewers; }

    bool filterMessage(ChatMessage &m);   // public for unit tests

Q_SIGNALS:
    void accountsChanged();
    void destinationsChanged();
    void chatMessage(const ChatMessage &m);
    void chatMessageDeleted(const QString &platform, const QString &id);
    void streamEvent(const StreamEvent &e);
    void notice(const QString &text, bool isError);
    void deviceCode(const QString &platform, const QString &code, const QString &url);

private:
    PlatformHub() = default;
    void onEvent(const StreamEvent &e);
    QNetworkAccessManager m_nam;
    QHash<QString, PlatformAccount *> m_accounts;
    QList<ChatMessage> m_chat;
    QList<StreamEvent> m_events;
    QHash<QString, int> m_sessionCounts;
    QHash<QString, double> m_sessionAmounts;
    QHash<QString, QString> m_latest;
    QHash<QString, QList<qint64>> m_userRate;
    QHash<QString, QPair<QString, qint64>> m_lastText;
    QHash<QString, int> m_viewers;
    QTimer m_viewerTimer;
    bool m_initialised = false;
};

} // namespace bc
