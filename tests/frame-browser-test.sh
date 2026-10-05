#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/stearlight-frame-browser.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM
root="$tmp/steam"
mkdir -p "$root/steamrtarm64"
wrapper="$root/steamrtarm64/steamwebhelper.sh"
prepare="$base/system_files/usr/local/libexec/stearlight/frame-browser"
printf '#!/bin/bash\nexit 0\n' > "$wrapper"
cp "$wrapper" "$tmp/expected"

# Default preparation leaves a fresh recovery alone and restores a saved
# launcher byte-for-byte after an optional GPU session.
STEARLIGHT_STEAM_ROOT="$root" sh "$prepare" 0
cmp "$wrapper" "$tmp/expected"
cp "$wrapper" "$wrapper.stearlight-original"
printf '#!/bin/bash\nexit 1\n' > "$wrapper"
STEARLIGHT_STEAM_ROOT="$root" sh "$prepare" 0
STEARLIGHT_STEAM_ROOT="$root" sh "$prepare" 0
cmp "$wrapper" "$tmp/expected"

# An unknown recovery and an invalid mode cannot replace the launcher.
printf 'different ANGLE build\n' > "$root/steamrtarm64/libEGL.so"
if STEARLIGHT_STEAM_ROOT="$root" sh "$prepare" 1 2>"$tmp/error"; then
    echo 'Unexpected acceptance of an unknown ANGLE build.' >&2
    exit 1
fi
grep -q 'does not match' "$tmp/error"
cmp "$wrapper" "$tmp/expected"
if STEARLIGHT_STEAM_ROOT="$root" sh "$prepare" 2 2>"$tmp/error"; then exit 1; fi
cmp "$wrapper" "$tmp/expected"
echo 'Frame browser default, restore and recovery guard checks passed'
