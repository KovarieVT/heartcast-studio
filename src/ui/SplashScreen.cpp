#include "ui/SplashScreen.h"
#include "ui/KawaiiPainter.h"
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
    QT_TRANSLATE_NOOP("Splash", "Preparing your studio…"),
    QT_TRANSLATE_NOOP("Splash", "Polishing your stream…"),
    QT_TRANSLATE_NOOP("Splash", "Sprinkling some magic…"),
    QT_TRANSLATE_NOOP("Splash", "Tying the ribbons…"),
    QT_TRANSLATE_NOOP("Splash", "Getting everything ready…"),
};

SplashScreen::SplashScreen(bool animated, QWidget *parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_animated(animated), m_logo(QStringLiteral(":/icons/logo.svg")) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(680, 440);
    if (auto *scr = QGuiApplication::primaryScreen())
        move(scr->availableGeometry().center() - rect().center());
    m_message = tr(kQuips[0]);
    m_clock.start();

    for (int i = 0; i < 46; ++i) { Particle p; spawn(p, true); m_particles << p; }

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

void SplashScreen::spawn(Particle &p, bool anywhere) {
    auto *rng = QRandomGenerator::global();
    p.kind = rng->bounded(5);   // 0 heart 1 star 2 sparkle 3 bow 4 bubble
    const double w = width(), h = height();
    p.pos = QPointF(rng->bounded(w), anywhere ? rng->bounded(h) : h + 20);
    p.vel = QPointF((rng->generateDouble() - 0.5) * 0.25, -(0.15 + rng->generateDouble() * 0.45));
    p.size = 6 + rng->generateDouble() * 14;
    p.phase = rng->generateDouble() * 6.28;
    p.spin = (rng->generateDouble() - 0.5) * 0.6;
    static const QColor cols[] = {theme::CottonCandy, theme::PastelMint, theme::Lavender, theme::BlushPink,
                                  theme::MintAccent, QColor(255, 255, 255)};
    p.color = cols[rng->bounded(6)];
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
    const double dt = 1.0;
    m_shownPct += (m_targetPct - m_shownPct) * 0.12;
    if (m_animated) {
        for (auto &p : m_particles) {
            p.pos += p.vel * dt * 1.6;
            p.pos.rx() += std::sin(m_clock.elapsed() / 700.0 + p.phase) * 0.25;
            if (p.pos.y() < -30) spawn(p, false);
        }
    }
    // Rotate friendly quips every ~1.8 s when the engine isn't reporting a specific step.
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
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect().adjusted(10, 10, -10, -10);

    // pearlescent card
    QPainterPath clip; clip.addRoundedRect(r, 32, 32);
    for (int i = 8; i > 0; --i) {
        p.setPen(Qt::NoPen); p.setBrush(QColor(201, 111, 157, 5 * i));
        p.drawRoundedRect(r.adjusted(-i, -i + 3, i, i + 3), 32 + i, 32 + i);
    }
    const double t = m_clock.elapsed() / 1000.0;
    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0, theme::PearlWhite);
    bg.setColorAt(0.45 + 0.08 * std::sin(t * 0.8), theme::BabyPink);
    bg.setColorAt(0.8, theme::SoftLilac);
    bg.setColorAt(1, theme::SoftMint);
    p.fillPath(clip, bg);
    // pearl sheen sweeping across
    QLinearGradient sheen(QPointF(r.left() + std::fmod(t * 220, r.width() * 2) - r.width() * 0.5, 0),
                          QPointF(r.left() + std::fmod(t * 220, r.width() * 2) - r.width() * 0.2, r.height()));
    sheen.setColorAt(0, QColor(255, 255, 255, 0));
    sheen.setColorAt(0.5, QColor(255, 255, 255, m_animated ? 70 : 0));
    sheen.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillPath(clip, sheen);
    p.setClipPath(clip);

    // floating decorations
    for (const auto &pt : m_particles) {
        const double tw = 0.55 + 0.45 * std::sin(t * 3 + pt.phase);   // twinkle
        QColor c = pt.color; c.setAlphaF(0.35 + 0.5 * tw);
        p.save();
        p.translate(pt.pos);
        p.rotate(std::sin(t + pt.phase) * 15 + pt.spin * t * 40);
        p.setPen(Qt::NoPen); p.setBrush(c);
        switch (pt.kind) {
        case 0: p.drawPath(kawaii::heart({0, 0}, pt.size)); break;
        case 1: p.drawPath(kawaii::star({0, 0}, pt.size * 0.6, pt.size * 0.28)); break;
        case 2: p.drawPath(kawaii::sparkle({0, 0}, pt.size * 0.6 * (0.6 + 0.4 * tw))); break;
        case 3: p.setPen(QPen(c.darker(110), 1.2)); p.drawPath(kawaii::bow({0, 0}, pt.size * 0.7)); break;
        default: p.setBrush(QColor(255, 255, 255, int(60 * tw))); p.setPen(QPen(QColor(255, 255, 255, 140), 1));
                 p.drawEllipse(QPointF(0, 0), pt.size * 0.4, pt.size * 0.4);
        }
        p.restore();
    }

    // glossy hearts with ribbons bobbing in the lower corners
    const double bobL = m_animated ? std::sin(t * 2.0) * 6 : 0, bobR = m_animated ? std::sin(t * 2.0 + 1.3) * 6 : 0;
    kawaii::drawGlossyHeart(p, QPointF(r.left() + 78, r.bottom() - 62 + bobL), 74, theme::PastelRose, theme::CottonCandy, theme::DeepRose);
    kawaii::drawGlossyHeart(p, QPointF(r.right() - 78, r.bottom() - 58 + bobR), 62, theme::SoftMint, theme::PastelMint, theme::MintAccent);
    p.setPen(QPen(theme::MintAccent.darker(120), 1.6)); p.setBrush(theme::PastelMint);
    p.drawPath(kawaii::bow(QPointF(r.left() + 78, r.bottom() - 92 + bobL), 26));
    p.setPen(QPen(theme::DeepRose, 1.6)); p.setBrush(theme::PastelRose);
    p.drawPath(kawaii::bow(QPointF(r.right() - 78, r.bottom() - 84 + bobR), 22));

    // logo with gentle bob + glow
    const double bob = m_animated ? std::sin(t * 1.6) * 5 : 0;
    const QRectF logoR(width() / 2.0 - 78, 62 + bob, 156, 156);
    QRadialGradient glow(logoR.center(), 120);
    glow.setColorAt(0, QColor(255, 255, 255, 170)); glow.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(glow); p.setPen(Qt::NoPen); p.drawEllipse(logoR.center(), 120, 120);
    m_logo.render(&p, logoR);

    // title
    QFont f("Nunito"); f.setPointSize(24); f.setWeight(QFont::ExtraBold);
    p.setFont(f); p.setPen(theme::DeepRose);
    p.drawText(QRectF(r.left(), 232 + bob * 0.3, r.width(), 40), Qt::AlignCenter, QStringLiteral(BC_APP_NAME));
    QFont sub("Nunito"); sub.setPointSize(10);
    p.setFont(sub); p.setPen(theme::Charcoal);
    p.drawText(QRectF(r.left(), 270, r.width(), 22), Qt::AlignCenter, m_message.isEmpty() ? tr(kQuips[m_quipIdx]) : m_message);

    // progress bar
    const QRectF bar(width() / 2.0 - 170, 304, 340, 14);
    p.setPen(Qt::NoPen); p.setBrush(QColor(237, 228, 255, 230));
    p.drawRoundedRect(bar, 7, 7);
    QRectF fill = bar; fill.setWidth(qMax(14.0, bar.width() * m_shownPct / 100.0));
    QLinearGradient pg(bar.topLeft(), bar.topRight());
    pg.setColorAt(0, theme::PastelMint); pg.setColorAt(0.5, theme::CottonCandy); pg.setColorAt(1, theme::Lavender);
    p.setBrush(pg); p.drawRoundedRect(fill, 7, 7);
    p.setBrush(QColor(255, 255, 255, 110));
    p.drawRoundedRect(QRectF(fill.left() + 3, fill.top() + 2, fill.width() - 6, 4), 2, 2);
    // little heart riding the progress bar
    p.setBrush(theme::BlushPink);
    p.drawPath(kawaii::heart(QPointF(fill.right() - 2, bar.center().y() - 1 - (m_animated ? std::abs(std::sin(t * 6)) * 3 : 0)), 16));

    p.setFont(sub); p.setPen(QColor(0x8C, 0x7F, 0x8B));
    p.drawText(QRectF(r.left(), 324, r.width(), 20), Qt::AlignCenter, QStringLiteral("%1%  ·  v%2").arg(int(m_shownPct + 0.5)).arg(BC_APP_VERSION));
}

} // namespace bc
