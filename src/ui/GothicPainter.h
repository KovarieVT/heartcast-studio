#pragma once
#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

class QPainter;

// Shared vector motifs for the splash screen, brand header, overlay widgets,
// stingers and starter art. Everything is drawn procedurally (no third-party artwork).
namespace bc::gothic {

// Bat silhouette. `span` is the full wingspan; `flap` in [-1, 1] lifts (+) or lowers (-) the wings.
QPainterPath bat(const QPointF &center, double span, double flap = 0.0);
// Crescent moon opening to the upper right.
QPainterPath crescent(const QPointF &center, double radius);
// Equilateral pointed (lancet) arch filling `r`: straight sides, two arcs meeting at the top.
QPainterPath lancet(const QRectF &r);
// Rectangle with chamfered corners.
QPainterPath chamfered(const QRectF &r, double cut);
QPainterPath petal(const QPointF &center, double size);     // rose petal, tip pointing down
QPainterPath feather(const QPointF &center, double length);
QPainterPath diamond(const QPointF &center, double size);

void drawRose(QPainter &p, const QPointF &center, double radius, const QColor &petal, const QColor &shade);
// Scrollwork ornament for a corner. quadrant: 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left.
void drawFiligreeCorner(QPainter &p, const QPointF &corner, double size, int quadrant, const QColor &color, double width = 1.4);
// Dark panel with chamfered corners, gold outer line, accent inner line and corner filigree.
void drawGothicCard(QPainter &p, const QRectF &r, const QColor &top, const QColor &bottom, const QColor &accent,
                    const QColor &ornament, bool corners = true);
// The KovaLive! mark: a glowing lancet window with a crescent moon and play symbol, framed by bat wings.
void drawEmblem(QPainter &p, const QPointF &center, double size, const QColor &accent, double glow = 1.0);
// Rose-window medallion used by alerts; `pulse` in [0, 1] brightens the glass.
void drawSigil(QPainter &p, const QPointF &center, double radius, const QColor &accent, const QColor &metal, double pulse = 0.0);
// Soft halo for moons and candles.
void drawGlow(QPainter &p, const QPointF &center, double radius, const QColor &color, double strength = 1.0);

} // namespace bc::gothic
