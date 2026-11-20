#include "app/Theme.h"
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>
#include <algorithm>

namespace bc::theme {

static QString s_display = QStringLiteral("Cinzel");
static QString s_decorative = QStringLiteral("Cinzel Decorative");
static QString s_body = QStringLiteral("Inter");

QString displayFamily() { return s_display; }
QString decorativeFamily() { return s_decorative; }
QString bodyFamily() { return s_body; }

void loadFonts() {
    static bool done = false;
    if (done) return;
    done = true;
    auto add = [](const char *file, QString &family) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(file));
        const QStringList fams = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
        if (fams.isEmpty()) qWarning("theme: could not load font %s", file);
        else family = fams.first();
    };
    add(":/fonts/Cinzel-Variable.ttf", s_display);
    add(":/fonts/CinzelDecorative-Bold.ttf", s_decorative);
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/CinzelDecorative-Black.ttf"));
    add(":/fonts/Inter-Variable.ttf", s_body);
}

void apply(QApplication &app, const QString &accent, int fontPt) {
    loadFonts();
    QFile f(":/theme/nocturne.qss");
    if (!f.open(QIODevice::ReadOnly)) { qWarning("theme stylesheet missing"); return; }
    QString qss = QString::fromUtf8(f.readAll());

    QColor acc = accent.isEmpty() ? Crimson : QColor(accent);
    if (!acc.isValid()) acc = Crimson;
    // Accent ramps stay readable on near-black: hi is for hover/borders, lo for pressed/fills.
    const QColor accHi = acc.lighter(135), accLo = acc.darker(170);

    QList<QPair<QString, QString>> tokens = {
        {"@void", Void.name()},           {"@crypt", Crypt.name()},       {"@obsidian", Obsidian.name()},
        {"@shadow", Shadow.name()},       {"@smoke", Smoke.name()},       {"@ash", Ash.name()},
        {"@bone", Bone.name()},           {"@parchment", Parchment.name()},
        {"@accenthi", accHi.name()},      {"@accentlo", accLo.name()},    {"@accent", acc.name()},
        {"@accentrgb", QStringLiteral("%1, %2, %3").arg(acc.red()).arg(acc.green()).arg(acc.blue())},
        {"@blood", Blood.name()},         {"@garnet", Garnet.name()},     {"@amethyst", Amethyst.name()},
        {"@plum", Plum.name()},           {"@silver", Silver.name()},     {"@gold", OldGold.name()},
        {"@verdigris", Verdigris.name()}, {"@amber", Amber.name()},       {"@fontscale", QString::number(fontPt)},
        {"@display", s_display},          {"@body", s_body},
    };
    // Longest tokens first so "@accent" can't eat "@accenthi".
    std::sort(tokens.begin(), tokens.end(), [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
    for (const auto &[k, v] : tokens) qss.replace(k, v);

    QFont base(s_body);
    base.setPointSize(fontPt);
    app.setFont(base);

    QPalette pal;
    pal.setColor(QPalette::Window, Crypt);
    pal.setColor(QPalette::WindowText, Bone);
    pal.setColor(QPalette::Base, Void);
    pal.setColor(QPalette::AlternateBase, Obsidian);
    pal.setColor(QPalette::Text, Bone);
    pal.setColor(QPalette::PlaceholderText, Ash);
    pal.setColor(QPalette::Button, Obsidian);
    pal.setColor(QPalette::ButtonText, Bone);
    pal.setColor(QPalette::BrightText, Parchment);
    pal.setColor(QPalette::ToolTipBase, Obsidian);
    pal.setColor(QPalette::ToolTipText, Bone);
    pal.setColor(QPalette::Highlight, acc);
    pal.setColor(QPalette::HighlightedText, Parchment);
    pal.setColor(QPalette::Link, accHi);
    pal.setColor(QPalette::LinkVisited, Amethyst.lighter(140));
    pal.setColor(QPalette::Disabled, QPalette::Text, Smoke);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, Smoke);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, Smoke);
    app.setPalette(pal);
    app.setStyleSheet(qss);
}

} // namespace bc::theme
