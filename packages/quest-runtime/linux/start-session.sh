#!/bin/bash
set -eu
unset LD_LIBRARY_PATH LD_PRELOAD
export HOME=/home/stearlight USER=stearlight LOGNAME=stearlight
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export DISPLAY=:0 XDG_RUNTIME_DIR=/tmp/stearlight-runtime
export GLIBC_TUNABLES=glibc.pthread.rseq=0
export MESA_VK_ENABLE_SUBMIT_THREAD=1
export QUEST_SOCKET_REGISTRY=/run/stearlight/quest/socket-registry.sock
# SteamVR creates a separate GLX context for browser texture sharing. Xvfb
# has no DRI3 display; Zink still renders its offscreen GL objects on Turnip.
export GALLIUM_DRIVER=zink MESA_LOADER_DRIVER_OVERRIDE=zink
export LIBGL_KOPPER_DISABLE=true LIBGL_KOPPER_DRI2=1
# SDL's OpenVR streaming player uses EGL without an Android/X11 window.
export EGL_PLATFORM=surfaceless
mkdir -p "$HOME" "$HOME/.config" "$HOME/.cache" "$XDG_RUNTIME_DIR" /tmp/.X11-unix /run/dbus
rm -f /run/stearlight/quest/dashboard.ready /tmp/.X0-lock /tmp/.X11-unix/X0
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
dbus_pid=$(dbus-daemon --session --fork --print-pid --address=unix:path="$XDG_RUNTIME_DIR/bus")
system_bus_pid=$(dbus-daemon --config-file=/opt/quest/system-bus.conf --fork --print-pid)
/opt/quest/bluetooth-bridge >/tmp/quest-bluetooth.log 2>&1 &
bluetooth_pid=$!
export DBUS_SESSION_BUS_ADDRESS=unix:path="$XDG_RUNTIME_DIR/bus"
export PULSE_SERVER=unix:$XDG_RUNTIME_DIR/pulse/native
export PULSE_SINK=quest_output PULSE_SOURCE=quest_microphone
export PULSE_LATENCY_MSEC=30
pulseaudio -n --daemonize=no --exit-idle-time=-1 --use-pid-file=no \
 --log-target=stderr --file=/opt/quest/audio.pa >/tmp/quest-audio.log 2>&1 &
audio_pid=$!
mkdir -p /var/lib/xkb
xkbcomp -version
printf '%s\n' 'xkb_keymap { xkb_keycodes { include "evdev+aliases(qwerty)" }; xkb_types { include "complete" }; xkb_compatibility { include "complete" }; xkb_symbols { include "pc+us+inet(evdev)" }; };' | xkbcomp -w 0 -xkm - /tmp/quest-keymap.xkm
# Android owns the display. Xvfb supplies X11 services to Valve's Linux clients;
# the Quest Vulkan bridge supplies the GPU swapchain, independently of Xvfb.
Xvfb :0 -screen 0 3664x1920x24 -xkbdir /opt/frame-gamescope/share/X11/xkb -ac -nolisten tcp -noreset > /tmp/xvfb.log 2>&1 &
xvfb_pid=$!
monitor_pid=
store_watch_pid=
trap 'kill ${monitor_pid:+"$monitor_pid"} ${store_watch_pid:+"$store_watch_pid"} "$audio_pid" "$bluetooth_pid" "$xvfb_pid" "$dbus_pid" "$system_bus_pid" 2>/dev/null || true' EXIT
for attempt in $(seq 1 50);do
 if pactl info >/dev/null 2>&1;then echo STEARLIGHT_QUEST_AUDIO_READY;break;fi
 if ! kill -0 "$audio_pid" 2>/dev/null;then cat /tmp/quest-audio.log >&2;break;fi
 sleep .1
done
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
/bin/sh /opt/quest/prepare-vrclient.sh || true
/usr/local/bin/stearlight-steamvr-native prepare
/bin/sh /opt/quest/prepare-frame-controls.sh || true
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
export STEARLIGHT_STEAM_VULKAN_ICD=/opt/quest/quest_icd.json
export STEARLIGHT_STEAMVR_VULKAN_ICD=/opt/quest/turnip_icd.json
export STEARLIGHT_GAMESCOPE_VK_ICD=/opt/quest/turnip_icd.json
export VK_ICD_FILENAMES=/opt/quest/turnip_icd.json
# The adapter supplies Valve's required surface extension discovery; ANGLE
# renders offscreen. Only vrcompositor may create an OpenXR display swapchain.
# Adapt the installed UI rather than copying a library from another version.
/bin/sh /opt/quest/prepare-steam-client.sh
/bin/sh /opt/quest/prepare-streaming-client.sh
/opt/quest/store-browser-watch >/tmp/quest-store-browser.log 2>&1 &
store_watch_pid=$!
# The APK verifies its rootfs payload. Steam's optional repair pass would undo
# the VR Store-origin adapter before its browser starts.
export STEAM_EXTRA_ARGS="${STEAM_EXTRA_ARGS:-} -cef-use-vulkan -noverifyfiles"
export STEARLIGHT_FAKE_TRACKING=0
export STEARLIGHT_QUEST_RUNTIME=1
printf '%s' 'Starting SteamVR and Steam' >/run/stearlight/quest/linux-status
(
 for attempt in $(seq 1 120);do
  if pidof vrcompositor >/dev/null 2>&1;then
   exec /opt/quest/runtime-monitor
  fi
  sleep .25
 done
 echo 'Quest runtime monitor: compositor did not start' >&2
 exit 1
) >/tmp/quest-runtime-monitor.log 2>&1 &
monitor_pid=$!
export LD_PRELOAD=/opt/quest/libquest_socket.so
/opt/quest/frame-session
