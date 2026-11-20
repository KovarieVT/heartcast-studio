#pragma once
#include <QIcon>
#include <QString>

namespace bc::icons {

// Small vector icons for dock buttons, drawn with QPainter in the theme's bone colour
// (crimson when checked/active). Using painted icons instead of Unicode symbols keeps
// them identical on every Windows font setup.
//
// Names: add, remove, duplicate, up, down, gear, filters, speaker, mute, headphones,
//        eye, eye-off, lock, unlock, pause, play, save, more
QIcon get(const QString &name);
// Same drawing in one fixed colour for every mode/state (item views flip QIcon::On for
// expanded rows, so state-dependent icons would change colour there).
QIcon flat(const QString &name, const QColor &color);

} // namespace bc::icons
