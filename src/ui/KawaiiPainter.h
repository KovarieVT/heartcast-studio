#pragma once
#include <QColor>
#include <QPainterPath>
#include <QPointF>

class QPainter;

// Shared vector shapes for splash screen, widgets and alert cards.
namespace bc::kawaii {

QPainterPath heart(const QPointF &center, double size);
QPainterPath star(const QPointF &center, double outer, double inner, int points = 5);
QPainterPath sparkle(const QPointF &center, double size);       // 4-point twinkle
QPainterPath bow(const QPointF &center, double size);
void drawGlossyHeart(QPainter &p, const QPointF &center, double size, const QColor &top, const QColor &bottom, const QColor &outline);
void drawGlossyCard(QPainter &p, const QRectF &r, const QColor &top, const QColor &bottom, const QColor &border, double radius = 22);

} // namespace bc::kawaii
