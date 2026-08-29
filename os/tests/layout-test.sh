#!/usr/bin/env bash
set -euo pipefail

base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test ! -e "$base/upstream"
test ! -e "$base/armada"
test ! -d "$base/abl"
test ! -d "$base/disk_config"
test ! -d "$base/post_process"
test -d "$base/build_files"
test -d "$base/decky"
test -f "$base/build_files/generate-steam-bootstrap.sh"
grep -q 'fedora-bootc' "$base/Containerfile"
grep -q 'linux/arm64' "$base/build.sh"
grep -q 'gamescope' "$base/system_files/usr/libexec/stearlight/session"
grep -q '/usr/libexec/stearlight/steam' "$base/system_files/usr/libexec/stearlight/session"
grep -q -- '--output-width 2880' "$base/system_files/usr/libexec/stearlight/session"
grep -q -- '--output-height 1600' "$base/system_files/usr/libexec/stearlight/session"
grep -q 'NetworkManager' "$base/Containerfile"
grep -q 'FEX_PKG=ghcr.io/armada-os/armada-packages/fex' "$base/Containerfile"
grep -q 'install-compat.sh' "$base/Containerfile"
grep -q 'armada-decky-sync.service' "$base/Containerfile"
grep -q 'systemctl mask getty@tty1.service sshd.service' "$base/Containerfile"
test -f "$base/boot/config.txt"
grep -q 'dtoverlay=vc4-kms-v3d-pi4' "$base/boot/config.txt"
# Avoid scanning the vendored binary firmware payloads; all credential-bearing
# logic is text under these runtime directories.
if grep -R -n -E 'STEARLIGHT_WIFI_(SSID|PSK)|netsh.*wlan|wpa_passphrase' \
    "$base/system_files/etc" "$base/system_files/usr/libexec" \
    "$base/system_files/usr/share/armada" "$base/system_files/usr/share/gamescope-session-plus" \
    "$base/scripts" 2>/dev/null; then
  echo 'Armada target must not embed Wi-Fi credentials.' >&2
  exit 1
fi
for file in "$base/build.sh" "$base/run-vm.sh" "$base/system_files/usr/libexec/stearlight/session" \
            "$base/scripts/steam" "$base/scripts/configure-fex.sh"; do
  bash -n "$file"
done
echo 'Stearlight Armada layout checks passed'
