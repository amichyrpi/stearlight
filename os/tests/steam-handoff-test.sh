#!/usr/bin/env bash
set -euo pipefail

# Exercise the production URI argument boundary without starting Steam. The
# launcher must preserve Valve's steamlink URI and add the native Gamepad UI /
# Steam Frame flags when it invokes the installed client.
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/stearlight-steam-handoff.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

steam_root="$tmp/home/.local/share/Steam"
steam_arm_dir="$steam_root/steamrtarm64"
mkdir -p "$steam_arm_dir"
echo_binary=$(type -P echo)
output=$(HOME="$tmp/home" \
  STEAM_ROOT="$steam_root" \
  STEAM_ARM_DIR="$steam_arm_dir" \
  STEAM_ARM_BINARY="$echo_binary" \
  STEARLIGHT_STEAM_FRAME=1 \
  "$base/overlay/usr/local/libexec/stearlight/launch-steam" \
  'steamlink://lookup/')

case "$output" in
  *-gamepadui*'-steamos3'*'-steampal'*'-steamdeck'*'-steamframe'*'steamlink://lookup/'*)
    printf '%s\n' 'Steam native URI handoff passed'
    ;;
  *)
    printf '%s\n' "Unexpected Steam argument vector: $output" >&2
    exit 1
    ;;
esac
