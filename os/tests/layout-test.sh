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
test -f "${base}/vm-systemd-loader.conf"
test -f "${base}/vm-systemd-entry.conf"
test -d "${base}/vm-overlay"

grep -q '^FROM.*alpine:' "${base}/Dockerfile"
grep -q 'SVRT_STEARLIGHT_OS=ON' "${base}/Dockerfile"
grep -q 'SVRT_BUILD_RECEIVER=OFF' "${base}/Dockerfile"
grep -q 'SVRT_BUILD_OS_SHELL=ON' "${base}/Dockerfile"
grep -q 'stearlight-shell' \
  "${base}/overlay/usr/local/libexec/stearlight/session"
grep -q 'COPY os/overlay/' "${base}/Dockerfile"
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
grep -q 'SVRT_DISPLAY_WIDTH=2880' "${base}/CMakeLists.txt"
grep -q 'SVRT_DISPLAY_HEIGHT=1600' "${base}/CMakeLists.txt"
grep -q 'SVRT_DISPLAY_REFRESH_HZ=60' "${base}/CMakeLists.txt"
grep -q 'SVRT_ENFORCE_DISPLAY_MODE=1' "${base}/CMakeLists.txt"
grep -q 'disable_splash=1' "${base}/boot/config.txt"
grep -q 'logo.nologo' "${base}/boot/cmdline.txt"
grep -q 'console=tty12' "${base}/boot/cmdline.txt"
grep -q 'os/vm-overlay/' "${base}/Dockerfile.vm"
grep -q 'os/overlay/etc/steamos-oobe-image' "${base}/Dockerfile.vm"
grep -q 'gamescope' "${base}/Dockerfile.vm"
grep -q 'weston-backend-x11' "${base}/Dockerfile.vm"
grep -q 'weston-shell-desktop' "${base}/Dockerfile.vm"
grep -q 'mesa-vulkan-swrast' "${base}/Dockerfile.vm"
grep -q 'vulkan-loader' "${base}/Dockerfile.vm" "${base}/Dockerfile"
grep -q 'steam_latest.deb' "${base}/Dockerfile.vm"
grep -q 'bubblewrap' "${base}/Dockerfile.vm"
grep -q 'chmod 4755 /usr/bin/bwrap' "${base}/Dockerfile.vm"
grep -q 'SVRT_BUILD_RECEIVER=OFF' "${base}/Dockerfile.vm"
grep -q 'SVRT_BUILD_OS_SHELL=ON' "${base}/Dockerfile.vm"
test -f "${base}/steam_client.c"
test -f "${base}/steam_client.h"
test -f "${base}/vm-overlay/usr/local/libexec/stearlight/steam32-launch"
test -f "${base}/overlay/usr/local/libexec/stearlight/launch-steam"
grep -q 'SVRT_STEAM_COMPOSITOR' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam32-launch"
grep -q 'SVRT_STEAM_WESTON_LOG' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam32-launch"
grep -q 'steamrt64/steam' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam32-launch"
! grep -q '\.\./pi-receiver/steam_client' "${base}/CMakeLists.txt"
# Xvfb must be started without Steam's ABI-specific Mesa environment.  Keep
# this invariant covered because inheriting LIBGL_DRIVERS_PATH makes the
# server silently omit GLX, and Steam then fails at glXChooseVisual.
grep -q 'unsetenv("LIBGL_DRIVERS_PATH")' "${base}/steam_client.c"
grep -q 'SVRT_STEAM_DRI_PATH' "${base}/steam_client.c"
grep -q 'unsetenv("LD_LIBRARY_PATH")' "${base}/steam_client.c"
grep -q 'SVRT_STEAM_PREPARE' "${base}/steam_client.c"
grep -q 'SVRT_STEAM_SOFTWARE_GL' "${base}/steam_client.c"
grep -q 'return "/usr/local/libexec/stearlight/launch-steam"' \
  "${base}/steam_client.c"
