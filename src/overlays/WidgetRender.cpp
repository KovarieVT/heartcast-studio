// Painting for every widget kind. WidgetManager::render() is called on the main thread with
// either a live source (real data only) or nullptr for gallery previews (sample data).
#include "overlays/WidgetManager.h"
#include "app/Theme.h"
#include "engine/Engine.h"
#include "platforms/PlatformHub.h"
#include "ui/GothicPainter.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QTimeZone>
#include <QtMath>
#include <algorithm>
#include <functional>

namespace bc {

namespace {

const QStringList kSampleNames = {"Nightshade", "Ravenmoor", "VelvetCrypt", "Hollowmoon", "Ashgrave", "Morrow", "Wraithling"};

QString fmtDuration(qint64 secs) {
    const bool neg = secs < 0;
    secs = std::abs(secs);
    const QString s = secs >= 3600 ? QStringLiteral("%1:%2:%3").arg(secs / 3600).arg((secs / 60) % 60, 2, 10, QChar('0')).arg(secs % 60, 2, 10, QChar('0'))
                                   : QStringLiteral("%1:%2").arg(secs / 60, 2, 10, QChar('0')).arg(secs % 60, 2, 10, QChar('0'));
    return neg ? "-" + s : s;
}

QString fmtAmount(double v) { return std::abs(v - std::round(v)) < 0.005 ? QString::number(qint64(std::round(v))) : QString::number(v, 'f', 2); }

struct Ctx {
    QPainter &p;
    const OverlayPack &pk;
    const WidgetConfig &cfg;
    WidgetManager &wm;
    obs_source_t *src;
    int w, h;
    QRectF card;
    qint64 now;
    bool preview;

    QFont display(double px) const { QFont f(pk.font); f.setPixelSize(qMax(8, int(px))); f.setWeight(QFont::Bold); return f; }
    QFont body(double px, bool bold = false) const { QFont f(theme::bodyFamily()); f.setPixelSize(qMax(8, int(px))); if (bold) f.setWeight(QFont::DemiBold); return f; }
    // Font size in px. 0 = automatic: big, card-filling text for one-line widgets (clocks,
    // counters, labels) and a comfortable reading size for tall list widgets (chat, events).
    double fs() const {
        if (cfg.fontSize > 0) return cfg.fontSize;
        return h <= 220 ? qMax(12.0, h * 0.38) : qBound(20.0, std::sqrt(double(w) * h) / 18.0, 44.0);
    }

    void panel(const QRectF &r) const { gothic::drawGothicCard(p, r, pk.bg1, pk.bg2, pk.accent, pk.metal); }
    void panel() const { panel(card); }

    // Draws text centred in r, shrinking the font until it fits.
    void fit(const QString &t, const QRectF &r, QFont f, const QColor &col, int flags = Qt::AlignCenter | Qt::TextWordWrap) const {
        for (int px = f.pixelSize(); px > 8; px = int(px * 0.9)) {
            f.setPixelSize(px);
            const QRectF need = QFontMetricsF(f).boundingRect(r, flags, t);
            if (need.width() <= r.width() + 0.5 && need.height() <= r.height() + 0.5) break;
        }
        p.setFont(f); p.setPen(col);
        p.drawText(r, flags, t);
    }
    void title(const QString &t, const QRectF &r) const { fit(t, r.adjusted(r.width() * 0.05, r.height() * 0.1, -r.width() * 0.05, -r.height() * 0.1), display(fs()), pk.text); }
    void heading(const QString &t, const QRectF &r) const { fit(t, r, display(fs() * 0.75), pk.metal.lighter(115), Qt::AlignLeft | Qt::AlignVCenter); }
    QString glyphed(const QString &t) const { return pk.glyph.isEmpty() ? t : pk.glyph + "  " + t; }
    QColor dim(const QColor &c, int alpha) const { QColor d = c; d.setAlpha(alpha); return d; }

    void bar(const QRectF &r, double frac) const {
        p.setPen(QPen(pk.metal.darker(130), 1)); p.setBrush(QColor(0, 0, 0, 160)); p.drawRect(r);
        const QRectF fill(r.left() + 1, r.top() + 1, qMax(0.0, (r.width() - 2) * qBound(0.0, frac, 1.0)), r.height() - 2);
        QLinearGradient g(fill.topLeft(), fill.topRight()); g.setColorAt(0, pk.accent.darker(200)); g.setColorAt(1, pk.accent.lighter(115));
        p.setPen(Qt::NoPen); p.setBrush(g); p.drawRect(fill);
        if (frac > 0) {
            gothic::drawGlow(p, QPointF(fill.right(), r.center().y()), r.height() * 1.6, pk.accent, 0.8);
            p.setBrush(pk.metal); p.drawPath(gothic::diamond(QPointF(fill.right(), r.center().y()), r.height() * 0.85));
        }
    }

    // Rows of text with an optional right-aligned value, under an optional heading.
    void list(const QString &head, const QList<QPair<QString, QString>> &rows, int maxRows, const QString &empty, bool numbered = false) const {
        panel();
        double y = card.top() + card.height() * 0.06;
        const QRectF inner = card.adjusted(card.width() * 0.07, 0, -card.width() * 0.07, 0);
        if (!head.isEmpty()) { heading(head, QRectF(inner.left(), y, inner.width(), fs() * 1.3)); y += fs() * 1.55; }
        const double rowH = qMax(fs() * 0.95, (card.bottom() - card.height() * 0.06 - y) / qMax(1, maxRows));
        const QFont f = body(qMin(rowH * 0.55, fs() * 0.8));
        if (rows.isEmpty()) { fit(empty, QRectF(inner.left(), y, inner.width(), card.bottom() - y - 10), body(fs() * 0.6), dim(pk.text, 170)); return; }
        for (int i = 0; i < rows.size() && i < maxRows; ++i) {
            const QRectF row(inner.left(), y + i * rowH, inner.width(), rowH);
            p.setPen(Qt::NoPen); p.setBrush(i == 0 ? pk.accent : pk.metal);
            p.drawPath(gothic::diamond(QPointF(row.left() + 6, row.center().y()), rowH * 0.14));
            p.setFont(f); p.setPen(pk.text);
            const QString left = numbered ? QStringLiteral("%1.  %2").arg(i + 1).arg(rows[i].first) : rows[i].first;
            p.drawText(row.adjusted(20, 0, -(rows[i].second.isEmpty() ? 0 : row.width() * 0.3), 0), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetricsF(f).elidedText(left, Qt::ElideRight, row.width() * (rows[i].second.isEmpty() ? 0.95 : 0.65)));
            if (!rows[i].second.isEmpty()) { p.setPen(pk.metal.lighter(130)); p.drawText(row, Qt::AlignRight | Qt::AlignVCenter, rows[i].second); }
            if (i + 1 < rows.size() && i + 1 < maxRows) {
                p.setPen(QPen(dim(pk.metal, 50), 1));
                p.drawLine(QPointF(row.left() + 20, row.bottom()), QPointF(row.right(), row.bottom()));
            }
        }
    }

