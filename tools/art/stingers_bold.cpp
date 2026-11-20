// Bold stingers: steel, fire, storms, claws and impact.
#include "art_common.h"

#include <memory>

namespace art {

namespace {

// ---------------------------------------------------------------- Steel Shutter
StingerDef steelShutter() {
    return {"steelshutter", "Steel Shutter", "Bold", "steel-shutter.webm", 44, [](QPainter &p, double t) {
        double y;   // bottom edge of the shutter
        if (t < 0.40) y = easeIn(t / 0.40) * (SH + 30);
        else if (t < 0.50) y = SH + 30 - std::sin((t - 0.40) / 0.10 * M_PI) * 18;
        else if (t < 0.84) y = SH + 30;
        else y = (SH + 30) * (1 - easeInOut((t - 0.84) / 0.52));
        if (y <= 1) return;
        const double slatH = 46;
        p.setPen(Qt::NoPen);
        for (double top = y - 60; top > -slatH; top -= slatH) {
            QLinearGradient g(QPointF(0, top), QPointF(0, top + slatH));
            g.setColorAt(0, QColor(0x8A, 0x90, 0x98)); g.setColorAt(0.12, QColor(0x5C, 0x62, 0x6A));
            g.setColorAt(0.55, QColor(0x3A, 0x3F, 0x46)); g.setColorAt(0.9, QColor(0x22, 0x25, 0x2A)); g.setColorAt(1, QColor(0x14, 0x16, 0x19));
            p.setBrush(g); p.drawRect(QRectF(0, top, SW, slatH + 1));
        }
        QLinearGradient bar(QPointF(0, y - 60), QPointF(0, y));   // heavy bottom bar
        bar.setColorAt(0, QColor(0x6A, 0x70, 0x78)); bar.setColorAt(0.3, QColor(0x2A, 0x2E, 0x34)); bar.setColorAt(1, QColor(0x10, 0x11, 0x13));
        p.setBrush(bar); p.drawRect(QRectF(0, y - 60, SW, 60));
        p.setBrush(QColor(0xB0, 0x86, 0x2A));   // hazard stripes
        for (double x = -60; x < SW; x += 80) {
            QPolygonF s({QPointF(x, y - 18), QPointF(x + 40, y - 18), QPointF(x + 60, y - 4), QPointF(x + 20, y - 4)});
            p.drawPolygon(s);
        }
        p.setBrush(QColor(0x14, 0x16, 0x19)); p.drawRoundedRect(QRectF(SW / 2.0 - 70, y - 50, 140, 20), 6, 6);   // handle
        p.setBrush(QColor(0xA0, 0xA6, 0xAE));
        for (double x = 40; x < SW; x += 160) p.drawEllipse(QPointF(x, y - 36), 5, 5);
        if (t > 0.38 && t < 0.62) {   // sparks on impact
            const double k = (t - 0.38) / 0.24;
            for (int i = 0; i < 46; ++i) {
                const double side = i % 2 ? 1 : -1, ang = (0.15 + 0.6 * hash2(i, 1, 7)) * M_PI, sp = 500 + 900 * hash2(i, 2, 7);
                const QPointF o(i % 4 < 2 ? 30 : SW - 30, SH - 4);
                const QPointF a = o + QPointF(side * std::cos(ang), -std::sin(ang)) * sp * k * 0.4 + QPointF(0, 900 * k * k * 0.3);
                const QPointF b = a - QPointF(side * std::cos(ang), -std::sin(ang)) * 26;
                p.setPen(QPen(QColor(255, 200 - int(100 * k), 80, int(255 * (1 - k))), 3, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(a, b);
            }
        }
    }};
}

// ---------------------------------------------------------------- Inferno
StingerDef inferno() {
    return {"inferno", "Inferno", "Bold", "inferno.webm", 46, [](QPainter &p, double t) {
        const double front = 1.12 - 2.6 * (t / 1.5);   // top of the fire wall, screen heights
        p.drawImage(0, 0, noiseLayer(2, [&](double x, double y) {
            const double n = fbm(x * 6, y * 4 + t * 3.0, 71);
            const double top = smooth(front - 0.05, front + 0.12, y + 0.3 * (n - 0.5));
            const double tail = smooth(front + 2.25, front + 2.0, y + 0.2 * (n - 0.5));
            return top * tail;
        }, [&](double x, double y) {
            const double n = fbm(x * 7, y * 5 + t * 3.5, 73);
            const double depth = y - front + 0.25 * (n - 0.5);   // distance below the leading edge
            const double heat = clamp01(1.0 - std::abs(depth - 0.42) / 0.42) * (0.6 + 0.6 * n);
            QColor c = mix(QColor(0x14, 0x09, 0x08), QColor(0xC8, 0x24, 0x10), clamp01(heat * 1.6));
            c = mix(c, QColor(0xFF, 0x9A, 0x1E), clamp01(heat * 2.0 - 0.9));
            return mix(c, QColor(0xFF, 0xF0, 0xA0), clamp01(heat * 3.0 - 2.4));
        }));
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 60; ++i) {   // sparks above the flames
            const double y = (front + 0.05 - 0.4 * hash2(i, 1, 5)) * SH - t * 120;
            if (y < -10 || y > SH) continue;
            const QPointF pt(hash2(i, 2, 5) * SW + std::sin(t * 6 + i) * 16, y);
            gothic::drawGlow(p, pt, 12, QColor(255, 140, 40), 0.7);
            p.setBrush(QColor(255, 220, 140)); p.drawEllipse(pt, 2, 2);
        }
    }};
}

// ---------------------------------------------------------------- Lightning Strike
StingerDef lightningStrike() {
    auto bolt = std::make_shared<std::vector<QPolygonF>>();
    {
        QPolygonF main; QPointF pt(SW * 0.56, -20); main << pt;
        for (int i = 1; i <= 16; ++i) { pt += QPointF((hash2(i, 1, 17) - 0.5) * 120, SH / 15.0); main << pt; }
        bolt->push_back(main);
        for (int b = 0; b < 4; ++b) {   // branches
            QPolygonF br; QPointF q = main[3 + b * 3]; br << q;
            for (int i = 1; i <= 5; ++i) { q += QPointF((b % 2 ? 1 : -1) * (30 + 40 * hash2(b, i, 19)), 34); br << q; }
            bolt->push_back(br);
        }
    }
    return {"lightning", "Lightning Strike", "Bold", "lightning-strike.webm", 42, [bolt](QPainter &p, double t) {
        const double a = envelope(t, 0.40, 0.82, 1.36);
        if (a <= 0.001) return;
        p.drawImage(0, 0, maskImage([&](double x, double y) { return a * (1.4 + 0.6 * (fbm(x * 3, y * 3, 31) - 0.5)) - 0.02; }, [&](double x, double y) {
            const double n = fbm(x * 4 + t * 0.8, y * 4, 33);
            return mix(QColor(0x08, 0x0B, 0x14), QColor(0x34, 0x3E, 0x58), n * (1.2 - y * 0.6));
        }));
        auto strike = [&](double s0, double s1) { return t >= s0 && t <= s1; };
        if (strike(0.52, 0.70) || strike(0.74, 0.78)) {
            for (int pass = 0; pass < 3; ++pass) {
                const double w = pass == 0 ? 26 : pass == 1 ? 10 : 3;
                const QColor c = pass == 0 ? QColor(140, 160, 255, 70) : pass == 1 ? QColor(190, 205, 255, 170) : QColor(255, 255, 255);
                p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
                for (size_t i = 0; i < bolt->size(); ++i) {
                    QPen pen = p.pen(); pen.setWidthF(i == 0 ? w : w * 0.5); p.setPen(pen);
                    p.drawPolyline((*bolt)[i]);
                }
            }
        }
        if (strike(0.54, 0.62)) p.fillRect(QRect(0, 0, SW, SH), QColor(235, 240, 255, int(220 * (1 - (t - 0.54) / 0.08))));
    }};
}

// ---------------------------------------------------------------- Claw Slash
StingerDef clawSlash() {
    return {"clawslash", "Claw Slash", "Bold", "claw-slash.webm", 44, [](QPainter &p, double t) {
        // Three diagonal tears: they rip in, widen until the screen is black, then the dark
        // fades leaving glowing scars.
        const QPointF dir = QPointF(-0.78, 0.62);
        auto slash = [&](double offset, double draw, double width) {
            const QPointF a = QPointF(SW * 0.95 + offset, -SH * 0.15), b = a + dir * (SW * 1.6) * draw;
            const QPointF n(-dir.y(), dir.x());
            QPainterPath s;
            s.moveTo(a); s.quadTo((a + b) / 2 + n * width, b); s.quadTo((a + b) / 2 - n * width, a);
            return s;
        };
        const double draw = easeOut(t / 0.22);
        const double widen = t < 0.22 ? 0 : easeIn((t - 0.22) / 0.30);
        const double fade = 1 - smooth(0.84, 1.30, t);
        if (fade <= 0) return;
        p.setOpacity(fade);
        for (int i = 0; i < 3; ++i) {
            const double off = (i - 1) * 260;
            const double w = 22 + widen * 900;
            const QPainterPath s = slash(off, draw, w);
            p.setPen(QPen(QColor(255, 120, 40, 200), 10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(QColor(4, 3, 4));
            p.drawPath(s);
            p.setPen(QPen(QColor(255, 220, 160), 3)); p.setBrush(Qt::NoBrush); p.drawPath(s);
        }
        if (widen > 0.85) p.fillRect(QRect(0, 0, SW, SH), QColor(4, 3, 4, int(255 * smooth(0.85, 1.0, widen))));
        p.setOpacity(1);
        if (t > 0.84) {   // glowing scars linger as the dark lifts
            const double k = clamp01((t - 0.84) / 0.5);
            for (int i = 0; i < 3; ++i) {
                p.setPen(QPen(QColor(255, 110, 40, int(230 * (1 - k))), 6 * (1 - k) + 1, Qt::SolidLine, Qt::RoundCap));
                p.setBrush(Qt::NoBrush); p.drawPath(slash((i - 1) * 260, 1.0, 22));
            }
        }
    }};
}

// ---------------------------------------------------------------- Armor Plates
StingerDef armorPlates() {
    struct Hex { QPointF c; double in, out; };
    auto hexes = std::make_shared<std::vector<Hex>>();
    const double r = 74, w = std::sqrt(3.0) * r;
    const QPointF centre(SW / 2.0, SH / 2.0);
    const double maxD = std::hypot(SW, SH) / 2.0;
    for (int row = -1; row * r * 1.5 < SH + r; ++row)
        for (int col = -1; col * w < SW + w; ++col) {
            const QPointF c(col * w + (row % 2 ? w / 2 : 0), row * r * 1.5);
            const double d = QLineF(c, centre).length() / maxD;
            hexes->push_back({c, 0.04 + 0.38 * d + 0.04 * hash2(row, col, 3), 0.86 + 0.38 * (1 - d) + 0.04 * hash2(row, col, 4)});
        }
    return {"armorplates", "Armor Plates", "Bold", "armor-plates.webm", 48, [hexes, r](QPainter &p, double t) {
        for (const auto &h : *hexes) {
            double s;
            if (t < h.in) continue;
            else if (t < h.out) s = easeOutBack((t - h.in) / 0.14);
            else s = 1 - easeIn((t - h.out) / 0.14);
            if (s <= 0.001) continue;
            QPolygonF hex;
            for (int k = 0; k < 6; ++k) { const double a = M_PI / 6 + k * M_PI / 3; hex << h.c + QPointF(std::cos(a), std::sin(a)) * r * 1.06 * s; }
            QLinearGradient g(h.c - QPointF(0, r), h.c + QPointF(0, r));
            g.setColorAt(0, QColor(0x5A, 0x60, 0x6A)); g.setColorAt(0.5, QColor(0x33, 0x37, 0x3E)); g.setColorAt(1, QColor(0x1C, 0x1F, 0x24));
            p.setPen(QPen(QColor(0x9A, 0xA2, 0xAE), 2)); p.setBrush(g); p.drawPolygon(hex);
            p.setPen(Qt::NoPen); p.setBrush(QColor(0x8A, 0x92, 0x9E)); p.drawEllipse(h.c, 6 * s, 6 * s);
            p.setBrush(QColor(0x1A, 0x1C, 0x20)); p.drawRect(QRectF(h.c.x() - 4 * s, h.c.y() - 1 * s, 8 * s, 2 * s));
        }
    }};
}

// ---------------------------------------------------------------- Shockwave
StingerDef shockwave() {
    return {"shockwave", "Shockwave", "Bold", "shockwave.webm", 46, [](QPainter &p, double t) {
        const QPointF c(SW / 2.0, SH / 2.0);
        const double halfDiag = std::hypot(SW, SH) / 2.0;
        const double R = halfDiag * 1.25 * easeOut(t / 0.48);
        const double clear = smooth(0.86, 1.42, t);   // smoke thins from the middle out
        p.drawImage(0, 0, maskImage([&](double x, double y) {
            const double d = std::hypot((x - 0.5) * SW, (y - 0.5) * SH) + 80 * (fbm(x * 5, y * 5, 81) - 0.5);
            const double inside = smooth(R + 20, R - 40, d);
            // holes open where noise + closeness to the centre beat a falling threshold
            const double thr = 1.35 - clear * 1.5;
            const double thin = clear > 0 ? smooth(thr - 0.05, thr + 0.12, fbm(x * 4, y * 4 - t, 83) + (1 - d / halfDiag) * 0.5) : 0;
            return inside * (1 - thin);
        }, [&](double x, double y) {
            const double d = std::hypot((x - 0.5) * SW, (y - 0.5) * SH);
            const double n = fbm(x * 6 + t, y * 6, 85);
            const double heat = clamp01(1 - d / (R * 0.55 + 1)) * clamp01(1 - t / 0.7);
            QColor smoke = mix(QColor(0x18, 0x16, 0x15), QColor(0x4A, 0x44, 0x40), n);
            smoke = mix(smoke, QColor(0xE8, 0x5A, 0x18), heat * 1.3);
            return mix(smoke, QColor(0xFF, 0xE8, 0xA0), clamp01(heat * 2 - 1));
        }));
        if (t < 0.16) {   // the flash
            gothic::drawGlow(p, c, 420, QColor(255, 240, 200), 1.6 * (1 - t / 0.16));
            p.fillRect(QRect(0, 0, SW, SH), QColor(255, 245, 225, int(200 * (1 - t / 0.16))));
        }
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 60 && t < 0.8; ++i) {   // debris
            const double ang = hash2(i, 1, 87) * 2 * M_PI, sp = 900 + 900 * hash2(i, 2, 87);
            const QPointF pt = c + QPointF(std::cos(ang), std::sin(ang)) * sp * t;
            p.setBrush(QColor(30, 26, 22)); p.drawRect(QRectF(pt.x() - 4, pt.y() - 4, 8 + 6 * hash2(i, 3, 87), 6));
        }
    }};
}

// ---------------------------------------------------------------- Glitch
StingerDef glitch() {
    return {"glitch", "Glitch", "Bold", "glitch.webm", 40, [](QPainter &p, double t) {
        const int f = int(t * FPS);
        const double cover = envelope(t, 0.42, 0.78, 1.25);
        if (cover <= 0.001) return;
        const int bands = 24;
        for (int i = 0; i < bands; ++i) {   // horizontal bands snap on in a random order
            const double on = hash2(i, f / 3, 101);
            if (on > cover * 1.15) continue;
            const double y = i * SH / double(bands), h = SH / double(bands) + 1;
            const double shift = (hash2(i, f, 103) - 0.5) * 60 * (1 - cover * 0.5);
            p.fillRect(QRectF(shift - 40, y, SW + 80, h), QColor(8, 8, 12));
            p.fillRect(QRectF(shift - 40 + 6, y + h * 0.15, SW * hash2(i, f, 105), 2), QColor(255, 40, 90, 200));
            p.fillRect(QRectF(SW * hash2(i, f, 107) - 40, y + h * 0.65, SW * 0.4, 2), QColor(40, 220, 255, 200));
        }
        if (cover > 0.99) {
            for (int y = 0; y < SH; y += 4) p.fillRect(QRect(0, y, SW, 1), QColor(255, 255, 255, 12));   // scanlines
            p.setPen(QColor(255, 40, 90)); QFont fnt("Inter"); fnt.setPixelSize(64); fnt.setBold(true); p.setFont(fnt);
            const QPointF jitter((hash2(f, 1, 9) - 0.5) * 10, 0);
            p.drawText(QRectF(0, 0, SW, SH).translated(jitter + QPointF(-4, 0)), Qt::AlignCenter, QStringLiteral("SIGNAL LOST"));
            p.setPen(QColor(40, 220, 255)); p.drawText(QRectF(0, 0, SW, SH).translated(jitter + QPointF(4, 0)), Qt::AlignCenter, QStringLiteral("SIGNAL LOST"));
            p.setPen(QColor(240, 240, 255)); p.drawText(QRectF(0, 0, SW, SH).translated(jitter), Qt::AlignCenter, QStringLiteral("SIGNAL LOST"));
        }
    }};
}

} // namespace

std::vector<StingerDef> boldStingers() { return {steelShutter(), inferno(), lightningStrike(), clawSlash(), armorPlates(), shockwave(), glitch()}; }

} // namespace art
