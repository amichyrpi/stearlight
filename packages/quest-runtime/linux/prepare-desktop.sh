#!/bin/bash
set -eu
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
desktop=/opt/stearlight-desktop
[ -x "$desktop/usr/bin/plasmashell" ] || exit 0
if [ -f /opt/quest/prepare-desktop-cli.sh ];then
 /bin/bash /opt/quest/prepare-desktop-cli.sh
fi
# Remove indexing plugins left by an older desktop payload. This workspace
# has no Baloo daemon; KIO must not advertise its unavailable protocols.
for plugin in kio/tags.so kio/timeline.so kio/baloosearch.so \
 kded/baloosearchmodule.so kfileitemaction/tagsfileitemaction.so;do
 rm -f "$desktop/usr/lib/qt6/plugins/kf6/$plugin"
done
rm -f "$desktop/usr/share/dbus-1/services/org.kde.KSplash.service"
# Relocated VLC plugins cannot use the recovery image's original cache.
rm -f "$desktop/usr/lib/vlc/plugins/plugins.dat"
/usr/bin/python3 /opt/quest/prepare-desktop-kio.py
if [ -x /opt/stearlight-browser/usr/lib/chromium/chromium ];then
 cat >"$desktop/usr/share/applications/org.chromium.Chromium.desktop" <<'ENTRY'
[Desktop Entry]
Type=Application
Name=Chromium
GenericName=Web Browser
Exec=/opt/quest/chromium-launcher.sh %U
Icon=chromium
Terminal=false
Categories=Network;WebBrowser;
MimeType=text/html;x-scheme-handler/http;x-scheme-handler/https;
StartupWMClass=chromium
ENTRY
 # Chromium's Debian desktop file otherwise shadows the Frame entry and
 # points to an uninstalled /usr/bin wrapper. Keep one visible menu entry.
 cp "$desktop/usr/share/applications/org.chromium.Chromium.desktop" \
  /opt/stearlight-browser/usr/share/applications/chromium.desktop
 printf 'NoDisplay=true\n' >>/opt/stearlight-browser/usr/share/applications/chromium.desktop
 if [ ! -e /usr/bin/chromium ];then ln -s /opt/quest/chromium-launcher.sh /usr/bin/chromium;fi
fi
mkdir -p /usr/lib /usr/local/bin
ln -sf /opt/quest/desktop-manager.py /usr/local/bin/stearlight-desktop
ln -sf /opt/quest/desktop-launcher.sh /usr/bin/steamos-nested-desktop
mkdir -p /usr/share/applications
for setting in headset wifi bluetooth;do
 case "$setting" in
  headset) name='Headset Settings';icon=preferences-system;;
  wifi) name='Wi-Fi Connections';icon=network-wireless;;
  bluetooth) name='Bluetooth Devices';icon=preferences-system-bluetooth;;
 esac
 cat >"$desktop/usr/share/applications/stearlight-$setting.desktop" <<ENTRY
[Desktop Entry]
Type=Application
Name=$name
Comment=Configure the Quest hardware in Horizon OS
Exec=/bin/bash /opt/quest/desktop-device-settings.sh $setting
Icon=$icon
Terminal=false
Categories=Settings;HardwareSettings;
ENTRY
done
if [ -f "$desktop/usr/share/applications/deckard-nested-desktop.desktop" ];then
 ln -sf "$desktop/usr/share/applications/deckard-nested-desktop.desktop" /usr/share/applications/deckard-nested-desktop.desktop
fi
if [ -d "$desktop/usr/share/applications/steam" ] && [ ! -e /usr/share/applications/steam ];then
 ln -s "$desktop/usr/share/applications/steam" /usr/share/applications/steam
fi
for command in plasma_session plasmashell kwin_x11 kwin_wayland kwin_wayland_wrapper \
 ksmserver kded6 kactivitymanagerd kglobalacceld kaccess ksplashqml plasma_waitforname \
 kbuildsycoca6 kreadconfig6 kwriteconfig6 kquitapp6 kstart kcminit kcminit_startup unix_chkpwd xauth xrdp-chansrv \
 dolphin konsole systemsettings plasma-discover kmenuedit plasma-emojier kcmshell6 kdialog \
 vlc bssh bvnc;do
 if [ -x "$desktop/usr/bin/$command" ] && [ ! -e "/usr/bin/$command" ];then
  ln -s "$desktop/usr/bin/$command" "/usr/bin/$command"
 fi
done
for command in gpg gpgv gpgconf gpg-agent gpg-connect-agent gpgsm dirmngr pinentry;do
 if [ -x "$desktop/usr/bin/$command" ] && [ ! -e "/usr/bin/$command" ];then
  ln -s "$desktop/usr/bin/$command" "/usr/bin/$command"
 fi
done
if [ -d "$desktop/usr/lib/gnupg" ] && [ ! -e /usr/lib/gnupg ];then
 ln -s "$desktop/usr/lib/gnupg" /usr/lib/gnupg
fi
if [ -f /opt/quest/prepare-flatpak.py ] && [ -x /usr/bin/gpg ];then
 /usr/bin/python3 /opt/quest/prepare-flatpak.py
fi
for command in kactivitymanagerd kglobalacceld;do
 if [ -x "$desktop/usr/lib/$command" ] && [ ! -e "/usr/lib/$command" ];then
  ln -s "$desktop/usr/lib/$command" "/usr/lib/$command"
 fi
done
for directory in kf6 qt6 plasma xrdp;do
 if [ -d "$desktop/usr/lib/$directory" ] && [ ! -e "/usr/lib/$directory" ];then
  ln -s "$desktop/usr/lib/$directory" "/usr/lib/$directory"
 fi
done
if [ -d "$desktop/usr/lib/ssh" ] && [ ! -e /usr/lib/ssh ];then
 ln -s "$desktop/usr/lib/ssh" /usr/lib/ssh
fi
if [ -d "$desktop/usr/share/xrdp" ] && [ ! -e /usr/share/xrdp ];then
 ln -s "$desktop/usr/share/xrdp" /usr/share/xrdp
fi
