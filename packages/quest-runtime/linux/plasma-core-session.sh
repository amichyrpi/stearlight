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
/usr/bin/python3 - <<'PY'
import os, shlex
from pathlib import Path
path = Path('/run/stearlight/quest/desktop-shell.env')
keys = ['DISPLAY', 'WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR', 'XDG_SESSION_TYPE', 'STEARLIGHT_DESKTOP_INSTANCE',
        'DBUS_SESSION_BUS_ADDRESS', 'PULSE_SERVER', 'XDG_CONFIG_HOME',
        'XDG_DATA_DIRS', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH',
        'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'FONTCONFIG_PATH', 'FONTCONFIG_FILE',
        'LANG', 'LC_ALL', 'LANGUAGE', 'SPA_PLUGIN_DIR']
temporary = path.with_suffix('.next')
temporary.write_text(''.join('export ' + key + '=' + shlex.quote(os.environ[key]) + '\n'
                             for key in keys if key in os.environ))
temporary.chmod(0o600)
temporary.replace(path)
PY
exec "$desktop/usr/bin/plasmashell"
