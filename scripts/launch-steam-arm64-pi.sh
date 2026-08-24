#!/usr/bin/env bash
set -euo pipefail

steam_root="$HOME/.local/share/Steam"
runtime="$steam_root/steamrtarm64"
platform=$(find "$runtime/pv-runtime/steam-runtime-steamrt-arm64" \
    -maxdepth 1 -type d -name 'steamrt3c_platform_*' -print -quit 2>/dev/null)
platform=${platform:+$platform/files}
export LD_LIBRARY_PATH="$runtime${platform:+:$platform/lib/aarch64-linux-gnu:$platform/lib}:${LD_LIBRARY_PATH-}"
export PROTON_NO_ESYNC=1 PROTON_NO_FSYNC=1 PROTON_NO_NTSYNC=1

exec "$runtime/steam" -gamepadui -720p -vrskip -vrdisable -fasthtml \
    -noverifyfiles -nocrashmonitor -no-cef-sandbox -cef-disable-sandbox \
    -cef-single-process -cef-disable-breakpad \
    -cef-disable-gpu -cef-disable-gpu-compositing \
    -cef-disable-js-logging -cef-disable-seccomp-sandbox "$@"
