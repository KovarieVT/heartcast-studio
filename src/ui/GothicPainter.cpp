#include "ui/GothicPainter.h"
#include "app/Theme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

namespace bc::gothic {

QPainterPath bat(const QPointF &c, double span, double flap) {
    const double s = span / 2.0;
    flap = qBound(-1.0, flap, 1.0);
    // Wing points for the left wing in units of s (y down). Points further from the
    // body move more when flapping, like a wing rotating at the shoulder.
    struct P { double x, y; };
    auto lift = [&](P p) { return QPointF(c.x() + p.x * s, c.y() + (p.y - flap * 0.55 * std::abs(p.x)) * s); };
    auto mirror = [](P p) { return P{-p.x, p.y}; };
    const P shoulder{-0.07, -0.10}, wrist{-0.42, -0.36}, tip{-1.0, -0.14}, f1{-0.74, 0.14}, f2{-0.47, 0.25}, hip{-0.07, 0.17};

    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (int side = 0; side < 2; ++side) {
        auto m = [&](P p) { return lift(side ? mirror(p) : p); };
        auto ctrl = [&](P a, P b) {   // scallops bow up into the wing
            P mid{(a.x + b.x) / 2, (a.y + b.y) / 2 - 0.13};
            return m(mid);
        };
        QPainterPath w;
        w.moveTo(m(shoulder));
        w.quadTo(m(P{-0.22, -0.34}), m(wrist));
        w.quadTo(m(P{-0.74, -0.38}), m(tip));
        w.quadTo(ctrl(tip, f1), m(f1));
        w.quadTo(ctrl(f1, f2), m(f2));
        w.quadTo(ctrl(f2, hip), m(hip));
        w.closeSubpath();
        path.addPath(w);
    }
    // body, head and ears
    path.addEllipse(QPointF(c.x(), c.y() + 0.04 * s), 0.10 * s, 0.22 * s);
    path.addEllipse(QPointF(c.x(), c.y() - 0.19 * s), 0.075 * s, 0.07 * s);
    QPainterPath ears;
    ears.moveTo(c.x() - 0.07 * s, c.y() - 0.21 * s); ears.lineTo(c.x() - 0.055 * s, c.y() - 0.34 * s); ears.lineTo(c.x() - 0.01 * s, c.y() - 0.24 * s);
    ears.closeSubpath();
    ears.moveTo(c.x() + 0.07 * s, c.y() - 0.21 * s); ears.lineTo(c.x() + 0.055 * s, c.y() - 0.34 * s); ears.lineTo(c.x() + 0.01 * s, c.y() - 0.24 * s);
    ears.closeSubpath();
    path.addPath(ears);
    return path.simplified();
}

QPainterPath crescent(const QPointF &c, double r) {
    QPainterPath outer; outer.addEllipse(c, r, r);
    QPainterPath bite; bite.addEllipse(c + QPointF(r * 0.42, -r * 0.20), r * 0.86, r * 0.86);
    return outer.subtracted(bite);
}

QPainterPath lancet(const QRectF &r) {
    const double w = r.width();
    const double natural = w * 0.866;                  // height of an equilateral arch
    const double h = std::max(r.height(), natural);    // build tall enough, squash later if needed
    const double spring = r.top() + natural;
    const double bottom = r.top() + h;
    QPainterPath path;
    path.moveTo(r.left(), bottom);
    path.lineTo(r.left(), spring);
    // arc centred on the right springing point, from 180° up to the apex at 120°
    path.arcTo(QRectF(r.right() - w, spring - w, 2 * w, 2 * w), 180, -60);
    // arc centred on the left springing point, from the apex (60°) down to 0°
    path.arcTo(QRectF(r.left() - w, spring - w, 2 * w, 2 * w), 60, -60);
    path.lineTo(r.right(), bottom);
    path.closeSubpath();
    if (h > r.height()) {   // short rect: squash vertically so the whole arch fits
        QTransform t;
        t.translate(0, r.top());
        t.scale(1, r.height() / h);
        t.translate(0, -r.top());
        path = t.map(path);
    }
    return path;
}

QPainterPath chamfered(const QRectF &r, double cut) {
    cut = std::min({cut, r.width() / 3, r.height() / 3});
    QPainterPath p;
    p.moveTo(r.left() + cut, r.top());
    p.lineTo(r.right() - cut, r.top());
    p.lineTo(r.right(), r.top() + cut);
    p.lineTo(r.right(), r.bottom() - cut);
    p.lineTo(r.right() - cut, r.bottom());
    p.lineTo(r.left() + cut, r.bottom());
    p.lineTo(r.left(), r.bottom() - cut);
    p.lineTo(r.left(), r.top() + cut);
    p.closeSubpath();
    return p;
}

QPainterPath petal(const QPointF &c, double size) {
    const double w = size * 0.8, h = size;
    QPainterPath p;
    p.moveTo(c.x(), c.y() + h / 2);
    p.cubicTo(c.x() + w * 0.62, c.y() + h * 0.18, c.x() + w * 0.55, c.y() - h * 0.52, c.x(), c.y() - h * 0.36);
    p.cubicTo(c.x() - w * 0.55, c.y() - h * 0.52, c.x() - w * 0.62, c.y() + h * 0.18, c.x(), c.y() + h / 2);
    return p;
}

QPainterPath feather(const QPointF &c, double len) {
    QPainterPath p;
    p.moveTo(c.x(), c.y() - len / 2);
    p.quadTo(c.x() + len * 0.24, c.y() - len * 0.05, c.x() + len * 0.02, c.y() + len / 2);
    p.quadTo(c.x() - len * 0.16, c.y(), c.x(), c.y() - len / 2);
    QPainterPath spine;
    spine.addRect(QRectF(c.x() - len * 0.008, c.y() - len * 0.35, len * 0.016, len * 0.85));
    return p.subtracted(spine);
}

QPainterPath diamond(const QPointF &c, double s) {
    QPainterPath p;
    p.moveTo(c.x(), c.y() - s); p.lineTo(c.x() + s * 0.62, c.y()); p.lineTo(c.x(), c.y() + s); p.lineTo(c.x() - s * 0.62, c.y());
    p.closeSubpath();
    return p;
}

void drawRose(QPainter &p, const QPointF &c, double r, const QColor &petalCol, const QColor &shade) {
    p.save();
    p.setPen(QPen(shade.darker(160), std::max(0.6, r * 0.025)));
    auto ring = [&](int n, double dist, double size, double rot, const QColor &col) {
        for (int i = 0; i < n; ++i) {
            const double a = rot + i * 360.0 / n;
            p.save();
            p.translate(c);
            p.rotate(a);
            QRadialGradient g(QPointF(0, -dist * 0.6), size);
            g.setColorAt(0, col.lighter(118)); g.setColorAt(0.7, col); g.setColorAt(1, shade);
            p.setBrush(g);
            p.drawPath(petal(QPointF(0, -dist), size).translated(0, 0));
            p.restore();
        }
    };
    ring(5, r * 0.55, r * 0.95, 0, petalCol.darker(115));
    ring(5, r * 0.32, r * 0.7, 36, petalCol);
    ring(3, r * 0.14, r * 0.45, 12, petalCol.lighter(110));
    // spiral heart of the bloom
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(shade.darker(180), std::max(0.8, r * 0.04), Qt::SolidLine, Qt::RoundCap));
    QPainterPath sp;
    for (int i = 0; i <= 40; ++i) {
        const double t = i / 40.0, a = t * 3.2 * M_PI, rr = r * 0.2 * (1 - t);
        const QPointF pt = c + QPointF(std::cos(a) * rr, std::sin(a) * rr);
        i ? sp.lineTo(pt) : sp.moveTo(pt);
    }
    p.drawPath(sp);
    p.restore();
}

