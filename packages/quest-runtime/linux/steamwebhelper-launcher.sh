#!/bin/bash
set -eu
directory=${STEARLIGHT_STEAM_ROOT:-/opt/steam}/steamrtarm64
cd "$directory"
export DISPLAY=${DISPLAY:-:0} LIBGL_KOPPER_DISABLE=true
export LD_PRELOAD=/opt/quest/libquest_socket.so
. /opt/quest/cef-arguments.sh
# Android limits unmanaged child processes. Keep renderers separate while
# avoiding the two zygotes for each CEF browser.
exec taskset 0x7c "$directory/steamwebhelper" "${quest_cef_args[@]}" \
 --no-zygote --renderer-process-limit=1 \
 >"$HOME/.steam/steam/logs/steamwebhelper.log" 2>&1