grep -q 'SVRT_SHOW_STEAM_STARTING_FALLBACK=0' "${base}/CMakeLists.txt"
test -f "${base}/overlay/etc/steamos-oobe-image"
grep -q 'STEAMOS_OOBE=1' \
  "${base}/steam_client.c" \
  "${base}/overlay/usr/local/libexec/stearlight/session" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'STEAM_USE_MANGOAPP=1' \
  "${base}/steam_client.c" \
  "${base}/overlay/usr/local/libexec/stearlight/session" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'initial X11 tree query' "${base}/steam_client.c"
grep -q 'bridge heartbeat' "${base}/steam_client.c"
! grep -q '/usr/lib/i386-linux-gnu/dri' "${base}/steam_client.c"
grep -q 'stearlight_steam_client_start' "${base}/shell.c"
! grep -q 'SVRT_VM_RECEIVER_ONLY' "${base}/Dockerfile.vm" \
  "${base}/vm-overlay"
! grep -R -q 'svrt-receiver' "${base}/overlay" "${base}/vm-overlay"
grep -q 'stearlight-shell' \
  "${base}/overlay/usr/local/libexec/stearlight/session" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'user.max_user_namespaces' \
  "${base}/overlay/usr/local/libexec/stearlight/session" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q '/var/cache/ldconfig/ld.so.cache' \
  "${base}/overlay/usr/local/libexec/stearlight/session" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'seed_host_dri' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'ensure_steam_runtime_seed' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'bootstraplinux_ubuntu12_32.tar.xz' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q '^unset VK_ICD_FILENAMES$' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam32-launch"
grep -q '^unset VK_ICD_FILENAMES VK_DRIVER_FILES$' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'SVRT_STEAM_RUNTIME_SEED' "${base}/steam-firstboot"
! grep -q 'STEARLIGHT_WEBHELPER_ABI_BOUNDARY' \
  "${base}/steam-firstboot" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam-runtime-watch"
! grep -q 'patch_webhelper' \
  "${base}/steam-firstboot" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam-runtime-watch"
grep -q 'SVRT_GAMESCOPE_VK_ICD=/usr/share/vulkan/icd.d/stearlight-alpine-lvp.json' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'SVRT_GAMESCOPE_VK_ICD' "${base}/steam_client.c"
test -f "${base}/vm-overlay/usr/share/vulkan/icd.d/stearlight-alpine-lvp.json"
grep -q '"library_path": "/usr/lib/libvulkan_lvp.so"' \
  "${base}/vm-overlay/usr/share/vulkan/icd.d/stearlight-alpine-lvp.json"
grep -q 'lvp_icd.json' \
  "${base}/Dockerfile.vm" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam-runtime-watch"
grep -q 'out/usr/lib/steam/stearlight_libs_32' "${base}/Dockerfile.vm"
grep -q 'out/usr/lib/steam/stearlight_libs_64' "${base}/Dockerfile.vm"
grep -q 'unset LIBGL_DRIVERS_PATH' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam32-launch"
grep -q 'SVRT_STEAM_USE_SEED_DRI' "${base}/patch-steam-runtime.sh"
! grep -q '^export SVRT_STEAM_DRI_PATH=' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'libvulkan1:i386' "${base}/Dockerfile.vm"
grep -q -- "-name 'libvulkan\*'" "${base}/Dockerfile.vm"
grep -q 'usr/share/vulkan/icd.d' "${base}/Dockerfile.vm"
grep -q 'Virtual 2880 1600' \
  "${base}/vm-overlay/etc/X11/xorg.conf.d/20-stearlight-vm.conf"
grep -q 'STEARLIGHT VM DISPLAY READY 2880x1600 @ 60Hz' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
grep -q 'os/genimage-vm.cfg' "${base}/Dockerfile.vm"
grep -q 'size = 12288M' "${base}/genimage-vm.cfg"
grep -q 'os/vm-systemd-loader.conf' "${base}/Dockerfile.vm"
grep -q 'os/vm-systemd-entry.conf' "${base}/Dockerfile.vm"
grep -q 'systemd-boot' "${base}/Dockerfile.vm"
grep -q 'systemd-bootx64.efi' "${base}/Dockerfile.vm"
grep -q 'vmlinuz-virt' "${base}/Dockerfile.vm"
grep -q 'initramfs-virt' "${base}/Dockerfile.vm"
grep -q 'preextract-steam-client.sh' "${base}/Dockerfile.vm"
grep -q 'sanitize-steam-profile.sh' "${base}/Dockerfile.vm"
grep -q 'Steam Gamepad UI extraction complete' \
  "${base}/scripts/preextract-steam-client.sh"
