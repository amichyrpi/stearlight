#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
vm="$repo/vm"
for file in Dockerfile.arm64 build-image.sh fetch-recovery.py sources.lock.json start-vm.ps1 start-vm.cmd os/arm64-vm.start os/driver/dashboard_ready.c host/build-runtime.sh tests/launcher-test.ps1; do
  test -s "$vm/$file" || { echo "Missing ARM64 VM input: $file" >&2; exit 1; }
done
for file in "$vm/os/arm64-vm.start" "$vm/os/extract-gamescope-runtime.sh" "$vm/os/overlay/usr/local/libexec/stearlight/steamframe-session" "$vm/os/overlay/usr/local/libexec/stearlight/steamvr-native"; do
  sh -n "$file"
done
if grep -nE 'Stearlight-.*20[0-9][0-9]|[FG]:[/\\]|Users[/\\]jfran' "$vm/start-vm.ps1" "$vm/start-vm.cmd" "$vm/build-image.sh" "$vm/host/build-runtime.sh" "$repo/.github/workflows/build.yml"; then
  echo 'Portable VM path contains developer host configuration' >&2
  exit 1
fi
if find "$vm/host/windows-venus/experimental" -type f 2>/dev/null | grep -q .; then
  echo 'Abandoned GPU experiment files remain' >&2
  exit 1
fi
echo 'Maintained ARM64 VM layout and portable path checks passed.'
