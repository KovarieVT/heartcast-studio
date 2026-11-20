// Gothic stingers. Each draws one frame at time t; render_art.cpp encodes them and
// checks that every stinger fully covers the screen around its cut point.
#include "art_common.h"

#include <memory>

namespace art {

namespace {

// ---------------------------------------------------------------- Bat Swarm
StingerDef batSwarm() {
    struct B { double x0, y, span, phase, speed, wob; };
    auto bats = std::make_shared<std::vector<B>>();
    for (int i = 0; i < 230; ++i)
        bats->push_back({hash2(i, 1, 41) * 1.5 - 0.75, hash2(i, 2, 41), 0.025 + 0.06 * std::pow(hash2(i, 3, 41), 2), hash2(i, 4, 41) * 6.28,
                         0.85 + 0.3 * hash2(i, 5, 41), 0.02 + 0.03 * hash2(i, 6, 41)});
    return {"batswarm", "Bat Swarm", "Gothic", "bat-swarm.webm", 45, [bats](QPainter &p, double t) {
        const double tp = 0.70, v = 3.3;   // swarm centre crosses mid-screen at tp (screen widths/s)
        const double cx = 0.5 + v * (tp - t);
        p.drawImage(0, 0, maskImage(
            [&](double x, double y) { return smooth(1.2, 0.78, std::abs(x - cx) + 0.22 * (fbm(x * 4 + t * 1.5, y * 4, 3) - 0.5)); },
            [&](double x, double y) { const double n = fbm(x * 6 - t, y * 6, 9); return QColor::fromRgbF(0.04 + 0.08 * n, 0.025 + 0.04 * n, 0.05 + 0.08 * n); }));
        for (const auto &b : *bats) {
            const double x = (cx + b.x0 * b.speed) * SW, y = (b.y + std::sin(t * 5 + b.phase) * b.wob) * SH;
            if (x < -120 || x > SW + 120) continue;
            p.save(); p.translate(x, y); p.scale(-1, 1);
            p.setPen(QPen(QColor(0x3A, 0x2A, 0x40, 160), 1.0)); p.setBrush(QColor(6, 4, 8));
            p.drawPath(gothic::bat(QPointF(0, 0), b.span * SW, std::sin(t * 26 * b.speed + b.phase)));
            p.restore();
        }
    }};
}

// ---------------------------------------------------------------- Crimson Veil
StingerDef crimsonVeil() {
    auto hem = std::make_shared<std::vector<double>>(SW);
    struct Drip { double x, w, len; };
    std::vector<Drip> drips;
    for (int i = 0; i < 34; ++i) drips.push_back({hash2(i, 1, 61) * SW, 5 + 9 * hash2(i, 2, 61), 25 + 140 * std::pow(hash2(i, 3, 61), 1.6)});
    for (int x = 0; x < SW; ++x) {
        double d = 18 * fbm(x / 90.0, 0.5, 23);
        for (const auto &dr : drips) { const double k = (x - dr.x) / dr.w; if (std::abs(k) < 1) d = std::max(d, dr.len * std::sqrt(1 - k * k) + 6); }
        (*hem)[size_t(x)] = d;
    }
    return {"crimsonveil", "Crimson Veil", "Gothic", "crimson-veil.webm", 46, [hem](QPainter &p, double t) {
        const double tp = 0.75;
        const double grow = easeInOut(t / tp), lift = clamp01((t - tp - 0.04) / 0.66);
        const double line = (1.0 - easeInOut(lift) * 1.25) * SH;
        QImage color = maskImage([](double, double) { return 1.0; }, [&](double x, double y) {
            const double n = fbm(x * 7 + t * 0.4, y * 7, 29), vein = std::pow(1.0 - std::abs(2 * fbm(x * 3, y * 3, 5) - 1), 6);
            return QColor::fromRgbF(0.30 + 0.32 * n, 0.02 + 0.03 * n, 0.06 + 0.05 * n).darker(int(100 + 70 * vein));
        });
        for (int y = 0; y < SH; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(color.scanLine(y));
            for (int x = 0; x < SW; ++x) {
                const double nx = (x + 0.5) / SW, ny = (y + 0.5) / SH, dx = (nx - 0.5) * 1.78, dy = ny - 0.5;
                const double r = std::sqrt(dx * dx + dy * dy) + 0.25 * (fbm(nx * 5, ny * 5, 17, 4) - 0.5);
                double a = smooth(grow * 1.35 + 0.02, grow * 1.35 - 0.05, r);
                if (lift > 0) a *= smooth(line + (*hem)[size_t(x)] + 1.5, line + (*hem)[size_t(x)] - 1.5, y);
                const QColor c = QColor::fromRgb(row[x]);
                const double rim = 1.0 - std::abs(2 * a - 1);
                row[x] = qRgba(int(c.red() * (1 - 0.35 * rim)), int(c.green() * (1 - 0.35 * rim)), int(c.blue() * (1 - 0.35 * rim)), int(a * 255));
            }
        }
        p.drawImage(0, 0, color);
    }};
}

// ---------------------------------------------------------------- Cathedral Doors
void drawDoor(QPainter &p, const QRectF &r, bool left, double seamGlow) {
    QLinearGradient wood(r.topLeft(), r.topRight());
    wood.setColorAt(0, left ? QColor(0x1A, 0x13, 0x17) : QColor(0x24, 0x1B, 0x20));
    wood.setColorAt(1, left ? QColor(0x24, 0x1B, 0x20) : QColor(0x1A, 0x13, 0x17));
    p.setPen(Qt::NoPen); p.setBrush(wood); p.drawRect(r);
    p.setPen(QPen(QColor(0, 0, 0, 90), 2));
    for (int i = 0; i < 9; ++i) { const double x = r.left() + r.width() * (i + 1) / 10.0; p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom())); }
    const double pw = r.width() * 0.62, px = left ? r.right() - pw - r.width() * 0.16 : r.left() + r.width() * 0.16;
    const QRectF panel(px, r.top() + SH * 0.12, pw, SH * 0.70);
    p.setPen(QPen(OldGold.darker(115), 4)); p.setBrush(QColor(0x12, 0x0D, 0x10)); p.drawPath(gothic::lancet(panel));
    p.setPen(QPen(QColor(0xB8, 0x93, 0x5A, 120), 1.5)); p.setBrush(Qt::NoBrush); p.drawPath(gothic::lancet(panel.adjusted(14, 14, -14, -10)));
    gothic::drawRose(p, QPointF(panel.center().x(), panel.top() + panel.height() * 0.42), pw * 0.17, Crimson, Garnet);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0x5A, 0x50, 0x58));
    for (int i = 0; i < 6; ++i) {
        const double y = r.top() + SH * (0.06 + 0.176 * i);
        p.drawEllipse(QPointF(r.left() + r.width() * 0.07, y), 5, 5); p.drawEllipse(QPointF(r.right() - r.width() * 0.07, y), 5, 5);
    }
    const QPointF ring(left ? r.right() - 46 : r.left() + 46, SH * 0.56);
    p.setPen(QPen(OldGold, 5)); p.setBrush(Qt::NoBrush); p.drawEllipse(ring, 18, 18);
    p.setPen(Qt::NoPen); p.setBrush(OldGold.darker(130)); p.drawEllipse(ring - QPointF(0, 18), 7, 7);
    if (seamGlow > 0) {
        QLinearGradient g(left ? QPointF(r.right(), 0) : QPointF(r.left(), 0), left ? QPointF(r.right() - 60, 0) : QPointF(r.left() + 60, 0));
        g.setColorAt(0, QColor::fromRgbF(0.85, 0.15, 0.2, 0.55 * seamGlow)); g.setColorAt(1, QColor::fromRgbF(0.85, 0.15, 0.2, 0));
        p.setBrush(g); p.drawRect(r);
    }
    p.setPen(QPen(QColor(0, 0, 0, 200), 3)); p.setBrush(Qt::NoBrush); p.drawRect(r);
}

