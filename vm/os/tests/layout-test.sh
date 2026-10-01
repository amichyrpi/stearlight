#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
vm="$repo/vm"

for required in \
  "$vm/.dockerignore" \
  "$vm/Dockerfile" \
  "$vm/build.ps1" \
  "$vm/test-vm.ps1" \
  "$vm/extract-frame-ui.ps1" \
  "$vm/os/genimage-vm.cfg" \
  "$vm/os/vm-systemd-loader.conf" \
  "$vm/os/vm-systemd-entry.conf" \
  "$vm/os/driver/fake_tracking.cpp" \
  "$vm/os/driver/stearlight-fake/driver.vrdrivermanifest" \
  "$vm/os/overlay/usr/local/libexec/stearlight/steamvr-native" \
  "$vm/os/vm-overlay/usr/local/libexec/stearlight/vm-console" \
  "$vm/os/tests/steamvr-native-test.sh"; do
  test -e "$required" || { printf 'Missing VM input: %s\n' "$required" >&2; exit 1; }
done

grep -q 'steamframe-oobe-repair-' "$vm/build.ps1"
grep -q 'steamdeck-images.steamos.cloud/recovery' "$vm/build.ps1"
grep -q 'extract-frame-ui.ps1' "$vm/build.ps1"
grep -q 'steamvr-linux' "$vm/build.ps1"
grep -q 'frame-ui=' "$vm/build.ps1"
grep -q 'COPY --from=frame-ui /resources /opt/steamvr/resources' "$vm/Dockerfile"
grep -q 'COPY os/driver/fake_tracking.cpp' "$vm/Dockerfile"
grep -q 'COPY os/vm-overlay/' "$vm/Dockerfile"
grep -q 'genimage-vm.cfg' "$vm/Dockerfile"
grep -q 'stearlight-vm.raw.zst' "$vm/Dockerfile"
grep -q 'virtio-vga-gl,venus=on' "$vm/test-vm.ps1"
grep -q 'sdl,gl=on' "$vm/test-vm.ps1"
grep -q 'initramfs emergency recovery shell' "$vm/test-vm.ps1"
grep -q 'lazy_journal_init=0' "$vm/os/genimage-vm.cfg"
grep -q 'STEARLIGHT DASHBOARD INPUT READY' "$vm/test-vm.ps1"
grep -q 'NATIVE COMPONENTS READY' "$vm/test-vm.ps1"
grep -q 'vm/build' "$vm/README.md"
grep -q '^build/$' "$vm/.dockerignore"
grep -q '^  vm-source-checks:' "$repo/.github/workflows/build.yml"

for project_file in \
  "$repo/CMakeLists.txt" \
  "$repo/assets/stearlight.svg" \
  "$repo/pipe/pipe.c" \
  "$repo/protocol/stearlight_protocol.c" \
  "$repo/steamvr-driver/CMakeLists.txt" \
  "$repo/utility/main.cpp"; do
  test -e "$project_file" || { printf 'Missing restored project source: %s\n' "$project_file" >&2; exit 1; }
done

for source_file in $(find "$vm" -type f ! -name layout-test.sh); do
  if grep -niE 'h.?265|SVRT' "$source_file"; then
    echo 'The Stearlight OS tree still uses an old project namespace.' >&2
    exit 1
  fi
done

for removed in \
  "$vm/CMakeLists.txt" \
  "$vm/os/CMakeLists.txt" \
  "$repo/pi-receiver" \
  "$repo/buildroot" \
  "$repo/docker/raspberrypi4" \
  "$repo/scripts/install-pi.sh" \
  "$repo/scripts/install-steam-arm64-pi.sh" \
  "$repo/scripts/launch-steam-arm64-pi.sh" \
  "$repo/scripts/test-pi.sh" \
  "$repo/tests/pi_2880x1600_90_test.sh" \
  "$repo/tests/pi_4k60_test.sh" \
  "$repo/os" \
  "$vm/assets" \
  "$vm/gui" \
  "$repo/assets/steamframe-native"; do
  test ! -e "$removed" || { printf 'Obsolete path remains: %s\n' "$removed" >&2; exit 1; }
done

if grep -Fqi 'F:\' "$vm/build.ps1" "$vm/test-vm.ps1" ||
   grep -Fqi 'G:\' "$vm/build.ps1" "$vm/test-vm.ps1" ||
   grep -Fqi 'C:\Program Files' "$vm/build.ps1" "$vm/test-vm.ps1" ||
   grep -Fqi 'stearlightBuild' "$vm/build.ps1" "$vm/test-vm.ps1" ||
   grep -Fqi 'BuildArchive' "$vm/build.ps1" "$vm/test-vm.ps1"; then
  echo 'VM scripts contain a machine-specific drive or install path.' >&2
  exit 1
fi

if grep -n -E \
  'CMakeLists|cmake -S|stearlight-shell|STEARLIGHT_BUILD_RECEIVER|STEARLIGHT_BUILD_PI_LIBRARY|STEARLIGHT_STEARLIGHT_OS|STEARLIGHT_BUILD_DRIVER|STEARLIGHT_BUILD_UTILITY|VGA,xres|STEARLIGHT_STEAM_COMPOSITOR.*weston|STEARLIGHT_VM_GAMESCOPE_WINDOWED' \
  "$vm/Dockerfile" "$vm/build.ps1" "$vm/test-vm.ps1" \
  "$repo/.github/workflows/build.yml"; then
  echo 'Legacy GUI, Raspberry Pi, or VGA/Weston build paths remain.' >&2
  exit 1
fi

for script in \
  "$vm/os/patch-steam-runtime.sh" \
  "$vm/os/steam-firstboot" \
  "$vm/os/overlay/usr/local/libexec/stearlight/steamvr-native" \
  "$vm/os/vm-overlay/usr/local/libexec/stearlight/vm-console" \
  "$vm/os/vm-overlay/usr/local/libexec/stearlight/steam32-launch" \
  "$vm/os/tests/steam-firstboot-test.sh" \
  "$vm/os/tests/steam-runtime-patch-test.sh" \
  "$vm/os/tests/steamvr-native-test.sh" \
  "$vm/os/tests/xr-bridge-env-test.sh"; do
  sh -n "$script"
done
if command -v bash >/dev/null 2>&1; then
  bash -n "$vm/os/overlay/usr/share/gamescope-session-plus/gamescope-session-plus"
fi

echo 'VM-only native SteamVR layout checks passed.'
