#!/usr/bin/env bash
set -euo pipefail

steam_root="$HOME/.local/share/Steam"
runtime="$steam_root/steamrtarm64"
platform=$(find "$runtime/pv-runtime/steam-runtime-steamrt-arm64" \
    -maxdepth 1 -type d -name 'steamrt3c_platform_*' -print -quit 2>/dev/null)
platform=${platform:+$platform/files}
export LD_LIBRARY_PATH="$runtime${platform:+:$platform/lib/aarch64-linux-gnu:$platform/lib}:${LD_LIBRARY_PATH-}"
export PROTON_NO_ESYNC=1 PROTON_NO_FSYNC=1 PROTON_NO_NTSYNC=1

# This launcher is retained for the optional legacy receiver, but its default
# presentation must still be Valve's native Steam Frame client.  The previous
# unconditional diagnostic flags selected the old 2D/CEF path and could also
# override a steamlink:// URI handoff.
if [ "${STEARLIGHT_STEAM_FRAME:-1}" != 0 ]; then
    case "${1-}" in
        steam://*|steamlink://*)
            uri=$1
            shift
            set -- -gamepadui -steamos3 -steampal -steamdeck -steamframe \
                   "$@" "$uri"
            ;;
        '')
            set -- -gamepadui -steamos3 -steampal -steamdeck -steamframe
            ;;
    esac
    set -- -noverifyfiles -nocrashmonitor -no-cef-sandbox \
           -cef-disable-sandbox -cef-disable-breakpad "$@"
    exec "$runtime/steam" "$@"
fi

exec "$runtime/steam" -gamepadui -720p -vrskip -vrdisable -fasthtml \
    -noverifyfiles -nocrashmonitor -no-cef-sandbox -cef-disable-sandbox \
    -cef-single-process -cef-disable-breakpad \
    -cef-disable-gpu -cef-disable-gpu-compositing \
    -cef-disable-js-logging -cef-disable-seccomp-sandbox "$@"
