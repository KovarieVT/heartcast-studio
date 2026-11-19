#include "platforms/PlatformHub.h"
#include "auth/SecureStore.h"
#include "core/Settings.h"
#include "platforms/Twitch.h"
#include "platforms/YouTube.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSharedPointer>
#include <QUuid>

namespace bc {

// ---------------------------------------------------------------- catalog
const QVector<PlatformInfo> &PlatformCatalog::all() {
    static QVector<PlatformInfo> list = [] {
        QVector<PlatformInfo> out;
        QFile f(":/config/platforms.json");
        f.open(QIODevice::ReadOnly);
        for (const auto v : QJsonDocument::fromJson(f.readAll()).object().value("platforms").toArray()) {
            const QJsonObject o = v.toObject();
            PlatformInfo p;
            p.id = o.value("id").toString(); p.name = o.value("name").toString(); p.color = o.value("color").toString();
            p.auth = o.value("auth").toString(); p.defaultServer = o.value("defaultServer").toString();
            p.keyHelp = o.value("keyHelp").toString(); p.limitations = o.value("limitations").toString();
            for (auto c : o.value("capabilities").toArray()) p.capabilities << c.toString();
            for (auto c : o.value("scopes").toArray()) p.scopes << c.toString();
            p.recommended = o.value("recommended").toObject();
            out << p;
        }
        return out;
    }();
    return list;
}

PlatformInfo PlatformCatalog::get(const QString &id) {
    for (const auto &p : all()) if (p.id == id) return p;
    return all().isEmpty() ? PlatformInfo{} : all().last();
}

// ---------------------------------------------------------------- hub
PlatformHub &PlatformHub::instance() {
    static PlatformHub h;
    return h;
}

void PlatformHub::init() {
    if (m_initialised) return;
    m_initialised = true;
    m_accounts.insert("twitch", new TwitchAccount(&m_nam, this));
    m_accounts.insert("youtube", new YouTubeAccount(&m_nam, this));
    for (PlatformAccount *a : std::as_const(m_accounts)) {
        connect(a, &PlatformAccount::connectionChanged, this, &PlatformHub::accountsChanged);
        connect(a, &PlatformAccount::status, this, [this](const QString &s) { Q_EMIT notice(s, false); });
        connect(a, &PlatformAccount::error, this, [this](const QString &s) { Q_EMIT notice(s, true); });
        connect(a, &PlatformAccount::userCodeNeeded, this, [this, a](const QString &c, const QString &u) { Q_EMIT deviceCode(a->platformId(), c, u); });
        connect(a, &PlatformAccount::chatMessage, this, [this](ChatMessage m) {
            filterMessage(m);
            m_chat.append(m);
            while (m_chat.size() > 500) m_chat.removeFirst();
            Q_EMIT chatMessage(m);
        });
        connect(a, &PlatformAccount::chatMessageDeleted, this, [this](const QString &p, const QString &id) {
            for (auto &m : m_chat) if (m.platform == p && m.id == id) m.deleted = true;
            Q_EMIT chatMessageDeleted(p, id);
        });
        connect(a, &PlatformAccount::streamEvent, this, &PlatformHub::onEvent);
    }
    connect(&m_viewerTimer, &QTimer::timeout, this, [this] {
        if (!Engine::instance().isStreaming()) { m_viewers.clear(); return; }
        for (auto *a : std::as_const(m_accounts))
            if (a->isConnected()) a->fetchViewerCount([this, id = a->platformId()](int v) { m_viewers[id] = v; });
    });
    m_viewerTimer.start(30000);
}

int PlatformHub::totalViewers() const {
    int total = -1;
    for (int v : m_viewers) if (v >= 0) total = qMax(0, total) + v;
    return total;
}

QVector<Destination> PlatformHub::destinations() const {
    QVector<Destination> out;
    const QJsonArray arr = QJsonArray::fromVariantList(Settings::instance().get("destinations").toList());
    for (const auto v : arr) out << Destination::fromJson(v.toObject());
    return out;
}

void PlatformHub::saveDestination(const Destination &dIn, const QString &key) {
    Destination d = dIn;
    if (d.id.isEmpty()) d.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (d.keyRef.isEmpty()) d.keyRef = "streamkey." + d.id;
    if (!key.isEmpty()) SecureStore::put(d.keyRef, key.trimmed().toUtf8());
    QJsonArray arr;
    bool replaced = false;
    for (const auto &e : destinations()) {
        if (e.id == d.id) { arr.append(d.toJson()); replaced = true; }
        else arr.append(e.toJson());
    }
    if (!replaced) arr.append(d.toJson());
    Settings::instance().set("destinations", arr.toVariantList());
    Settings::instance().save();
    Q_EMIT destinationsChanged();
}

void PlatformHub::removeDestination(const QString &id) {
    QJsonArray arr;
    for (const auto &e : destinations()) {
        if (e.id == id) { SecureStore::remove(e.keyRef); continue; }
        arr.append(e.toJson());
    }
    Settings::instance().set("destinations", arr.toVariantList());
    Settings::instance().save();
    Q_EMIT destinationsChanged();
}

bool PlatformHub::hasStreamKey(const Destination &d) const { return !SecureStore::get(d.keyRef).isEmpty(); }

void PlatformHub::goLive(const QString &title, const std::function<void(bool, const QString &)> &done) {
    QVector<Destination> dests;
    for (const auto &d : destinations()) if (d.enabled) dests << d;
    if (dests.isEmpty()) { done(false, tr("Add at least one streaming destination in Stream Manager first.")); return; }

    // Fetch fresh ingest targets for connected API platforms (in parallel), then start.
    auto pending = QSharedPointer<int>::create(0);
    auto notes = QSharedPointer<QStringList>::create();
    auto finish = [this, dests, done, notes]() {
        QString err;
        const bool ok = Engine::instance().startStreaming(destinations(), &err);
        if (!err.isEmpty()) notes->append(err);
        if (ok) startChat();
        done(ok, notes->join('\n'));
    };
    for (const auto &d : dests) {
        PlatformAccount *acc = account(d.platform);
        if (!acc || !acc->isConnected()) continue;
        ++*pending;
        acc->fetchStreamTarget(title, [this, d, pending, notes, finish](bool ok, const QString &server, const QString &key, const QString &err) {
            if (ok) { Destination nd = d; nd.server = server; saveDestination(nd, key); }
            else notes->append(QStringLiteral("%1: %2").arg(d.name, err));
            if (--*pending == 0) finish();
        });
    }
    if (*pending == 0) finish();
}

void PlatformHub::endLive() {
    Engine::instance().stopStreaming();
}

void PlatformHub::startChat() { for (auto *a : std::as_const(m_accounts)) if (a->isConnected()) a->startChat(); }
void PlatformHub::stopChat() { for (auto *a : std::as_const(m_accounts)) a->stopChat(); }

void PlatformHub::sendChat(const QString &platform, const QString &text) {
    for (auto *a : std::as_const(m_accounts))
        if (a->isConnected() && (platform == "all" || platform == a->platformId())) a->sendChat(text);
}

void PlatformHub::moderate(ModAction a, const ChatMessage &m, int seconds, bool enable) {
    PlatformAccount *acc = account(m.platform);
    if (!acc || !acc->isConnected()) { Q_EMIT notice(tr("Moderation needs a connected %1 account.").arg(m.platform), true); return; }
    if (!acc->supports(a)) { Q_EMIT notice(tr("%1 does not allow this moderation action for third-party apps.").arg(PlatformCatalog::get(m.platform).name), true); return; }
    acc->moderate(a, m, seconds, enable);
}

QStringList PlatformHub::blockedWords() const { return Settings::instance().get("chat.blockedWords").toStringList(); }
void PlatformHub::setBlockedWords(const QStringList &w) { Settings::instance().set("chat.blockedWords", w); Settings::instance().save(); }

bool PlatformHub::filterMessage(ChatMessage &m) {
    if (m.fromMod) return false;
    const QString lower = m.text.toLower();
    for (const QString &w : blockedWords()) {
        if (!w.trimmed().isEmpty() && lower.contains(w.trimmed().toLower())) {
            m.hidden = true; m.hiddenReason = tr("blocked word"); return true;
        }
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = m.platform + ":" + (m.userId.isEmpty() ? m.user : m.userId);
    auto &times = m_userRate[key];
    times.append(now);
    while (!times.isEmpty() && now - times.first() > 5000) times.removeFirst();
    if (times.size() > 5) { m.hidden = true; m.hiddenReason = tr("flooding"); return true; }
    const auto last = m_lastText.value(key);
    if (last.first == lower && now - last.second < 15000 && lower.size() > 3) { m.hidden = true; m.hiddenReason = tr("repeated message"); return true; }
    m_lastText[key] = {lower, now};
    if (m.text.size() > 20 && m.text.count(QRegularExpression("[A-Z]")) > m.text.size() * 0.8) { m.hidden = true; m.hiddenReason = tr("all caps"); return true; }
    return false;
}

void PlatformHub::onEvent(const StreamEvent &e) {
    if (!e.test) {
        m_sessionCounts[e.type] += e.type == "gift" ? int(e.amount) : 1;
        m_sessionAmounts[e.type] += e.amount;
    }
    m_latest[e.type] = e.amount > 0 && e.type != "subscribe" ? QStringLiteral("%1 (%2)").arg(e.user).arg(e.amount) : e.user;
    m_events.prepend(e);
    while (m_events.size() > 200) m_events.removeLast();
    Q_EMIT streamEvent(e);
}

void PlatformHub::emitTestEvent(const QString &type) {
    StreamEvent e;
    e.platform = "test"; e.type = type; e.test = true;
    static const char *names[] = {"MochiMochi", "StrawberryMilk", "Pastelle", "CloudHopper", "SugarStar"};
    e.user = QString::fromLatin1(names[QDateTime::currentMSecsSinceEpoch() % 5]);
    if (type == "cheer") e.amount = 500;
    else if (type == "raid") e.amount = 42;
    else if (type == "gift") e.amount = 5;
    else if (type == "donation") { e.amount = 5; e.message = tr("you're so cute!"); }
    onEvent(e);
}

void PlatformHub::resetSession() {
    m_sessionCounts.clear();
    m_sessionAmounts.clear();
}

} // namespace bc
