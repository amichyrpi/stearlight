#!/bin/bash
set -eu
unset LD_LIBRARY_PATH LD_PRELOAD EGL_PLATFORM
export PATH=/opt/quest:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export GLIBC_TUNABLES=glibc.pthread.rseq=0
export VK_ICD_FILENAMES=/opt/quest/turnip_icd.json
export XKB_CONFIG_ROOT=/opt/frame-gamescope/share/X11/xkb
export VR_PATHREG_OVERRIDE="$HOME/.config/openvr/openvrpaths.vrpath"
export VR_CONFIG_PATH="$HOME/.steam/steam/config"
export QUEST_SOCKET_REGISTRY=/run/stearlight/quest/socket-registry.sock
export STEARLIGHT_DESKTOP_GAMESCOPE=1
exec /opt/frame-gamescope/lib/ld-linux-aarch64.so.1 \
 --preload /opt/quest/libquest_socket.so:/usr/lib/aarch64-linux-gnu/libpipewire-0.3.so.0 \
 --library-path /opt/frame-gamescope/lib:/usr/lib/aarch64-linux-gnu \
 /opt/frame-gamescope/bin/gamescope --backend openvr --expose-wayland \
 --xwayland-count 1 -w 1280 -h 800 -W 1280 -H 800 -r 60 \
 --steam --virtual-connector-strategy PerAppId \
 --vr-overlay-key stearlight.plasma.desktop --vr-overlay-show-immediately \
 --vr-app-overlay-key stearlight.desktop.app \
 --vr-overlay-default-name Desktop \
 --vr-overlay-icon /opt/quest/desktop-icons/desktop.png \
 --vr-overlay-enable-control-bar --vr-overlay-enable-control-bar-keyboard
