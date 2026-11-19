#include "app/Theme.h"
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>

namespace bc::theme {

void apply(QApplication &app, const QString &accent, int fontPt) {
    QFile f(":/theme/kawaii.qss");
    if (!f.open(QIODevice::ReadOnly)) { qWarning("theme stylesheet missing"); return; }
    QString qss = QString::fromUtf8(f.readAll());

    const QColor pink = accent.isEmpty() ? CottonCandy : QColor(accent);
    // Longest tokens first so "@pink" doesn't eat "@pinkish" style names.
    const QList<QPair<QString, QString>> tokens = {
        {"@mintaccent", MintAccent.name()}, {"@softmint", SoftMint.name()},
        {"@deeprose", DeepRose.name()},     {"@lavender", Lavender.name()},
        {"@charcoal", Charcoal.name()},     {"@fontscale", QString::number(fontPt)},
        {"@pearl", PearlWhite.name()},      {"@lilac", SoftLilac.name()},
        {"@blush", BlushPink.name()},       {"@baby", BabyPink.name()},
        {"@rose", PastelRose.name()},       {"@mint", PastelMint.name()},
        {"@gray", LightGray.name()},        {"@pink", pink.name()},
    };
    for (const auto &[k, v] : tokens) qss.replace(k, v);

    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, PearlWhite);
    pal.setColor(QPalette::WindowText, Charcoal);
    pal.setColor(QPalette::Base, PearlWhite);
    pal.setColor(QPalette::Text, Charcoal);
    pal.setColor(QPalette::Button, BabyPink);
    pal.setColor(QPalette::ButtonText, Charcoal);
    pal.setColor(QPalette::Highlight, pink);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(pal);
    app.setStyleSheet(qss);
}

} // namespace bc::theme
