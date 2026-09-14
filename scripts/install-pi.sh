#!/bin/sh
set -eu
if [ "${SVRT_LEGACY_H265:-0}" != 1 ]; then
  echo "This script builds the optional legacy H.265 receiver." >&2
  echo "Use os/build.sh for the native Steam client/Steam Frame path." >&2
  echo "Set SVRT_LEGACY_H265=1 only when intentionally testing legacy code." >&2
  exit 2
fi
src_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="${src_dir}/build-pi"
if ! dpkg-query -W -f='${Status}' libasound2-dev 2>/dev/null | grep -q 'install ok installed'; then
  echo "Install the audio development package first: sudo apt install -y libasound2-dev" >&2
  exit 1
fi
cmake -S "$src_dir" -B "$build_dir" -DSVRT_BUILD_DRIVER=OFF \
  -DSVRT_BUILD_RECEIVER=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j4
echo "Built legacy receiver ${build_dir}/pi-receiver/svrt-receiver"
