#!/usr/bin/env bash
# Host test for the subscription data source — see test_sub.cpp.
# Reuses the render tool's Arduino/Preferences mocks and the ArduinoJson copy
# PlatformIO already fetched (run `pio run` once if .pio/libdeps is missing).
set -euo pipefail
cd "$(dirname "$0")"

AJ=$(ls -d ../../.pio/libdeps/*/ArduinoJson/src 2>/dev/null | head -1)
if [ -z "${AJ}" ]; then
  echo "ArduinoJson not found — run 'pio run' once to fetch it" >&2
  exit 1
fi

c++ -std=c++17 -g -O0 -Wall -I. -I../render -I../../src -I"${AJ}" \
    test_sub.cpp -o test_sub
./test_sub
