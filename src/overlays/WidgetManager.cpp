#include "overlays/WidgetManager.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "platforms/PlatformHub.h"
#include "ui/KawaiiPainter.h"

#include <QDateTime>
#include <QFile>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QtMath>

namespace bc {

WidgetManager &WidgetManager::instance() {
    static WidgetManager w;
    return w;
}

QStringList WidgetManager::alertTypes() {
    return {"follow", "subscribe", "gift", "raid", "cheer", "donation", "membership", "redemption"};
}

void WidgetManager::init() {
    QFile f(":/overlays/packs.json");
    f.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const auto v : root.value("packs").toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonArray bg = o.value("bg").toArray();
        m_packs << OverlayPack{o.value("id").toString(), o.value("name").toString(), o.value("emoji").toString(),
                               o.value("font").toString(), QColor(bg.at(0).toString()), QColor(bg.at(1).toString()),
                               QColor(o.value("accent").toString()), QColor(o.value("text").toString())};
    }
    m_defaultTemplates = root.value("alertTemplates").toObject();

    auto &reg = WidgetRegistry::instance();
    connect(&reg, &WidgetRegistry::widgetsChanged, this, [this] {
        for (obs_source_t *s : WidgetRegistry::instance().sources()) {
            if (!m_timerStart.contains(s)) m_timerStart.insert(s, QDateTime::currentMSecsSinceEpoch());
            renderWidget(s);
        }
    });
    connect(&reg, &WidgetRegistry::widgetConfigChanged, this, &WidgetManager::renderWidget);

    auto &hub = PlatformHub::instance();
    connect(&hub, &PlatformHub::streamEvent, this, &WidgetManager::onEvent);
    connect(&hub, &PlatformHub::chatMessage, this, [this] {
        for (obs_source_t *s : WidgetRegistry::instance().sources())
            if (WidgetRegistry::instance().config(s).kind == "chat") renderWidget(s);
    });
    connect(&Engine::instance(), &Engine::streamingStateChanged, this, [this] {
        if (Engine::instance().isStreaming() && m_streamStartMs == 0) m_streamStartMs = QDateTime::currentMSecsSinceEpoch();
        if (!Engine::instance().isStreaming()) m_streamStartMs = 0;
    });

    // 1 Hz for clocks/labels/goals; widgets whose content didn't change still get a
    // cheap re-render because async sources need no per-frame work in between.
    connect(&m_secondTimer, &QTimer::timeout, this, [this] {
        for (obs_source_t *s : WidgetRegistry::instance().sources()) {
            const QString k = WidgetRegistry::instance().config(s).kind;
            if (k == "countdown" || k == "stopwatch" || k == "label" || k == "goal" || k == "viewers" || k == "events")
                renderWidget(s);
        }
    });
    m_secondTimer.start(1000);
    m_animTimer.setInterval(33);
    connect(&m_animTimer, &QTimer::timeout, this, &WidgetManager::animateTick);
}

void WidgetManager::shutdown() {
    m_secondTimer.stop();
    m_animTimer.stop();
    if (m_sound) obs_set_output_source(6, nullptr);
    m_sound = nullptr;
}

OverlayPack WidgetManager::pack(const QString &id) const {
    for (const auto &p : m_packs) if (p.id == id) return p;
    return m_packs.isEmpty() ? OverlayPack{"x", "x", "♡", "Nunito", QColor("#FFDCEB"), QColor("#DCCCF7"), QColor("#F5A6C8"), QColor("#514852")}
                             : m_packs.first();
}

AlertRule WidgetManager::rule(const QString &type) const {
    auto &s = Settings::instance();
    AlertRule r;
    r.enabled = s.get("alerts." + type + ".enabled", true).toBool();
    r.minAmount = s.get("alerts." + type + ".min", 0).toDouble();
    r.templ = s.get("alerts." + type + ".template", m_defaultTemplates.value(type).toString("{user} ♡")).toString();
    return r;
}

void WidgetManager::setRule(const QString &type, const AlertRule &r) {
    auto &s = Settings::instance();
    s.set("alerts." + type + ".enabled", r.enabled);
    s.set("alerts." + type + ".min", r.minAmount);
    s.set("alerts." + type + ".template", r.templ);
    s.save();
}

void WidgetManager::restartTimers() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_timerStart.begin(); it != m_timerStart.end(); ++it) it.value() = now;
    refreshAll();
}

