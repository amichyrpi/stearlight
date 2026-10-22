#!/bin/bash
set -eu
unset LD_LIBRARY_PATH LD_PRELOAD EGL_PLATFORM
. /run/stearlight/quest/desktop-shell.env
export PATH=/opt/stearlight-desktop/usr/bin:/usr/local/bin:/usr/bin:/bin
export XDG_CONFIG_DIRS=/opt/stearlight-desktop/etc/xdg:/etc/xdg
export XDG_MENU_PREFIX=plasma- KDE_FULL_SESSION=true KDE_SESSION_VERSION=6
export VLC_PLUGIN_PATH=/opt/stearlight-desktop/usr/lib/vlc/plugins
export QT_XCB_GL_INTEGRATION=none QT_XCB_NO_MITSHM=1
export GLIBC_TUNABLES=glibc.pthread.rseq=0
exec "$@"