StingerDef cathedralDoors() {
    return {"cathedraldoors", "Cathedral Doors", "Gothic", "cathedral-doors.webm", 45, [](QPainter &p, double t) {
        const double closeEnd = 0.62, openStart = 0.78, openEnd = 1.36;
        double closed = t < closeEnd ? easeInOut(t / closeEnd) : t < openStart ? 1.0 : 1.0 - easeInOut((t - openStart) / (openEnd - openStart));
        if (closed <= 0.001) return;
        const double half = SW / 2.0;
        const double thud = (t > closeEnd && t < closeEnd + 0.08) ? std::sin((t - closeEnd) / 0.08 * M_PI) * 6 : 0;
        const double lx = -half + half * closed + thud * 0.3, rx = SW - half * closed - thud * 0.3;
        const double glow = closed > 0.9 ? 1.0 - (closed - 0.9) * 8 + (t > closeEnd ? 0.25 : 0) : 0;
        QLinearGradient sh(QPointF(lx + half, 0), QPointF(lx + half + 80, 0));
        sh.setColorAt(0, QColor(0, 0, 0, 140)); sh.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(QRectF(lx + half, 0, 80, SH), sh);
        QLinearGradient sh2(QPointF(rx, 0), QPointF(rx - 80, 0));
        sh2.setColorAt(0, QColor(0, 0, 0, 140)); sh2.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(QRectF(rx - 80, 0, 80, SH), sh2);
        drawDoor(p, QRectF(lx, 0, half + 1, SH), true, qMax(0.0, glow));
        drawDoor(p, QRectF(rx - 1, 0, half + 1, SH), false, qMax(0.0, glow));
        if (closed >= 0.999) {
            p.setPen(Qt::NoPen); p.setBrush(OldGold); p.drawPath(gothic::diamond(QPointF(half, SH * 0.48), 16));
            gothic::drawGlow(p, QPointF(half, SH * 0.48), 90, Blood, 0.5);
        }
    }};
}

