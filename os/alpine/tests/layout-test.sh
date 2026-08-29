#!/usr/bin/env bash
set -euo pipefail
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

grep -qx 'ROOT' "$base/overlay/etc/hostname"
grep -q 'disable_splash=1' "$base/boot/config.txt"
grep -q 'logo.nologo' "$base/boot/cmdline.txt"
grep -q 'console=tty12' "$base/boot/cmdline.txt"
test -f "$base/overlay/etc/inittab"
if grep -Eq '^[[:space:]]*tty[0-9]+::.*getty' "$base/overlay/etc/inittab"; then
  echo "A local password getty must not be enabled in Stearlight OS" >&2
  exit 1
fi
grep -q 'STEARLIGHT_EYE_WIDTH="1440"' "$base/overlay/etc/conf.d/stearlight"
grep -q 'STEARLIGHT_EYE_HEIGHT="1600"' "$base/overlay/etc/conf.d/stearlight"
grep -q 'SVRT_USE_GAMESCOPE=1' \
  "$base/overlay/usr/local/libexec/stearlight/session"
grep -q 'SVRT_STEARLIGHT_OS=ON' "$base/Dockerfile"
grep -q 'SVRT_ENABLE_DEBUG_LEFT_EYE_UI=0' \
  "$base/../../pi-receiver/CMakeLists.txt"
grep -q 'os/os_boot.mp4' "$base/../../pi-receiver/CMakeLists.txt"
grep -q '3127680' "$base/scripts/download-steam-compat.sh"
grep -q '4628740' "$base/scripts/download-steam-compat.sh"
grep -q 'STEARLIGHT_BUILD_DATE' "$base/build.sh"
grep -q 'ExpectedSerial' "$base/flash-windows.ps1"
grep -q 'supervisor="supervise-daemon"' \
  "$base/overlay/etc/init.d/stearlight-session"
grep -q 'addgroup stearlight tty' "$base/Dockerfile"
grep -q 'rc-update add avahi-daemon default' "$base/Dockerfile"
grep -q 'video decode failed' "$base/../../gui/ui.c"
if grep -q 'SDL_RENDER_DRIVER=' \
  "$base/overlay/usr/local/libexec/stearlight/session"; then
  echo "The display session must let SDL select the available KMS renderer" >&2
  exit 1
fi
test -x "$base/overlay/etc/init.d/stearlight-wifi" || \
  test -f "$base/overlay/etc/init.d/stearlight-wifi"

for script in \
  "$base/build.sh" "$base/apply-quiet-eeprom.sh" \
  "$base/scripts/install-steam-arm64.sh" \
  "$base/scripts/download-steam-compat.sh" \
  "$base/overlay/usr/local/libexec/stearlight/provision-wifi" \
  "$base/overlay/etc/init.d/stearlight-wifi" \
  "$base/overlay/usr/local/libexec/stearlight/session" \
  "$base/overlay/home/stearlight/.local/share/Steam/launch-steam.sh"; do
  bash -n "$script"
done

echo "Stearlight OS layout checks passed"
