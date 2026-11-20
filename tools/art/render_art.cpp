// Renders KovaLive!'s raster branding and stinger frames from the same vector code the
// app uses (src/ui/GothicPainter.cpp), so every asset matches the UI.
//
//   render_art <out-dir>
//
// Writes: logo_<size>.png (16…512), logo.svg, wizard_side_<w>.png, wizard_small_<s>.png,
// stingers/<id>/f###.png (1280×720, 30 fps, with alpha) and stingers/stingers.json.
// Every stinger is checked: it must fully cover the screen for at least 3 frames, and its
// cut point is placed in the middle of that stretch. tools/make_assets.sh encodes the rest.
#include "art_common.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QRandomGenerator>
#include <QSvgGenerator>
#include <QtMath>
#include <cmath>
#include <functional>
#include <vector>

using namespace art;

namespace {

// ------------------------------------------------------------------ logo & icons
void drawIcon(QPainter &p, double s) {
    p.setRenderHint(QPainter::Antialiasing);
    if (s <= 40) {   // small sizes: just the glowing window and moon, so it stays legible
        const QRectF win(s * 0.20, s * 0.04, s * 0.60, s * 0.92);
        const QPainterPath arch = gothic::lancet(win);
        QRadialGradient g(QPointF(s * 0.5, s * 0.55), s * 0.6);
        g.setColorAt(0, Blood.lighter(150)); g.setColorAt(0.6, Crimson); g.setColorAt(1, Garnet.darker(150));
        p.setPen(QPen(OldGold, std::max(1.0, s / 16.0)));
        p.setBrush(g);
        p.drawPath(arch);
        p.setPen(Qt::NoPen); p.setBrush(Parchment);
        p.drawPath(gothic::crescent(QPointF(s * 0.47, s * 0.50), s * 0.17));
        return;
    }
    gothic::drawEmblem(p, QPointF(s / 2.0, s * 0.53), s * 0.98, Crimson, 1.0);
}

QImage icon(int s) {
    QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    drawIcon(p, s);
    p.end();
    return img;
}

// ------------------------------------------------------------------ installer art
QImage wizardSide(int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0, QColor(0x0E, 0x0B, 0x14)); bg.setColorAt(0.55, Void); bg.setColorAt(1, QColor(0x2A, 0x08, 0x14));
    p.fillRect(img.rect(), bg);
    const double u = w / 164.0;
    for (int i = 0; i < 40; ++i) {   // stars
        QColor c = Parchment; c.setAlpha(50 + int(hash2(i, 1, 7) * 150));
        p.setPen(Qt::NoPen); p.setBrush(c);
        p.drawEllipse(QPointF(hash2(i, 2, 7) * w, hash2(i, 3, 7) * h * 0.55), 0.5 * u, 0.5 * u);
    }
    const QPointF moon(w * 0.74, h * 0.12);
    gothic::drawGlow(p, moon, 60 * u, Silver, 0.6);
    p.setBrush(Parchment); p.drawEllipse(moon, 13 * u, 13 * u);
    p.setBrush(QColor(4, 3, 6));
    for (int i = 0; i < 5; ++i)
        p.drawPath(gothic::bat(QPointF(w * (0.16 + 0.17 * i), h * (0.22 + 0.05 * (i % 2))), (10 + 4 * (i % 3)) * u, (i % 3) - 1.0));
    gothic::drawEmblem(p, QPointF(w / 2.0, h * 0.52), w * 0.82, Crimson, 1.0);
    QPainterPath ground; ground.moveTo(0, h);   // spires
    for (int i = 0; i <= 6; ++i) {
        const double x = i * w / 6.0, top = h * (0.80 - 0.06 * (i % 3));
        ground.lineTo(x - w * 0.05, h * 0.88); ground.lineTo(x - w * 0.05, top + 8 * u); ground.lineTo(x, top); ground.lineTo(x + w * 0.05, top + 8 * u); ground.lineTo(x + w * 0.05, h * 0.88);
    }
    ground.lineTo(w, h); ground.closeSubpath();
    p.setBrush(QColor(5, 4, 7)); p.drawPath(ground);
    QColor orn = OldGold; orn.setAlpha(170);
    gothic::drawFiligreeCorner(p, QPointF(5 * u, 5 * u), 36 * u, 0, orn, 0.9 * u);
    gothic::drawFiligreeCorner(p, QPointF(w - 5 * u, 5 * u), 36 * u, 1, orn, 0.9 * u);
    p.end();
    return img;
}

