#pragma once
#include <QImage>
#include <QString>
#include <QStringList>

namespace bc {

// "kova_shape_transition": a luma-wipe transition whose wipe pattern is generated
// in memory (no image files), with an optional glowing edge. Shapes:
//   cathedral    a pointed arch rises from the bottom of the screen
//   moonrise     moonlight spreads from the upper right with a ragged edge
//   bloodfall    the new scene pours down from the top, drips first
//   stainedglass shards of glass fall into place one by one
//   thorns       creeping, thorny growth sweeps in from the left
//   wingsweep    a scalloped bat-wing edge sweeps across
inline constexpr const char *kShapeTransitionId = "kova_shape_transition";

void registerShapeTransition();
void unregisterShapeTransition();   // libobs frees registrations on obs_shutdown
QStringList shapeTransitionShapes();
// The wipe pattern for a shape: pixel value = moment (0..255) the new scene appears there.
QImage shapeTransitionPattern(const QString &shape, int width, int height);

} // namespace bc