void WidgetManager::refreshAll() { for (obs_source_t *s : WidgetRegistry::instance().sources()) renderWidget(s); }

// ---------------------------------------------------------------- alerts
void WidgetManager::onEvent(const StreamEvent &e) {
    const AlertRule r = rule(e.type);
    if (!r.enabled || (e.amount < r.minAmount && !e.test)) return;
    m_alertQueue.enqueue(e);
    if (!m_alertActive) nextAlert();
    for (obs_source_t *s : WidgetRegistry::instance().sources()) {
        const QString k = WidgetRegistry::instance().config(s).kind;
        if (k == "events" || k == "label" || k == "goal") renderWidget(s);
    }
}

void WidgetManager::nextAlert() {
    if (m_alertQueue.isEmpty()) {
        m_alertActive = false;
        m_animTimer.stop();
        for (obs_source_t *s : WidgetRegistry::instance().sources())
            if (WidgetRegistry::instance().config(s).kind == "alerts") renderWidget(s);   // clear
        return;
    }
    m_current = m_alertQueue.dequeue();
    m_alertActive = true;
    m_alertClock.restart();
    for (obs_source_t *s : WidgetRegistry::instance().sources()) {
        const WidgetConfig c = WidgetRegistry::instance().config(s);
        if (c.kind == "alerts" && !c.soundFile.isEmpty()) { playSound(c.soundFile); break; }
    }
    m_animTimer.start();
}

void WidgetManager::animateTick() {
    int durMs = 6000;
    for (obs_source_t *s : WidgetRegistry::instance().sources()) {
        const WidgetConfig c = WidgetRegistry::instance().config(s);
        if (c.kind == "alerts") { durMs = qMax(2, c.alertSeconds) * 1000; renderWidget(s); }
    }
    if (m_alertClock.elapsed() > durMs) nextAlert();
}