grep -q 'Retry' "${base}/scripts/preextract-steam-client.sh"
grep -q 'patch_userns_probe' "${base}/scripts/preextract-steam-client.sh"
grep -q 'restore_userns_probe' "${base}/scripts/preextract-steam-client.sh"
test -f "${base}/overlay/etc/steamos-oobe-image"
grep -q 'steamdeck_publicbeta' "${base}/steam-firstboot"
grep -q 'steamdeck_publicbeta' \
  "${base}/scripts/install-steam-arm64.sh"
grep -q 'tar -xJf.*-C "\${steam_root}"' \
  "${base}/scripts/install-steam-arm64.sh"
! grep -q 'runtime}/pv-runtime' "${base}/scripts/install-steam-arm64.sh"
grep -q 'bootstrap_extracted' "${base}/steam-firstboot"
grep -q 'SVRT_STEAM_LAUNCHER=/usr/local/libexec/stearlight/launch-steam' \
  "${base}/overlay/usr/local/libexec/stearlight/session"
grep -q 'STEARLIGHT_SESSION_MODE' \
  "${base}/overlay/usr/local/libexec/stearlight/session"
grep -q 'SVRT_VM_SESSION_MODE' \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console"
test -f "${base}/overlay/usr/share/gamescope-session-plus/gamescope-session-plus"
test -f "${base}/overlay/usr/share/gamescope-session-plus/sessions.d/steam"
test -f "${base}/overlay/usr/bin/gamescope-session-plus"
test -f "${base}/overlay/usr/share/wayland-sessions/gamescope-session-steam.desktop"
grep -q 'startup.socket' \
  "${base}/overlay/usr/share/gamescope-session-plus/gamescope-session-plus"
grep -q 'CLIENTCMD' \
  "${base}/overlay/usr/share/gamescope-session-plus/sessions.d/steam"
for helper in \
  "${base}/overlay/usr/bin/jupiter-biosupdate" \
  "${base}/overlay/usr/bin/jupiter-dock-updater" \
  "${base}/overlay/usr/bin/pkexec" \
  "${base}/overlay/usr/bin/steamos-update" \
  "${base}/overlay/usr/bin/steamos-select-branch" \
  "${base}/overlay/usr/bin/steamos-session-select" \
  "${base}/overlay/usr/bin/timedatectl" \
  "${base}/overlay/usr/bin/localectl" \
  "${base}/overlay/usr/bin/steamos-set-timezone" \
  "${base}/overlay/usr/bin/steamos-devkit-mode" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/jupiter-biosupdate" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/jupiter-dock-updater" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/steamos-priv-write" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/steamos-set-timezone" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/steamos-devkit-mode"; do
  test -f "$helper"
done
grep -q 'steamos-polkit-helpers' \
  "${base}/overlay/home/stearlight/.local/share/Steam/launch-steam.sh"
grep -q 'Dockerfile.vm' "${base}/build-vm.ps1"
grep -q 'build-vm.ps1' "${base}/VM.md"
grep -q 'Read-PpmToken' "${base}/test-vm.ps1"
grep -q 'stearlight-vm.ppm' "${base}/test-vm.ps1"
grep -q 'zoom-to-fit=on' "${base}/test-vm.ps1"
grep -q 'gtk,gl=on' "${base}/test-vm.ps1"
grep -q "'-accel', 'whpx'" "${base}/test-vm.ps1"
grep -q "'-accel', 'tcg,thread=multi'" "${base}/test-vm.ps1"
grep -q 'Set-QemuWindowAspect' "${base}/test-vm.ps1"
grep -q 'targetClientHeight' "${base}/test-vm.ps1"
grep -q 'Measure-QemuBootFps' "${base}/test-vm.ps1"
grep -q 'Boot animation frame samples' "${base}/test-vm.ps1"
grep -q 'sleep_ui_frame' "${repo}/pi-receiver/main.c"
grep -q 'SVRT_TRACE_UI_FPS' "${repo}/gui/ui.c"
grep -q 'pace_pairing_frame' "${repo}/gui/pairing.c"
! grep -q 'SVRT_UI_WELCOME\|WELCOME REQUIRED\|SVRT_WELCOME_MARKER\|Welcome to Stearlight OS' \
  "${repo}/gui/ui.h" "${repo}/gui/ui.c" "${repo}/pi-receiver/main.c"