void drawFiligreeCorner(QPainter &p, const QPointF &corner, double s, int quadrant, const QColor &col, double width) {
    p.save();
    p.translate(corner);
    p.rotate(90.0 * quadrant);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(col, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    auto arm = [&](bool flip) {
        auto P = [&](double x, double y) { return flip ? QPointF(y * s, x * s) : QPointF(x * s, y * s); };
        QPainterPath a;
        a.moveTo(P(0.20, 0.07)); a.lineTo(P(0.95, 0.07));                       // rule along the edge
        a.moveTo(P(0.18, 0.18));
        a.cubicTo(P(0.46, 0.13), P(0.58, 0.34), P(0.42, 0.40));                // scroll
        a.cubicTo(P(0.30, 0.44), P(0.27, 0.30), P(0.36, 0.29));
        a.moveTo(P(0.52, 0.10));
        a.quadTo(P(0.62, 0.20), P(0.74, 0.15));                                  // leaf stem
        p.drawPath(a);
        QPainterPath leaf;
        leaf.moveTo(P(0.74, 0.15)); leaf.quadTo(P(0.80, 0.08), P(0.86, 0.13)); leaf.quadTo(P(0.80, 0.19), P(0.74, 0.15));
        p.save(); p.setBrush(col); p.drawPath(leaf); p.restore();
    };
    arm(false);
    arm(true);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawPath(diamond(QPointF(s * 0.11, s * 0.11), s * 0.075));
    p.restore();
}

void drawGlow(QPainter &p, const QPointF &c, double r, const QColor &col, double strength) {
    QRadialGradient g(c, r);
    QColor a = col; a.setAlphaF(qBound(0.0, 0.55 * strength, 1.0));
    QColor b = col; b.setAlphaF(qBound(0.0, 0.18 * strength, 1.0));
    QColor z = col; z.setAlpha(0);
    g.setColorAt(0, a); g.setColorAt(0.35, b); g.setColorAt(1, z);
    p.save(); p.setPen(Qt::NoPen); p.setBrush(g); p.drawEllipse(c, r, r); p.restore();
}

void drawGothicCard(QPainter &p, const QRectF &r, const QColor &top, const QColor &bottom, const QColor &accent,
                    const QColor &ornament, bool corners) {
    p.save();
    const double cut = std::clamp(r.height() * 0.14, 6.0, 22.0);
    // drop shadow
    for (int i = 6; i > 0; --i) {
        p.setPen(Qt::NoPen); p.setBrush(QColor(0, 0, 0, 14 * i));
        p.drawPath(chamfered(r.adjusted(-i, -i + 3, i, i + 3), cut + i));
    }
    const QPainterPath body = chamfered(r, cut);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, top); g.setColorAt(1, bottom);
    p.fillPath(body, g);
    // candle-light glow from the top edge
    p.setClipPath(body);
    QRadialGradient glow(QPointF(r.center().x(), r.top()), r.width() * 0.6);
    QColor ga = accent; ga.setAlpha(46); QColor gz = accent; gz.setAlpha(0);
    glow.setColorAt(0, ga); glow.setColorAt(1, gz);
    p.fillRect(r, glow);
    p.setClipping(false);
    // borders
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ornament, 1.5));
    p.drawPath(body);
    QColor inner = accent; inner.setAlpha(170);
    p.setPen(QPen(inner, 1.0));
    p.drawPath(chamfered(r.adjusted(4, 4, -4, -4), cut - 2));
    if (corners && r.height() > 40 && r.width() > 80) {
        const double s = std::clamp(r.height() * 0.32, 14.0, 46.0);
        QColor oc = ornament; oc.setAlpha(220);
        drawFiligreeCorner(p, r.topLeft() + QPointF(7, 7), s, 0, oc, 1.2);
        drawFiligreeCorner(p, r.topRight() + QPointF(-7, 7), s, 1, oc, 1.2);
        drawFiligreeCorner(p, r.bottomRight() + QPointF(-7, -7), s, 2, oc, 1.2);
        drawFiligreeCorner(p, r.bottomLeft() + QPointF(7, -7), s, 3, oc, 1.2);
    }
    p.restore();
}

