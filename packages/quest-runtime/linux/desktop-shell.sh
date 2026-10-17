#!/bin/bash
unset LD_LIBRARY_PATH LD_PRELOAD
export HOME=/home/stearlight USER=stearlight LOGNAME=stearlight
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:/opt/stearlight-desktop/usr/bin
# Keep the nested compositor's display. SSH/ADB shells may also join the
# active desktop using its own exported environment, without guessing :0.
if [ -z "${STEARLIGHT_DESKTOP_INSTANCE:-}" ] && [ -O /run/stearlight/quest/desktop-shell.env ];then
 . /run/stearlight/quest/desktop-shell.env
fi
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/stearlight-runtime}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/tmp/stearlight-runtime/bus}"
export PULSE_SERVER="${PULSE_SERVER:-unix:/tmp/stearlight-runtime/pulse/native}"
if [ -z "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ];then export DISPLAY=:0;fi
export GLIBC_TUNABLES=glibc.pthread.rseq=0
cd "$HOME"
exec /bin/bash "$@"
