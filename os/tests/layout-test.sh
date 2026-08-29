#!/usr/bin/env bash
set -euo pipefail

base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo=$(CDPATH= cd -- "${base}/.." && pwd)

# The Alpine appliance is the only OS implementation. Keep the old Armada,
# nested Alpine, and nested VM trees out of the build graph.
for removed in alpine vm Containerfile build_files decky system_files; do
  test ! -e "${base}/${removed}"
done

test -f "${base}/Dockerfile"
test -f "${base}/Dockerfile.vm"
test -f "${base}/build.sh"
test -f "${base}/build-vm.ps1"
test -f "${base}/test-vm.ps1"
test -f "${base}/genimage.cfg"
test -f "${base}/genimage-vm.cfg"
test -f "${base}/grub-vm.cfg"
test -d "${base}/vm-overlay"

grep -q '^FROM.*alpine:' "${base}/Dockerfile"
grep -q 'SVRT_STEARLIGHT_OS=ON' "${base}/Dockerfile"
grep -q 'COPY os/overlay/' "${base}/Dockerfile"
grep -q 'COPY os/generated/' "${base}/Dockerfile"
grep -q 'COPY os/boot/' "${base}/Dockerfile"
grep -q 'linux/arm64' "${base}/build.sh"
grep -q '3127680' "${base}/scripts/download-steam-compat.sh"
grep -q '4628740' "${base}/scripts/download-steam-compat.sh"
grep -q 'STEARLIGHT_BUILD_DATE' "${base}/build.sh"
grep -q 'ExpectedSerial' "${base}/flash-windows.ps1"
grep -q 'supervisor="supervise-daemon"' \
  "${base}/overlay/etc/init.d/stearlight-session"
grep -q 'SVRT_USE_GAMESCOPE=1' \
  "${base}/overlay/usr/local/libexec/stearlight/session"
grep -q 'STEARLIGHT_EYE_WIDTH="1440"' \
  "${base}/overlay/etc/conf.d/stearlight"
grep -q 'STEARLIGHT_EYE_HEIGHT="1600"' \
  "${base}/overlay/etc/conf.d/stearlight"
grep -q 'STEARLIGHT_REFRESH_HZ="60"' \
  "${base}/overlay/etc/conf.d/stearlight"
grep -q 'SVRT_DISPLAY_WIDTH=2880' "${repo}/pi-receiver/CMakeLists.txt"
grep -q 'SVRT_DISPLAY_HEIGHT=1600' "${repo}/pi-receiver/CMakeLists.txt"
grep -q 'SVRT_DISPLAY_REFRESH_HZ=60' "${repo}/pi-receiver/CMakeLists.txt"
grep -q 'SVRT_ENFORCE_DISPLAY_MODE=1' "${repo}/pi-receiver/CMakeLists.txt"
grep -q 'disable_splash=1' "${base}/boot/config.txt"
grep -q 'logo.nologo' "${base}/boot/cmdline.txt"
grep -q 'console=tty12' "${base}/boot/cmdline.txt"
grep -q 'os/vm-overlay/' "${base}/Dockerfile.vm"
grep -q 'gamescope' "${base}/Dockerfile.vm"
grep -q 'Virtual 2880 1600' \
  "${base}/vm-overlay/etc/X11/xorg.conf.d/20-stearlight-vm.conf"
grep -q 'STEARLIGHT VM DISPLAY READY 2880x1600 @ 60Hz' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'os/genimage-vm.cfg' "${base}/Dockerfile.vm"
grep -q 'os/grub-vm.cfg' "${base}/Dockerfile.vm"
grep -q 'Dockerfile.vm' "${base}/build-vm.ps1"
grep -q 'build-vm.ps1' "${base}/VM.md"
grep -q 'Read-PpmToken' "${base}/test-vm.ps1"
grep -q 'stearlight-vm.ppm' "${base}/test-vm.ps1"

if git -C "${repo}" ls-files --error-unmatch os/generated/wifi.env \
    >/dev/null 2>&1; then
  echo 'Wi-Fi credentials must not be committed to the repository.' >&2
  exit 1
fi

for script in \
  "${base}/build.sh" "${base}/apply-quiet-eeprom.sh" \
  "${base}/scripts/install-steam-arm64.sh" \
  "${base}/scripts/download-steam-compat.sh" \
  "${base}/overlay/usr/local/libexec/stearlight/provision-wifi" \
  "${base}/overlay/etc/init.d/stearlight-wifi" \
  "${base}/overlay/usr/local/libexec/stearlight/session" \
  "${base}/overlay/home/stearlight/.local/share/Steam/launch-steam.sh" \
  "${base}/vm-overlay/etc/init.d/stearlight-vm-session" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-session"; do
  bash -n "${script}"
done

for documentation in \
  "${base}/README.md" "${base}/VM.md" \
  "${repo}/.github/workflows/build.yml" "${repo}/README.md"; do
  if grep -n -i -E 'os/(alpine|vm)/|stearlight-armada|STEARLIGHT_ARMADA|armada' \
      "${documentation}" 2>/dev/null; then
    echo "Stale Armada or nested Alpine/VM reference in ${documentation}." >&2
    exit 1
  fi
done

echo 'Stearlight Alpine OS layout checks passed'
