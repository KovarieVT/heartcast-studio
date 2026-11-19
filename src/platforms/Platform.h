#pragma once
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

namespace bc {

struct PlatformInfo {
    QString id, name, color, auth, defaultServer, keyHelp, limitations;
    QStringList capabilities, scopes;
    QJsonObject recommended;
    bool has(const QString &cap) const { return capabilities.contains(cap); }
};

class PlatformCatalog {
public:
    static const QVector<PlatformInfo> &all();
    static PlatformInfo get(const QString &id);
};

struct ChatMessage {
    QString platform, id, user, userId, color, text;
    QStringList badges;
    QDateTime ts = QDateTime::currentDateTime();
    bool fromMod = false, hidden = false, deleted = false;
    QString hiddenReason;
};

struct StreamEvent {
    QString platform, type, user, message;   // type: follow | subscribe | gift | raid | cheer | donation | membership | redemption
    double amount = 0;
    QDateTime ts = QDateTime::currentDateTime();
    bool test = false;
};

enum class ModAction { Delete, Timeout, Ban, Block, SlowMode, FollowerOnly, SubscriberOnly };

// One connected account on an API-integrated platform.
class PlatformAccount : public QObject {
    Q_OBJECT
public:
    explicit PlatformAccount(QNetworkAccessManager *nam, QObject *parent = nullptr) : QObject(parent), m_nam(nam) {}
    virtual QString platformId() const = 0;
    virtual bool isConnected() const = 0;
    virtual QString accountName() const = 0;
    virtual void connectAccount() = 0;
    virtual void disconnectAccount() = 0;
    virtual void refreshIfNeeded(const std::function<void(bool)> &done) = 0;

    using TargetCb = std::function<void(bool ok, const QString &server, const QString &key, const QString &err)>;
    virtual void fetchStreamTarget(const QString &title, const TargetCb &cb) = 0;
    virtual void updateStreamInfo(const QString &title, const QString &category, const QString &description,
                                  const std::function<void(bool, const QString &)> &cb) = 0;

    virtual void fetchViewerCount(const std::function<void(int viewers)> &cb) = 0;   // -1 = not live/unknown
    virtual void startChat() = 0;
    virtual void stopChat() = 0;
    virtual void sendChat(const QString &text) = 0;
    virtual bool supports(ModAction a) const = 0;
    virtual void moderate(ModAction a, const ChatMessage &target, int seconds, bool enable = true) = 0;

Q_SIGNALS:
    void connectionChanged();
    void status(const QString &message);
    void userCodeNeeded(const QString &code, const QString &url);
    void chatMessage(const ChatMessage &m);
    void chatMessageDeleted(const QString &platform, const QString &messageId);
    void streamEvent(const StreamEvent &e);
    void error(const QString &message);

protected:
    QNetworkAccessManager *m_nam;
};

} // namespace bc
