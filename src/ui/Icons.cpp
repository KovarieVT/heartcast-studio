#include "ui/Icons.h"
#include "app/Theme.h"
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace bc::icons {

static void paintIcon(QPainter &p, const QString &n, const QColor &c) {
    // 24×24 design grid
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto fill = [&](const QPainterPath &path) { p.save(); p.setPen(Qt::NoPen); p.setBrush(c); p.drawPath(path); p.restore(); };

    if (n == "add") { p.drawLine(QPointF(12, 5), QPointF(12, 19)); p.drawLine(QPointF(5, 12), QPointF(19, 12)); }
    else if (n == "remove") { p.drawLine(QPointF(5, 12), QPointF(19, 12)); }
    else if (n == "duplicate") { p.drawRoundedRect(QRectF(8, 8, 11, 11), 1.5, 1.5); p.drawPolyline(QPolygonF({QPointF(5, 15), QPointF(5, 5), QPointF(15, 5)})); }
    else if (n == "up" || n == "down") {
        QPainterPath t;
        if (n == "up") { t.moveTo(12, 6); t.lineTo(19, 16); t.lineTo(5, 16); }
        else { t.moveTo(12, 18); t.lineTo(19, 8); t.lineTo(5, 8); }
        t.closeSubpath();
        fill(t);
    } else if (n == "gear") {
        QPainterPath g;
        const int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i) {
            const double a = M_PI * i / teeth;
            const double r = (i % 2) ? 7.0 : 9.5;
            const double a0 = a - M_PI / (teeth * 2) * 0.9, a1 = a + M_PI / (teeth * 2) * 0.9;
            const QPointF p0(12 + r * std::cos(a0), 12 + r * std::sin(a0)), p1(12 + r * std::cos(a1), 12 + r * std::sin(a1));
            if (i == 0) g.moveTo(p0); else g.lineTo(p0);
            g.lineTo(p1);
        }
        g.closeSubpath();
        QPainterPath hole; hole.addEllipse(QPointF(12, 12), 3.2, 3.2);
        fill(g.subtracted(hole));
    } else if (n == "filters") {   // four-point star with a small companion
        auto star = [&](QPointF c0, double r) {
            QPainterPath s;
            s.moveTo(c0.x(), c0.y() - r);
            s.quadTo(c0, QPointF(c0.x() + r, c0.y()));
            s.quadTo(c0, QPointF(c0.x(), c0.y() + r));
            s.quadTo(c0, QPointF(c0.x() - r, c0.y()));
            s.quadTo(c0, QPointF(c0.x(), c0.y() - r));
            fill(s);
        };
        star(QPointF(10, 13), 8); star(QPointF(18.5, 6), 3.6);
    } else if (n == "speaker" || n == "mute") {
        QPainterPath s;
        s.moveTo(4, 9); s.lineTo(8, 9); s.lineTo(13, 4.5); s.lineTo(13, 19.5); s.lineTo(8, 15); s.lineTo(4, 15); s.closeSubpath();
        fill(s);
        if (n == "speaker") { p.drawArc(QRectF(11, 8, 7, 8), -55 * 16, 110 * 16); p.drawArc(QRectF(11, 5, 10, 14), -55 * 16, 110 * 16); }
        else { p.drawLine(QPointF(16, 9), QPointF(21, 15)); p.drawLine(QPointF(21, 9), QPointF(16, 15)); }
    } else if (n == "headphones") {
        p.drawArc(QRectF(4, 4, 16, 16), 0, 180 * 16);
        p.drawLine(QPointF(4, 12), QPointF(4, 16)); p.drawLine(QPointF(20, 12), QPointF(20, 16));
        QPainterPath l; l.addRoundedRect(QRectF(3, 13, 5, 7), 1.5, 1.5); fill(l);
        QPainterPath r; r.addRoundedRect(QRectF(16, 13, 5, 7), 1.5, 1.5); fill(r);
    } else if (n == "eye" || n == "eye-off") {
        QPainterPath e;
        e.moveTo(2.5, 12); e.quadTo(12, 3, 21.5, 12); e.quadTo(12, 21, 2.5, 12);
        p.drawPath(e);
        QPainterPath pupil; pupil.addEllipse(QPointF(12, 12), 3, 3); fill(pupil);
        if (n == "eye-off") { p.drawLine(QPointF(4, 20), QPointF(20, 4)); }
    } else if (n == "lock" || n == "unlock") {
        QPainterPath b; b.addRoundedRect(QRectF(5, 11, 14, 10), 2, 2); fill(b);
        if (n == "lock") p.drawArc(QRectF(8, 4, 8, 11), 0, 180 * 16), p.drawLine(QPointF(8, 9.5), QPointF(8, 11)), p.drawLine(QPointF(16, 9.5), QPointF(16, 11));
        else p.drawArc(QRectF(12, 3, 8, 11), 0, 180 * 16), p.drawLine(QPointF(20, 8.5), QPointF(20, 10));
    } else if (n == "pause") {
        QPainterPath a; a.addRoundedRect(QRectF(6, 5, 4, 14), 1, 1); a.addRoundedRect(QRectF(14, 5, 4, 14), 1, 1); fill(a);
    } else if (n == "play") {
        QPainterPath t; t.moveTo(7, 5); t.lineTo(19, 12); t.lineTo(7, 19); t.closeSubpath(); fill(t);
    } else if (n == "save") {
        p.drawLine(QPointF(12, 4), QPointF(12, 15));
        p.drawPolyline(QPolygonF({QPointF(7, 10.5), QPointF(12, 15.5), QPointF(17, 10.5)}));
        p.drawLine(QPointF(5, 19.5), QPointF(19, 19.5));
    } else if (n == "more") {
        for (double x : {6.0, 12.0, 18.0}) { QPainterPath d; d.addEllipse(QPointF(x, 12), 1.8, 1.8); fill(d); }
    }
}

static QPixmap render(const QString &name, const QColor &c, int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.scale(px / 24.0, px / 24.0);
    paintIcon(p, name, c);
    return pm;
}

QIcon get(const QString &name) {
    static QHash<QString, QIcon> cache;
    if (auto it = cache.constFind(name); it != cache.constEnd()) return *it;
    QIcon icon;
    for (int px : {16, 20, 24, 32, 48}) {
        icon.addPixmap(render(name, theme::Bone, px), QIcon::Normal, QIcon::Off);
        icon.addPixmap(render(name, theme::Blood, px), QIcon::Normal, QIcon::On);
        icon.addPixmap(render(name, theme::Smoke, px), QIcon::Disabled, QIcon::Off);
        icon.addPixmap(render(name, theme::Smoke, px), QIcon::Disabled, QIcon::On);
    }
    cache.insert(name, icon);
    return icon;
}

QIcon flat(const QString &name, const QColor &color) {
    static QHash<QString, QIcon> cache;
    const QString key = name + color.name(QColor::HexArgb);
    if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;
    QIcon icon;
    for (int px : {16, 20, 24, 32}) {
        const QPixmap pm = render(name, color, px);
        for (QIcon::Mode m : {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled})
            for (QIcon::State st : {QIcon::Off, QIcon::On}) icon.addPixmap(pm, m, st);
    }
    cache.insert(key, icon);
    return icon;
}

} // namespace bc::icons
