#!/usr/bin/env bash
# Host-side renderer for the CYD-Claudemon landscape UI.
# Compiles the real src/ui_*.h page code against a mock TFT_eSPI and writes a
# 320x240 PNG of every page to out/ — no Wokwi, no hardware, no cloud, instant.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
clang++ -std=c++17 -O1 -DFAKE_DATA=1 -DCLAUDEMON_VERSION='"0.1.0"' \
  -I . -I ../../src -Wno-deprecated-declarations \
  render.cpp -lz -o render
./render
echo "wrote PNGs to $(pwd)/out"

# Optional: stitch every page into one labelled contact sheet for at-a-glance
# review. Needs ImageMagick (`brew install imagemagick`); skipped if absent so
# the core renderer keeps its zero-dependency promise.
if command -v montage >/dev/null 2>&1; then
  ( cd out && montage \
      1_usage.png 2_cost.png 3_models.png 4_trend.png \
      6_clock.png 7_system.png 8_settings.png \
      alt_usage.png alt_cost.png alt_usage_expired.png alt_usage_nodata.png \
      -tile 4x -geometry +8+10 -background '#101010' \
      -bordercolor '#333333' -border 1 \
      -label '%t' -fill '#bbbbbb' -pointsize 12 \
      contact.png )
  echo "wrote contact sheet $(pwd)/out/contact.png"
else
  echo "(install ImageMagick for a single-image contact sheet: brew install imagemagick)"
fi