grep -q 'svrt_ui_draw(&ui,' "${repo}/pi-receiver/main.c"
grep -q 'SVRT_UI_HOME' "${repo}/pi-receiver/main.c"
grep -q 'svrt_ui_draw' "${base}/shell.c"
grep -q 'stearlight_steam_client_start' "${base}/shell.c"
! grep -q 'svrt_steam_link\|authorize_steam_link\|svrt_status_server' \
  "${base}/shell.c"

# The appliance has no password-based remote shell and never copies host Wi-Fi
# credentials into an image.  Network setup is performed by the first-boot UI
# and Steam settings instead.
test ! -e "${base}/capture-wifi.ps1"
if test -e "${base}/generated"; then
  test -z "$(find "${base}/generated" -mindepth 1 -print -quit)"
fi
test ! -e "${base}/overlay/etc/init.d/stearlight-wifi"
test ! -e "${base}/overlay/usr/local/libexec/stearlight/provision-wifi"
! grep -q 'openssh' "${base}/Dockerfile"
! grep -q 'chpasswd' "${base}/Dockerfile"
! grep -q 'wifi\.env' "${base}/Dockerfile"
! grep -q 'openssh' "${base}/Dockerfile.vm"
! grep -q 'chpasswd' "${base}/Dockerfile.vm"

if git -C "${repo}" ls-files --error-unmatch os/generated/wifi.env \
    >/dev/null 2>&1; then
  echo 'Wi-Fi credentials must not be committed to the repository.' >&2
  exit 1
fi

for script in \
  "${base}/build.sh" "${base}/apply-quiet-eeprom.sh" \
  "${base}/scripts/install-steam-arm64.sh" \
  "${base}/scripts/download-steam-compat.sh" \
  "${base}/scripts/preextract-steam-client.sh" \
  "${base}/scripts/sanitize-steam-profile.sh" \
  "${base}/tests/steam-firstboot-test.sh" \
  "${base}/steam-firstboot" \
  "${base}/overlay/usr/bin/jupiter-biosupdate" \
  "${base}/overlay/usr/bin/jupiter-dock-updater" \
  "${base}/overlay/usr/bin/pkexec" \
  "${base}/overlay/usr/bin/steamos-update" \
  "${base}/overlay/usr/bin/steamos-select-branch" \
  "${base}/overlay/usr/bin/steamos-session-select" \
  "${base}/overlay/usr/bin/timedatectl" \
  "${base}/overlay/usr/bin/localectl" \
  "${base}/overlay/usr/bin/steamos-set-timezone" \
  "${base}/overlay/usr/bin/steamos-devkit-mode" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/jupiter-biosupdate" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/jupiter-dock-updater" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/steamos-priv-write" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/steamos-set-timezone" \
  "${base}/overlay/usr/bin/steamos-polkit-helpers/steamos-devkit-mode" \
  "${base}/overlay/usr/local/libexec/stearlight/session" \
  "${base}/overlay/usr/local/libexec/stearlight/launch-steam" \
  "${base}/overlay/usr/bin/gamescope-session-plus" \
  "${base}/overlay/usr/share/gamescope-session-plus/gamescope-session-plus" \
  "${base}/overlay/usr/share/gamescope-session-plus/sessions.d/steam" \
  "${base}/overlay/home/stearlight/.local/share/Steam/launch-steam.sh" \
  "${base}/vm-overlay/etc/init.d/stearlight-vm-session" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-console" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/vm-session" \
  "${base}/vm-overlay/usr/local/libexec/stearlight/steam-runtime-watch"; do
  bash -n "${script}"
done

"${base}/tests/steam-firstboot-test.sh"

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