void WidgetManager::playSound(const QString &file) {
    if (!QFile::exists(file)) return;
    if (!m_sound) {
        OBSDataAutoRelease st = obs_data_create();
        obs_data_set_bool(st, "is_local_file", true);
        obs_data_set_bool(st, "restart_on_activate", false);
        obs_data_set_bool(st, "close_when_inactive", false);
        m_sound = obs_source_create_private("ffmpeg_source", "HeartCast Alert Sound", st);
        if (!m_sound) return;
        obs_source_set_monitoring_type(m_sound, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
        obs_set_output_source(6, m_sound);   // mixed into stream + recordings
    }
    OBSDataAutoRelease st = obs_data_create();
    obs_data_set_string(st, "local_file", file.toUtf8().constData());
    obs_data_set_bool(st, "is_local_file", true);
    obs_source_update(m_sound, st);
    obs_source_media_restart(m_sound);
}

// ---------------------------------------------------------------- rendering
void WidgetManager::renderWidget(obs_source_t *src) {
    const WidgetConfig cfg = WidgetRegistry::instance().config(src);
    if (cfg.kind.isEmpty()) return;
    WidgetRegistry::instance().pushFrame(src, render(src, cfg));
}

QImage WidgetManager::renderPreview(const WidgetConfig &cfg, const QSize &size) {
    WidgetConfig c = cfg; c.width = size.width(); c.height = size.height();
    return render(nullptr, c);
}

static QString fmtDuration(qint64 secs) {
    secs = qMax<qint64>(0, secs);
    return secs >= 3600 ? QStringLiteral("%1:%2:%3").arg(secs / 3600).arg((secs / 60) % 60, 2, 10, QChar('0')).arg(secs % 60, 2, 10, QChar('0'))
                        : QStringLiteral("%1:%2").arg(secs / 60, 2, 10, QChar('0')).arg(secs % 60, 2, 10, QChar('0'));
}

QImage WidgetManager::render(obs_source_t *src, const WidgetConfig &cfg) {
    const int w = qBound(16, cfg.width, 3840), h = qBound(16, cfg.height, 2160);
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const OverlayPack pk = pack(cfg.pack);
    QFont font(pk.font); font.setPixelSize(qMax(8, cfg.fontSize)); font.setWeight(QFont::Bold);
    QFont small(pk.font); small.setPixelSize(qMax(8, int(cfg.fontSize * 0.7)));
    const QRectF card(6, 6, w - 12, h - 12);
    auto &hub = PlatformHub::instance();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    auto panel = [&](const QRectF &r) { kawaii::drawGlossyCard(p, r, pk.bg1, pk.bg2, pk.accent, qMin(26.0, r.height() / 3)); };
    auto title = [&](const QString &t, const QRectF &r) {
        p.setFont(font); p.setPen(pk.text);
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, t);
    };

    const QString k = cfg.kind;
    if (k == "alerts") {
        if (!m_alertActive && src) { p.end(); return img; }   // transparent while idle
        const StreamEvent e = m_alertActive ? m_current : StreamEvent{"preview", "follow", "MochiMochi", {}};
        const qint64 t = m_alertActive ? m_alertClock.elapsed() : 900;
        const int dur = qMax(2, cfg.alertSeconds) * 1000;
        // pop in (elastic), hold, fade out
        double sc = 1, op = 1;
        if (t < 450) { const double x = t / 450.0; sc = 1 - std::pow(2, -10 * x) * std::cos(x * 10); op = qMin(1.0, x * 2); }
        if (t > dur - 400) { op = qMax(0.0, (dur - t) / 400.0); sc = 0.9 + 0.1 * op; }
        p.setOpacity(op);
        p.translate(w / 2.0, h / 2.0); p.scale(sc, sc); p.translate(-w / 2.0, -h / 2.0);
        panel(card.adjusted(w * 0.05, h * 0.08, -w * 0.05, -h * 0.08));
        // floating hearts
        for (int i = 0; i < 9; ++i) {
            const double ph = (t / 1000.0 + i * 0.37);
            const double x = w * (0.1 + 0.8 * std::fmod(i * 0.618, 1.0));
            const double y = h - std::fmod(ph * h * 0.45, double(h));
            QColor c = i % 2 ? pk.accent : QColor("#A9E5D0"); c.setAlphaF(0.75);
            p.setPen(Qt::NoPen); p.setBrush(c);
            p.drawPath(kawaii::heart(QPointF(x, y), h * 0.09));
        }
        QString text = rule(e.type).templ;
        text.replace("{user}", e.user).replace("{amount}", QString::number(e.amount)).replace("{message}", e.message);
        const double beat = 1.0 + 0.08 * std::max(0.0, std::sin(t / 90.0));   // little heartbeat
        kawaii::drawGlossyHeart(p, QPointF(card.left() + h * 0.42, h * 0.52), h * 0.42 * beat, QColor("#F7C4D8"), pk.accent, QColor("#C96F9D"));
        title(pk.emoji + " " + text, card.adjusted(h * 0.75, 0, -w * 0.06, 0));
    } else if (k == "chat") {
        panel(card);
        p.setFont(small);
        const QFontMetrics fm(small);
        double y = card.bottom() - 12;
        const auto msgs = hub.recentChat();
        for (int i = msgs.size() - 1; i >= 0 && y > card.top() + 10; --i) {
            const ChatMessage &m = msgs[i];
            if (m.hidden || m.deleted) continue;
            const QString line = m.user + ": " + m.text;
            const QRectF br = fm.boundingRect(QRect(0, 0, int(card.width() - 44), 2000), Qt::TextWordWrap, line);
            y -= br.height() + 6;
            if (y < card.top() + 6) break;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(m.platform == "twitch" ? "#9146FF" : m.platform == "youtube" ? "#FF0033" : "#C96F9D"));
            p.drawEllipse(QPointF(card.left() + 20, y + fm.height() / 2.0), 5, 5);
            p.setPen(pk.text);
            p.drawText(QRectF(card.left() + 32, y, card.width() - 44, br.height() + 2), Qt::TextWordWrap, line);
        }
    } else if (k == "events") {
        panel(card);
        p.setFont(small); p.setPen(pk.text);
        double y = card.top() + 14;
        for (const auto &e : hub.recentEvents()) {
            if (y > card.bottom() - 20) break;
            p.drawText(QRectF(card.left() + 18, y, card.width() - 36, 30), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1 %2 · %3%4").arg(pk.emoji, e.user, e.type, e.amount > 0 ? " ×" + QString::number(e.amount) : QString()));
            y += small.pixelSize() * 1.6;
        }
    } else if (k == "label") {
        panel(card);
        const QString lt = cfg.labelType;
        QString v = lt == "recent_follower" ? hub.latest("follow") : lt == "recent_sub" ? hub.latest("subscribe")
                  : lt == "recent_donation" ? (hub.latest("cheer").isEmpty() ? hub.latest("donation") : hub.latest("cheer"))
                  : QString::number(hub.sessionCount("follow"));
        const QString pre = cfg.title.isEmpty() ? (lt == "recent_follower" ? tr("Latest follower") : lt == "recent_sub" ? tr("Latest sub")
                          : lt == "recent_donation" ? tr("Latest tip") : tr("New followers")) : cfg.title;
        title(QStringLiteral("%1 %2: %3").arg(pk.emoji, pre, v.isEmpty() ? QStringLiteral("—") : v), card);
    } else if (k == "goal") {
        panel(card);
        const QString metric = cfg.goalMetric;
        const double cur = cfg.goalStart + (metric == "donations" ? hub.sessionAmount("cheer") + hub.sessionAmount("donation")
                                         : metric == "subs" ? hub.sessionCount("subscribe") + hub.sessionCount("gift")
                                         : hub.sessionCount("follow"));
        const double frac = qBound(0.0, cur / qMax(1, cfg.goalTarget), 1.0);
        p.setFont(small); p.setPen(pk.text);
        p.drawText(card.adjusted(20, 8, -20, -card.height() / 2), Qt::AlignLeft | Qt::AlignVCenter,
                   cfg.title.isEmpty() ? tr("Goal ♡") : cfg.title);
        p.drawText(card.adjusted(20, 8, -20, -card.height() / 2), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 / %2").arg(cur).arg(cfg.goalTarget));
        const QRectF bar(card.left() + 20, card.center().y() + 4, card.width() - 40, qMax(10.0, card.height() * 0.22));
        p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 170)); p.drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);
        QLinearGradient g(bar.topLeft(), bar.topRight()); g.setColorAt(0, QColor("#A9E5D0")); g.setColorAt(1, pk.accent);
        p.setBrush(g); p.drawRoundedRect(QRectF(bar.left(), bar.top(), qMax(bar.height(), bar.width() * frac), bar.height()), bar.height() / 2, bar.height() / 2);
        p.setBrush(QColor("#E98DB7")); p.drawPath(kawaii::heart(QPointF(bar.left() + qMax(bar.height(), bar.width() * frac) - bar.height() / 2, bar.center().y()), bar.height() * 1.1));
    } else if (k == "countdown" || k == "stopwatch") {
        panel(card);
        const qint64 start = src ? m_timerStart.value(src, now) : now;
        const qint64 elapsed = (k == "stopwatch" && m_streamStartMs > 0 ? now - m_streamStartMs : now - start) / 1000;
        const QString t = k == "countdown" ? fmtDuration(qint64(cfg.countdownMinutes) * 60 - elapsed) : fmtDuration(elapsed);
        title((cfg.title.isEmpty() ? QString() : cfg.title + "  ") + t, card);
    } else if (k == "social" || k == "sponsor") {
        panel(card);
        const QStringList items = cfg.socials.split(';', Qt::SkipEmptyParts);
        p.setFont(small); p.setPen(pk.text);
        const double rowH = items.isEmpty() ? card.height() : (card.height() - 20) / items.size();
        if (items.isEmpty()) title(cfg.title.isEmpty() ? tr("Add handles in widget settings ♡") : cfg.title, card);
        for (int i = 0; i < items.size(); ++i) {
            const QString plat = items[i].section(':', 0, 0).trimmed(), name = items[i].section(':', 1).trimmed();
            const QRectF row(card.left() + 18, card.top() + 10 + i * rowH, card.width() - 36, rowH);
            p.setPen(Qt::NoPen); p.setBrush(QColor(PlatformCatalog::get(plat).color.isEmpty() ? "#C96F9D" : PlatformCatalog::get(plat).color));
            p.drawEllipse(QPointF(row.left() + 8, row.center().y()), 7, 7);
            p.setPen(pk.text);
            p.drawText(row.adjusted(24, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, (k == "sponsor" ? QString() : "@") + name + (plat.isEmpty() ? QString() : "  · " + plat));
        }
    } else if (k == "viewers") {
        panel(card);
        const int v = hub.totalViewers();
        title(QStringLiteral("%1 %2").arg(pk.emoji, v < 0 ? tr("offline") : tr("%n viewer(s)", nullptr, v)), card);
    } else { // text_panel
        panel(card);
        title(cfg.title.isEmpty() ? tr("hello, cuties ♡") : cfg.title, card);
    }
    // tiny sparkle accent on every widget
    p.setOpacity(0.9); p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 220));
    p.drawPath(kawaii::sparkle(QPointF(w - 22, 20), 9));
    p.end();
    return img;
}

} // namespace bc
