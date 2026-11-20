#pragma once
#include <QColor>
#include <QString>

class QApplication;

// "Nocturne": KovaLive!'s gothic theme. Near-black surfaces, bone text, crimson
// accents and old-gold ornaments. Headings use Cinzel, body text uses Inter
// (both bundled, SIL Open Font License).
namespace bc::theme {

inline const QColor Void      {0x0B, 0x0A, 0x0D};   // window background
inline const QColor Crypt     {0x14, 0x11, 0x18};   // panels
inline const QColor Obsidian  {0x1D, 0x19, 0x22};   // raised surfaces, inputs
inline const QColor Shadow    {0x2E, 0x27, 0x33};   // borders
inline const QColor Smoke     {0x4A, 0x40, 0x50};   // strong borders, disabled
inline const QColor Ash       {0x8E, 0x84, 0x92};   // muted text
inline const QColor Bone      {0xE6, 0xDE, 0xD2};   // text
inline const QColor Parchment {0xF3, 0xEC, 0xDF};   // bright text
inline const QColor Crimson   {0x9E, 0x1B, 0x32};   // primary accent (default)
inline const QColor Blood     {0xC8, 0x28, 0x3F};   // live, hover, alerts
inline const QColor Garnet    {0x5C, 0x0E, 0x1D};   // deep accent
inline const QColor Amethyst  {0x6E, 0x3B, 0x8A};   // secondary accent
inline const QColor Plum      {0x33, 0x1A, 0x40};   // secondary deep
inline const QColor Silver    {0xB4, 0xAE, 0xB8};   // moonlight, ornaments
inline const QColor OldGold   {0xB8, 0x93, 0x5A};   // headings, filigree
inline const QColor Verdigris {0x3F, 0x8F, 0x7A};   // success
inline const QColor Amber     {0xC6, 0x8E, 0x2F};   // warning

inline constexpr const char *kThemeId = "nocturne";
inline constexpr const char *kDefaultAccent = "#9E1B32";

// Bundled font families (registered by loadFonts()).
QString displayFamily();      // Cinzel: headings, dock titles
QString decorativeFamily();   // Cinzel Decorative: wordmark, splash
QString bodyFamily();         // Inter: everything else
void loadFonts();

// Loads the stylesheet, substitutes palette tokens, applies it to the app.
// `accent` overrides the crimson accent; `fontPt` scales the base font.
void apply(QApplication &app, const QString &accent = {}, int fontPt = 10);

} // namespace bc::theme
