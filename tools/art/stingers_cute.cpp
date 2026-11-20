// Cute stingers: pastel, bouncy, soft.
#include "art_common.h"

#include <memory>

namespace art {

namespace {

const QColor kPink(0xFF, 0xB3, 0xCF), kRose(0xFF, 0x7E, 0xAE), kLav(0xDC, 0xCC, 0xF7), kMint(0xB8, 0xEE, 0xDA), kCream(0xFF, 0xF7, 0xFB), kSky(0xC9, 0xE6, 0xFF);

// ---------------------------------------------------------------- Heart Burst
StingerDef heartBurst() {
    struct H { QPointF c; double size, appear, pop; QColor col; };
    auto hs = std::make_shared<std::vector<H>>();
    const QColor cols[] = {kPink, kRose, kCream, QColor(0xFF, 0x96, 0xC0), QColor(0xF7, 0xC4, 0xD8)};
    for (int i = 0; i < 70; ++i)
        hs->push_back({QPointF(hash2(i, 1, 81) * SW, hash2(i, 2, 81) * SH), 50 + 150 * hash2(i, 3, 81), 0.03 + 0.42 * hash2(i, 4, 81),
                       0.84 + 0.30 * hash2(i, 5, 81), cols[i % 5]});
    return {"heartburst", "Heart Burst", "Cute", "heart-burst.webm", 44, [hs](QPainter &p, double t) {
        const double halfDiag = std::hypot(SW, SH) / 2.0 + 40;
        const double R = t < 0.5 ? halfDiag * easeOutBack(t / 0.5) : t < 0.86 ? halfDiag : halfDiag * (1 - easeIn((t - 0.86) / 0.44));
        const QPointF c(SW / 2.0, SH / 2.0);
        if (R > 1) {
            QRadialGradient g(c, R);
            g.setColorAt(0, kCream); g.setColorAt(0.6, kPink); g.setColorAt(1, QColor(0xFF, 0x9E, 0xC4));
            p.setPen(Qt::NoPen); p.setBrush(g);
            p.drawPath(heart(c + QPointF(0, R * 0.12), R * 2.6));
            p.drawEllipse(c, R, R);
        }
        for (const auto &h : *hs) {
            if (t < h.appear) continue;
            double sc = easeOutBack((t - h.appear) / 0.18), op = 1;
            if (t > h.pop) { const double k = (t - h.pop) / 0.16; sc *= 1 + 0.5 * k; op = 1 - clamp01(k); }
            if (op <= 0) continue;
            p.save(); p.setOpacity(op);
            p.translate(h.c); p.rotate(std::sin(t * 4 + h.size) * 8); p.scale(sc, sc);
            QLinearGradient g(QPointF(0, -h.size / 2), QPointF(0, h.size / 2));
            g.setColorAt(0, h.col.lighter(112)); g.setColorAt(1, h.col.darker(112));
            p.setPen(QPen(QColor(255, 255, 255, 200), 3)); p.setBrush(g);
            p.drawPath(heart(QPointF(0, 0), h.size));
            p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 150));
            p.drawEllipse(QPointF(-h.size * 0.18, -h.size * 0.12), h.size * 0.08, h.size * 0.05);
            p.restore();
        }
    }};
}

// ---------------------------------------------------------------- Sparkle Sweep
StingerDef sparkleSweep() {
    return {"sparklesweep", "Sparkle Sweep", "Cute", "sparkle-sweep.webm", 44, [](QPainter &p, double t) {
        const double c = -1.0 + 3.4 * (t / 1.45);   // band centre along the diagonal
        auto s = [](double x, double y) { return x * 0.8 + y * 0.6; };
        p.drawImage(0, 0, noiseLayer(4, [&](double x, double y) { return smooth(1.05, 0.92, std::abs(s(x, y) - c)); }, [&](double x, double y) {
            const double u = s(x, y) - c;
            return u < 0 ? mix(kLav, kPink, clamp01(u + 1)) : mix(kPink, kMint, clamp01(u));
        }));
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 90; ++i) {
            const double x = hash2(i, 1, 91), y = hash2(i, 2, 91);
            if (std::abs(s(x, y) - c) > 0.9) continue;
            const double tw = 0.5 + 0.5 * std::sin(t * 14 + i * 1.7);
            const QPointF pt(x * SW, y * SH);
            gothic::drawGlow(p, pt, 30 * tw, QColor(255, 255, 255), 0.6);
            p.setBrush(QColor(255, 255, 255, 230));
            p.drawPath(star4(pt, (8 + 18 * hash2(i, 3, 91)) * (0.5 + tw)));
        }
    }};
}

