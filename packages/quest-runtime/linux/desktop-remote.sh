#!/bin/bash
set -eu
unset LD_LIBRARY_PATH LD_PRELOAD
export HOME=/home/stearlight USER=stearlight LOGNAME=stearlight
export PATH=/usr/local/bin:/usr/bin:/bin
export XDG_RUNTIME_DIR=/tmp/stearlight-runtime
export PULSE_SERVER=unix:$XDG_RUNTIME_DIR/pulse/native
/usr/local/bin/stearlight-desktop remote-start
# The manager watches XRDP's Xorg process and owns detached KDE cleanup.
exec /bin/bash /opt/quest/desktop-session.sh remote
