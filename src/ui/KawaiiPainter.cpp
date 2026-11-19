#include "ui/KawaiiPainter.h"
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>

namespace bc::kawaii {

QPainterPath heart(const QPointF &c, double s) {
    QPainterPath p;
    const double x = c.x(), y = c.y();
    p.moveTo(x, y + s * 0.45);
    p.cubicTo(x - s * 0.95, y - s * 0.15, x - s * 0.5, y - s * 0.75, x, y - s * 0.3);
    p.cubicTo(x + s * 0.5, y - s * 0.75, x + s * 0.95, y - s * 0.15, x, y + s * 0.45);
    p.closeSubpath();
    return p;
}

QPainterPath star(const QPointF &c, double outer, double inner, int n) {
    QPainterPath p;
    for (int i = 0; i < n * 2; ++i) {
        const double r = (i % 2) ? inner : outer;
        const double a = -M_PI / 2 + i * M_PI / n;
        const QPointF pt(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
        i == 0 ? p.moveTo(pt) : p.lineTo(pt);
    }
    p.closeSubpath();
    return p;
}

QPainterPath sparkle(const QPointF &c, double s) {
    QPainterPath p;
    const double k = s * 0.18;
    p.moveTo(c.x(), c.y() - s);
    p.quadTo(c.x() + k, c.y() - k, c.x() + s, c.y());
    p.quadTo(c.x() + k, c.y() + k, c.x(), c.y() + s);
    p.quadTo(c.x() - k, c.y() + k, c.x() - s, c.y());
    p.quadTo(c.x() - k, c.y() - k, c.x(), c.y() - s);
    return p;
}

QPainterPath bow(const QPointF &c, double s) {
    QPainterPath p;
    const double x = c.x(), y = c.y();
    p.moveTo(x, y);
    p.cubicTo(x - s * 0.9, y - s * 0.75, x - s * 1.05, y + s * 0.55, x, y);
    p.moveTo(x, y);
    p.cubicTo(x + s * 0.9, y - s * 0.75, x + s * 1.05, y + s * 0.55, x, y);
    p.addEllipse(c, s * 0.16, s * 0.16);
    p.moveTo(x - s * 0.08, y + s * 0.1); p.lineTo(x - s * 0.35, y + s * 0.8); p.lineTo(x - s * 0.1, y + s * 0.65);
    p.moveTo(x + s * 0.08, y + s * 0.1); p.lineTo(x + s * 0.35, y + s * 0.8); p.lineTo(x + s * 0.1, y + s * 0.65);
    return p;
}

void drawGlossyHeart(QPainter &p, const QPointF &c, double s, const QColor &top, const QColor &bottom, const QColor &outline) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QPainterPath h = heart(c, s);
    QLinearGradient g(QPointF(c.x(), c.y() - s * 0.75), QPointF(c.x(), c.y() + s * 0.45));
    g.setColorAt(0, top); g.setColorAt(1, bottom);
    p.setPen(QPen(outline, qMax(1.0, s * 0.045)));
    p.setBrush(g);
    p.drawPath(h);
    // glossy highlight
    QRadialGradient gl(QPointF(c.x() - s * 0.28, c.y() - s * 0.32), s * 0.42);
    gl.setColorAt(0, QColor(255, 255, 255, 190)); gl.setColorAt(1, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen); p.setBrush(gl);
    p.drawPath(h);
    p.restore();
}

void drawGlossyCard(QPainter &p, const QRectF &r, const QColor &top, const QColor &bottom, const QColor &border, double radius) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // soft shadow
    for (int i = 6; i > 0; --i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(201, 111, 157, 6 * i));
        p.drawRoundedRect(r.adjusted(-i, -i + 4, i, i + 4), radius + i, radius + i);
    }
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, top); g.setColorAt(1, bottom);
    p.setBrush(g);
    p.setPen(QPen(border, 2));
    p.drawRoundedRect(r, radius, radius);
    // gloss highlight
    QLinearGradient gl(r.topLeft(), QPointF(r.left(), r.top() + r.height() * 0.5));
    gl.setColorAt(0, QColor(255, 255, 255, 150)); gl.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(gl); p.setPen(Qt::NoPen);
    p.drawRoundedRect(r.adjusted(4, 3, -4, -r.height() * 0.5), radius - 4, radius - 4);
    p.restore();
}

} // namespace bc::kawaii
