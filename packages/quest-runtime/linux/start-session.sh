#!/bin/bash
set -eu
unset LD_LIBRARY_PATH LD_PRELOAD
export HOME=/home/stearlight USER=stearlight LOGNAME=stearlight
export HOSTNAME=frame
export STEARLIGHT_QUEST_RUNTIME=1
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export DISPLAY=:0 XDG_RUNTIME_DIR=/tmp/stearlight-runtime
export GLIBC_TUNABLES=glibc.pthread.rseq=0
# Horizon owns physical USB/Bluetooth devices. SDL's Linux udev monitor is
# denied by Android; retrying initialization on every Steam input poll wastes
# CPU and repeatedly regenerates mountinfo. Keep HIDAPI's polling fallback.
if [ "${STEARLIGHT_LINUX_HIDAPI:-0}" != 1 ];then
 export SDL_HIDAPI_UDEV=0 SDL_HIDAPI_LIBUSB=0 SDL_HIDAPI_DISABLE_LIBUSB=1
fi
export MESA_VK_ENABLE_SUBMIT_THREAD=1
export QUEST_SOCKET_REGISTRY=/run/stearlight/quest/socket-registry.sock
# SteamVR creates a separate GLX context for browser texture sharing. Xvfb
# has no DRI3 display; Zink still renders its offscreen GL objects on Turnip.
export GALLIUM_DRIVER=zink MESA_LOADER_DRIVER_OVERRIDE=zink
export LIBGL_KOPPER_DISABLE=true LIBGL_KOPPER_DRI2=1
# SDL's OpenVR streaming player uses EGL without an Android/X11 window.
export EGL_PLATFORM=surfaceless
mkdir -p "$HOME" "$HOME/.config" "$HOME/.cache" "$HOME/.local/share/Steam/logs" "$XDG_RUNTIME_DIR" /tmp/.X11-unix /run/dbus
printf 'frame\n' >/etc/hostname
# Keep localhost and existing resolver entries; add the guest's own name.
if ! grep -Eq '^[[:space:]]*127\.0\.1\.1[[:space:]]+frame([[:space:]]|$)' /etc/hosts 2>/dev/null;then
 printf '\n127.0.1.1 frame\n' >>/etc/hosts
fi
rm -f /run/stearlight/quest/dashboard.ready /run/stearlight/quest/desktop-mode.active /run/stearlight/quest/desktop-mode.ready /run/stearlight/quest/desktop-focus.request /tmp/.X0-lock /tmp/.X11-unix/X0
printf '%s' 'Starting Linux GPU and IPC services' >/run/stearlight/quest/linux-status
chmod 700 "$XDG_RUNTIME_DIR"
# Fonts are bundled for offline use. Rebuild the ARM64 cache only when the
# font payload changes, keeping later launches inexpensive.
if [ -f /opt/quest/fonts.version ] && ! cmp -s /opt/quest/fonts.version "$HOME/.cache/stearlight-fonts.version";then
 if fc-cache -f /usr/share/fonts/truetype/noto /usr/share/fonts/opentype/noto;then
  cp /opt/quest/fonts.version "$HOME/.cache/stearlight-fonts.version"
 fi
fi
# Linux and Android share the real uid. Steam checks the uid on its local IPC.
app_uid=$(id -u)
sed -i '/^stearlight:/d' /etc/passwd
printf 'stearlight:x:%s:%s:Stearlight:/home/stearlight:/bin/bash\n' "$app_uid" "$(id -g)" >> /etc/passwd
echo "STEARLIGHT_QUEST_NATIVE_LINUX uid=$app_uid arch=$(uname -m)"
/lib/ld-linux-aarch64.so.1 --library-path /usr/lib/aarch64-linux-gnu:/opt/quest /opt/quest/gpu-probe
/lib/ld-linux-aarch64.so.1 --library-path /usr/lib/aarch64-linux-gnu:/opt/quest /opt/quest/gpu-probe --bridge
if ! /lib/ld-linux-aarch64.so.1 --library-path /usr/lib/aarch64-linux-gnu /opt/quest/ipc-probe;then
 printf '%s' 'Linux IPC verification failed' >/run/stearlight/quest/linux-status
 exit 78
