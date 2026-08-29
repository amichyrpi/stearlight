#!/usr/bin/env bash
set -euo pipefail

steam_root="${HOME}/.local/share/Steam"
runtime="${steam_root}/steamrtarm64"
work="${HOME}/.cache/stearlight-steam-install"
bootstrap_url="https://client-update.steamstatic.com/bins_linuxarm64_linuxarm64.zip.f523fa87fc6b9b5435a5e7370cb0d664ef53b50b"
mkdir -p "${steam_root}/package" "${runtime}" "${work}" "${HOME}/.steam"
printf '%s\n' publicbeta > "${steam_root}/package/beta"
ln -sfn "${steam_root}" "${HOME}/.steam/root"
ln -sfn "${steam_root}" "${HOME}/.steam/steam"

curl -fL --retry 8 --retry-all-errors -o "${work}/bootstrap.zip" \
  "${bootstrap_url}"
unzip -q -o "${work}/bootstrap.zip" 'steamrtarm64/steam' -d "${steam_root}"
chmod +x "${runtime}/steam"

version=$(curl -fsSL \
  https://repo.steampowered.com/steamrt3c/images/latest-public-beta.txt | \
  tr -d '[:space:]')
mkdir -p "${runtime}/pv-runtime"
curl -fL --retry 8 --retry-all-errors -o "${work}/runtime.tar.xz" \
  "https://repo.steampowered.com/steamrt3c/images/${version}/steam-runtime-steamrt-arm64.tar.xz"
tar -xf "${work}/runtime.tar.xz" -C "${runtime}/pv-runtime"

Xvfb :8 -screen 0 1024x640x24 -nolisten tcp -noreset \
  >"${work}/xvfb.log" 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 1
set +e
timeout 900 env DISPLAY=:8 LD_LIBRARY_PATH="${runtime}" \
  "${runtime}/steam" -noverifyfiles -silent >"${work}/bootstrap.log" 2>&1
set -e
kill "$xvfb_pid" 2>/dev/null || true
wait "$xvfb_pid" 2>/dev/null || true
trap - EXIT

# Raspberry Pi 4 is ARMv8.0. Current public-beta files use ARMv8.1 LSE, so
# keep the known ARMv8.0-compatible client payload until Valve restores an
# outlined-atomics build. This is deliberately isolated to the Steam layer.
curl -fL --retry 8 --retry-all-errors -o "${work}/switchdeck.tar.gz" \
  https://github.com/SildurFX/Switchdeck/archive/refs/heads/main.tar.gz
mkdir -p "${work}/switchdeck"
tar -xzf "${work}/switchdeck.tar.gz" -C "${work}/switchdeck" \
  --strip-components=1
downgrade="${work}/switchdeck/files/downgrade"
mkdir -p "${steam_root}/linuxarm64"
tar -xzf "${downgrade}/linuxarm64.tar.gz" -C "${steam_root}/linuxarm64"
unzip -q -o "${downgrade}/linux_x86_64.zip" -d "${steam_root}"
unzip -q -o "${downgrade}/steamui_websrc_all.zip" 'steamui/*' \
  -d "${steam_root}"
cat "${downgrade}"/steamrtarm64.tar.gz.part* > "${work}/steamrtarm64.tar.gz"
tar -xzf "${work}/steamrtarm64.tar.gz" -C "${runtime}"
cp "${downgrade}/steam.cfg" "${steam_root}/steam.cfg"
chmod -R u+rwX "${steam_root}"
ln -sfn "${steam_root}/linuxarm64" "${HOME}/.steam/sdkarm64"
ln -sfn "${runtime}" "${steam_root}/steamrtarm32"
rm -rf "${work}"
