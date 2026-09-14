#!/bin/sh
set -eu
if [ "${SVRT_LEGACY_H265:-0}" != 1 ]; then
  echo "This script tests the optional legacy H.265 receiver." >&2
  echo "Use the Stearlight OS image/VM checks for the native Steam client path." >&2
  echo "Set SVRT_LEGACY_H265=1 only when intentionally testing legacy code." >&2
  exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test "$(uname -m)" = aarch64
test -e /dev/dri/card0
test -e /dev/dri/renderD128
ffmpeg -hide_banner -buildconf 2>&1 | grep -q -- '--enable-v4l2-request'
ffmpeg -hide_banner -buildconf 2>&1 | grep -q -- '--enable-libdrm'
cmake -S "$root" -B "$root/build-pi" -DSVRT_BUILD_DRIVER=OFF \
  -DSVRT_BUILD_RECEIVER=ON -DSVRT_BUILD_TESTS=ON \
  -DSVRT_BUILD_VENDORED_SDL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build-pi" -j4
ctest --test-dir "$root/build-pi" --output-on-failure
"$root/build-pi/pi-receiver/svrt-receiver" --help
sh "$root/tests/pi_4k60_test.sh" "$root/build-pi/pi-receiver/svrt-receiver"