fi
dbus-uuidgen --ensure=/etc/machine-id
mkdir -p /run/stearlight/quest/desktop-services
rm -f /run/stearlight/quest/desktop-services/*.service
dbus_pid=$(dbus-daemon --config-file=/opt/quest/system-bus.conf --fork --print-pid)
export DBUS_SESSION_BUS_ADDRESS=unix:path="$XDG_RUNTIME_DIR/bus"
export PULSE_SERVER=unix:$XDG_RUNTIME_DIR/pulse/native
export PULSE_SINK=quest_output PULSE_SOURCE=quest_microphone
export PULSE_LATENCY_MSEC=30
/bin/bash /opt/quest/prepare-desktop.sh
/usr/bin/python3 /opt/quest/desktop-manager.py serve >/tmp/quest-desktop-manager.log 2>&1 &
desktop_pid=$!
pulseaudio -n --daemonize=no --exit-idle-time=-1 --use-pid-file=no \
 --log-target=stderr --file=/opt/quest/audio.pa >/tmp/quest-audio.log 2>&1 &
audio_pid=$!
pipewire_pid=
wireplumber_pid=
mkdir -p /var/lib/xkb
xkbcomp -version
printf '%s\n' 'xkb_keymap { xkb_keycodes { include "evdev+aliases(qwerty)" }; xkb_types { include "complete" }; xkb_compatibility { include "complete" }; xkb_symbols { include "pc+us+inet(evdev)" }; };' | xkbcomp -w 0 -xkm - /tmp/quest-keymap.xkm
# Android owns the display. Xvfb supplies X11 services to Valve's Linux clients;
# the Quest Vulkan bridge supplies the GPU swapchain, independently of Xvfb.
# This is the flat sharing monitor, independent of OpenXR's per-eye resolution.
Xvfb :0 -screen 0 1280x720x24 -xkbdir /opt/frame-gamescope/share/X11/xkb -ac -nolisten tcp -noreset > /tmp/xvfb.log 2>&1 &
xvfb_pid=$!
trap 'kill ${wireplumber_pid:+"$wireplumber_pid"} ${pipewire_pid:+"$pipewire_pid"} "$desktop_pid" "$audio_pid" "$xvfb_pid" "$dbus_pid" 2>/dev/null || true' EXIT
for attempt in $(seq 1 50);do
 if pactl info >/dev/null 2>&1;then echo STEARLIGHT_QUEST_AUDIO_READY;break;fi
 if ! kill -0 "$audio_pid" 2>/dev/null;then cat /tmp/quest-audio.log >&2;break;fi
 sleep .1
done
# The spatial PCVR player uses native PipeWire streams, rather than PulseAudio.
# Publish the same real speaker/microphone endpoints through Pulse tunnels.
export PIPEWIRE_RUNTIME_DIR="$XDG_RUNTIME_DIR"
export PIPEWIRE_MODULE_DIR=/usr/lib/aarch64-linux-gnu/pipewire-0.3
export SPA_PLUGIN_DIR=/usr/lib/aarch64-linux-gnu/spa-0.2
/lib/ld-linux-aarch64.so.1 --library-path /usr/lib/aarch64-linux-gnu \
 /usr/bin/pipewire -c /opt/quest/pipewire.conf >/tmp/quest-pipewire.log 2>&1 &
pipewire_pid=$!
for attempt in $(seq 1 100);do
 if [ -S "$XDG_RUNTIME_DIR/pipewire-0" ];then break;fi
 if ! kill -0 "$pipewire_pid" 2>/dev/null;then cat /tmp/quest-pipewire.log >&2;break;fi
 sleep .05
done
/lib/ld-linux-aarch64.so.1 --library-path /usr/lib/aarch64-linux-gnu \
 /usr/bin/wireplumber >/tmp/quest-wireplumber.log 2>&1 &
wireplumber_pid=$!
export QUEST_PIPEWIRE_PID="$pipewire_pid" QUEST_WIREPLUMBER_PID="$wireplumber_pid"
for attempt in $(seq 1 100);do
 if xset q >/dev/null 2>&1;then break;fi
 if ! kill -0 "$xvfb_pid" 2>/dev/null;then cat /tmp/xvfb.log;exit 1;fi
 sleep .1
done
if ! xset q >/dev/null 2>&1;then cat /tmp/xvfb.log;exit 1;fi
echo STEARLIGHT_QUEST_X11_READY
# Verify pixels in shared GPU memory, not just extension names. SteamVR uses
# this separate GLX context even when its browser renders through ANGLE.
VK_ICD_FILENAMES=/opt/quest/quest_icd.json /opt/quest/glx-probe
VK_ICD_FILENAMES=/opt/quest/turnip_icd.json /opt/quest/glx-probe --egl
export VR_PATHREG_OVERRIDE="$HOME/.config/openvr/openvrpaths.vrpath"
export VR_CONFIG_PATH="$HOME/.steam/steam/config"
mkdir -p "$HOME/.config/openvr" "$VR_CONFIG_PATH"
/usr/bin/python3 /opt/quest/activate-os-update.py
/bin/sh /opt/quest/prepare-vrclient.sh || true
/usr/local/bin/stearlight-steamvr-native prepare
/bin/sh /opt/quest/prepare-frame-controls.sh || true
/usr/bin/python3 /opt/quest/prepare-gamepad-ui.py || true
# SteamVR invokes this installed launcher directly. Refresh our Quest adapter
# after runtime updates as well as APK updates.
cp /usr/local/bin/stearlight-frame-vrwebhelper /opt/steamvr/bin/vrwebhelper/linuxarm64/vrwebhelper.sh
chmod 755 /opt/steamvr/bin/vrwebhelper/linuxarm64/vrwebhelper.sh
/opt/steamvr/bin/linuxarm64/vrpathreg removedriver /opt/steamvr/drivers/stearlight-fake || true
/opt/steamvr/bin/linuxarm64/vrpathreg adddriver /opt/quest/stearlight-quest
if [ ! -f "$VR_CONFIG_PATH/quest-prepared" ];then
 cat > "$VR_CONFIG_PATH/steamvr.vrsettings" <<'SETTINGS'
{"steamvr":{"requireHmd":true,"activateMultipleDrivers":true,"allowDisplayLockedMode":true,"allowFallbackMirrorWindowLinux":true,"disableAsync":false,"enableLinuxVulkanAsync":true,"useFacetRenderer":true,"renderSystemLayerInDistortPass":true,"mirrorViewGeometry":"0 0 3664 1920","showMirrorView":false,"showLegacyMirrorView":false},"direct_mode":{"enable":false},"driver_stearlight-fake":{"enable":false},"driver_stearlight-quest":{"enable":true,"blocked_by_safe_mode":false}}
SETTINGS
 touch "$VR_CONFIG_PATH/quest-prepared"
fi
# Existing installations also need Frame's final-pass composition. The old
# precomposition path asserts when a spatially encoded PCVR frame arrives.
sed -i 's/"useFacetRenderer"[[:space:]]*:[[:space:]]*false/"useFacetRenderer":true/g;s/"renderSystemLayerInDistortPass"[[:space:]]*:[[:space:]]*false/"renderSystemLayerInDistortPass":true/g' "$VR_CONFIG_PATH/steamvr.vrsettings"
# Frame's computer-vision driver requires its physical cameras. Quest tracking
# and passthrough come from OpenXR through stearlight-quest instead.
/usr/bin/python3 - <<'PY'
import json
from pathlib import Path
path = Path('/home/stearlight/.steam/steam/config/steamvr.vrsettings')
settings = json.loads(path.read_text())
settings.setdefault('driver_cv', {})['enable'] = False
path.write_text(json.dumps(settings) + '\n')
PY
export STEARLIGHT_STEAM_VULKAN_ICD=/opt/quest/quest_icd.json
export STEARLIGHT_STEAMVR_VULKAN_ICD=/opt/quest/turnip_icd.json
export STEARLIGHT_GAMESCOPE_VK_ICD=/opt/quest/turnip_icd.json
export VK_ICD_FILENAMES=/opt/quest/turnip_icd.json
# The adapter supplies Valve's required surface extension discovery; ANGLE
# renders offscreen. Only vrcompositor may create an OpenXR display swapchain.
# Adapt the installed UI rather than copying a library from another version.
/bin/sh /opt/quest/prepare-steam-client.sh
/bin/sh /opt/quest/prepare-streaming-client.sh
# The APK verifies its rootfs payload. Steam's optional repair pass would undo
# the VR Store-origin adapter before its browser starts.
export STEAM_EXTRA_ARGS="${STEAM_EXTRA_ARGS:-} -cef-use-vulkan -noverifyfiles"
export STEARLIGHT_FAKE_TRACKING=0
printf '%s' 'Starting SteamVR and Steam' >/run/stearlight/quest/linux-status
export LD_PRELOAD=/opt/quest/libquest_socket.so
# PRoot's --kill-on-exit owns service cleanup. Avoid keeping another shell
# alive while games add their FEX processes to Android's child-process limit.
exec /opt/quest/runtime-monitor --launch-session >/tmp/quest-runtime-monitor.log 2>&1
