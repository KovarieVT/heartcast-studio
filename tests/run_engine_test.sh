#!/usr/bin/env bash
# Runs the libobs integration test on Linux: virtual X display + two local
# RTMP servers (ffmpeg listeners) to prove multistreaming end to end.
# Usage: tests/run_engine_test.sh <build-dir>
set -u
BUILD="${1:-build}"
OUT="$(mktemp -d)"
export LIBGL_ALWAYS_SOFTWARE=1
Xvfb :97 -screen 0 1280x800x24 >/dev/null 2>&1 & XV=$!
sleep 1
export DISPLAY=:97 QT_QPA_PLATFORM=xcb
ffmpeg -loglevel error -listen 1 -i rtmp://127.0.0.1:19350/live/keya -c copy -f flv "$OUT/a.flv" & FA=$!
ffmpeg -loglevel error -listen 1 -i rtmp://127.0.0.1:19351/live/keyb -c copy -f flv "$OUT/b.flv" & FB=$!
sleep 1
export HC_RTMP_A=rtmp://127.0.0.1:19350/live HC_RTMP_B=rtmp://127.0.0.1:19351/live
"$BUILD/tests/kovalive_engine_test" -platform xcb
RC=$?
sleep 2
kill $FA $FB 2>/dev/null; wait $FA $FB 2>/dev/null
for f in a b; do
  if [ -s "$OUT/$f.flv" ]; then
    echo "RTMP server $f received: $(ffprobe -v error -show_entries stream=codec_name,width,height -of csv=p=0 "$OUT/$f.flv" | tr '\n' ' ') ($(stat -c%s "$OUT/$f.flv") bytes)"
  else
    echo "RTMP server $f received nothing"; RC=1
  fi
done
kill $XV 2>/dev/null
exit $RC