// ---------------------------------------------------------------- Raven Feathers
StingerDef ravenFeathers() {
    struct F { double x0, y, len, rot, spin, speed; };
    auto fs = std::make_shared<std::vector<F>>();
    for (int i = 0; i < 260; ++i)
        fs->push_back({hash2(i, 1, 71) * 1.6 - 0.8, hash2(i, 2, 71) * 1.3 - 0.15, 30 + 90 * std::pow(hash2(i, 3, 71), 1.5), hash2(i, 4, 71) * 360,
                       (hash2(i, 5, 71) - 0.5) * 500, 0.85 + 0.3 * hash2(i, 6, 71)});
    return {"ravenfeathers", "Raven Feathers", "Gothic", "raven-feathers.webm", 45, [fs](QPainter &p, double t) {
        const double tp = 0.72, v = 3.2;
        const double cx = 0.5 - v * (tp - t);   // left → right, rising
        p.drawImage(0, 0, maskImage(
            [&](double x, double y) { return smooth(1.45, 1.0, std::abs(x - cx + (y - 0.5) * 0.35) + 0.2 * (fbm(x * 4 - t, y * 4, 13) - 0.5)); },
            [&](double x, double y) { const double n = fbm(x * 5 + t, y * 5, 19); return QColor::fromRgbF(0.03 + 0.05 * n, 0.035 + 0.06 * n, 0.06 + 0.10 * n); }));
        for (const auto &f : *fs) {
            const double x = (cx + f.x0 * f.speed) * SW, y = (f.y - (cx + f.x0) * 0.25) * SH;
            if (x < -150 || x > SW + 150) continue;
            p.save(); p.translate(x, y); p.rotate(f.rot + f.spin * t);
            QLinearGradient g(QPointF(-f.len * 0.2, 0), QPointF(f.len * 0.2, 0));
            g.setColorAt(0, QColor(8, 9, 14)); g.setColorAt(0.6, QColor(30, 34, 52)); g.setColorAt(1, QColor(6, 6, 9));
            p.setPen(QPen(QColor(70, 80, 110, 120), 0.8)); p.setBrush(g);
            p.drawPath(gothic::feather(QPointF(0, 0), f.len));
            p.restore();
        }
    }};
}

// ---------------------------------------------------------------- Coffin Lid
StingerDef coffinLid() {
    return {"coffinlid", "Coffin Lid", "Gothic", "coffin-lid.webm", 46, [](QPainter &p, double t) {
        double x;
        if (t < 0.55) x = -1.15 + 1.15 * easeInOut(t / 0.55);
        else if (t < 0.85) x = 0;
        else x = 1.15 * easeIn((t - 0.85) / 0.6);
        if (x >= 1.12) return;
        const QRectF lid(x * SW - 30, -30, SW + 60, SH + 60);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRect(lid.translated(x < 0 ? 28 : -28, 0));
        QLinearGradient wood(lid.topLeft(), lid.bottomLeft());
        wood.setColorAt(0, QColor(0x2E, 0x1B, 0x14)); wood.setColorAt(0.5, QColor(0x22, 0x13, 0x0E)); wood.setColorAt(1, QColor(0x14, 0x0B, 0x08));
        p.setBrush(wood); p.drawRect(lid);
        p.setPen(QPen(QColor(0, 0, 0, 70), 1.4));   // grain
        for (int i = 0; i < 46; ++i) {
            const double y = lid.top() + (i + 0.5) * lid.height() / 46;
            QPainterPath g; g.moveTo(lid.left(), y);
            for (int k = 1; k <= 16; ++k) g.lineTo(lid.left() + k * lid.width() / 16, y + 6 * (fbm(k * 0.4, i * 0.7, 3) - 0.5));
            p.drawPath(g);
        }
        // coffin outline (hexagon) carved into the lid
        auto P = [&](double u, double v) { return QPointF(lid.left() + 30 + u * SW, lid.top() + 30 + v * SH); };
        QPolygonF hex({P(0.10, 0.50), P(0.26, 0.10), P(0.86, 0.14), P(0.93, 0.50), P(0.86, 0.86), P(0.26, 0.90)});
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Silver.darker(130), 6)); p.drawPolygon(hex);
        p.setPen(QPen(Silver, 2)); p.drawPolygon(hex);
        gothic::drawRose(p, P(0.52, 0.50), 70, Crimson, Garnet);
        p.setPen(QPen(Silver, 3));
        p.drawLine(P(0.40, 0.50), P(0.465, 0.50)); p.drawLine(P(0.575, 0.50), P(0.64, 0.50));
        p.setPen(Qt::NoPen); p.setBrush(Silver);
        for (const QPointF &c : hex) p.drawEllipse(c, 7, 7);                         // nails
        for (double u : {0.30, 0.55, 0.80}) {                                          // brass handles
            p.setBrush(OldGold.darker(120)); p.drawRoundedRect(QRectF(P(u, 0.0).x() - 40, lid.top() + 18, 80, 16), 8, 8);
            p.drawRoundedRect(QRectF(P(u, 0.0).x() - 40, lid.bottom() - 34, 80, 16), 8, 8);
        }
    }};
}