// ---------------------------------------------------------------- Bubble Pop
StingerDef bubblePop() {
    struct B { double x, y0, r, speed, pop; QColor tint; };
    auto bs = std::make_shared<std::vector<B>>();
    const QColor tints[] = {kSky, kPink, kLav, kMint};
    for (int i = 0; i < 80; ++i)
        bs->push_back({hash2(i, 1, 95) * SW, SH + 80 + hash2(i, 2, 95) * SH * 0.8, 24 + 70 * hash2(i, 3, 95), 0.8 + 0.5 * hash2(i, 4, 95),
                       0.86 + 0.36 * hash2(i, 5, 95), tints[i % 4]});
    return {"bubblepop", "Bubble Pop", "Cute", "bubble-pop.webm", 46, [bs](QPainter &p, double t) {
        const double rise = easeOut(t / 0.55);
        const double fade = 1 - smooth(0.88, 1.25, t);
        const double top = SH + 60 - rise * (SH + 220);
        if (fade > 0) {
            p.setOpacity(fade);
            QPainterPath wave; wave.moveTo(-10, SH + 10);
            for (int x = -10; x <= SW + 10; x += 20) wave.lineTo(x, top + 26 * std::sin(x * 0.012 + t * 6));
            wave.lineTo(SW + 10, SH + 10); wave.closeSubpath();
            QLinearGradient g(0, top, 0, SH);
            g.setColorAt(0, kLav); g.setColorAt(0.5, kSky); g.setColorAt(1, kPink);
            p.setPen(Qt::NoPen); p.setBrush(g); p.drawPath(wave);
            p.setOpacity(1);
        }
        for (const auto &b : *bs) {
            const double y = b.y0 - (SH + 500) * easeOut(t / 0.7) * b.speed;
            const QPointF c(b.x + std::sin(t * 3 + b.r) * 14, y);
            if (t < b.pop) {
                QRadialGradient g(c - QPointF(b.r * 0.3, b.r * 0.3), b.r * 1.2);
                g.setColorAt(0, QColor(255, 255, 255, 230)); g.setColorAt(0.5, b.tint.lighter(108)); g.setColorAt(1, b.tint.darker(108));
                p.setPen(QPen(QColor(255, 255, 255, 220), 2.5)); p.setBrush(g); p.drawEllipse(c, b.r, b.r);
                p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 200)); p.drawEllipse(c - QPointF(b.r * 0.35, b.r * 0.35), b.r * 0.18, b.r * 0.12);
            } else if (t < b.pop + 0.2) {   // pop ring
                const double k = (t - b.pop) / 0.2;
                p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor(255, 255, 255, int(220 * (1 - k))), 3 * (1 - k) + 0.5, Qt::DotLine));
                p.drawEllipse(c, b.r * (1 + 0.7 * k), b.r * (1 + 0.7 * k));
            }
        }
    }};
}

