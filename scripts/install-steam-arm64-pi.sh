#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this installer as root." >&2
    exit 1
fi
if [ "$(uname -m)" != "aarch64" ]; then
    echo "This installer is only for the ARM64 Pi receiver." >&2
    exit 1
fi

service_user=svrt-receiver
service_home=/var/lib/svrt-receiver
steam_root="$service_home/.local/share/Steam"
launcher_source="$(dirname "$0")/launch-steam-arm64-pi.sh"

apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates curl lsof unzip xz-utils xvfb xauth \
    matchbox-window-manager steam-devices \
    libasound2t64 libgbm1 libgtk2.0-0t64 libnss3 libopenal1 \
    libpipewire-0.3-0t64 \
    libx11-6 libxcomposite1 libxdamage1 libxfixes3 libxrandr2 \
    libxss1 libxtst6

systemctl stop svrt-receiver.service || true
trap 'systemctl start svrt-receiver.service >/dev/null 2>&1 || true' EXIT
install -d -m 0750 -o "$service_user" -g "$service_user" "$service_home"
usermod -aG input,render,video "$service_user"

runuser -u "$service_user" -- env HOME="$service_home" \
    SERVICE_HOME="$service_home" STEAM_ROOT="$steam_root" \
    LAUNCHER_SOURCE="$launcher_source" bash <<'STEAM_INSTALL'
set -euo pipefail

steam_home="$SERVICE_HOME/.steam"
runtime="$STEAM_ROOT/steamrtarm64"
work="$SERVICE_HOME/.cache/svrt-steam-install"
bootstrap_url="https://client-update.steamstatic.com/bins_linuxarm64_linuxarm64.zip.f523fa87fc6b9b5435a5e7370cb0d664ef53b50b"
install_version=1

mkdir -p "$STEAM_ROOT/package" "$steam_home" "$work"
printf '%s\n' publicbeta > "$STEAM_ROOT/package/beta"
ln -sfn "$STEAM_ROOT" "$steam_home/root"
ln -sfn "$STEAM_ROOT" "$steam_home/steam"

if [ -x "$runtime/steam" ] &&
   [ "$(cat "$STEAM_ROOT/.svrt-arm64-install-version" 2>/dev/null || true)" =
     "$install_version" ]; then
    cp "$LAUNCHER_SOURCE" "$STEAM_ROOT/launch-steam.sh"
    chmod +x "$STEAM_ROOT/launch-steam.sh"
    echo "Steam ARM64 client is already installed."
    exit 0
fi

if [ ! -x "$runtime/steam" ]; then
    echo "Downloading Valve's ARM64 Steam bootstrap..."
    curl -fL -C - --retry 8 --retry-all-errors \
        -o "$work/bootstrap.zip" "$bootstrap_url"
    unzip -q -o "$work/bootstrap.zip" 'steamrtarm64/steam' -d "$STEAM_ROOT"
    chmod +x "$runtime/steam"
fi

if [ ! -x "$runtime/pv-runtime/steam-runtime-steamrt-arm64" ]; then
    version=$(curl -fsSL \
        https://repo.steampowered.com/steamrt3c/images/latest-public-beta.txt | \
        tr -d '[:space:]')
    echo "Downloading Valve Steam ARM64 runtime $version..."
    mkdir -p "$runtime/pv-runtime"
    curl -fL -C - --retry 8 --retry-all-errors -o "$work/runtime.tar.xz" \
        "https://repo.steampowered.com/steamrt3c/images/$version/steam-runtime-steamrt-arm64.tar.xz"
    tar -xf "$work/runtime.tar.xz" -C "$runtime/pv-runtime"
fi

echo "Running the initial Valve client update..."
Xvfb :8 -screen 0 1024x640x24 -nolisten tcp -noreset >"$work/xvfb.log" 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 1
set +e
timeout 900 env DISPLAY=:8 LD_LIBRARY_PATH="$runtime" \
    "$runtime/steam" -noverifyfiles -silent >"$work/bootstrap.log" 2>&1
set -e
kill "$xvfb_pid" 2>/dev/null || true
wait "$xvfb_pid" 2>/dev/null || true
trap - EXIT

echo "Applying the ARMv8.0-compatible April client files..."
curl -fL -C - --retry 8 --retry-all-errors -o "$work/switchdeck.tar.gz" \
    https://github.com/SildurFX/Switchdeck/archive/refs/heads/main.tar.gz
rm -rf "$work/switchdeck"
mkdir "$work/switchdeck"
tar -xzf "$work/switchdeck.tar.gz" -C "$work/switchdeck" --strip-components=1
downgrade="$work/switchdeck/files/downgrade"

mkdir -p "$STEAM_ROOT/linuxarm64" "$runtime"
tar -xzf "$downgrade/linuxarm64.tar.gz" -C "$STEAM_ROOT/linuxarm64"
unzip -q -o "$downgrade/linux_x86_64.zip" -d "$STEAM_ROOT"
unzip -q -o "$downgrade/steamui_websrc_all.zip" 'steamui/*' -d "$STEAM_ROOT"
cat "$downgrade"/steamrtarm64.tar.gz.part* > "$work/steamrtarm64.tar.gz"
tar -xzf "$work/steamrtarm64.tar.gz" -C "$runtime"
cp -f "$downgrade/steam.cfg" "$STEAM_ROOT/steam.cfg"
chmod +x "$runtime/steam" "$runtime/steamwebhelper" \
    "$runtime/steamsysinfo" "$runtime/gldriverquery" \
    "$runtime/vulkandriverquery"

cp "$LAUNCHER_SOURCE" "$STEAM_ROOT/launch-steam.sh"
chmod +x "$STEAM_ROOT/launch-steam.sh"
ln -sfn "$STEAM_ROOT/linuxarm64" "$steam_home/sdkarm64"
ln -sfn "$runtime" "$STEAM_ROOT/steamrtarm32"
chmod -R +x "$STEAM_ROOT"
printf '%s\n' "$install_version" > "$STEAM_ROOT/.svrt-arm64-install-version"

rm -rf "$work/switchdeck" "$work/bootstrap.zip" "$work/runtime.tar.xz" \
       "$work/switchdeck.tar.gz" "$work/steamrtarm64.tar.gz"
echo "Steam ARM64 client installation completed."
STEAM_INSTALL

chown -R "$service_user:$service_user" "$service_home"
systemctl start svrt-receiver.service
trap - EXIT
