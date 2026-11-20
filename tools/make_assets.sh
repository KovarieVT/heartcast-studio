#!/usr/bin/env bash
# Regenerates all raster branding and the bundled stinger videos from the same
# vector code the app draws with (src/ui/GothicPainter.cpp, tools/art/render_art.cpp).
# Needs: a build with -DKOVALIVE_BUILD_ART_TOOL=ON, ImageMagick 6/7, ffmpeg with libvpx-vp9.
#
#   tools/make_assets.sh <build-dir>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:?usage: make_assets.sh <build-dir with render_art>}"
ICO_DIR="$ROOT/resources/windows"
INST="$ROOT/installer/assets"
TMP="$(mktemp -d)"
IM=$(command -v magick || command -v convert)

QT_QPA_PLATFORM=offscreen "$BUILD/render_art" "$TMP"   # exits non-zero if a stinger fails its coverage check

cp "$TMP/logo.svg" "$ROOT/resources/icons/logo.svg"
cp "$TMP/logo_256.png" "$ROOT/resources/icons/logo_256.png"
cp "$TMP/logo_512.png" "$ROOT/resources/icons/logo_512.png"

# Multi-resolution Windows icon (exe, taskbar, window, shortcut, installer)
$IM "$TMP"/logo_{16,20,24,32,40,48,64,96,128,256}.png "$ICO_DIR/app.ico"
cp "$ICO_DIR/app.ico" "$INST/setup.ico"

# Inno Setup wizard images at 100/125/150/200 % DPI (24-bit BMP)
i=0; for w in 164 205 246 328; do pct=(100 125 150 200); $IM "$TMP/wizard_side_$w.png" -type TrueColor BMP3:"$INST/wizard_side_${pct[$i]}.bmp"; i=$((i+1)); done
i=0; for s in 55 69 83 110; do pct=(100 125 150 200); $IM "$TMP/wizard_small_$s.png" -type TrueColor BMP3:"$INST/wizard_small_${pct[$i]}.bmp"; i=$((i+1)); done

# Stingers: VP9 with alpha (OBS decodes these with libvpx when alpha_mode=1).
# render_art already verified that each one fully covers the screen at its cut point.
rm -f "$ROOT"/resources/stingers/*.webm
cp "$TMP/stingers/stingers.json" "$ROOT/resources/stingers/stingers.json"
python3 -c 'import json,sys; [print(s["id"], s["file"]) for s in json.load(open(sys.argv[1]))["stingers"]]' "$TMP/stingers/stingers.json" |
while read -r id file; do
  ffmpeg -nostdin -hide_banner -loglevel error -y -framerate 30 -i "$TMP/stingers/$id/f%03d.png" \
    -c:v libvpx-vp9 -pix_fmt yuva420p -b:v 0 -crf 34 -deadline good -cpu-used 2 -row-mt 1 \
    -auto-alt-ref 0 -metadata:s:v:0 alpha_mode=1 "$ROOT/resources/stingers/$file"
  echo "encoded $file"
done

rm -rf "$TMP"
echo "Assets regenerated."
