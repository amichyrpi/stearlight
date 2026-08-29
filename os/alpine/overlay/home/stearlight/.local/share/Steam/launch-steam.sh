#!/bin/sh
set -eu

runtime=/opt/stearlight/steam-runtime
home=/home/stearlight
steam_root="$home/.local/share/Steam"
platform=$(find "$steam_root/steamrtarm64/pv-runtime/steam-runtime-steamrt-arm64" \
  -maxdepth 1 -type d -name 'steamrt3c_platform_*' -print -quit 2>/dev/null || true)
platform=${platform:+$platform/files}

exec bwrap --die-with-parent --new-session \
  --bind "$runtime" / \
  --dev-bind /dev /dev --proc /proc --ro-bind /sys /sys \
  --bind /run /run --bind /tmp /tmp --bind "$home" "$home" \
  --setenv HOME "$home" --setenv USER stearlight --setenv LOGNAME stearlight \
  --setenv DISPLAY "${DISPLAY:-:8}" \
  --setenv LD_LIBRARY_PATH "$steam_root/steamrtarm64${platform:+:$platform/lib/aarch64-linux-gnu:$platform/lib}" \
  --setenv PROTON_NO_ESYNC 1 --setenv PROTON_NO_FSYNC 1 \
  -- "$steam_root/steamrtarm64/steam" \
  -gamepadui -720p -vrskip -vrdisable -fasthtml -noverifyfiles \
  -nocrashmonitor -no-cef-sandbox -cef-disable-sandbox \
  -cef-single-process -cef-disable-breakpad -cef-disable-gpu \
  -cef-disable-gpu-compositing -cef-disable-js-logging "$@"
