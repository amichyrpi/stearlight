#!/usr/bin/env bash
set -euo pipefail

# This helper runs in the x86_64 VM image build stage as the unprivileged
# Steam user.  It performs Valve's normal bootstrap/update once, then leaves
# the resulting Gamepad UI in the image.  Keeping the client payload in the
# image means a new VM reaches Steam's language/network/account welcome flow
# without downloading the same half-gigabyte archive on every test boot.

launcher=${1:?usage: preextract-steam-client.sh /path/to/steam}
home=${HOME:?HOME must name the Steam staging user}
steam_root="$home/.local/share/Steam"
sanitizer=${STEARLIGHT_STEAM_PROFILE_SANITIZER:-sanitize-steam-profile.sh}
timeout_seconds=${STEARLIGHT_STEAM_PREEXTRACT_TIMEOUT:-1200}
log_file="$home/stearlight-steam-preextract.log"
bootstrap_config="$home/.config/gamescope/bootstrap.cfg"
build_bin="$home/.cache/stearlight-steam-build-bin"

mkdir -p "$steam_root/package" "$steam_root/steamapps" "$home/.steam"
mkdir -p "$(dirname "$bootstrap_config")"
touch "$bootstrap_config"
ln -sfn "$steam_root" "$home/.steam/root"
ln -sfn "$steam_root" "$home/.steam/steam"
printf '%s\n' steamdeck_stable > "$steam_root/package/beta"
mkdir -p "$steam_root/config" "$home/.config/gamescope"
touch "$steam_root/config/SteamAppData.vdf"
mkdir -p "$build_bin"

if [ ! -x "$launcher" ]; then
    echo "Steam launcher is missing: $launcher" >&2
    exit 1
fi

Xvfb :99 -screen 0 1024x640x24 +extension GLX +iglx \
    -nolisten tcp -noreset >"$home/stearlight-xvfb.log" 2>&1 &
