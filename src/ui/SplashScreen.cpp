#include "ui/SplashScreen.h"
#include "ui/GothicPainter.h"
#include "app/Theme.h"
#include "BuildInfo.h"

#include <QApplication>
#include <QPainter>
#include <QRandomGenerator>
#include <QScreen>
#include <QtMath>
#include <iterator>

namespace bc {

static const char *kQuips[] = {
    QT_TRANSLATE_NOOP("Splash", "Lighting the candles…"),
    QT_TRANSLATE_NOOP("Splash", "Opening the crypt…"),
    QT_TRANSLATE_NOOP("Splash", "Gathering the shadows…"),
    QT_TRANSLATE_NOOP("Splash", "Tuning the pipe organ…"),
    QT_TRANSLATE_NOOP("Splash", "Preparing your stage…"),
};

SplashScreen::SplashScreen(bool animated, QWidget *parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), m_animated(animated) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(680, 440);
    if (auto *scr = QGuiApplication::primaryScreen())
        move(scr->availableGeometry().center() - rect().center());
    m_message = tr(kQuips[0]);
    m_clock.start();

    for (int i = 0; i < 9; ++i) { Bat b; spawnBat(b, true); m_bats << b; }
    for (int i = 0; i < 34; ++i) { Ember e; spawnEmber(e, true); m_embers << e; }

    connect(&m_timer, &QTimer::timeout, this, &SplashScreen::tick);
    m_timer.start(animated ? 16 : 100);

    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        m_message = tr("Loading is taking longer than expected…");
        update();
        Q_EMIT stalled();
    });
    m_watchdog.start(kStallMs);
}

void SplashScreen::spawnBat(Bat &b, bool anywhere) {
    auto *rng = QRandomGenerator::global();
    const double w = width(), h = height();
    const bool leftToRight = rng->bounded(2) == 0;
    b.span = 16 + rng->generateDouble() * 26;
    b.pos = QPointF(anywhere ? rng->bounded(w) : (leftToRight ? -40.0 : w + 40), 40 + rng->generateDouble() * h * 0.45);
    b.speed = 0.6 + rng->generateDouble() * 0.9;
    b.vel = QPointF(leftToRight ? b.speed : -b.speed, (rng->generateDouble() - 0.5) * 0.25);
    b.phase = rng->generateDouble() * 6.28;
}

void SplashScreen::spawnEmber(Ember &e, bool anywhere) {
    auto *rng = QRandomGenerator::global();
    const double w = width(), h = height();
    e.pos = QPointF(rng->bounded(w), anywhere ? rng->bounded(h) : h + 10);
    e.vel = QPointF((rng->generateDouble() - 0.5) * 0.2, -(0.15 + rng->generateDouble() * 0.35));
    e.size = 1.0 + rng->generateDouble() * 2.2;
    e.phase = rng->generateDouble() * 6.28;
}

void SplashScreen::setProgress(int pct, const QString &message) {
    m_targetPct = qBound(0, pct, 100);
    if (!message.isEmpty()) m_message = message;
    m_watchdog.start(kStallMs);   // progress = not stalled
    update();
    // Keep animating while the (main-thread) engine loads modules.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
}

void SplashScreen::finish(QWidget *mainWindow) {
    m_next = mainWindow;
    m_targetPct = 100;
    m_finishing = true;
    m_watchdog.stop();
    if (!m_animated) { if (m_next) m_next->show(); close(); }
}

void SplashScreen::tick() {
    m_shownPct += (m_targetPct - m_shownPct) * 0.12;
    if (m_animated) {
        for (auto &b : m_bats) {
            b.pos += b.vel * 1.6;
            b.pos.ry() += std::sin(m_clock.elapsed() / 400.0 + b.phase) * 0.35;
            if (b.pos.x() < -60 || b.pos.x() > width() + 60) spawnBat(b, false);
        }
        for (auto &e : m_embers) {
            e.pos += e.vel * 1.6;
            e.pos.rx() += std::sin(m_clock.elapsed() / 600.0 + e.phase) * 0.2;
            if (e.pos.y() < -10) spawnEmber(e, false);
        }
    }
    // Rotate quips every ~1.8 s when the engine isn't reporting a specific step.
    const qint64 now = m_clock.elapsed();
    if (now - m_lastQuipMs > 1800 && m_targetPct < 100 && m_message.isEmpty()) {
        m_quipIdx = (m_quipIdx + 1) % int(std::size(kQuips));
        m_message = tr(kQuips[m_quipIdx]);
        m_lastQuipMs = now;
    }
    if (m_finishing && m_shownPct > 99) {
        m_opacity -= m_animated ? 0.06 : 1.0;
        setWindowOpacity(qMax(0.0, m_opacity));
        if (m_opacity <= 0) {
            m_timer.stop();
            if (m_next) { m_next->show(); m_next->raise(); m_next->activateWindow(); }
            close();
            return;
        }
    }
    update();
}

