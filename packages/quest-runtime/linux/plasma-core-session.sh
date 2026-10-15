#!/bin/bash
set -eu
desktop=/opt/stearlight-desktop
if [ "${1:-}" = remote ];then
 "$desktop/usr/bin/kwin_x11" --replace &
fi
if [ "${XDG_SESSION_TYPE:-}" = wayland ];then
export WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland
# KWin's exit-with-session child inherits the outer display environment.
# Connect the workspace to the nested compositor after its socket is ready.
for attempt in {1..100};do
 [ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ] && break
 sleep 0.1
done
 /usr/bin/dbus-update-activation-environment WAYLAND_DISPLAY DISPLAY
fi
# The workspace, window manager and activity service supply the desktop.
# Full login-session autostart exceeds Quest's Android child-process limit.
exec "$desktop/usr/bin/plasmashell"
