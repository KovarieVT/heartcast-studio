#pragma once
#include <QColor>
#include <QString>

class QApplication;

namespace bc::theme {

inline const QColor CottonCandy {0xF5, 0xA6, 0xC8};
inline const QColor PastelRose  {0xF7, 0xC4, 0xD8};
inline const QColor BabyPink    {0xFF, 0xDC, 0xEB};
inline const QColor PastelMint  {0xA9, 0xE5, 0xD0};
inline const QColor SoftMint    {0xD2, 0xF3, 0xE7};
inline const QColor PearlWhite  {0xFF, 0xF9, 0xFC};
inline const QColor Lavender    {0xDC, 0xCC, 0xF7};
inline const QColor SoftLilac   {0xED, 0xE4, 0xFF};
inline const QColor BlushPink   {0xE9, 0x8D, 0xB7};
inline const QColor DeepRose    {0xC9, 0x6F, 0x9D};
inline const QColor MintAccent  {0x77, 0xCD, 0xB1};
inline const QColor Charcoal    {0x51, 0x48, 0x52};
inline const QColor LightGray   {0xF5, 0xF1, 0xF5};

// Loads kawaii.qss, substitutes palette tokens, applies to the app.
// `accent` overrides the primary pink; `fontPt` scales the base font.
void apply(QApplication &app, const QString &accent = {}, int fontPt = 10);

} // namespace bc::theme