void drawEmblem(QPainter &p, const QPointF &c, double size, const QColor &accent, double glow) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QColor gold = theme::OldGold, bone = theme::Parchment;
    // wings behind the window
    const QPainterPath wings = bat(c + QPointF(0, -size * 0.06), size * 1.0, 0.18);
    QLinearGradient wg(c + QPointF(0, -size * 0.4), c + QPointF(0, size * 0.2));
    wg.setColorAt(0, QColor(0x2A, 0x22, 0x30)); wg.setColorAt(1, QColor(0x0E, 0x0B, 0x11));
    p.setPen(QPen(gold.darker(115), std::max(1.0, size * 0.012)));
    p.setBrush(wg);
    p.drawPath(wings);

    // lancet window of stained glass
    const QRectF win(c.x() - size * 0.21, c.y() - size * 0.38, size * 0.42, size * 0.70);
    const QPainterPath arch = lancet(win);
    drawGlow(p, c + QPointF(0, size * 0.02), size * 0.62, accent, 0.9 * glow);
    QRadialGradient glass(c + QPointF(0, size * 0.04), size * 0.42);
    glass.setColorAt(0, accent.lighter(170));
    glass.setColorAt(0.35, accent.lighter(115));
    glass.setColorAt(0.75, accent.darker(170));
    glass.setColorAt(1, QColor(0x14, 0x08, 0x0E));
    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawPath(arch);
    // tracery
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(gold, std::max(1.2, size * 0.022), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(arch);
    QColor thin = gold; thin.setAlpha(150);
    p.setPen(QPen(thin, std::max(0.8, size * 0.008)));
    p.drawPath(lancet(win.adjusted(size * 0.035, size * 0.035, -size * 0.035, 0)));
    // crescent moon in the arch
    p.setPen(Qt::NoPen);
    p.setBrush(bone);
    p.drawPath(crescent(QPointF(c.x() - size * 0.025, c.y() - size * 0.14), size * 0.085));
    // play mark
    QPainterPath play;
    const QPointF pc = c + QPointF(size * 0.012, size * 0.13);
    const double t = size * 0.085;
    play.moveTo(pc + QPointF(-t * 0.75, -t)); play.lineTo(pc + QPointF(t, 0)); play.lineTo(pc + QPointF(-t * 0.75, t)); play.closeSubpath();
    p.setBrush(bone);
    p.drawPath(play);
    // sill
    p.setPen(QPen(gold, std::max(1.2, size * 0.022), Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(win.left() - size * 0.04, win.bottom()), QPointF(win.right() + size * 0.04, win.bottom()));
    p.restore();
}

void drawSigil(QPainter &p, const QPointF &c, double r, const QColor &accent, const QColor &metal, double pulse) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    drawGlow(p, c, r * 1.6, accent, 0.5 + 0.6 * pulse);
    // metal ring
    QLinearGradient ring(c + QPointF(-r, -r), c + QPointF(r, r));
    ring.setColorAt(0, metal.lighter(150)); ring.setColorAt(0.5, metal); ring.setColorAt(1, metal.darker(180));
    p.setPen(Qt::NoPen);
    p.setBrush(ring);
    p.drawEllipse(c, r, r);
    // glass
    QRadialGradient glass(c, r * 0.86);
    glass.setColorAt(0, accent.lighter(int(150 + 40 * pulse)));
    glass.setColorAt(0.55, accent);
    glass.setColorAt(1, accent.darker(260));
    p.setBrush(glass);
    p.drawEllipse(c, r * 0.86, r * 0.86);
    // rose-window tracery
    QColor lead = metal.darker(120);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(lead, std::max(1.0, r * 0.045)));
    for (int i = 0; i < 8; ++i) {
        const double a = i * M_PI / 4;
        p.drawLine(c + QPointF(std::cos(a), std::sin(a)) * r * 0.30, c + QPointF(std::cos(a), std::sin(a)) * r * 0.86);
        p.drawEllipse(c + QPointF(std::cos(a + M_PI / 8), std::sin(a + M_PI / 8)) * r * 0.62, r * 0.13, r * 0.13);
    }
    p.drawEllipse(c, r * 0.30, r * 0.30);
    // crescent heart
    p.setPen(Qt::NoPen);
    p.setBrush(theme::Parchment);
    p.drawPath(crescent(c, r * 0.20));
    p.restore();
}

} // namespace bc::gothic