// ---------------------------------------------------------------- Blood Moon
StingerDef bloodMoon() {
    return {"bloodmoon", "Blood Moon", "Gothic", "blood-moon.webm", 46, [](QPainter &p, double t) {
        const double a = envelope(t, 0.5, 0.85, 1.45);
        if (a <= 0.001) return;
        p.setOpacity(a);
        QLinearGradient sky(0, 0, 0, SH);
        sky.setColorAt(0, QColor(0x07, 0x05, 0x0A)); sky.setColorAt(0.7, QColor(0x1A, 0x05, 0x0C)); sky.setColorAt(1, QColor(0x33, 0x06, 0x10));
        p.fillRect(QRect(0, 0, SW, SH), sky);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 120; ++i) { QColor c = Parchment; c.setAlpha(60 + int(150 * hash2(i, 9, 2))); p.setBrush(c); p.drawEllipse(QPointF(hash2(i, 1, 2) * SW, hash2(i, 2, 2) * SH * 0.8), 1.2, 1.2); }
        const double rise = easeOut(t / 0.8);
        const QPointF mc(SW * 0.5, SH * (1.35 - 0.88 * rise) + std::max(0.0, t - 0.9) * 260);
        const double mr = 150 + 90 * rise;
        gothic::drawGlow(p, mc, mr * 2.6, Blood, 0.9);
        QRadialGradient mg(mc + QPointF(-mr * 0.3, -mr * 0.3), mr * 1.3);
        mg.setColorAt(0, QColor(0xFF, 0x7A, 0x5A)); mg.setColorAt(0.45, Blood); mg.setColorAt(1, Garnet);
        p.setBrush(mg); p.drawEllipse(mc, mr, mr);
        for (int i = 0; i < 9; ++i) {   // craters
            QColor c = Garnet; c.setAlpha(90);
            p.setBrush(c);
            p.drawEllipse(mc + QPointF((hash2(i, 1, 5) - 0.5) * mr * 1.2, (hash2(i, 2, 5) - 0.5) * mr * 1.2), mr * (0.06 + 0.1 * hash2(i, 3, 5)), mr * (0.05 + 0.08 * hash2(i, 3, 5)));
        }
        p.setBrush(QColor(4, 3, 6));
        for (int i = 0; i < 4; ++i)
            p.drawPath(gothic::bat(mc + QPointF(-mr * 1.4 + std::fmod(t * 320 + i * 150, mr * 2.8), -mr * 0.2 + i * 30 - 40), 40 + 10 * i, std::sin(t * 22 + i)));
        p.setOpacity(1);
    }};
}

// ---------------------------------------------------------------- Shattered Stained Glass
QPolygonF clipHalfPlane(const QPolygonF &poly, QPointF n, double c) {   // keep n·p <= c
    QPolygonF out;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF a = poly[i], b = poly[(i + 1) % poly.size()];
        const double da = n.x() * a.x() + n.y() * a.y() - c, db = n.x() * b.x() + n.y() * b.y() - c;
        if (da <= 0) out << a;
        if ((da < 0) != (db < 0)) out << a + (b - a) * (da / (da - db));
    }
    return out;
}

