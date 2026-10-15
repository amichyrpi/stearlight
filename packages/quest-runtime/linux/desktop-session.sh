#!/bin/bash
# Launch Valve's Plasma workspace on the display provided by Gamescope/XRDP.
set -eu
desktop=/opt/stearlight-desktop
unset LD_LIBRARY_PATH LD_PRELOAD EGL_PLATFORM XDG_DESKTOP_PORTAL_DIR
export PATH="$desktop/usr/bin:/usr/local/bin:/usr/bin:/bin"
export XDG_DATA_DIRS="$desktop/usr/share:/usr/share"
export XDG_MENU_PREFIX=plasma-
export XDG_CONFIG_DIRS="$desktop/etc/xdg:/etc/xdg"
export XDG_CONFIG_HOME="$HOME/.config/stearlight-plasma"
export QT_PLUGIN_PATH="$desktop/usr/lib/qt6/plugins:$desktop/usr/lib/qt/plugins"
export QML_IMPORT_PATH="$desktop/usr/lib/qt6/qml"
export QML2_IMPORT_PATH="$QML_IMPORT_PATH"
export QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software
export QT_XCB_GL_INTEGRATION=none
# Qt's private Frame libc cannot share SysV image buffers with the guest X
# server through PRoot. Send the software backing store over X11 instead.
export QT_XCB_NO_MITSHM=1
export QT_FORCE_STDERR_LOGGING=1
export FONTCONFIG_PATH="$desktop/etc/fonts"
export FONTCONFIG_FILE=/opt/quest/desktop-fonts.conf
export LOCPATH="$desktop/usr/lib/locale" LANG=C.UTF-8 LC_ALL=C.UTF-8
export XCURSOR_PATH="$desktop/usr/share/icons:/usr/share/icons"
export XCURSOR_THEME=breeze_cursors
export GSETTINGS_SCHEMA_DIR="$desktop/usr/share/glib-2.0/schemas"
export VLC_PLUGIN_PATH="$desktop/usr/lib/vlc/plugins"
export KDE_FULL_SESSION=true KDE_SESSION_VERSION=6
export XDG_CURRENT_DESKTOP=KDE XDG_SESSION_DESKTOP=KDE XDG_SESSION_TYPE=x11
export KDE_SKIP_SYSTEMD_BOOT=1
export GLIBC_TUNABLES=glibc.pthread.rseq=0
mkdir -p "$XDG_CONFIG_HOME" "$HOME/Desktop" "$HOME/.local/share/applications"
printf '[General]\nsystemdBoot=false\n' >"$XDG_CONFIG_HOME/startkderc"
printf '[KSplash]\nEngine=none\nTheme=None\n' >"$XDG_CONFIG_HOME/ksplashrc"
printf '[Basic Settings]\nIndexing-Enabled=false\n' >"$XDG_CONFIG_HOME/baloofilerc"
if [ ! -f "$XDG_CONFIG_HOME/kdeglobals" ];then
 printf '[KDE]\nLookAndFeelPackage=com.valve.vapor.desktop\nwidgetStyle=Breeze\n[General]\nColorScheme=Vapor\n[Icons]\nTheme=breeze-dark\n' >"$XDG_CONFIG_HOME/kdeglobals"
fi
if ! grep -qF '[Colors:View]' "$XDG_CONFIG_HOME/kdeglobals";then
 cat "$desktop/usr/share/color-schemes/Vapor.colors" >>"$XDG_CONFIG_HOME/kdeglobals"
fi
if [ ! -f "$XDG_CONFIG_HOME/plasmarc" ];then
 printf '[Theme]\nname=Vapor\n' >"$XDG_CONFIG_HOME/plasmarc"
fi
if [ ! -f "$XDG_CONFIG_HOME/kwinrc" ];then
 printf '[Compositing]\nEnabled=false\n' >"$XDG_CONFIG_HOME/kwinrc"
fi
if [ ! -f "$XDG_CONFIG_HOME/kscreenlockerrc" ];then
 printf '[Daemon]\nAutolock=false\nLockOnResume=false\n' >"$XDG_CONFIG_HOME/kscreenlockerrc"
fi
cat >"$HOME/Desktop/Return to Gaming Mode.desktop" <<'ENTRY'
[Desktop Entry]
Name=Return to Gaming Mode
Comment=Close the Plasma desktop and return to Steam
Type=Application
Icon=steam
Exec=/usr/local/bin/stearlight-desktop stop
Terminal=false
ENTRY
chmod 755 "$HOME/Desktop/Return to Gaming Mode.desktop"
for application in org.kde.dolphin org.kde.konsole systemsettings;do
 source="$desktop/usr/share/applications/$application.desktop"
 target="$HOME/Desktop/$application.desktop"
 if [ -f "$source" ] && [ ! -f "$target" ];then
  cp "$source" "$target"
  chmod 755 "$target"
 fi
done
# The Frame folder view uses indexing-backed virtual locations. PRoot has no
# Baloo service; keep this workspace on the user's real Desktop directory.
configuration="$XDG_CONFIG_HOME/plasma-org.kde.plasma.desktop-appletsrc"
if [ ! -f "$configuration" ];then
 cat >"$configuration" <<'LAYOUT'
