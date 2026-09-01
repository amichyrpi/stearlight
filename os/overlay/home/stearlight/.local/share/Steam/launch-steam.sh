#!/bin/sh
set -eu

runtime=/opt/stearlight/steam-runtime
home=/home/stearlight
steam_root="$home/.local/share/Steam"

# New Pi images carry Valve's native ARM64 client.  Do not wrap it in the
# legacy x86 bwrap root (that root deliberately exists only for the VM); doing
# so hides steamrtarm64's loader and prevents the real SteamOS OOBE from
# starting.  Keep this file as a compatibility entry point for older images.
if [ -x /usr/local/libexec/stearlight/launch-steam ] &&
   [ -x "$steam_root/steamrtarm64/steam" ]; then
    exec /usr/local/libexec/stearlight/launch-steam "$@"
fi

# Keep the same first-run contract as SteamOS' gamescope-session.  Steam owns
# language, timezone, network, update, sign-in and tour pages; this launcher
# only supplies the runtime boundary and starts Valve's client.
export STEAMOS=1
export STEAM_RUNTIME=1
export STEAMOS_OOBE=1
export STEAMOS_OOBE_IMAGE=1
export STEAM_USE_MANGOAPP=1
export STEAM_DISABLE_MANGOAPP_ATOM_WORKAROUND=1
export CLIENTCMD="steam -gamepadui -steamos3 -steampal -steamdeck"
export CURSOR_FILE="$steam_root/tenfoot/resource/images/cursors/arrow.png"

# Valve's SteamOS first-run pages call these helpers from inside the runtime.
# The runtime is intentionally isolated, so expose only the small allowlisted
# commands instead of the host's complete /usr tree.
set -f
bwrap_extra=
for helper in \
    /usr/bin/jupiter-biosupdate \
    /usr/bin/jupiter-dock-updater \
    /usr/bin/pkexec \
    /usr/bin/steamos-update \
    /usr/bin/steamos-select-branch \
    /usr/bin/steamos-session-select \
    /usr/bin/timedatectl \
    /usr/bin/localectl \
    /usr/bin/steamos-set-timezone \
    /usr/bin/steamos-devkit-mode; do
    if [ -e "$helper" ]; then
        bwrap_extra="$bwrap_extra --ro-bind $helper $helper"
    fi
done
if [ -d /usr/bin/steamos-polkit-helpers ]; then
    bwrap_extra="$bwrap_extra --ro-bind /usr/bin/steamos-polkit-helpers /usr/bin/steamos-polkit-helpers"
fi
if [ -d /usr/share/zoneinfo ]; then
    bwrap_extra="$bwrap_extra --ro-bind /usr/share/zoneinfo /usr/share/zoneinfo"
fi
if [ -d /etc/ssl/certs ]; then
    # Valve's account and update pages use HTTPS from inside the glibc
    # runtime. Keep the distro CA bundle visible without exposing /etc.
    bwrap_extra="$bwrap_extra --ro-bind /etc/ssl/certs /etc/ssl/certs"
fi
if [ -d /etc/first-boot ]; then
    bwrap_extra="$bwrap_extra --ro-bind /etc/first-boot /etc/first-boot"
fi
platform=$(find "$steam_root/steam-runtime-steamrt-arm64" \
  -maxdepth 1 -type d -name 'steamrt3c_platform_*' -print -quit 2>/dev/null || true)
if [ -z "$platform" ]; then
  platform=$(find "$steam_root/steamrtarm64/pv-runtime/steam-runtime-steamrt-arm64" \
    -maxdepth 1 -type d -name 'steamrt3c_platform_*' -print -quit 2>/dev/null || true)
fi
platform=${platform:+$platform/files}

exec bwrap --die-with-parent --new-session \
  --bind "$runtime" / \
  --dev-bind /dev /dev --proc /proc --ro-bind /sys /sys \
  --bind /run /run --bind /tmp /tmp --bind "$home" "$home" \
  --setenv HOME "$home" --setenv USER stearlight --setenv LOGNAME stearlight \
  --setenv DISPLAY "${DISPLAY:-:8}" \
  --setenv LD_LIBRARY_PATH "$steam_root/steamrtarm64${platform:+:$platform/lib/aarch64-linux-gnu:$platform/lib}" \
  --setenv PROTON_NO_ESYNC 1 --setenv PROTON_NO_FSYNC 1 \
  $bwrap_extra \
  -- "$steam_root/steamrtarm64/steam" \
  -gamepadui -720p -vrskip -vrdisable -noverifyfiles \
  -nocrashmonitor -no-cef-sandbox -cef-disable-sandbox \
  -cef-disable-breakpad -disable-gpu -cef-disable-gpu "$@"
