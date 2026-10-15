#!/bin/bash
unset LD_LIBRARY_PATH LD_PRELOAD
export HOME=/home/stearlight USER=stearlight LOGNAME=stearlight
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export DISPLAY=:0 XDG_RUNTIME_DIR=/tmp/stearlight-runtime
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
export PULSE_SERVER=unix:$XDG_RUNTIME_DIR/pulse/native
export GLIBC_TUNABLES=glibc.pthread.rseq=0
cd "$HOME"
exec /bin/bash "$@"