StingerDef stainedGlass() {
    struct Cell { QPolygonF poly; QPointF c; double appear, fall, spin; QColor col; };
    auto cells = std::make_shared<std::vector<Cell>>();
    std::vector<QPointF> seeds;
    for (int gy = 0; gy < 7; ++gy)
        for (int gx = 0; gx < 11; ++gx)
            seeds.push_back(QPointF((gx + 0.15 + 0.7 * hash2(gx, gy, 3)) / 11.0 * SW, (gy + 0.15 + 0.7 * hash2(gx, gy, 5)) / 7.0 * SH));
    const QColor palette[] = {QColor(0x9E, 0x1B, 0x32), QColor(0x4B, 0x2A, 0x7A), QColor(0x1F, 0x3A, 0x7A), QColor(0xC6, 0x8E, 0x2F), QColor(0x2F, 0x6B, 0x4A), QColor(0xB0, 0x3A, 0x5A)};
    for (size_t i = 0; i < seeds.size(); ++i) {
        QPolygonF poly({QPointF(-20, -20), QPointF(SW + 20, -20), QPointF(SW + 20, SH + 20), QPointF(-20, SH + 20)});
        const QPointF si = seeds[i];
        for (size_t j = 0; j < seeds.size(); ++j) {
            if (i == j) continue;
            const QPointF sj = seeds[j];
            poly = clipHalfPlane(poly, sj - si, (QPointF::dotProduct(sj, sj) - QPointF::dotProduct(si, si)) / 2);
        }
        const int k = int(i);
        cells->push_back({poly, si, 0.04 + 0.42 * hash2(k, 1, 7), 0.86 + 0.22 * hash2(k, 2, 7), (hash2(k, 3, 7) - 0.5) * 220, palette[k % 6]});
    }
    return {"stainedglass", "Shattered Stained Glass", "Gothic", "stained-glass.webm", 48, [cells](QPainter &p, double t) {
        for (const auto &c : *cells) {
            if (t < c.appear) continue;
            const double in = easeOutBack((t - c.appear) / 0.14);
            double dy = 0, rot = 0, op = 1;
            if (t > c.fall) { const double ft = t - c.fall; dy = 1900 * ft * ft; rot = c.spin * ft; op = 1 - clamp01(ft / 0.55); }
            if (op <= 0) continue;
            p.save();
            p.setOpacity(op);
            p.translate(c.c + QPointF(0, dy)); p.rotate(rot); p.scale(0.6 + 0.4 * in, 0.6 + 0.4 * in); p.translate(-c.c);
            QRadialGradient g(c.c, 140);
            g.setColorAt(0, c.col.lighter(150)); g.setColorAt(1, c.col.darker(140));
            p.setPen(QPen(QColor(10, 8, 10), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(g);
            p.drawPolygon(c.poly);
            p.restore();
        }
    }};
}

// ---------------------------------------------------------------- Candle Snuff
StingerDef candleSnuff() {
    return {"candlesnuff", "Candle Snuff", "Gothic", "candle-snuff.webm", 46, [](QPainter &p, double t) {
        const double diag = std::hypot(SW, SH) * 0.62;
        // the dark closes past the centre (R < 0) so nothing of the old scene remains
        const double e = easeInOut(t / 0.55);
        double R = t < 0.55 ? diag * (1 - e) - 150 * e : t < 0.9 ? -150 : -150 + (diag + 150) * easeInOut((t - 0.9) / 0.6);
        const QPointF c(SW / 2.0, SH * 0.55);
        p.drawImage(0, 0, maskImage(
            [&](double x, double y) {
                const double d = std::hypot((x - 0.5) * SW, (y - 0.55) * SH) + 60 * (fbm(x * 6, y * 6 + t, 11) - 0.5);
                return smooth(R - 70, R + 30, d);
            },
            [&](double x, double y) {
                const double d = std::hypot((x - 0.5) * SW, (y - 0.55) * SH);
                return mix(QColor(0x3A, 0x18, 0x10), QColor(0x05, 0x03, 0x07), smooth(R - 40, R + 160, d));
            }));
        // candle appears in the dark, is snuffed at the cut, then fades with the dark
        const double candleOp = t < 0.9 ? smooth(0.25, 0.5, t) : 1 - smooth(0.95, 1.3, t);
        if (candleOp <= 0) return;
        p.setOpacity(candleOp);
        const QRectF body(c.x() - 26, c.y() - 20, 52, 170);
        QLinearGradient wax(body.topLeft(), body.topRight());
        wax.setColorAt(0, QColor(0xC9, 0xBF, 0xAE)); wax.setColorAt(0.5, Parchment); wax.setColorAt(1, QColor(0xA8, 0x9C, 0x8A));
        p.setPen(Qt::NoPen); p.setBrush(wax); p.drawRoundedRect(body, 6, 6);
        p.drawEllipse(QPointF(c.x() - 18, c.y() + 6), 8, 22);   // drips
        p.drawEllipse(QPointF(c.x() + 14, c.y() + 14), 6, 30);
        p.setPen(QPen(QColor(0x20, 0x18, 0x14), 3)); p.drawLine(QPointF(c.x(), c.y() - 20), QPointF(c.x(), c.y() - 34));
        const double flame = t < 0.70 ? 1.0 : 1.0 - clamp01((t - 0.70) / 0.08);
        if (flame > 0) {
            const double fl = 1 + 0.08 * std::sin(t * 40) + 0.05 * std::sin(t * 67);
            gothic::drawGlow(p, QPointF(c.x(), c.y() - 60), 230 * flame, QColor(0xFF, 0x9A, 0x40), 0.9 * flame);
            QPainterPath f; const QPointF b(c.x(), c.y() - 32); const double h = 64 * fl * flame, w = 18 * flame;
            f.moveTo(b); f.cubicTo(b + QPointF(-w, -h * 0.25), b + QPointF(-w * 0.4, -h * 0.75), b + QPointF(0, -h));
            f.cubicTo(b + QPointF(w * 0.4, -h * 0.75), b + QPointF(w, -h * 0.25), b);
            QRadialGradient fg(b + QPointF(0, -h * 0.35), h * 0.7);
            fg.setColorAt(0, QColor(255, 250, 220)); fg.setColorAt(0.5, QColor(255, 190, 80)); fg.setColorAt(1, QColor(230, 90, 30, 0));
            p.setPen(Qt::NoPen); p.setBrush(fg); p.drawPath(f);
        } else {   // smoke wisp
            const double st = t - 0.78;
            QPainterPath s; s.moveTo(c.x(), c.y() - 34);
            for (int i = 1; i <= 20; ++i) s.lineTo(c.x() + std::sin(i * 0.5 + st * 6) * (4 + i * 1.6), c.y() - 34 - i * 9 * (0.5 + st * 2));
            p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor(0x9A, 0x92, 0xA0, int(160 * clamp01(1 - st))), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawPath(s);
        }
        p.setOpacity(1);
    }};
}

// ---------------------------------------------------------------- Iron Portcullis
StingerDef ironPortcullis() {
    return {"portcullis", "Iron Portcullis", "Gothic", "iron-portcullis.webm", 44, [](QPainter &p, double t) {
        double y;
        if (t < 0.42) y = easeIn(t / 0.42) * (SH + 40);
        else if (t < 0.52) y = SH + 40 - std::sin((t - 0.42) / 0.10 * M_PI) * 22;
        else if (t < 0.85) y = SH + 40;
        else y = (SH + 40) * (1 - easeInOut((t - 0.85) / 0.55));
        if (y <= 1) return;
        p.drawImage(0, 0, maskImage([&](double, double ny) { return ny * SH <= y - 30 ? 1.0 : 0.0; }, [&](double x, double yy) {
            const double n = fbm(x * 9, yy * 9 + (SH - y) / SH * 0, 21);   // stone in the dark behind the gate
            return QColor::fromRgbF(0.04 + 0.05 * n, 0.035 + 0.04 * n, 0.05 + 0.05 * n);
        }));
        const QColor iron(0x3E, 0x3A, 0x42), ironHi(0x7A, 0x74, 0x80);
        for (int i = 0; i * 64 - 20 < SW; ++i) {   // vertical bars with spikes
            const double x = i * 64.0 + 12;
            QLinearGradient g(QPointF(x - 9, 0), QPointF(x + 9, 0));
            g.setColorAt(0, iron.darker(140)); g.setColorAt(0.4, ironHi); g.setColorAt(1, iron.darker(160));
            p.setPen(Qt::NoPen); p.setBrush(g);
            p.drawRect(QRectF(x - 9, -10, 18, y - 26));
            QPainterPath sp; sp.moveTo(x - 11, y - 38); sp.lineTo(x, y + 6); sp.lineTo(x + 11, y - 38); sp.closeSubpath();
            p.drawPath(sp);
        }
        for (int j = 1; j * 120 < y - 40; ++j) {   // cross bars
            const double yy = y - 40 - j * 120;
            QLinearGradient g(QPointF(0, yy - 8), QPointF(0, yy + 8));
            g.setColorAt(0, ironHi); g.setColorAt(1, iron.darker(170));
            p.setBrush(g); p.drawRect(QRectF(0, yy - 8, SW, 16));
            p.setBrush(QColor(0x9A, 0x94, 0xA0));
            for (int i = 0; i * 64 - 20 < SW; ++i) p.drawEllipse(QPointF(i * 64.0 + 12, yy), 4, 4);
        }
        if (t > 0.40 && t < 0.70) {   // dust from the slam
            const double dt = t - 0.40;
            for (int i = 0; i < 40; ++i) {
                QColor c(0x8A, 0x80, 0x78, int(140 * (1 - dt / 0.3)));
                p.setBrush(c);
                p.drawEllipse(QPointF(hash2(i, 1, 4) * SW, SH - 10 - dt * 300 * hash2(i, 2, 4)), 3 + 6 * hash2(i, 3, 4), 3 + 6 * hash2(i, 3, 4));
            }
        }
    }};
}

// ---------------------------------------------------------------- Thorn Vines
StingerDef thornVines() {
    struct Vine { std::vector<QPointF> pts; };
    auto vines = std::make_shared<std::vector<Vine>>();
    for (int i = 0; i < 22; ++i) {
        // start on an edge, wander toward the centre
        const int edge = i % 4;
        const double u = hash2(i, 1, 33);
        QPointF pt = edge == 0 ? QPointF(u * SW, -10) : edge == 1 ? QPointF(SW + 10, u * SH) : edge == 2 ? QPointF(u * SW, SH + 10) : QPointF(-10, u * SH);
        Vine v; v.pts.push_back(pt);
        double ang = std::atan2(SH / 2.0 - pt.y(), SW / 2.0 - pt.x());
        for (int k = 0; k < 60; ++k) {
            ang += (fbm(k * 0.15, i * 3.1, 7) - 0.5) * 0.9;
            pt += QPointF(std::cos(ang), std::sin(ang)) * 14;
            v.pts.push_back(pt);
        }
        vines->push_back(v);
    }
    return {"thornvines", "Thorn Vines", "Gothic", "thorn-vines.webm", 48, [vines](QPainter &p, double t) {
        const double grow = t < 0.62 ? easeOut(t / 0.62) : t < 0.86 ? 1.0 : 1.0 - easeIn((t - 0.86) / 0.6);
        if (grow <= 0.001) return;
        // tangle mass growing in from the edges
        p.drawImage(0, 0, maskImage([&](double x, double y) {
            const double edge = std::min({x, 1 - x, (y) * SH / SW, (1 - y) * SH / SW});
            return smooth(grow * 0.42 + 0.03, grow * 0.42 - 0.03, edge + 0.08 * (fbm(x * 8, y * 8, 41) - 0.5));
        }, [&](double x, double y) { const double n = fbm(x * 14, y * 14, 43); return QColor::fromRgbF(0.04 + 0.05 * n, 0.07 + 0.08 * n, 0.045 + 0.04 * n); }));
        for (const auto &v : *vines) {
            const int n = std::max(2, int(v.pts.size() * grow));
            QPainterPath path; path.moveTo(v.pts[0]);
            for (int k = 1; k < n; ++k) path.lineTo(v.pts[size_t(k)]);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0x1E, 0x2E, 0x20), 9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(path);
            p.setPen(QPen(QColor(0x3A, 0x55, 0x34), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(path);
            p.setPen(Qt::NoPen); p.setBrush(QColor(0x1E, 0x2E, 0x20));
            for (int k = 3; k < n; k += 4) {   // thorns
                const QPointF a = v.pts[size_t(k)], d = v.pts[size_t(k)] - v.pts[size_t(k - 1)];
                const QPointF nrm(-d.y() / 14, d.x() / 14), s = (k % 8 ? 1.0 : -1.0) * nrm;
                QPainterPath th; th.moveTo(a + d * 0.3); th.lineTo(a + s * 16 - d * 0.2); th.lineTo(a - d * 0.3); th.closeSubpath();
                p.drawPath(th);
            }
            if (grow > 0.75) gothic::drawRose(p, v.pts[size_t(n - 1)], 22 * smooth(0.75, 1.0, grow), Crimson, Garnet);
        }
    }};
}

// ---------------------------------------------------------------- Midnight Smoke
StingerDef midnightSmoke() {
    return {"midnightsmoke", "Midnight Smoke", "Gothic", "midnight-smoke.webm", 48, [](QPainter &p, double t) {
        const double front = 1.12 - 2.25 * (t / 1.6);   // top edge, in screen heights
        p.drawImage(0, 0, maskImage([&](double x, double y) {
            const double n = fbm(x * 5, y * 5 + t * 1.2, 51);
            const double top = smooth(front - 0.06, front + 0.10, y + 0.22 * (n - 0.5));
            const double tail = smooth(front + 2.1, front + 1.9, y + 0.22 * (n - 0.5));
            return top * tail;
        }, [&](double x, double y) {
            const double n = fbm(x * 7 - t * 0.3, y * 7 + t, 53);
            return QColor::fromRgbF(0.05 + 0.09 * n, 0.03 + 0.05 * n, 0.07 + 0.11 * n);
        }));
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 70; ++i) {   // embers carried by the smoke
            const double y = (front + 0.15 + 1.4 * hash2(i, 1, 8)) * SH - t * 60 * hash2(i, 2, 8);
            if (y < -10 || y > SH + 10) continue;
            const double x = hash2(i, 3, 8) * SW + std::sin(t * 3 + i) * 20;
            const double fl = 0.5 + 0.5 * std::sin(t * 12 + i);
            gothic::drawGlow(p, QPointF(x, y), 10, Blood, 0.5 * fl);
            p.setBrush(QColor(255, 120 + int(80 * fl), 60, 220)); p.drawEllipse(QPointF(x, y), 1.8, 1.8);
        }
    }};
}

// ---------------------------------------------------------------- Velvet Curtain
void drawCurtain(QPainter &p, const QRectF &r, bool left, double sway) {
    QLinearGradient g(r.topLeft(), r.topRight());
    const int folds = 9;
    for (int i = 0; i <= folds * 4; ++i) {
        const double u = double(i) / (folds * 4);
        const double s = 0.5 + 0.5 * std::cos((u * folds + sway) * 2 * M_PI);
        g.setColorAt(u, mix(QColor(0x3A, 0x05, 0x0E), QColor(0xA8, 0x18, 0x2E), std::pow(s, 1.6)));
    }
    p.setPen(Qt::NoPen); p.setBrush(g); p.drawRect(r);
    QLinearGradient shade(r.topLeft(), r.bottomLeft());
    shade.setColorAt(0, QColor(0, 0, 0, 120)); shade.setColorAt(0.3, QColor(0, 0, 0, 0)); shade.setColorAt(1, QColor(0, 0, 0, 110));
    p.setBrush(shade); p.drawRect(r);
    // gold fringe
    const QRectF fr(r.left(), r.bottom() - 34, r.width(), 34);
    QLinearGradient gold(fr.topLeft(), fr.bottomLeft());
    gold.setColorAt(0, OldGold.lighter(130)); gold.setColorAt(1, OldGold.darker(150));
    p.setBrush(gold); p.drawRect(fr.adjusted(0, 0, 0, -22));
    for (double x = fr.left() + 4; x < fr.right(); x += 9) p.drawRect(QRectF(x, fr.top() + 10, 4, 24));
    (void)left;
}

StingerDef velvetCurtain() {
    return {"velvetcurtain", "Velvet Curtain", "Gothic", "velvet-curtain.webm", 46, [](QPainter &p, double t) {
        const double closed = t < 0.6 ? easeInOut(t / 0.6) : t < 0.86 ? 1.0 : 1.0 - easeInOut((t - 0.86) / 0.62);
        if (closed <= 0.001) return;
        const double half = SW / 2.0 + 30;
        const double sway = std::sin(t * 5) * 0.05;
        drawCurtain(p, QRectF(-half + half * closed, 0, half, SH), true, sway);
        drawCurtain(p, QRectF(SW - half * closed, 0, half, SH), false, -sway + 0.5);
        // valance
        const double vh = 90 * closed;
        QLinearGradient vg(0, 0, 0, vh);
        vg.setColorAt(0, QColor(0x5A, 0x08, 0x16)); vg.setColorAt(1, QColor(0x2A, 0x03, 0x0A));
        QPainterPath val; val.moveTo(0, 0); val.lineTo(SW, 0); val.lineTo(SW, vh * 0.6);
        for (int i = 6; i >= 0; --i) val.quadTo(SW * (i + 0.5) / 7.0, vh * 1.25, SW * i / 7.0, vh * 0.6);
        val.closeSubpath();
        p.setPen(QPen(OldGold, 3)); p.setBrush(vg); p.drawPath(val);
    }};
}

// ---------------------------------------------------------------- Graveyard Fog
StingerDef graveyardFog() {
    return {"graveyardfog", "Graveyard Fog", "Gothic", "graveyard-fog.webm", 48, [](QPainter &p, double t) {
        const double dens = t < 0.6 ? 2.4 * easeInOut(t / 0.6) : t < 0.88 ? 2.4 : 2.4 * (1 - easeInOut((t - 0.88) / 0.6));
        if (dens <= 0.001) return;
        p.drawImage(0, 0, maskImage([&](double x, double y) {
            const double n = fbm(x * 3 + t * 0.6, y * 3, 61) * 0.7 + fbm(x * 7 - t, y * 7, 63) * 0.3;
            return dens * (0.55 + 0.8 * n) * (0.75 + 0.35 * y);
        }, [&](double x, double y) {
            const double n = fbm(x * 4 - t * 0.3, y * 4, 67);
            return mix(QColor(0x2A, 0x26, 0x32), QColor(0x6E, 0x66, 0x7A), n * (0.6 + 0.4 * (1 - y)));
        }));
        // silhouettes looming in the fog
        p.setOpacity(clamp01(dens - 0.3) * 0.9);
        p.setPen(Qt::NoPen); p.setBrush(QColor(0x12, 0x10, 0x16));
        for (int i = 0; i < 7; ++i) {
            const double x = 80 + i * 180 + 40 * hash2(i, 1, 9), h = 90 + 60 * hash2(i, 2, 9), w = 60 + 30 * hash2(i, 3, 9);
            QPainterPath stone; stone.addRoundedRect(QRectF(x, SH - h, w, h + 10), w / 2, w / 2);
            QPainterPath cut; cut.addRect(QRectF(x - 1, SH - h + w / 2, w + 2, h));
            p.drawPath(stone.united(cut.intersected(stone)));
        }
        QPainterPath tree; tree.moveTo(1040, SH); tree.lineTo(1060, SH - 260); tree.lineTo(1010, SH - 360); tree.lineTo(1066, SH - 280);
        tree.lineTo(1100, SH - 400); tree.lineTo(1080, SH - 260); tree.lineTo(1150, SH - 330); tree.lineTo(1086, SH - 230); tree.lineTo(1095, SH); tree.closeSubpath();
        p.drawPath(tree);
        p.setOpacity(1);
    }};
}

// ---------------------------------------------------------------- Black Rose
StingerDef blackRose() {
    return {"blackrose", "Black Rose", "Gothic", "black-rose.webm", 46, [](QPainter &p, double t) {
        const double halfDiag = std::hypot(SW, SH) / 2.0;
        const double bloom = easeOut(t / 0.58);
        const double fade = 1.0 - smooth(0.88, 1.40, t);
        if (fade <= 0) return;
        const QPointF c(SW / 2.0, SH / 2.0);
        p.setOpacity(fade);
        const double R = halfDiag * 1.15 * bloom;
        QRadialGradient bg(c, std::max(1.0, R));
        bg.setColorAt(0, QColor(0x2A, 0x08, 0x12)); bg.setColorAt(0.8, QColor(0x0E, 0x04, 0x08)); bg.setColorAt(1, QColor(0x05, 0x02, 0x04));
        p.setPen(Qt::NoPen); p.setBrush(bg); p.drawEllipse(c, R, R);
        p.save(); p.translate(c); p.rotate(t * 25); p.translate(-c);
        gothic::drawRose(p, c, R * 0.55, QColor(0x3A, 0x0A, 0x16), QColor(0x0A, 0x02, 0x05));
        p.restore();
        p.setOpacity(1);
        if (t > 0.86) {   // petals scatter as it fades
            for (int i = 0; i < 70; ++i) {
                const double ft = t - 0.86, ang = hash2(i, 1, 13) * 2 * M_PI, sp = 300 + 700 * hash2(i, 2, 13);
                const QPointF pos = c + QPointF(std::cos(ang), std::sin(ang)) * sp * ft + QPointF(0, 500 * ft * ft);
                p.save(); p.translate(pos); p.rotate(ft * 400 * (hash2(i, 3, 13) - 0.5));
                QColor col = QColor(0x4A, 0x0C, 0x1A); col.setAlphaF(clamp01(1 - ft / 0.55));
                p.setBrush(col); p.drawPath(gothic::petal(QPointF(0, 0), 30 + 30 * hash2(i, 4, 13)));
                p.restore();
            }
        }
    }};
}

} // namespace

std::vector<StingerDef> gothicStingers() {
    return {batSwarm(), crimsonVeil(), cathedralDoors(), ravenFeathers(), coffinLid(), bloodMoon(), stainedGlass(),
            candleSnuff(), ironPortcullis(), thornVines(), midnightSmoke(), velvetCurtain(), graveyardFog(), blackRose()};
}

} // namespace art
