#!/usr/bin/env bash
set -euo pipefail

# Install Valve's native ARM64 Steam seed and Steam Runtime during the image
# build. The ARM seed is a content-addressed Valve CDN object; its URL can be
# overridden when Valve publishes a newer seed. Steam itself still owns
# first-run language, timezone, network, update, sign-in and tour setup.

steam_root="${HOME}/.local/share/Steam"
runtime="${steam_root}/steamrtarm64"
work="${HOME}/.cache/stearlight-steam-install"
cdn="${STEAM_ARM_CDN:-https://client-update.steamstatic.com}"
channel="${STEARLIGHT_STEAM_BETA:-steamdeck_publicbeta}"
# Valve publishes the ARM64 seed as a content-addressed CDN object rather than
# the branch manifest used by the x86 client.  Keep the URL overridable so the
# image can move to a newer Valve seed without changing the installer logic.
bootstrap_url="${STEAM_ARM_BOOTSTRAP_URL:-${cdn}/bins_linuxarm64_linuxarm64.zip.f523fa87fc6b9b5435a5e7370cb0d664ef53b50b}"
runtime_base="${STEAM_ARM_RUNTIME_BASE:-https://repo.steampowered.com/steamrt3c/images}"
runtime_channel="${STEAM_ARM_RUNTIME_CHANNEL:-latest-public-beta}"
bootstrap_timeout="${STEAM_BOOTSTRAP_TIMEOUT:-900}"

mkdir -p "${steam_root}/package" "${runtime}" "${work}" "${HOME}/.steam"
printf '%s\n' "${channel}" > "${steam_root}/package/beta"
ln -sfn "${steam_root}" "${HOME}/.steam/root"
ln -sfn "${steam_root}" "${HOME}/.steam/steam"
ln -sfn "${steam_root}/linux32" "${HOME}/.steam/sdk32"
ln -sfn "${steam_root}/linux64" "${HOME}/.steam/sdk64"
ln -sfn "${steam_root}/linuxarm64" "${HOME}/.steam/sdkarm64"
ln -sfn "${steam_root}/ubuntu12_32" "${HOME}/.steam/bin32"
ln -sfn "${steam_root}/ubuntu12_64" "${HOME}/.steam/bin64"

seed_archive="${work}/steam-arm64-bootstrap.zip"
curl -fsSL --retry 8 --retry-all-errors -o "${seed_archive}" \
  "${bootstrap_url}"
unzip -q -o "${seed_archive}" -d "${steam_root}"
chmod +x "${runtime}/steam"

runtime_snapshot=$(curl -fsSL --retry 8 --retry-all-errors \
  "${runtime_base}/${runtime_channel}.txt" | tr -d '[:space:]')
test -n "${runtime_snapshot}"
runtime_archive="${work}/steam-runtime-steamrt-arm64.tar.xz"
curl -fsSL --retry 8 --retry-all-errors -o "${runtime_archive}" \
  "${runtime_base}/${runtime_snapshot}/steam-runtime-steamrt-arm64.tar.xz"
# The archive contains a top-level steam-runtime-steamrt-arm64 directory.
# Extract it beside steamrtarm64, exactly as Armada does.  Keeping it under a
# pv-runtime subdirectory leaves Steam's loader unable to find its runtime
# bin/ and lib trees on the first real boot.
tar -xJf "${runtime_archive}" -C "${steam_root}"

# The ARM runtime's ibus library is outside the seed's default search path;
# Valve's SteamOS image exposes it through this stable SONAME link.
ibus=$(find "${steam_root}/steam-runtime-steamrt-arm64" \
  -path '*/files/lib/aarch64-linux-gnu/libibus-1.0.so.5.*' -type f \
  -print -quit 2>/dev/null || true)
if [ -n "${ibus}" ]; then
  mkdir -p "${steam_root}/lib/aarch64-linux-gnu"
  ln -sfn "${ibus}" "${steam_root}/lib/aarch64-linux-gnu/libibus-1.0.so.5"
fi

test -x "${runtime}/steam"

# Run Valve's own bootstrap once. A clean image has no account or login state,
# so this only extracts the client and leaves the welcome pipeline for the
# first real boot. Retry a transient updater failure without rebuilding the
# whole operating-system layer.
Xvfb :8 -screen 0 1280x800x24 -nolisten tcp -noreset \
  >"${work}/xvfb.log" 2>&1 &
xvfb_pid=$!
trap 'kill "${xvfb_pid}" 2>/dev/null || true' EXIT
sleep 1

verified=0
for attempt in 1 2 3; do
  set +e
  timeout "${bootstrap_timeout}" env \
    HOME="${HOME}" DISPLAY=:8 USER="$(id -un)" LOGNAME="$(id -un)" \
    LD_LIBRARY_PATH="${runtime}:${steam_root}/lib/aarch64-linux-gnu" \
    XDG_CURRENT_DESKTOP=gamescope STEAMOS=1 STEAM_RUNTIME=1 \
    STEAM_GAMESCOPE=1 STEAM_GAMEPADUI=1 LIBGL_ALWAYS_SOFTWARE=1 \
    "${runtime}/steam" -steamdeck -steamos3 -gamepadui -exitsteam \
    -noverifyfiles -vrskip -vrdisable -nocrashmonitor \
    -no-cef-sandbox -cef-disable-sandbox -disable-gpu -cef-disable-gpu \
    >"${work}/bootstrap.log" 2>&1
  rc=$?
  set -e
  if [ -x "${runtime}/steam" ] && [ -f "${runtime}/steamui.so" ]; then
    verified=1
    break
  fi
  printf 'Steam ARM bootstrap attempt %s incomplete (rc %s)\n' \
    "${attempt}" "${rc}" >&2
done

if [ "${verified}" -ne 1 ]; then
  echo 'Steam ARM64 bootstrap did not produce steamui.so.' >&2
  tail -n 80 "${work}/bootstrap.log" >&2 || true
  exit 1
fi

kill "${xvfb_pid}" 2>/dev/null || true
wait "${xvfb_pid}" 2>/dev/null || true
trap - EXIT

# Keep only the manifest/branch metadata needed for Steam to self-update. The
# large seed archive, caches and build log do not belong in the final image.
find "${steam_root}/package" -maxdepth 1 -type f \
  \( -name '*.zip' -o -name '*.partial' -o -name '*.tmp' -o -name '*.blob' \) \
  -delete 2>/dev/null || true
rm -rf "${steam_root}/logs" "${steam_root}/appcache/httpcache" \
       "${steam_root}/appcache/cefdata" "${steam_root}/config/htmlcache"
# The updater was run in a disposable image-build account.  Never carry its
# account/session state into the appliance; Steam must present its own OOBE on
# the first real boot.
if command -v sanitize-steam-profile.sh >/dev/null 2>&1; then
  sanitize-steam-profile.sh "${steam_root}"
else
  rm -f "${steam_root}/config/loginusers.vdf" \
        "${steam_root}/config/localconfig.vdf" \
        "${steam_root}/config/sharedconfig.vdf" \
        "${steam_root}/config/registeruserconfig.vdf" \
        "${steam_root}/config/configsetttings.vdf"
  rm -rf "${steam_root}/userdata"
  find "${steam_root}" -maxdepth 1 -type f -name 'ssfn*' -delete 2>/dev/null || true
fi
rm -rf "${work}"
touch "${steam_root}/.stearlight-prepared" \
      "${steam_root}/.cef-enable-remote-debugging"
printf 'Installed native ARM64 Steam channel %s (runtime %s)\n' \
  "${channel}" "${runtime_snapshot}"