QImage wizardSmall(int s) {
    QImage img(s, s, QImage::Format_RGB32);
    img.fill(Crypt);
    QPainter p(&img);
    p.translate(s * 0.02, s * 0.02);
    drawIcon(p, s * 0.96);
    p.end();
    return img;
}


} // namespace

// Renders one stinger's frames; returns the fraction of fully opaque pixels per frame.
static std::vector<double> renderStinger(const QString &dir, const StingerDef &s) {
    QDir().mkpath(dir);
    std::vector<double> cover;
    for (int f = 0; f < s.frames; ++f) {
        QImage img(SW, SH, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        s.draw(p, f / double(FPS));
        p.end();
        const QImage out = img.convertToFormat(QImage::Format_ARGB32);
        qint64 opaque = 0;
        for (int y = 0; y < SH; y += 2) {
            const QRgb *row = reinterpret_cast<const QRgb *>(out.constScanLine(y));
            for (int x = 0; x < SW; x += 2) opaque += qAlpha(row[x]) >= 250;
        }
        cover.push_back(opaque / double((SH / 2) * (SW / 2)));
        out.save(QStringLiteral("%1/f%2.png").arg(dir).arg(f, 3, 10, QChar('0')));
    }
    return cover;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(resources);
    theme::loadFonts();
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("art-out");
    const QString only = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();   // "--no-stingers" or a stinger id
    QDir().mkpath(out);

    if (only.isEmpty() || only == "--no-stingers") {
        for (int s : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256, 512}) icon(s).save(QStringLiteral("%1/logo_%2.png").arg(out).arg(s));
        QSvgGenerator svg;
        svg.setFileName(out + "/logo.svg");
        svg.setSize(QSize(512, 512));
        svg.setViewBox(QRect(0, 0, 512, 512));
        svg.setTitle("KovaLive!");
        svg.setDescription("KovaLive! emblem");
        { QPainter p(&svg); drawIcon(p, 512); }
        for (auto [w, h] : {std::pair{164, 314}, {205, 392}, {246, 471}, {328, 628}})
            wizardSide(w, h).save(QStringLiteral("%1/wizard_side_%2.png").arg(out).arg(w));
        for (int s : {55, 69, 83, 110}) wizardSmall(s).save(QStringLiteral("%1/wizard_small_%2.png").arg(out).arg(s));
    }
    if (only == "--no-stingers") return 0;

    std::vector<StingerDef> all;
    for (auto list : {gothicStingers(), cuteStingers(), boldStingers()}) for (auto &s : list) all.push_back(s);
    QJsonArray manifest;
    int failures = 0;
    for (const auto &s : all) {
        if (!only.isEmpty() && only != s.id) continue;
        const auto cover = renderStinger(out + "/stingers/" + s.id, s);
        // longest run of frames that hide the whole screen
        int bestStart = -1, bestLen = 0;
        for (int i = 0; i < int(cover.size());) {
            if (cover[size_t(i)] < 0.999) { ++i; continue; }
            int j = i; while (j < int(cover.size()) && cover[size_t(j)] >= 0.999) ++j;
            if (j - i > bestLen) { bestLen = j - i; bestStart = i; }
            i = j;
        }
        if (bestLen < 3) {
            double mx = 0; for (double c : cover) mx = std::max(mx, c);
            fprintf(stderr, "FAIL %-16s never fully covers the screen (best %.1f%%)\n", qPrintable(s.id), mx * 100);
            ++failures;
            continue;
        }
        const int pointFrame = bestStart + bestLen / 2;
        const int pointMs = int(std::lround(pointFrame * 1000.0 / FPS));
        printf("%-16s %-24s covered frames %2d–%2d  cut at %4d ms\n", qPrintable(s.id), qPrintable(s.title), bestStart, bestStart + bestLen - 1, pointMs);
        manifest.append(QJsonObject{{"id", s.id}, {"title", s.title}, {"group", s.group}, {"file", s.file},
                                    {"pointMs", pointMs}, {"durationMs", int(std::lround(s.frames * 1000.0 / FPS))}});
    }
    if (only.isEmpty()) {
        QFile mf(out + "/stingers/stingers.json");
        if (mf.open(QIODevice::WriteOnly)) mf.write(QJsonDocument(QJsonObject{{"stingers", manifest}}).toJson());
    }
    return failures ? 1 : 0;
}