    // A horizontally scrolling line of text segments.
    void marquee(const QStringList &items, const QString &empty) const {
        panel();
        const QRectF inner = card.adjusted(16, 4, -16, -4);
        const QFont f = body(qMin(inner.height() * 0.5, fs()), true);
        p.save();
        p.setClipRect(inner);
        if (items.isEmpty()) { p.restore(); fit(empty, inner, f, dim(pk.text, 170)); return; }
        const QString sep = QStringLiteral("     %1     ").arg(pk.glyph.isEmpty() ? QStringLiteral("✧") : pk.glyph);
        const QString line = items.join(sep) + sep;
        const double lw = QFontMetricsF(f).horizontalAdvance(line);
        const double speed = qMax(60.0, inner.height() * 1.8);   // px per second
        double x = inner.left() + inner.width() - std::fmod(now / 1000.0 * speed, lw + inner.width());
        p.setFont(f); p.setPen(pk.text);
        for (; x < inner.right(); x += lw) p.drawText(QRectF(x, inner.top(), lw + 4, inner.height()), Qt::AlignLeft | Qt::AlignVCenter, line);
        p.restore();
    }
};

// ---------------------------------------------------------------- data helpers
QList<StreamEvent> eventsOf(const QString &metric) {
    QList<StreamEvent> out;
    for (const auto &e : PlatformHub::instance().recentEvents()) {
        if (e.test) continue;
        const bool tips = e.type == "cheer" || e.type == "donation";
        if (metric == "all" || e.type == metric || (metric == "tips" && tips)) out << e;
    }
    return out;
}

QString describeEvent(const StreamEvent &e) {
    if (e.type == "follow") return QObject::tr("%1 followed").arg(e.user);
    if (e.type == "subscribe") return QObject::tr("%1 subscribed").arg(e.user);
    if (e.type == "membership") return QObject::tr("%1 became a member").arg(e.user);
    if (e.type == "gift") return QObject::tr("%1 gifted %2").arg(e.user, fmtAmount(e.amount));
    if (e.type == "raid") return QObject::tr("%1 raided with %2").arg(e.user, fmtAmount(e.amount));
    if (e.type == "cheer") return QObject::tr("%1 cheered %2").arg(e.user, fmtAmount(e.amount));
    if (e.type == "donation") return QObject::tr("%1 tipped %2").arg(e.user, fmtAmount(e.amount));
    if (e.type == "redemption") return QObject::tr("%1 redeemed %2").arg(e.user, e.message);
    return e.user;
}

double goalValue(const QString &metric, const WidgetManager &wm) {
    auto &hub = PlatformHub::instance();
    if (metric == "donations") return hub.sessionAmount("donation");
    if (metric == "bits") return hub.sessionAmount("cheer");
    if (metric == "subs") return hub.sessionCount("subscribe") + hub.sessionAmount("gift") + hub.sessionCount("membership");
    if (metric == "messages") return wm.session().messages;
    return hub.sessionCount("follow");
}

QList<QPair<QString, QString>> topOf(const QHash<QString, double> &totals, int n, bool money) {
    QList<QPair<QString, double>> v;
    for (auto it = totals.begin(); it != totals.end(); ++it) v.append({it.key(), it.value()});
    std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    QList<QPair<QString, QString>> rows;
    for (int i = 0; i < v.size() && i < n; ++i) rows.append({v[i].first, money ? fmtAmount(v[i].second) : QString::number(qint64(v[i].second))});
    return rows;
}

// Particles for ambience widgets and alert cards.
void particles(QPainter &p, const QString &kind, const QColor &accent, int w, int h, qint64 t, int count) {
    p.save();
    p.setPen(Qt::NoPen);
    for (int i = 0; i < count; ++i) {
        const double seed = std::fmod(i * 0.618034, 1.0), seed2 = std::fmod(i * 0.754877, 1.0);
        const double ph = t / 1000.0 + i * 0.37;
        const double x0 = w * (0.02 + 0.96 * seed);
        const double sz = qMin(w, h) * (0.6 + 0.8 * seed2);
        if (kind == "bats") {
            const double speed = 0.06 + 0.06 * seed2;
            const double x = std::fmod(x0 + ph * w * speed, double(w) + 80) - 40;
            const double y = h * (0.9 - std::fmod(ph * 0.05 + seed, 1.0) * 0.85) + std::sin(ph * 2 + i) * h * 0.02;
            p.setBrush(QColor(4, 3, 6, 225));
            p.drawPath(gothic::bat(QPointF(x, y), sz * 0.06, std::sin(ph * (9 + 4 * seed) + i)));
        } else if (kind == "petals") {
            const double y = std::fmod(ph * h * (0.06 + 0.05 * seed2) + seed * h, double(h) + 40) - 20;
            p.save(); p.translate(x0 + std::sin(ph * 1.6 + i) * w * 0.02, y); p.rotate(ph * 70 + i * 40);
            QColor c = accent; c.setAlphaF(0.85); p.setBrush(c);
            p.drawPath(gothic::petal(QPointF(0, 0), sz * 0.035));
            p.restore();
        } else if (kind == "feathers") {
            const double y = std::fmod(ph * h * (0.04 + 0.03 * seed2) + seed * h, double(h) + 80) - 40;
            p.save(); p.translate(x0 + std::sin(ph * 1.2 + i) * w * 0.03, y); p.rotate(std::sin(ph * 1.2 + i) * 35 + 20);
            p.setBrush(QColor(10, 10, 14, 230)); p.drawPath(gothic::feather(QPointF(0, 0), sz * 0.08));
            p.restore();
        } else if (kind == "fog") {
            const double x = std::fmod(seed * w + ph * w * 0.015 * (1 + seed2), double(w) * 1.4) - w * 0.2;
            const double y = h * (0.72 + 0.26 * seed2);
            QRadialGradient g(QPointF(x, y), w * 0.22);
            g.setColorAt(0, QColor(0x8A, 0x80, 0x96, 60)); g.setColorAt(1, QColor(0x8A, 0x80, 0x96, 0));
            p.setBrush(g); p.drawEllipse(QPointF(x, y), w * 0.25, h * 0.12);
        } else {   // embers rise, ash falls
            const bool ash = kind == "ash";
            const double prog = std::fmod(ph * (0.05 + 0.05 * seed2) + seed, 1.0);
            const double y = ash ? h * prog : h * (1.0 - prog);
            const double x = x0 + std::sin(ph + i) * w * 0.012;
            const double flick = 0.5 + 0.5 * std::sin(ph * 7 + i);
            const double r = qMin(w, h) * (ash ? 0.004 : 0.003) * (1 + seed2);
            if (!ash) gothic::drawGlow(p, QPointF(x, y), r * 6, accent, 0.4 * flick);
            QColor c = ash ? QColor(200, 196, 190) : accent.lighter(140);
            c.setAlphaF(ash ? 0.55 : 0.35 + 0.55 * flick);
            p.setBrush(c); p.drawEllipse(QPointF(x, y), r, r);
        }
    }
    p.restore();
}

void drawCandle(QPainter &p, const QPointF &base, double height, double width, double flicker) {
    const QRectF body(base.x() - width / 2, base.y() - height, width, height);
    QLinearGradient wax(body.topLeft(), body.topRight());
    wax.setColorAt(0, QColor(0xB9, 0xAF, 0x9E)); wax.setColorAt(0.45, QColor(0xF3, 0xEC, 0xDF)); wax.setColorAt(1, QColor(0x98, 0x8C, 0x7A));
    p.setPen(Qt::NoPen); p.setBrush(wax); p.drawRoundedRect(body, width * 0.12, width * 0.12);
    p.drawEllipse(QPointF(body.left() + width * 0.2, body.top() + height * 0.12), width * 0.12, height * 0.12);
    p.setPen(QPen(QColor(0x20, 0x18, 0x14), qMax(1.0, width * 0.05)));
    p.drawLine(QPointF(base.x(), body.top()), QPointF(base.x(), body.top() - width * 0.25));
    const QPointF fb(base.x(), body.top() - width * 0.22);
    const double fh = width * 1.1 * (0.9 + 0.2 * flicker), fw = width * 0.32;
    gothic::drawGlow(p, fb - QPointF(0, fh * 0.5), width * 2.8, QColor(255, 160, 70), 0.55 + 0.25 * flicker);
    QPainterPath f; f.moveTo(fb); f.cubicTo(fb + QPointF(-fw, -fh * 0.25), fb + QPointF(-fw * 0.4, -fh * 0.75), fb + QPointF(0, -fh));
    f.cubicTo(fb + QPointF(fw * 0.4, -fh * 0.75), fb + QPointF(fw, -fh * 0.25), fb);
    QRadialGradient g(fb - QPointF(0, fh * 0.35), fh * 0.7);
    g.setColorAt(0, QColor(255, 250, 220)); g.setColorAt(0.5, QColor(255, 190, 80)); g.setColorAt(1, QColor(230, 90, 30, 0));
    p.setPen(Qt::NoPen); p.setBrush(g); p.drawPath(f);
}

// Moon phase: 0 = new, 0.5 = full (days since a known new moon / synodic month).
double moonPhase(const QDateTime &when) {
    const QDateTime ref(QDate(2000, 1, 6), QTime(18, 14), QTimeZone::utc());
    const double days = ref.msecsTo(when) / 86400000.0;
    const double ph = std::fmod(days / 29.530588853, 1.0);
    return ph < 0 ? ph + 1 : ph;
}

QString moonName(double ph) {
    const char *names[] = {"New moon", "Waxing crescent", "First quarter", "Waxing gibbous", "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"};
    return QObject::tr(names[int(std::floor(ph * 8 + 0.5)) % 8]);
}

void drawMoon(QPainter &p, const QPointF &c, double r, double ph, const QColor &glow) {
    gothic::drawGlow(p, c, r * 2.2, glow, 0.5);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0x18, 0x14, 0x1C)); p.drawEllipse(c, r, r);   // dark side
    // lit part = half disc + an ellipse whose width follows cos(2π·phase)
    const double k = std::cos(ph * 2 * M_PI);   // 1 new … -1 full
    QPainterPath lit;
    const bool waxing = ph < 0.5;
    QPainterPath half;
    half.moveTo(c.x(), c.y() - r);
    half.arcTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 90, waxing ? -180 : 180);
    half.closeSubpath();
    QPainterPath ell; ell.addEllipse(c, r * std::abs(k), r);
    lit = k > 0 ? half.subtracted(ell) : half.united(ell);
    QRadialGradient g(c - QPointF(r * 0.3, r * 0.3), r * 1.4);
    g.setColorAt(0, theme::Parchment); g.setColorAt(1, theme::Silver.darker(120));
    p.setBrush(g); p.drawPath(lit);
    p.setBrush(QColor(0, 0, 0, 30));
    for (int i = 0; i < 6; ++i) p.drawEllipse(c + QPointF(std::cos(i * 1.7) * r * 0.5, std::sin(i * 2.3) * r * 0.45), r * (0.08 + 0.04 * (i % 3)), r * (0.07 + 0.03 * (i % 2)));
}

} // namespace