xvfb_pid=$!
steam_pid=''
patched_checks=()
patch_userns_probe() {
    local check
    while IFS= read -r check; do
        [ -f "$check" ] || continue
        [ -x "$check" ] || continue
        cp -p "$check" "$check.stearlight-real"
        printf '%s\n' '#!/bin/sh' 'exit 0' >"$check"
        chmod 0755 "$check"
        patched_checks+=("$check")
    done < <(find "$steam_root/ubuntu12_32/steam-runtime" -type f \
        -name 'steam-runtime-check-requirements' -print 2>/dev/null || true)
}
restore_userns_probe() {
    local check
    for check in "${patched_checks[@]}"; do
        if [ -f "$check.stearlight-real" ]; then
            mv -f "$check.stearlight-real" "$check"
        fi
    done
    patched_checks=()
}
cleanup() {
    restore_userns_probe
    if [ -n "$steam_pid" ] && kill -0 "$steam_pid" 2>/dev/null; then
        kill -TERM -- "-$steam_pid" 2>/dev/null || kill -TERM "$steam_pid" 2>/dev/null || true
        sleep 2
        kill -KILL -- "-$steam_pid" 2>/dev/null || kill -KILL "$steam_pid" 2>/dev/null || true
    fi
    kill "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# setsid gives the updater and its client children a private process group so
# the build never leaves a Steam process behind in the image.  Keeping this in
# a function also lets us retry once after the initial bootstrap extraction;
# the first invocation can exit immediately before steam.sh exists.
launch_steam() {
    build_path="${PATH:-/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin}"
    if [ "$retry_count" -gt 0 ]; then
        build_path="$build_bin:$build_path"
    fi
    setsid env HOME="$home" USER=steam LOGNAME=steam DISPLAY=:99 \
        PATH="$build_path" \
        XDG_SESSION_TYPE=x11 XDG_CURRENT_DESKTOP=gamescope \
        STEAM_BOOTSTRAP_CONFIG="$bootstrap_config" \
        STEAMOS=1 STEAM_RUNTIME=1 STEAM_GAMESCOPE=1 STEAM_GAMEPADUI=1 \
        STEAM_GAMESCOPE_VRR_SUPPORTED=1 STEAM_GAMESCOPE_HAS_TEARING_SUPPORT=1 \
        STEAM_GAMESCOPE_TEARING_SUPPORTED=1 STEAM_GAMESCOPE_DYNAMIC_FPSLIMITER=1 \
        STEAM_GAMESCOPE_NIS_SUPPORTED=1 STEAM_MULTIPLE_XWAYLANDS=1 \
        STEAM_ENABLE_VOLUME_HANDLER=1 STEAM_ALLOW_DRIVE_UNMOUNT=1 \
        SRT_URLOPEN_PREFER_STEAM=1 STEAM_DISABLE_AUDIO_DEVICE_SWITCHING=1 \
        LIBGL_ALWAYS_SOFTWARE=1 \
        "$launcher" -gamepadui -steamos3 -steampal -steamdeck -exitsteam -720p \
        -vrskip -vrdisable -nocrashmonitor -no-cef-sandbox \
        -cef-disable-sandbox -disable-gpu -cef-disable-gpu \
        >>"$log_file" 2>&1 &
    steam_pid=$!
}

retry_count=0
launch_steam

deadline=$((SECONDS + timeout_seconds))
ready=0
steam_exit_seen=''
while [ "$SECONDS" -lt "$deadline" ]; do
    # The bootstrap creates steam.sh and ubuntu12_32/steam first.  The Gamepad UI
    # payload is only considered complete once steamui has a non-empty file.
    steamui_file=''
    if [ -d "$steam_root/steamui" ]; then
        steamui_file=$(find "$steam_root/steamui" -type f -size +1c \
            -print -quit 2>/dev/null || true)
    fi
    if { [ -x "$steam_root/steam.sh" ] ||
         [ -x "$steam_root/ubuntu12_32/steam" ]; } &&
       [ -n "$steamui_file" ]; then
        ready=1
        break
    fi
    if ! kill -0 "$steam_pid" 2>/dev/null; then
        # The launcher can exit once while installing the bootstrap.  Retry
        # exactly once after the files appear, then fail fast instead of
        # silently sleeping for the entire 20-minute extraction timeout.
        if [ "$retry_count" -eq 0 ] &&
           { [ -x "$steam_root/steam.sh" ] ||
             [ -x "$steam_root/ubuntu12_32/steam" ]; }; then
            retry_count=1
            # Docker's build kernel commonly denies the namespace probe even
            # when the target VM kernel will allow it.  On the second pass,
            # shadow only the probe command in PATH; this does not alter the
            # Steam payload copied into the final image.
            printf '%s\n' '#!/bin/sh' 'exit 0' \
                >"$build_bin/steam-runtime-check-requirements"
            chmod 0755 "$build_bin/steam-runtime-check-requirements"
            patch_userns_probe
            launch_steam
            steam_exit_seen=''
            sleep 1
            continue
        fi
        if [ -z "$steam_exit_seen" ]; then
            steam_exit_seen=$SECONDS
        elif [ $((SECONDS - steam_exit_seen)) -ge 15 ]; then
            break
        fi
    else
        steam_exit_seen=''
    fi
    sleep 1
done

if [ "$ready" -ne 1 ]; then
    echo 'Steam Gamepad UI was not extracted before the build timeout.' >&2
    tail -100 "$log_file" >&2 || true
    for steam_log in "$steam_root"/logs/*.txt; do
        [ -f "$steam_log" ] || continue
        tail -100 "$steam_log" >&2 || true
    done
    restore_userns_probe
    exit 1
fi

# Downloaded update archives are not needed after installation and can be
# hundreds of megabytes.  Keep metrics and the beta choice so Steam can still
# self-update and preserve the first-run welcome state.
find "$steam_root/package" -maxdepth 1 -type f \
    \( -name '*.zip' -o -name '*.partial' -o -name '*.tmp' -o -name '*.blob' \) \
  -delete 2>/dev/null || true
rm -rf "$steam_root/logs" "$steam_root/appcache/httpcache" \
       "$steam_root/appcache/cefdata" "$steam_root/config/htmlcache"
restore_userns_probe
rm -rf "$build_bin"

# The build-stage Steam process is deliberately run far enough to download
# Valve's client, but it must never leave the staging account selected. Keep
# SteamAppData.vdf and the downloaded client payload intact while removing the
# account/session state that would suppress Steam's first-run wizard.
if command -v "$sanitizer" >/dev/null 2>&1; then
    "$sanitizer" "$steam_root"
else
    # This fallback keeps the script self-contained for callers that copy only
    # preextract-steam-client.sh into a build container.
    rm -f "$steam_root/config/loginusers.vdf" \
          "$steam_root/config/localconfig.vdf" \
          "$steam_root/config/sharedconfig.vdf" \
          "$steam_root/config/registeruserconfig.vdf" \
          "$steam_root/config/configsetttings.vdf"
    rm -rf "$steam_root/userdata"
    find "$steam_root" -maxdepth 1 -type f -name 'ssfn*' -delete 2>/dev/null || true
fi
touch "$steam_root/.stearlight-prepared"
grep -q '^set_bootstrap=1$' "$bootstrap_config" ||
    printf '%s\n' set_bootstrap=1 >> "$bootstrap_config"
echo 'Steam Gamepad UI extraction complete.'
