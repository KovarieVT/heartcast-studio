#!/usr/bin/env bash
# Regenerates all raster branding from resources/icons/logo.svg.
# Needs: rsvg-convert (librsvg2-bin), ImageMagick 6/7.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SVG="$ROOT/resources/icons/logo.svg"
ICO_DIR="$ROOT/resources/windows"
INST="$ROOT/installer/assets"
TMP="$(mktemp -d)"
IM=$(command -v magick || command -v convert)

for s in 16 24 32 48 64 128 256 512; do
  rsvg-convert -w $s -h $s "$SVG" -o "$TMP/logo_$s.png"
done
cp "$TMP/logo_256.png" "$ROOT/resources/icons/logo_256.png"
cp "$TMP/logo_512.png" "$ROOT/resources/icons/logo_512.png"

# Multi-resolution Windows icon (exe, taskbar, window, shortcut, installer)
$IM "$TMP"/logo_{16,24,32,48,64,128,256}.png "$ICO_DIR/app.ico"
cp "$ICO_DIR/app.ico" "$INST/setup.ico"

# Inno Setup wizard side image (several DPI sizes) — pastel gradient + logo + sparkles
make_side () { # w h logo
  local w=$1 h=$2 l=$3 out=$4
  $IM -size ${w}x${h} gradient:'#FFDCEB'-'#D2F3E7' \
     \( "$TMP/logo_512.png" -resize ${l}x${l} \) -gravity center -geometry +0-$((h/8)) -composite \
     -fill '#FFFFFF' -stroke '#DCCCF7' -strokewidth 1 \
     -draw "circle $((w/6)),$((h/8)) $((w/6+3)),$((h/8))" \
     -draw "circle $((w*5/6)),$((h*3/10)) $((w*5/6+4)),$((h*3/10))" \
     -draw "circle $((w/4)),$((h*4/5)) $((w/4+3)),$((h*4/5))" \
     -fill '#E98DB7' -stroke none -draw "circle $((w*3/4)),$((h*7/8)) $((w*3/4+3)),$((h*7/8))" \
     -type TrueColor BMP3:"$out"
}
make_side 164 314 120 "$INST/wizard_side_100.bmp"
make_side 205 392 150 "$INST/wizard_side_125.bmp"
make_side 246 471 180 "$INST/wizard_side_150.bmp"
make_side 328 628 240 "$INST/wizard_side_200.bmp"

make_small () { local s=$1 out=$2
  $IM -size ${s}x${s} xc:'#FFF9FC' \( "$TMP/logo_256.png" -resize $((s-4))x$((s-4)) \) -gravity center -composite -type TrueColor BMP3:"$out"; }
make_small 55 "$INST/wizard_small_100.bmp"
make_small 69 "$INST/wizard_small_125.bmp"
make_small 83 "$INST/wizard_small_150.bmp"
make_small 110 "$INST/wizard_small_200.bmp"

rm -rf "$TMP"
echo "Assets regenerated."
