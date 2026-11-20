// Shared helpers for tools/art (procedural branding and stinger rendering).
#pragma once
#include "app/Theme.h"
#include "ui/GothicPainter.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QtMath>
#include <cmath>
#include <functional>
#include <vector>

namespace art {

using namespace bc;
using namespace bc::theme;

constexpr int SW = 1280, SH = 720, FPS = 30;

inline double hash2(int x, int y, int seed) {
    quint32 h = quint32(x) * 374761393u + quint32(y) * 668265263u + quint32(seed) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return double(h ^ (h >> 16)) / 4294967295.0;
}
inline double vnoise(double x, double y, int seed) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const double fx = x - xi, fy = y - yi, u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    const double a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
inline double fbm(double x, double y, int seed, int oct = 5) {
    double s = 0, amp = 0.5, norm = 0;
    for (int o = 0; o < oct; ++o) { s += amp * vnoise(x, y, seed + o * 31); norm += amp; x *= 2.02; y *= 2.02; amp *= 0.5; }
    return s / norm;
}
inline double smooth(double e0, double e1, double x) { const double t = qBound(0.0, (x - e0) / (e1 - e0), 1.0); return t * t * (3 - 2 * t); }
inline double clamp01(double x) { return qBound(0.0, x, 1.0); }
inline double easeInOut(double t) { t = clamp01(t); return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }
inline double easeIn(double t) { t = clamp01(t); return t * t * t; }
inline double easeOut(double t) { t = clamp01(t); return 1 - std::pow(1 - t, 3); }
inline double easeOutBack(double t) { t = clamp01(t); const double c1 = 1.70158, c3 = c1 + 1; return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2); }
// 0 → 1 over [0, a], 1 until b, 1 → 0 over [b, c]
inline double envelope(double t, double a, double b, double c) {
    if (t <= a) return easeInOut(t / a);
    if (t <= b) return 1.0;
    return 1.0 - easeInOut((t - b) / (c - b));
}
inline QColor mix(const QColor &a, const QColor &b, double t) {
    t = clamp01(t);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

// A noise-heavy layer computed at 1/div resolution and scaled up smoothly.
inline QImage noiseLayer(int div, const std::function<double(double, double)> &alphaAt, const std::function<QColor(double, double)> &colorAt) {
    const int mw = SW / div, mh = SH / div;
    QImage m(mw, mh, QImage::Format_ARGB32);
    for (int y = 0; y < mh; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(m.scanLine(y));
        for (int x = 0; x < mw; ++x) {
            const double nx = (x + 0.5) / mw, ny = (y + 0.5) / mh;
            const double a = clamp01(alphaAt(nx, ny));
            const QColor c = a > 0.001 ? colorAt(nx, ny) : QColor(0, 0, 0);
            row[x] = qRgba(c.red(), c.green(), c.blue(), int(a * 255 + 0.5));
        }
    }
    return div == 1 ? m : m.scaled(SW, SH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
inline QImage maskImage(const std::function<double(double, double)> &alphaAt, const std::function<QColor(double, double)> &colorAt) {
    return noiseLayer(4, alphaAt, colorAt);
}

inline QPainterPath heart(const QPointF &c, double s) {
    QPainterPath p;
    p.moveTo(c.x(), c.y() + s * 0.42);
    p.cubicTo(c.x() - s * 0.62, c.y() - s * 0.02, c.x() - s * 0.42, c.y() - s * 0.62, c.x(), c.y() - s * 0.28);
    p.cubicTo(c.x() + s * 0.42, c.y() - s * 0.62, c.x() + s * 0.62, c.y() - s * 0.02, c.x(), c.y() + s * 0.42);
    return p;
}
inline QPainterPath star4(const QPointF &c, double r) {
    QPainterPath p;
    p.moveTo(c.x(), c.y() - r);
    p.quadTo(c.x(), c.y(), c.x() + r, c.y());
    p.quadTo(c.x(), c.y(), c.x(), c.y() + r);
    p.quadTo(c.x(), c.y(), c.x() - r, c.y());
    p.quadTo(c.x(), c.y(), c.x(), c.y() - r);
    return p;
}

// One stinger: draws frame at time t (seconds) onto a transparent 1280×720 canvas.
struct StingerDef {
    QString id, title, group, file;
    int frames = 45;
    std::function<void(QPainter &, double)> draw;
};

std::vector<StingerDef> gothicStingers();
std::vector<StingerDef> cuteStingers();
std::vector<StingerDef> boldStingers();

} // namespace art