// ---------------------------------------------------------------- Candy Swirl
StingerDef candySwirl() {
    return {"candyswirl", "Candy Swirl", "Cute", "candy-swirl.webm", 44, [](QPainter &p, double t) {
        const double halfDiag = std::hypot(SW, SH) / 2.0 + 30;
        const double R = t < 0.55 ? halfDiag * easeOutBack(t / 0.55) : t < 0.84 ? halfDiag : halfDiag * (1 - easeIn((t - 0.84) / 0.5));
        if (R <= 1) return;
        const double rot = t * 5.0;
        QImage img = noiseLayer(2, [&](double x, double y) {
            const double d = std::hypot((x - 0.5) * SW, (y - 0.5) * SH);
            return smooth(R + 2, R - 2, d);
        }, [&](double x, double y) {
            const double dx = (x - 0.5) * SW, dy = (y - 0.5) * SH, d = std::hypot(dx, dy) + 1;
            const double a = std::atan2(dy, dx) + rot - std::log(d) * 1.8;
            const int stripe = int(std::floor((a / (2 * M_PI)) * 9.0 + 900)) % 3;
            const QColor c = stripe == 0 ? kRose : stripe == 1 ? kCream : kMint;
            return c.darker(int(100 + 12 * smooth(R * 0.85, R, d)));
        });
        p.drawImage(0, 0, img);
        p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor(255, 255, 255, 230), 10));
        p.drawEllipse(QPointF(SW / 2.0, SH / 2.0), R - 5, R - 5);
    }};
}

// ---------------------------------------------------------------- Cloud Puff
void cloudBank(QPainter &p, double front, bool fromLeft, double t) {
    // solid body behind the front edge, then a frill of puffs
    QLinearGradient g(0, 0, 0, SH);
    g.setColorAt(0, kCream); g.setColorAt(1, kLav);
    p.setPen(Qt::NoPen); p.setBrush(g);
    if (fromLeft) p.drawRect(QRectF(-20, -20, front + 20, SH + 40));
    else p.drawRect(QRectF(front, -20, SW - front + 20, SH + 40));
    for (int i = 0; i < 26; ++i) {   // soft billows inside the bank
        const double depth = 120 + 640 * hash2(i, fromLeft ? 3 : 4, 97);
        const double x = front + (fromLeft ? -depth : depth), y = hash2(i, fromLeft ? 5 : 6, 97) * SH, r = 80 + 70 * hash2(i, 7, 97);
        QRadialGradient pg(QPointF(x - r * 0.2, y - r * 0.35), r * 1.2);
        pg.setColorAt(0, QColor(255, 255, 255)); pg.setColorAt(0.7, kCream); pg.setColorAt(1, QColor(0xE8, 0xDC, 0xFA));
        p.setBrush(pg); p.drawEllipse(QPointF(x, y), r, r * 0.8);
    }
    for (int i = 0; i < 14; ++i) {
        const double y = (i + 0.5) * SH / 13.0, r = 70 + 40 * hash2(i, fromLeft ? 1 : 2, 99);
        const double x = front + (fromLeft ? -1 : 1) * (r * 0.35 + 25 * std::sin(t * 4 + i));
        QRadialGradient pg(QPointF(x, y - r * 0.3), r * 1.3);
        pg.setColorAt(0, QColor(255, 255, 255)); pg.setColorAt(1, kLav);
        p.setBrush(pg); p.drawEllipse(QPointF(x, y), r, r * 0.85);
    }
}

StingerDef cloudPuff() {
    return {"cloudpuff", "Cloud Puff", "Cute", "cloud-puff.webm", 46, [](QPainter &p, double t) {
        const double k = t < 0.55 ? easeOut(t / 0.55) : t < 0.86 ? 1.0 : 1.0 - easeInOut((t - 0.86) / 0.6);
        if (k <= 0.001) return;
        const double reach = SW * 0.5 + 40;
        cloudBank(p, -160 + (reach + 160) * k, true, t);
        cloudBank(p, SW + 160 - (reach + 160) * k, false, t);
        if (k > 0.98) {   // sparkles where the clouds meet
            p.setPen(Qt::NoPen); p.setBrush(QColor(255, 220, 240));
            for (int i = 0; i < 8; ++i) p.drawPath(star4(QPointF(SW / 2.0 + (hash2(i, 1, 3) - 0.5) * 200, hash2(i, 2, 3) * SH), 10 + 8 * std::sin(t * 12 + i)));
        }
    }};
}

} // namespace

std::vector<StingerDef> cuteStingers() { return {heartBurst(), sparkleSweep(), bubblePop(), candySwirl(), cloudPuff()}; }

} // namespace art
