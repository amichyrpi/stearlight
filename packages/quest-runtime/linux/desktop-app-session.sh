#!/bin/bash
set -eu
desktop=/opt/stearlight-desktop
unset LD_LIBRARY_PATH LD_PRELOAD EGL_PLATFORM XDG_DESKTOP_PORTAL_DIR
export PATH="$desktop/usr/bin:/opt/stearlight-browser/usr/bin:/usr/local/bin:/usr/bin:/bin"
export XDG_DATA_DIRS="$HOME/.local/share/flatpak/exports/share:$HOME/.local/share:/opt/stearlight-browser/usr/share:$desktop/usr/share:/usr/share"
export XDG_CONFIG_DIRS="$desktop/etc/xdg:/etc/xdg"
export XDG_CONFIG_HOME="$HOME/.config/stearlight-plasma"
export XDG_MENU_PREFIX=plasma- KDE_FULL_SESSION=true KDE_SESSION_VERSION=6
export QT_PLUGIN_PATH="$desktop/usr/lib/qt6/plugins:$desktop/usr/lib/qt/plugins"
export QML_IMPORT_PATH="$desktop/usr/lib/qt6/qml" QML2_IMPORT_PATH="$desktop/usr/lib/qt6/qml"
export QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software QT_XCB_GL_INTEGRATION=none QT_XCB_NO_MITSHM=1
export FONTCONFIG_PATH="$desktop/etc/fonts" FONTCONFIG_FILE=/opt/quest/desktop-fonts.conf
export GSETTINGS_SCHEMA_DIR="$desktop/usr/share/glib-2.0/schemas"
export VLC_PLUGIN_PATH="$desktop/usr/lib/vlc/plugins" SPA_PLUGIN_DIR="$desktop/usr/lib/spa-0.2"
export GLIBC_TUNABLES=glibc.pthread.rseq=0
export STEARLIGHT_DESKTOP_APP_WINDOW=1
eval "$(/usr/bin/python3 /opt/quest/desktop-defaults.py locale)"
# Native switches keep an existing KDE process from taking the new window.
case "${1##*/}" in
 dolphin) command=$1;shift;set -- "$command" --new-window "$@";;
 konsole) command=$1;shift;set -- "$command" --separate "$@";;
 vlc) command=$1;shift;set -- "$command" --no-one-instance "$@";;
esac
export XDG_SESSION_TYPE=x11
exec "$@"