QImage WidgetManager::render(obs_source_t *src, const WidgetConfig &cfg) {
    const int w = qBound(16, cfg.width, 3840), h = qBound(16, cfg.height, 2160);
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    const OverlayPack pk = pack(cfg.pack);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Ctx c{p, pk, cfg, *this, src, w, h, QRectF(8, 8, w - 16, h - 16), now, src == nullptr};
    auto &hub = PlatformHub::instance();
    auto &eng = Engine::instance();
    const QString k = cfg.kind;
    const QString sample = kSampleNames.at(int(qHash(cfg.kind) % uint(kSampleNames.size())));

    if (k == "alerts") {
        if (!m_alertActive && src) { p.end(); return img; }   // transparent while idle
        const StreamEvent e = m_alertActive ? m_current : StreamEvent{"preview", "follow", sample, {}};
        const qint64 t = m_alertActive ? m_alertClock.elapsed() : 900;
        const int dur = qMax(2, cfg.alertSeconds) * 1000;
        double sc = 1, op = 1, lift = 0;
        if (t < 600) { const double x = t / 600.0, ease = 1 - std::pow(1 - x, 3); op = ease; sc = 0.94 + 0.06 * ease; lift = (1 - ease) * h * 0.08; }
        if (t > dur - 500) { op = qMax(0.0, (dur - t) / 500.0); sc = 0.97 + 0.03 * op; }
        p.setOpacity(op);
        p.translate(w / 2.0, h / 2.0 + lift); p.scale(sc, sc); p.translate(-w / 2.0, -h / 2.0);
        const QRectF body = c.card.adjusted(w * 0.04, h * 0.10, -w * 0.04, -h * 0.10);
        c.panel(body);
        p.save(); p.setClipPath(gothic::chamfered(body, 18)); particles(p, pk.particles, pk.accent, w, h, t, 12); p.restore();
        QString text = rule(e.type).templ;
        text.replace("{user}", e.user).replace("{amount}", fmtAmount(e.amount)).replace("{message}", e.message);
        gothic::drawSigil(p, QPointF(body.left() + body.height() * 0.55, body.center().y()), body.height() * 0.36, pk.accent, pk.metal, 0.5 + 0.5 * std::sin(t / 160.0));
        c.title(text, body.adjusted(body.height() * 1.05, 0, -w * 0.02, 0));
    } else if (k == "chat") {
        c.panel();
        const QFont f = c.body(c.fs() * 0.7);
        const QFontMetricsF fm(f);
        p.setFont(f);
        double y = c.card.bottom() - 14;
        QList<ChatMessage> msgs = hub.recentChat();
        if (!src && msgs.isEmpty())
            for (int i = 0; i < 4; ++i) { ChatMessage m; m.user = kSampleNames[i]; m.text = QStringList{"good evening ☾", "that transition!", "first time here", "lurking"}[i]; m.platform = i % 2 ? "youtube" : "twitch"; msgs << m; }
        for (int i = int(msgs.size()) - 1; i >= 0 && y > c.card.top() + 12; --i) {
            const ChatMessage &m = msgs[i];
            if (m.hidden || m.deleted) continue;
            const QString line = m.user + ": " + m.text;
            const QRectF br = fm.boundingRect(QRectF(0, 0, c.card.width() - 48, 2000), Qt::TextWordWrap, line);
            y -= br.height() + 6;
            if (y < c.card.top() + 8) break;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(m.platform == "twitch" ? "#9146FF" : m.platform == "youtube" ? "#FF0033" : pk.accent.name()));
            p.drawPath(gothic::diamond(QPointF(c.card.left() + 22, y + fm.height() / 2.0), 5));
            p.setPen(pk.text);
            p.drawText(QRectF(c.card.left() + 34, y, c.card.width() - 48, br.height() + 2), Qt::TextWordWrap, line);
        }
    } else if (k == "events" || k == "recent_list") {
        const QList<StreamEvent> evs = eventsOf(k == "events" ? "all" : cfg.metric);
        QList<QPair<QString, QString>> rows;
        for (const auto &e : evs) rows.append(k == "events" ? qMakePair(describeEvent(e), QString())
                                                            : qMakePair(e.user, e.amount > 0 && cfg.metric != "follow" ? fmtAmount(e.amount) : QString()));
        if (!src && rows.isEmpty()) for (int i = 0; i < 5; ++i) rows.append({kSampleNames[i], cfg.metric == "tips" ? QString::number(5 * (5 - i)) : QString()});
        c.list(k == "events" ? QString() : cfg.title, rows, qMax(1, cfg.count), tr("Nothing yet this stream"));
    } else if (k == "ticker") {
        QStringList items;
        for (const auto &e : eventsOf(cfg.metric.isEmpty() ? "all" : cfg.metric)) { items << describeEvent(e); if (items.size() >= qMax(1, cfg.count) * 2) break; }
        if (!src && items.isEmpty()) {   // preview: sample events of the kinds this ticker shows
            const QString m = cfg.metric.isEmpty() ? QStringLiteral("all") : cfg.metric;
            const QList<QPair<QString, double>> types = {{"follow", 0}, {"subscribe", 0}, {"cheer", 500}, {"donation", 5}, {"gift", 5}, {"raid", 42}};
            for (int n = 0; n < 5; ++n)
                for (int t = 0; t < types.size(); ++t) {
                    const QString &ty = types[t].first;
                    const bool tips = ty == "cheer" || ty == "donation";
                    if (!(m == "all" ? t == n : (ty == m || (m == "tips" && tips)))) continue;
                    StreamEvent ev; ev.type = ty; ev.user = kSampleNames[(n + t) % 5]; ev.amount = types[t].second;
                    items << describeEvent(ev);
                }
        }
        c.marquee(items, tr("Waiting for the first event…"));
    } else if (k == "raid_banner") {
        c.panel();
        QString who; double size = 0;
        for (const auto &e : eventsOf("raid")) { who = e.user; size = e.amount; break; }
        if (!src && who.isEmpty()) { who = sample; size = 42; }
        c.title(who.isEmpty() ? tr("No raids yet") : tr("%1 raided with %2").arg(who, fmtAmount(size)), c.card.adjusted(c.card.height() * 0.9, 0, 0, 0));
        p.setBrush(QColor(4, 3, 6)); p.setPen(QPen(pk.metal, 1.5));
        p.drawPath(gothic::bat(QPointF(c.card.left() + c.card.height() * 0.55, c.card.center().y()), c.card.height() * 0.8, std::sin(now / 150.0)));
    } else if (k == "hype_meter") {
        c.panel();
        const double target = qMax(1, cfg.goalTarget);
        const double score = src ? hypeScore() : target * 1.6;
        const int level = int(score / target) + 1;
        const double frac = std::fmod(score, target) / target;
        const QRectF inner = c.card.adjusted(c.card.width() * 0.06, c.card.height() * 0.12, -c.card.width() * 0.06, -c.card.height() * 0.12);
        c.heading((cfg.title.isEmpty() ? tr("Hype") : cfg.title) + QStringLiteral("  ·  ") + tr("Level %1").arg(level), QRectF(inner.left(), inner.top(), inner.width(), inner.height() * 0.45));
        c.bar(QRectF(inner.left(), inner.top() + inner.height() * 0.58, inner.width(), inner.height() * 0.28), frac);
    } else if (k == "combo") {
        const auto &ses = session();
        const int combo = src ? ses.combo : 5;
        if (combo <= 0) { p.end(); return img; }   // invisible until a combo starts
        c.panel();
        const double left = src ? qBound(0.0, 1.0 - (now - ses.comboLastMs) / 60000.0, 1.0) : 0.6;
        c.fit(QStringLiteral("×%1").arg(combo), QRectF(c.card.left(), c.card.top() + c.card.height() * 0.05, c.card.width(), c.card.height() * 0.55), c.display(c.card.height() * 0.45), pk.accent.lighter(140));
        c.fit(cfg.title.isEmpty() ? tr("Sub combo") : cfg.title, QRectF(c.card.left(), c.card.top() + c.card.height() * 0.55, c.card.width(), c.card.height() * 0.22), c.display(c.fs() * 0.7), pk.text);
        c.bar(QRectF(c.card.left() + c.card.width() * 0.1, c.card.bottom() - c.card.height() * 0.17, c.card.width() * 0.8, c.card.height() * 0.07), left);
    } else if (k == "label") {
        c.panel();
        const QString lt = cfg.labelType;
        auto latest = [&](const QStringList &types) {
            for (const auto &e : hub.recentEvents()) if (!e.test && types.contains(e.type)) return e.type == "redemption" ? e.user + " · " + e.message : e.amount > 0 && e.type != "follow" && e.type != "subscribe" ? e.user + " (" + fmtAmount(e.amount) + ")" : e.user;
            return QString();
        };
        QString pre, v;
        if (lt == "recent_follower") { pre = tr("Latest follower"); v = latest({"follow"}); }
        else if (lt == "recent_sub") { pre = tr("Latest sub"); v = latest({"subscribe"}); }
        else if (lt == "recent_gift") { pre = tr("Latest gifter"); v = latest({"gift"}); }
        else if (lt == "recent_raid") { pre = tr("Latest raid"); v = latest({"raid"}); }
        else if (lt == "recent_cheer") { pre = tr("Latest cheer"); v = latest({"cheer"}); }
        else if (lt == "recent_donation") { pre = tr("Latest tip"); v = latest({"donation", "cheer"}); }
        else if (lt == "recent_member") { pre = tr("Latest member"); v = latest({"membership"}); }
        else if (lt == "recent_redemption") { pre = tr("Latest redemption"); v = latest({"redemption"}); }
        else if (lt == "session_subs") { pre = tr("Subs this stream"); v = fmtAmount(goalValue("subs", *this)); }
        else if (lt == "session_bits") { pre = tr("Bits this stream"); v = fmtAmount(hub.sessionAmount("cheer")); }
        else if (lt == "session_tips") { pre = tr("Tips this stream"); v = fmtAmount(hub.sessionAmount("donation")); }
        else if (lt == "session_chatters") { pre = tr("Chatters this stream"); v = QString::number(session().chatCounts.size()); }
        else { pre = tr("Followers this stream"); v = QString::number(hub.sessionCount("follow")); }
        if (!src && (v.isEmpty() || v == "0")) v = lt.startsWith("session") ? QStringLiteral("12") : sample;
        c.title(c.glyphed(QStringLiteral("%1: %2").arg(cfg.title.isEmpty() ? pre : cfg.title, v.isEmpty() ? QStringLiteral("—") : v)), c.card);
    } else if (k == "goal" || k == "goal_ring" || k == "goal_candle") {
        const double target = qMax(1, cfg.goalTarget);
        double cur = cfg.goalStart + goalValue(cfg.goalMetric, *this);
        if (!src && cur <= cfg.goalStart) cur = std::round(target * 0.62);
        const double frac = qBound(0.0, cur / target, 1.0);
        const QString count = QStringLiteral("%1 / %2").arg(fmtAmount(cur)).arg(cfg.goalTarget);
        if (k == "goal") {
            c.panel();
            const QRectF top = c.card.adjusted(24, 8, -24, -c.card.height() / 2);
            c.heading(cfg.title.isEmpty() ? tr("Goal") : cfg.title, top);
            p.setFont(c.display(c.fs() * 0.7)); p.setPen(pk.text); p.drawText(top, Qt::AlignRight | Qt::AlignVCenter, count);
            c.bar(QRectF(c.card.left() + 24, c.card.center().y() + 4, c.card.width() - 48, qMax(8.0, c.card.height() * 0.16)), frac);
        } else if (k == "goal_ring") {
            const double r = qMin(w, h) * 0.42;
            const QPointF cc(w / 2.0, h / 2.0);
            gothic::drawGlow(p, cc, r * 1.3, pk.accent, 0.4);
            p.setPen(Qt::NoPen); p.setBrush(pk.bg2); p.drawEllipse(cc, r, r);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0, 0, 0, 160), r * 0.14, Qt::SolidLine, Qt::FlatCap)); p.drawEllipse(cc, r * 0.85, r * 0.85);
            QConicalGradient g(cc, 90); g.setColorAt(0, pk.accent.lighter(130)); g.setColorAt(1, pk.accent.darker(170));
            p.setPen(QPen(QBrush(g), r * 0.14, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(cc.x() - r * 0.85, cc.y() - r * 0.85, r * 1.7, r * 1.7), 90 * 16, -int(360 * 16 * frac));
            p.setPen(QPen(pk.metal, 2)); p.drawEllipse(cc, r, r); p.drawEllipse(cc, r * 0.7, r * 0.7);
            c.fit(QString::number(int(frac * 100)) + "%", QRectF(cc.x() - r * 0.6, cc.y() - r * 0.42, r * 1.2, r * 0.5), c.display(r * 0.4), pk.text);
            c.fit((cfg.title.isEmpty() ? tr("Goal") : cfg.title) + "\n" + count, QRectF(cc.x() - r * 0.6, cc.y() + r * 0.05, r * 1.2, r * 0.42), c.body(r * 0.13), c.dim(pk.text, 210));
        } else {   // the candle burns down toward the goal
            const double baseY = h * 0.92, maxH = h * 0.62, cw = w * 0.26;
            p.setPen(Qt::NoPen); p.setBrush(pk.metal.darker(150));
            p.drawEllipse(QPointF(w / 2.0, baseY + 4), cw * 1.1, cw * 0.22);   // holder
            drawCandle(p, QPointF(w / 2.0, baseY), qMax(cw * 0.3, maxH * (1.0 - frac * 0.92)), cw, 0.5 + 0.5 * std::sin(now / 120.0) * std::sin(now / 47.0));
            c.fit(cfg.title.isEmpty() ? tr("Goal") : cfg.title, QRectF(0, 4, w, h * 0.1), c.display(c.fs() * 0.75), pk.text);
            c.fit(count, QRectF(0, h * 0.1, w, h * 0.08), c.body(c.fs() * 0.6), c.dim(pk.text, 210));
        }
    } else if (k == "chat_bubble") {
        QString user = session().lastChatUser, text = session().lastChatText;
        if (!src && user.isEmpty()) { user = sample; text = tr("that stinger was so good"); }
        if (user.isEmpty()) { p.end(); return img; }
        const QRectF b = c.card.adjusted(0, 0, 0, -h * 0.16);
        c.panel(b);
        QPainterPath tail; tail.moveTo(b.left() + b.width() * 0.12, b.bottom() - 2); tail.lineTo(b.left() + b.width() * 0.10, h - 4); tail.lineTo(b.left() + b.width() * 0.2, b.bottom() - 2);
        p.setPen(QPen(pk.metal, 1.5)); p.setBrush(pk.bg2); p.drawPath(tail);
        c.fit(user, QRectF(b.left() + 24, b.top() + 10, b.width() - 48, b.height() * 0.3), c.display(c.fs() * 0.7), pk.accent.lighter(140), Qt::AlignLeft | Qt::AlignVCenter);
        c.fit(text, QRectF(b.left() + 24, b.top() + b.height() * 0.38, b.width() - 48, b.height() * 0.52), c.body(c.fs()), pk.text, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap);
    } else if (k == "chat_marquee") {
        QStringList items;
        const auto msgs = hub.recentChat();
        for (int i = int(msgs.size()) - 1; i >= 0 && items.size() < 12; --i) if (!msgs[i].hidden && !msgs[i].deleted) items.prepend(msgs[i].user + ": " + msgs[i].text);
        if (!src && items.isEmpty()) items = {kSampleNames[0] + ": good evening", kSampleNames[1] + ": first time here"};
        c.marquee(items, tr("Chat will scroll here"));
    } else if (k == "poll") {
        c.panel();
        const QStringList opts = cfg.text.split('\n', Qt::SkipEmptyParts);
        const ChatWidgetState st = chatState(src);
        QVector<int> votes(opts.size(), 0);
        for (int v : st.votes) if (v >= 1 && v <= votes.size()) votes[v - 1]++;
        if (!src) for (int i = 0; i < votes.size(); ++i) votes[i] = 12 - 4 * i;
        int total = 0; for (int v : votes) total += v;
        const QRectF in = c.card.adjusted(c.card.width() * 0.06, c.card.height() * 0.06, -c.card.width() * 0.06, -c.card.height() * 0.06);
        c.heading(cfg.title.isEmpty() ? tr("Poll") : cfg.title, QRectF(in.left(), in.top(), in.width(), in.height() * 0.16));
        const double rowH = in.height() * 0.68 / qMax<qsizetype>(1, opts.size());
        for (int i = 0; i < opts.size(); ++i) {
            const QRectF row(in.left(), in.top() + in.height() * 0.2 + i * rowH, in.width(), rowH * 0.86);
            const double frac = total ? votes[i] / double(total) : 0;
            c.fit(QStringLiteral("%1. %2").arg(i + 1).arg(opts[i]), QRectF(row.left(), row.top(), row.width() * 0.75, row.height() * 0.5), c.body(rowH * 0.32, true), pk.text, Qt::AlignLeft | Qt::AlignVCenter);
            p.setFont(c.body(rowH * 0.3)); p.setPen(pk.metal.lighter(130));
            p.drawText(QRectF(row.left(), row.top(), row.width(), row.height() * 0.5), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1% · %2").arg(int(frac * 100 + 0.5)).arg(votes[i]));
            c.bar(QRectF(row.left(), row.top() + row.height() * 0.58, row.width(), row.height() * 0.28), frac);
        }
        c.fit(tr("Type %1 1–%2 to vote").arg(cfg.keyword.isEmpty() ? QStringLiteral("!vote") : cfg.keyword).arg(opts.size()),
              QRectF(in.left(), in.bottom() - in.height() * 0.1, in.width(), in.height() * 0.1), c.body(in.height() * 0.07), c.dim(pk.text, 170));
    } else if (k == "giveaway") {
        c.panel();
        const ChatWidgetState st = chatState(src);
        const int entries = src ? int(st.entrants.size()) : 27;
        const QString winner = src ? st.winner : QString();
        const QRectF in = c.card.adjusted(c.card.width() * 0.06, c.card.height() * 0.1, -c.card.width() * 0.06, -c.card.height() * 0.1);
        c.heading(cfg.title.isEmpty() ? tr("Giveaway") : cfg.title, QRectF(in.left(), in.top(), in.width(), in.height() * 0.25));
        if (!winner.isEmpty()) c.fit(tr("Winner: %1").arg(winner), QRectF(in.left(), in.top() + in.height() * 0.3, in.width(), in.height() * 0.45), c.display(c.fs()), pk.accent.lighter(150));
        else c.fit(tr("Type %1 to enter").arg(cfg.keyword.isEmpty() ? QStringLiteral("!join") : cfg.keyword), QRectF(in.left(), in.top() + in.height() * 0.3, in.width(), in.height() * 0.4), c.display(c.fs() * 0.85), pk.text);
        c.fit(tr("%n entr(ies)", nullptr, entries), QRectF(in.left(), in.bottom() - in.height() * 0.22, in.width(), in.height() * 0.22), c.body(c.fs() * 0.6), c.dim(pk.text, 190));
    } else if (k == "qna") {
        c.panel();
        const ChatWidgetState st = chatState(src);
        QString who, q, counter;
        if (!src) { who = sample; q = tr("What's your favourite horror game?"); counter = tr("Question 1 of 3"); }
        else if (st.questionIndex >= 0 && st.questionIndex < st.questions.size()) {
            who = st.questions[st.questionIndex].first; q = st.questions[st.questionIndex].second;
            counter = tr("Question %1 of %2").arg(st.questionIndex + 1).arg(st.questions.size());
        }
        const QRectF in = c.card.adjusted(c.card.width() * 0.05, c.card.height() * 0.1, -c.card.width() * 0.05, -c.card.height() * 0.1);
        if (q.isEmpty()) {
            c.fit(st.questions.isEmpty() ? tr("Ask with %1 your question").arg(cfg.keyword.isEmpty() ? QStringLiteral("!q") : cfg.keyword) : tr("No more questions"),
                  in, c.display(c.fs() * 0.8), c.dim(pk.text, 200));
        } else {
            c.fit(who + (counter.isEmpty() ? QString() : "  ·  " + counter), QRectF(in.left(), in.top(), in.width(), in.height() * 0.25), c.body(c.fs() * 0.6, true), pk.accent.lighter(150), Qt::AlignLeft | Qt::AlignVCenter);
            c.fit(q, QRectF(in.left(), in.top() + in.height() * 0.28, in.width(), in.height() * 0.72), c.body(c.fs()), pk.text, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap);
        }
    } else if (k == "word_counter" || k == "counter") {
        c.panel();
        const int v = k == "counter" ? cfg.value : (src ? chatState(src).words : 37);
        const QString head = cfg.title.isEmpty() ? (k == "counter" ? tr("Count") : cfg.keyword) : cfg.title;
        c.fit(head, QRectF(c.card.left() + c.card.width() * 0.06, c.card.top(), c.card.width() * 0.55, c.card.height()), c.display(c.fs()), pk.text, Qt::AlignLeft | Qt::AlignVCenter);
        c.fit(QString::number(v), QRectF(c.card.left() + c.card.width() * 0.6, c.card.top(), c.card.width() * 0.34, c.card.height()), c.display(c.card.height() * 0.5), pk.accent.lighter(150), Qt::AlignRight | Qt::AlignVCenter);
    } else if (k == "winloss") {
        c.panel();
        const QRectF in = c.card.adjusted(c.card.width() * 0.05, 0, -c.card.width() * 0.05, 0);
        if (!cfg.title.isEmpty()) c.fit(cfg.title, QRectF(in.left(), in.top(), in.width() * 0.3, in.height()), c.display(c.fs() * 0.8), pk.metal.lighter(120), Qt::AlignLeft | Qt::AlignVCenter);
        const QRectF score(in.left() + (cfg.title.isEmpty() ? 0 : in.width() * 0.3), in.top(), in.width() * (cfg.title.isEmpty() ? 1.0 : 0.7), in.height());
        c.fit(QStringLiteral("W %1   ·   L %2").arg(cfg.value).arg(cfg.value2), score, c.display(c.card.height() * 0.4), pk.text);
    } else if (k == "first_chatter") {
        c.panel();
        QString who = session().firstChatter;
        if (!src && who.isEmpty()) who = sample;
        c.title(who.isEmpty() ? tr("Waiting for the first chatter…") : c.glyphed(QStringLiteral("%1: %2").arg(cfg.title.isEmpty() ? tr("First chatter") : cfg.title, who)), c.card);
    } else if (k == "leaderboard") {
        QList<QPair<QString, QString>> rows;
        if (cfg.metric == "gifters") rows = topOf(session().giftTotals, cfg.count, false);
        else if (cfg.metric == "tippers") rows = topOf(session().tipTotals, cfg.count, true);
        else {
            QHash<QString, double> chat;
            for (auto it = session().chatCounts.begin(); it != session().chatCounts.end(); ++it) chat.insert(it.key(), it.value());
            rows = topOf(chat, cfg.count, false);
        }
        if (!src && rows.isEmpty()) for (int i = 0; i < 5; ++i) rows.append({kSampleNames[i], QString::number(50 - 9 * i)});
        c.list(cfg.title, rows, qMax(1, cfg.count), tr("No one yet this stream"), true);
    } else if (k == "clock" || k == "world_clock") {
        c.panel();
        QDateTime t = QDateTime::currentDateTime();
        QString label = cfg.title;
        if (k == "world_clock") {
            const QTimeZone tz(cfg.target.toUtf8());
            if (!tz.isValid()) { c.title(tr("Unknown time zone: %1").arg(cfg.target), c.card); p.end(); return img; }
            t = t.toTimeZone(tz);
            if (label.isEmpty()) label = cfg.target.section('/', -1).replace('_', ' ');
        }
        const QString time = QLocale().toString(t.time(), QLocale::ShortFormat);
        c.title(label.isEmpty() ? time : label + "  " + time, c.card);
    } else if (k == "analog_clock") {
        const QTime t = QTime::currentTime();
        const double r = qMin(w, h) * 0.46;
        const QPointF cc(w / 2.0, h / 2.0);
        gothic::drawGlow(p, cc, r * 1.25, pk.accent, 0.35);
        QRadialGradient face(cc, r); face.setColorAt(0, pk.bg1.lighter(120)); face.setColorAt(1, pk.bg2);
        p.setPen(QPen(pk.metal, r * 0.04)); p.setBrush(face); p.drawEllipse(cc, r, r);
        p.setPen(QPen(pk.metal.darker(130), r * 0.012)); p.setBrush(Qt::NoBrush); p.drawEllipse(cc, r * 0.86, r * 0.86);
        const char *roman[] = {"XII", "I", "II", "III", "IIII", "V", "VI", "VII", "VIII", "IX", "X", "XI"};
        QFont rf(theme::displayFamily()); rf.setPixelSize(int(r * 0.15)); rf.setWeight(QFont::Bold); p.setFont(rf); p.setPen(pk.text);
        for (int i = 0; i < 12; ++i) {
            const double a = i * M_PI / 6 - M_PI / 2;
            const QPointF pt = cc + QPointF(std::cos(a), std::sin(a)) * r * 0.7;
            p.drawText(QRectF(pt.x() - r * 0.2, pt.y() - r * 0.1, r * 0.4, r * 0.2), Qt::AlignCenter, roman[i]);
        }
        auto hand = [&](double angle, double len, double width, const QColor &col) {
            p.save(); p.translate(cc); p.rotate(angle);
            QPainterPath hp; hp.moveTo(0, r * 0.08); hp.lineTo(-width, 0); hp.lineTo(-width * 0.3, -len * 0.8);
            hp.lineTo(-width * 1.4, -len * 0.84); hp.lineTo(0, -len); hp.lineTo(width * 1.4, -len * 0.84); hp.lineTo(width * 0.3, -len * 0.8); hp.lineTo(width, 0); hp.closeSubpath();
            p.setPen(Qt::NoPen); p.setBrush(col); p.drawPath(hp); p.restore();
        };
        hand((t.hour() % 12 + t.minute() / 60.0) * 30, r * 0.5, r * 0.045, pk.metal.lighter(120));
        hand((t.minute() + t.second() / 60.0) * 6, r * 0.74, r * 0.035, pk.metal.lighter(140));
        p.save(); p.translate(cc); p.rotate(t.second() * 6);
        p.setPen(QPen(pk.accent.lighter(130), qMax(1.0, r * 0.012))); p.drawLine(QPointF(0, r * 0.12), QPointF(0, -r * 0.8)); p.restore();
        p.setPen(Qt::NoPen); p.setBrush(pk.accent); p.drawEllipse(cc, r * 0.04, r * 0.04);
    } else if (k == "date") {
        c.panel();
        c.title(QLocale().toString(QDate::currentDate(), QLocale::LongFormat), c.card);
    } else if (k == "countdown" || k == "stopwatch" || k == "subathon" || k == "countdown_date") {
        c.panel();
        QString t, label = cfg.title;
        if (k == "countdown") t = fmtDuration(qMax<qint64>(0, qint64(cfg.countdownMinutes) * 60 - (now - widgetStartMs(src)) / 1000));
        else if (k == "stopwatch") t = m_streamStartMs > 0 ? fmtDuration((now - m_streamStartMs) / 1000) : (src ? tr("offline") : QStringLiteral("1:23:45"));
        else if (k == "subathon") {
            const qint64 end = cfg.subathonEnd > 0 ? cfg.subathonEnd : now + qint64(cfg.countdownMinutes) * 60000;
            t = end > now ? fmtDuration((end - now) / 1000) : tr("complete");
            if (label.isEmpty()) label = tr("Subathon");
        } else {
            const QDateTime target = QDateTime::fromString(cfg.target.trimmed(), "yyyy-MM-dd HH:mm");
            if (!target.isValid()) t = tr("set a date as YYYY-MM-DD HH:MM");
            else {
                const qint64 s = QDateTime::currentDateTime().secsTo(target);
                t = s <= 0 ? tr("it's here!") : QStringLiteral("%1d %2h %3m %4s").arg(s / 86400).arg((s / 3600) % 24, 2, 10, QChar('0')).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
            }
        }
        c.title(label.isEmpty() ? t : label + "  " + t, c.card);
    } else if (k == "pomodoro") {
        const qint64 focus = qint64(qMax(1, cfg.countdownMinutes)) * 60, brk = qint64(qMax(1, cfg.interval)) * 60;
        const qint64 el = ((now - widgetStartMs(src)) / 1000) % (focus + brk);
        const bool inFocus = el < focus;
        const qint64 left = inFocus ? focus - el : focus + brk - el;
        const double frac = inFocus ? el / double(focus) : (el - focus) / double(brk);
        const double r = qMin(w, h) * 0.38;
        const QPointF cc(w / 2.0, h * 0.45);
        p.setPen(QPen(QColor(0, 0, 0, 160), r * 0.16)); p.setBrush(pk.bg2); p.drawEllipse(cc, r, r);
        p.setPen(QPen(inFocus ? pk.accent : theme::Verdigris, r * 0.16, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(cc.x() - r, cc.y() - r, 2 * r, 2 * r), 90 * 16, -int(360 * 16 * frac));
        c.fit(fmtDuration(left), QRectF(cc.x() - r * 0.8, cc.y() - r * 0.4, r * 1.6, r * 0.8), c.display(r * 0.42), pk.text);
        c.fit(inFocus ? tr("Focus") : tr("Break"), QRectF(0, h * 0.86, w, h * 0.13), c.display(c.fs() * 0.8), pk.metal.lighter(120));
    } else if (k == "moon_phase") {
        c.panel();
        const double ph = moonPhase(QDateTime::currentDateTimeUtc());
        const double r = qMin(c.card.width() * 0.25, c.card.height() * 0.33);
        drawMoon(p, QPointF(w / 2.0, c.card.top() + c.card.height() * 0.42), r, ph, theme::Silver);
        c.fit(moonName(ph) + QStringLiteral(" · %1%").arg(int((1 - std::cos(ph * 2 * M_PI)) / 2 * 100 + 0.5)),
              QRectF(c.card.left(), c.card.bottom() - c.card.height() * 0.24, c.card.width(), c.card.height() * 0.18), c.display(c.fs() * 0.7), pk.text);
    } else if (k == "viewers") {
        c.panel();
        const int v = src ? hub.totalViewers() : 128;
        c.title(c.glyphed(v < 0 ? tr("offline") : tr("%n viewer(s)", nullptr, v)), c.card);
    } else if (k == "on_air") {
        const bool live = src ? eng.isStreaming() : true;
        const double pulse = live ? 0.75 + 0.25 * std::sin(now / 300.0) : 0;
        gothic::drawGothicCard(p, c.card, live ? QColor(0x3A, 0x06, 0x10) : pk.bg2, pk.bg2, live ? QColor(0xE0, 0x30, 0x3F) : pk.metal.darker(160), pk.metal);
        if (live) gothic::drawGlow(p, c.card.center(), c.card.width() * 0.6, QColor(0xE0, 0x30, 0x3F), 0.5 * pulse);
        c.fit(cfg.title.isEmpty() ? (live ? tr("ON AIR") : tr("OFF AIR")) : cfg.title, c.card.adjusted(16, 8, -16, -8), c.display(c.card.height() * 0.5),
              live ? QColor(255, 220 - int(40 * pulse), 220 - int(40 * pulse)) : c.dim(pk.text, 120));
    } else if (k == "rec_indicator") {
        const bool rec = src ? eng.isRecording() : true;
        c.panel();
        const bool on = rec && (now / 1000) % 2 == 0;
        const QPointF dot(c.card.left() + c.card.height() * 0.5, c.card.center().y());
        if (on) gothic::drawGlow(p, dot, c.card.height() * 0.6, QColor(255, 40, 60), 0.9);
        p.setPen(Qt::NoPen); p.setBrush(rec ? QColor(on ? 0xFF : 0x9A, 0x28, 0x3C) : pk.metal.darker(160));
        p.drawEllipse(dot, c.card.height() * 0.17, c.card.height() * 0.17);
        const QString t = rec ? QStringLiteral("REC  ") + fmtDuration(m_recStartMs > 0 ? (now - m_recStartMs) / 1000 : 754) : QStringLiteral("REC");
        c.fit(t, c.card.adjusted(c.card.height() * 0.85, 0, -10, 0), c.display(c.card.height() * 0.38), rec ? pk.text : c.dim(pk.text, 120), Qt::AlignLeft | Qt::AlignVCenter);
    } else if (k == "stream_health" || k == "fps_meter" || k == "cpu_meter") {
        c.panel();
        const bool live = eng.isStreaming();
        double kbps = 0; int dropped = 0, total = 0;
        for (const QString &id : eng.activeDestinations()) { const auto st = eng.destinationStatus(id); kbps += st.kbps; dropped += st.droppedFrames; total += st.totalFrames; }
        const double fps = eng.stats().activeFps;
        const double cpu = src ? m_cpu : 23;
        if (k == "fps_meter") {
            c.title(QStringLiteral("%1 FPS").arg(src ? fps : 60.0, 0, 'f', 1), c.card);
        } else if (k == "cpu_meter") {
            c.title(tr("CPU %1%").arg(int(cpu + 0.5)), c.card.adjusted(0, 0, 0, -c.card.height() * 0.25));
            c.bar(QRectF(c.card.left() + 24, c.card.bottom() - c.card.height() * 0.28, c.card.width() - 48, c.card.height() * 0.12), cpu / 100.0);
        } else {
            const double dropPct = total > 0 ? dropped * 100.0 / total : 0;
            const QColor state = !live && src ? pk.metal : dropPct > 2 ? theme::Blood : dropPct > 0.5 ? theme::Amber : theme::Verdigris;
            const QRectF in = c.card.adjusted(c.card.width() * 0.06, c.card.height() * 0.1, -c.card.width() * 0.06, -c.card.height() * 0.1);
            p.setPen(Qt::NoPen); p.setBrush(state); p.drawPath(gothic::diamond(QPointF(in.left() + in.height() * 0.15, in.top() + in.height() * 0.2), in.height() * 0.12));
            c.fit(!live && src ? tr("Offline") : dropPct > 2 ? tr("Stream struggling") : dropPct > 0.5 ? tr("Stream fair") : tr("Stream healthy"),
                  QRectF(in.left() + in.height() * 0.35, in.top(), in.width(), in.height() * 0.4), c.display(c.fs() * 0.75), pk.text, Qt::AlignLeft | Qt::AlignVCenter);
            c.fit(QStringLiteral("%1 kbps  ·  %2% dropped  ·  %3 fps").arg(int(src ? kbps : 6000)).arg(src ? dropPct : 0.0, 0, 'f', 1).arg(src ? fps : 60.0, 0, 'f', 0),
                  QRectF(in.left(), in.top() + in.height() * 0.5, in.width(), in.height() * 0.4), c.body(c.fs() * 0.6), c.dim(pk.text, 210), Qt::AlignLeft | Qt::AlignVCenter);
        }
    } else if (k == "bitrate_graph") {
        c.panel();
        QList<double> hist = src ? m_kbps : QList<double>{};
        if (!src) for (int i = 0; i < 60; ++i) hist << 5600 + 500 * std::sin(i * 0.4) + 200 * std::sin(i * 1.7);
        const QRectF g = c.card.adjusted(20, c.card.height() * 0.28, -20, -16);
        double mx = 1000; for (double v : hist) mx = qMax(mx, v * 1.15);
        c.heading(tr("Bitrate  %1 kbps").arg(int(hist.isEmpty() ? 0 : hist.last())), QRectF(c.card.left() + 20, c.card.top() + 6, c.card.width() - 40, c.card.height() * 0.2));
        p.setPen(QPen(c.dim(pk.metal, 60), 1));
        for (int i = 1; i < 4; ++i) p.drawLine(QPointF(g.left(), g.top() + g.height() * i / 4), QPointF(g.right(), g.top() + g.height() * i / 4));
        if (hist.size() > 1) {
            QPainterPath line, area;
            for (int i = 0; i < hist.size(); ++i) {
                const QPointF pt(g.left() + g.width() * i / 59.0 + g.width() * (60 - hist.size()) / 59.0, g.bottom() - g.height() * hist[i] / mx);
                i ? line.lineTo(pt) : line.moveTo(pt);
            }
            area = line; area.lineTo(g.right(), g.bottom()); area.lineTo(g.left() + g.width() * (60 - hist.size()) / 59.0, g.bottom()); area.closeSubpath();
            QLinearGradient ag(g.topLeft(), g.bottomLeft()); ag.setColorAt(0, c.dim(pk.accent, 120)); ag.setColorAt(1, c.dim(pk.accent, 0));
            p.setPen(Qt::NoPen); p.setBrush(ag); p.drawPath(area);
            p.setBrush(Qt::NoBrush); p.setPen(QPen(pk.accent.lighter(130), 2)); p.drawPath(line);
        }
    } else if (k == "scene_name") {
        c.panel();
        const QString scene = eng.programScene();
        c.title(c.glyphed((cfg.title.isEmpty() ? QString() : cfg.title + "  ") + (scene.isEmpty() ? tr("Scene") : scene)), c.card);
    } else if (k == "voice_sigil") {
        const double level = src ? audioLevel(cfg.audioSource) : 0.6;
        const double r = qMin(w, h) * (0.33 + 0.06 * level);
        gothic::drawSigil(p, QPointF(w / 2.0, h / 2.0), r, pk.accent, pk.metal, level);
    } else if (k == "audio_visualizer") {
        const int bands = qBound(4, cfg.count, 64);
        QVector<float> spec = src ? audioSpectrum(cfg.audioSource, bands) : QVector<float>(bands, 0.f);
        if (!src) for (int i = 0; i < bands; ++i) spec[i] = float(0.35 + 0.3 * std::sin(i * 0.7) + 0.2 * std::sin(i * 1.9));
        const double gap = w / double(bands) * 0.25, bw = w / double(bands) - gap;
        for (int i = 0; i < bands; ++i) {
            const double bh = qMax(3.0, spec[i] * h * 0.9);
            const QRectF b(i * (bw + gap) + gap / 2, h - bh, bw, bh);
            QLinearGradient g(b.bottomLeft(), QPointF(b.left(), h * 0.1));
            g.setColorAt(0, pk.accent.darker(180)); g.setColorAt(0.7, pk.accent); g.setColorAt(1, pk.accent.lighter(160));
            p.setPen(Qt::NoPen); p.setBrush(g); p.drawRect(b);
            p.setBrush(pk.metal); p.drawPath(gothic::diamond(QPointF(b.center().x(), b.top() - bw * 0.4), qMin(bw * 0.5, 6.0)));
        }
    } else if (k == "social" || k == "sponsor") {
        c.panel();
        const QStringList items = cfg.socials.split(';', Qt::SkipEmptyParts);
        const double rowH = items.isEmpty() ? c.card.height() : (c.card.height() - 20) / items.size();
        if (items.isEmpty()) c.title(cfg.title.isEmpty() ? tr("Add handles in widget settings") : cfg.title, c.card);
        for (int i = 0; i < items.size(); ++i) {
            const QString plat = items[i].section(':', 0, 0).trimmed(), name = items[i].section(':', 1).trimmed();
            const QRectF row(c.card.left() + 20, c.card.top() + 10 + i * rowH, c.card.width() - 40, rowH);
            p.setPen(Qt::NoPen); p.setBrush(QColor(PlatformCatalog::get(plat).color.isEmpty() ? pk.accent.name() : PlatformCatalog::get(plat).color));
            p.drawPath(gothic::diamond(QPointF(row.left() + 8, row.center().y()), 7));
            c.fit((k == "sponsor" ? QString() : "@") + name + (plat.isEmpty() ? QString() : "  · " + plat), row.adjusted(24, 0, 0, 0), c.body(c.fs() * 0.7), pk.text, Qt::AlignLeft | Qt::AlignVCenter);
        }
    } else if (k == "schedule") {
        QList<QPair<QString, QString>> rows;
        const bool rules = cfg.title.compare(tr("Rules"), Qt::CaseInsensitive) == 0;
        for (const QString &l : cfg.text.split('\n', Qt::SkipEmptyParts)) {
            const int dash = l.indexOf(QRegularExpression("\\s[—-]\\s"));
            rows.append(dash > 0 ? qMakePair(l.left(dash).trimmed(), l.mid(dash + 3).trimmed()) : qMakePair(l.trimmed(), QString()));
        }
        c.list(cfg.title, rows, qMax<qsizetype>(1, rows.size()), tr("Add lines in widget settings"), rules);
    } else if (k == "rotator") {
        c.panel();
        const QStringList lines = cfg.text.split('\n', Qt::SkipEmptyParts);
        if (lines.isEmpty()) { c.title(tr("Add messages in widget settings"), c.card); p.end(); return img; }
        const qint64 period = qint64(qMax(1, cfg.interval)) * 1000;
        const qint64 el = now - widgetStartMs(src);
        const int idx = int((el / period) % lines.size());
        const double inPhase = (el % period) / 1000.0, outPhase = (period - el % period) / 1000.0;
        p.setOpacity(qMin(1.0, qMin(inPhase / 0.4, outPhase / 0.4)));
        c.title(c.glyphed(lines.at(idx)), c.card);
    } else if (k == "lower_third") {
        const double t = (now - widgetStartMs(src)) / 1000.0;
        const double in = 1 - std::pow(1 - qBound(0.0, t / 0.8, 1.0), 3);
        const QRectF plate(-w * (1 - in), h * 0.18, w * 0.92, h * 0.64);
        gothic::drawGothicCard(p, plate, pk.bg1, pk.bg2, pk.accent, pk.metal, false);
        p.setPen(Qt::NoPen); p.setBrush(pk.accent); p.drawRect(QRectF(plate.left() + 10, plate.top() + 10, 8, plate.height() - 20));
        c.fit(cfg.title, QRectF(plate.left() + 34, plate.top() + plate.height() * 0.08, plate.width() - 50, plate.height() * 0.52), c.display(c.fs() * 1.3), pk.text, Qt::AlignLeft | Qt::AlignVCenter);
        const double sub = qBound(0.0, (t - 0.5) / 0.6, 1.0);
        p.setOpacity(sub);
        c.fit(cfg.text.section('\n', 0, 0), QRectF(plate.left() + 36, plate.top() + plate.height() * 0.58, plate.width() - 50, plate.height() * 0.34), c.body(c.fs() * 0.7), pk.metal.lighter(130), Qt::AlignLeft | Qt::AlignVCenter);
        p.setOpacity(1);
        const double shine = std::fmod(t * 0.25, 1.4) - 0.2;   // a slow glint across the plate
        QLinearGradient gl(QPointF(plate.left() + plate.width() * (shine - 0.1), 0), QPointF(plate.left() + plate.width() * (shine + 0.1), 0));
        gl.setColorAt(0, QColor(255, 255, 255, 0)); gl.setColorAt(0.5, QColor(255, 255, 255, 26)); gl.setColorAt(1, QColor(255, 255, 255, 0));
        p.fillPath(gothic::chamfered(plate, 18), gl);
    } else if (k == "banner") {
        c.marquee(cfg.title.isEmpty() ? QStringList() : QStringList{cfg.title}, tr("Set the banner text in widget settings"));
    } else if (k == "file_text") {
        c.panel();
        const QString text = fileText(cfg.file);
        QString shown;
        if (cfg.file.isEmpty()) shown = src ? tr("Choose a text file in this widget's settings") : tr("Now playing: Song — Artist");
        else if (text.isEmpty()) shown = tr("Waiting for %1").arg(QFileInfo(cfg.file).fileName());
        else shown = cfg.title.isEmpty() ? text : cfg.title + ": " + text;
        c.title(c.glyphed(shown), c.card);
    } else if (k == "slideshow") {
        const QImage frame = cfg.file.isEmpty() ? QImage() : slideshowFrame(cfg, src, now, QSize(w, h));
        if (frame.isNull()) { c.panel(); c.title(tr("Choose a folder of images in this widget's settings"), c.card); }
        else {
            p.setClipPath(gothic::chamfered(QRectF(0, 0, w, h), 14));
            p.drawImage(0, 0, frame);
            p.setClipping(false);
            p.setPen(QPen(pk.metal, 3)); p.setBrush(Qt::NoBrush); p.drawPath(gothic::chamfered(QRectF(1.5, 1.5, w - 3, h - 3), 14));
        }
    } else if (k == "reminder") {
        const qint64 period = qint64(qMax(1, cfg.interval)) * 60000;
        const qint64 el = (now - widgetStartMs(src)) % period;
        const qint64 showFor = 15000;
        const qint64 sinceStart = now - widgetStartMs(src);
        if (src && (sinceStart < period || el > showFor)) { p.end(); return img; }   // pops up after each interval for 15 s
        const double op = src ? qMin(1.0, qMin(el / 500.0, (showFor - el) / 500.0)) : 1.0;
        p.setOpacity(op);
        c.panel();
        drawCandle(p, QPointF(c.card.left() + c.card.height() * 0.45, c.card.bottom() - c.card.height() * 0.12), c.card.height() * 0.42, c.card.height() * 0.14, 0.5 + 0.5 * std::sin(now / 110.0));
        c.title(cfg.title.isEmpty() ? tr("Reminder") : cfg.title, c.card.adjusted(c.card.height() * 0.8, 0, 0, 0));
    } else if (k == "ambient") {
        particles(p, cfg.metric.isEmpty() ? QStringLiteral("bats") : cfg.metric, pk.accent, w, h, now, qBound(4, cfg.count * 6, 160));
    } else if (k == "candles") {
        const int n = qBound(1, cfg.count, 24);
        for (int i = 0; i < n; ++i) {
            const double x = w * (i + 0.5) / n, hh = h * (0.35 + 0.25 * std::fmod(i * 0.618, 1.0));
            drawCandle(p, QPointF(x, h * 0.96), hh, qMin(w / double(n) * 0.38, h * 0.12), 0.5 + 0.5 * std::sin(now / (90.0 + i * 7) + i) * std::sin(now / 41.0 + i));
        }
    } else if (k == "webcam_frame") {
        const QRectF win(w * 0.09, h * 0.06, w * 0.82, h * 0.9);
        QPainterPath outside; outside.addRect(QRectF(0, 0, w, h));
        outside = outside.subtracted(gothic::lancet(win));
        QLinearGradient stone(0, 0, 0, h); stone.setColorAt(0, pk.bg1); stone.setColorAt(1, pk.bg2);
        p.fillPath(outside, stone);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(pk.metal, qMax(3.0, w * 0.008))); p.drawPath(gothic::lancet(win));
        p.setPen(QPen(pk.accent, qMax(1.5, w * 0.003))); p.drawPath(gothic::lancet(win.adjusted(-w * 0.015, -w * 0.015, w * 0.015, 0)));
        gothic::drawRose(p, QPointF(win.left() + w * 0.01, win.bottom() - h * 0.04), qMin(w, h) * 0.07, pk.accent, pk.accent.darker(250));
        gothic::drawRose(p, QPointF(win.right() - w * 0.01, win.bottom() - h * 0.04), qMin(w, h) * 0.07, pk.accent, pk.accent.darker(250));
    } else if (k == "divider") {
        const double y = h / 2.0;
        p.setPen(QPen(pk.metal, qMax(1.5, h * 0.03)));
        p.drawLine(QPointF(w * 0.08, y), QPointF(w * 0.42, y)); p.drawLine(QPointF(w * 0.58, y), QPointF(w * 0.92, y));
        gothic::drawFiligreeCorner(p, QPointF(w * 0.42, y), h * 0.45, 1, pk.metal, qMax(1.0, h * 0.02));
        gothic::drawFiligreeCorner(p, QPointF(w * 0.58, y), h * 0.45, 0, pk.metal, qMax(1.0, h * 0.02));
        gothic::drawSigil(p, QPointF(w / 2.0, y), h * 0.32, pk.accent, pk.metal, 0.3);
    } else {   // text_panel and anything unknown
        c.panel();
        c.title(cfg.title.isEmpty() ? tr("Welcome to the night") : cfg.title, c.card);
    }
    p.end();
    return img;
}

} // namespace bc