[Containments][2]
formfactor=2
lastScreen=0
location=4
plugin=org.kde.panel

[Containments][2][Applets][3]
plugin=org.kde.plasma.kickoff

[Containments][2][Applets][4]
plugin=org.kde.plasma.icontasks

[Containments][2][Applets][4][Configuration][General]
launchers=applications:org.kde.dolphin.desktop,applications:org.kde.konsole.desktop,applications:systemsettings.desktop

[Containments][2][Applets][5]
plugin=org.kde.plasma.digitalclock

[Containments][2][General]
AppletOrder=3;4;5
LAYOUT
fi
# Plasma assigns desktop containment IDs when it creates the first activity.
# Apply defaults to real folder views rather than assuming containment 1.
/usr/bin/python3 - "$configuration" <<'PY'
import configparser, re, sys
from pathlib import Path
path = Path(sys.argv[1])
config = configparser.RawConfigParser(strict=False)
config.optionxform = str
config.read(path)
folders = [section for section in config.sections()
           if re.fullmatch(r'Containments\]\[\d+', section)
           and config.get(section, 'plugin', fallback='') == 'org.kde.plasma.folder']
for section in list(config.sections()):
    match = re.fullmatch(r'(Containments\]\[\d+)\]\[(Configuration\]\[General|Wallpaper\]\[org.kde.image\]\[General)', section)
    if match and not config.has_section(match[1]):
        config.remove_section(section)
for folder in folders:
    for section, key, value in [
        (folder + '][Configuration][General', 'url', 'file:///home/stearlight/Desktop'),
        (folder + '][Wallpaper][org.kde.image][General', 'Image',
         'file:///opt/stearlight-desktop/usr/share/wallpapers/Steam Deck Logo Default.jpg')]:
        if not config.has_section(section):
            config.add_section(section)
        if not config.has_option(section, key):
            config.set(section, key, value)
for section in list(config.sections()):
    if config.get(section, 'plugin', fallback='') != 'org.kde.plasma.kickoff':
        continue
    general = section + '][Configuration][General'
    if not config.has_section(general):
        config.add_section(general)
    if not config.has_option(general, 'favorites'):
        config.set(general, 'favorites', 'org.kde.dolphin.desktop,org.kde.konsole.desktop,systemsettings.desktop')
        config.set(general, 'favoritesPortedToKAstats', 'false')
with path.open('w') as stream:
    config.write(stream, space_around_delimiters=False)
PY
# The minimal session does not run the full login autostart that normally
# initializes KDE's application database.
"$desktop/usr/bin/kbuildsycoca6" --noincremental
if [ "${1:-remote}" = headset ];then
 parent_runtime="$XDG_RUNTIME_DIR"
 parent_wayland=gamescope-0
 for attempt in {1..100};do
  [ -S "$parent_runtime/$parent_wayland" ] && break
  sleep 0.1
 done
 case "$parent_wayland" in /*) ;; *) parent_wayland="$parent_runtime/$parent_wayland" ;; esac
 export XDG_RUNTIME_DIR="$parent_runtime/stearlight-plasma"
 mkdir -p "$XDG_RUNTIME_DIR/pulse"
 chmod 700 "$XDG_RUNTIME_DIR"
 ln -sf "$parent_runtime/pulse/native" "$XDG_RUNTIME_DIR/pulse/native"
 export WAYLAND_DISPLAY="$parent_wayland" XDG_SESSION_TYPE=wayland QT_QPA_PLATFORM=wayland
 export KWIN_COMPOSE=Q QT_QPA_OFFSCREEN_NO_GLX=1
 unset DISPLAY
 # The APK already owns a private bus. Reuse it rather than adding another
 # permanent child just for the headset workspace. KDE's activation services
 # inherit the nested display and the relocated Frame libraries.
 export DBUS_SESSION_BUS_ADDRESS=unix:path="$parent_runtime/bus"
 for service in "$desktop/usr/share/dbus-1/services/"*.service;do
  ln -sf "$service" "/run/stearlight/quest/desktop-services/${service##*/}"
 done
 /usr/bin/dbus-update-activation-environment DBUS_SESSION_BUS_ADDRESS PATH XDG_DATA_DIRS XDG_CONFIG_DIRS \
  XDG_CONFIG_HOME XDG_RUNTIME_DIR WAYLAND_DISPLAY XDG_SESSION_TYPE \
  XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP QT_QPA_PLATFORM QT_PLUGIN_PATH \
  QML_IMPORT_PATH QML2_IMPORT_PATH QT_QUICK_BACKEND QT_XCB_NO_MITSHM \
  FONTCONFIG_PATH FONTCONFIG_FILE LANG LC_ALL VLC_PLUGIN_PATH STEARLIGHT_DESKTOP_INSTANCE
 exec "$desktop/usr/bin/kwin_wayland" --socket wayland-0 \
  --wayland-display "$parent_wayland" --width 1280 --height 800 --no-lockscreen \
  --xwayland --exit-with-session /opt/quest/plasma-core-session.sh
fi
export DBUS_SESSION_BUS_ADDRESS=$("$desktop/usr/bin/dbus-daemon" --session --fork --print-address)
exec /opt/quest/plasma-core-session.sh remote