void SplashScreen::paintEvent(QPaintEvent *) {
    using namespace theme;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect().adjusted(10, 10, -10, -10);
    const double t = m_clock.elapsed() / 1000.0;

    // card
    const QPainterPath card = gothic::chamfered(r, 26);
    for (int i = 8; i > 0; --i) {
        p.setPen(Qt::NoPen); p.setBrush(QColor(0, 0, 0, 16 * i));
        p.drawPath(gothic::chamfered(r.adjusted(-i, -i + 3, i, i + 3), 26 + i));
    }
    QLinearGradient bg(r.topLeft(), r.bottomLeft());
    bg.setColorAt(0, QColor(0x0E, 0x0B, 0x14));
    bg.setColorAt(0.65, Void);
    bg.setColorAt(1, QColor(0x22, 0x08, 0x12));
    p.fillPath(card, bg);
    p.setClipPath(card);

    // moon and drifting fog
    const QPointF moon(r.right() - 110, r.top() + 92);
    gothic::drawGlow(p, moon, 150, Silver, 0.55);
    p.setPen(Qt::NoPen);
    QRadialGradient mg(moon + QPointF(-10, -10), 52);
    mg.setColorAt(0, Parchment); mg.setColorAt(1, Silver.darker(115));
    p.setBrush(mg);
    p.drawEllipse(moon, 44, 44);
    for (int i = 0; i < 4; ++i) {
        const double x = std::fmod(t * (14 + i * 6) + i * 210, r.width() + 400) - 200;
        QRadialGradient fog(QPointF(r.left() + x, r.bottom() - 40 - i * 26), 220);
        fog.setColorAt(0, QColor(0x5A, 0x4A, 0x66, m_animated ? 46 : 30));
        fog.setColorAt(1, QColor(0x5A, 0x4A, 0x66, 0));
        p.setBrush(fog);
        p.drawEllipse(QPointF(r.left() + x, r.bottom() - 40 - i * 26), 260, 90);
    }

    // embers rising from below
    for (const auto &e : m_embers) {
        const double fl = 0.5 + 0.5 * std::sin(t * 5 + e.phase);
        QColor c = Blood; c.setAlphaF(0.25 + 0.6 * fl * (e.pos.y() / height()));
        p.setBrush(c);
        p.drawEllipse(e.pos, e.size, e.size);
    }

    // bats crossing the night
    for (const auto &b : m_bats) {
        const double flap = std::sin(t * (9 + b.speed * 4) + b.phase);
        QColor c(0x06, 0x05, 0x08, 235);
        p.setBrush(c);
        p.save();
        p.translate(b.pos);
        if (b.vel.x() < 0) p.scale(-1, 1);
        p.drawPath(gothic::bat(QPointF(0, 0), b.span, flap));
        p.restore();
    }

    // emblem
    const double bob = m_animated ? std::sin(t * 1.4) * 3 : 0;
    const double flicker = m_animated ? 0.85 + 0.15 * std::sin(t * 7.3) * std::sin(t * 3.1) : 1.0;
    gothic::drawEmblem(p, QPointF(width() / 2.0, 140 + bob), 200, Crimson, flicker);

    // wordmark
    QFont f(decorativeFamily()); f.setPointSize(30); f.setWeight(QFont::Bold);
    p.setFont(f);
    const QRectF title(r.left(), 238, r.width(), 50);
    p.setPen(QColor(0xC8, 0x28, 0x3F, 90));
    p.drawText(title.translated(0, 2), Qt::AlignCenter, QStringLiteral(BC_APP_NAME));
    p.setPen(Parchment);
    p.drawText(title, Qt::AlignCenter, QStringLiteral(BC_APP_NAME));
    QFont sub(bodyFamily()); sub.setPointSize(10);
    p.setFont(sub); p.setPen(Ash);
    p.drawText(QRectF(r.left(), 288, r.width(), 22), Qt::AlignCenter, m_message.isEmpty() ? tr(kQuips[m_quipIdx]) : m_message);

    // progress: a thin blade with a glowing ember at its tip
    const QRectF bar(width() / 2.0 - 170, 322, 340, 6);
    p.setPen(QPen(Shadow, 1)); p.setBrush(QColor(0x0E, 0x0B, 0x11));
    p.drawRect(bar);
    QRectF fill = bar; fill.setWidth(qMax(2.0, bar.width() * m_shownPct / 100.0));
    QLinearGradient pg(bar.topLeft(), bar.topRight());
    pg.setColorAt(0, Garnet); pg.setColorAt(1, Blood);
    p.setPen(Qt::NoPen); p.setBrush(pg);
    p.drawRect(fill);
    gothic::drawGlow(p, QPointF(fill.right(), bar.center().y()), 16, Blood, 1.0);
    p.setPen(QPen(OldGold, 1));
    p.drawLine(QPointF(bar.left() - 14, bar.center().y()), QPointF(bar.left() - 4, bar.center().y()));
    p.drawLine(QPointF(bar.right() + 4, bar.center().y()), QPointF(bar.right() + 14, bar.center().y()));
    p.setPen(Qt::NoPen); p.setBrush(OldGold);
    p.drawPath(gothic::diamond(QPointF(bar.left() - 18, bar.center().y()), 4));
    p.drawPath(gothic::diamond(QPointF(bar.right() + 18, bar.center().y()), 4));

    QFont foot(displayFamily()); foot.setPointSize(8); foot.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    p.setFont(foot); p.setPen(Smoke.lighter(150));
    p.drawText(QRectF(r.left(), 338, r.width(), 20), Qt::AlignCenter, QStringLiteral("%1%  ·  v%2").arg(int(m_shownPct + 0.5)).arg(BC_APP_VERSION));

    // frame
    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(OldGold.darker(140), 1.5));
    p.drawPath(card);
    QColor inner = Crimson; inner.setAlpha(150);
    p.setPen(QPen(inner, 1));
    p.drawPath(gothic::chamfered(r.adjusted(6, 6, -6, -6), 22));
    QColor orn = OldGold; orn.setAlpha(200);
    gothic::drawFiligreeCorner(p, r.topLeft() + QPointF(12, 12), 60, 0, orn);
    gothic::drawFiligreeCorner(p, r.topRight() + QPointF(-12, 12), 60, 1, orn);
    gothic::drawFiligreeCorner(p, r.bottomRight() + QPointF(-12, -12), 60, 2, orn);
    gothic::drawFiligreeCorner(p, r.bottomLeft() + QPointF(12, -12), 60, 3, orn);
}

} // namespace bc
