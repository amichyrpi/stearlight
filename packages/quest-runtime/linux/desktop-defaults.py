#!/usr/bin/python3
"""Initialize desktop defaults while preserving user profiles and layouts."""
import configparser
import os
from pathlib import Path
import re
import shlex
import sys


def read(path):
    value = configparser.RawConfigParser(strict=False)
    value.optionxform = str
    value.read(path)
    return value


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w') as stream:
        value.write(stream, space_around_delimiters=False)


def configure(home, settings, desktop=Path('/opt/stearlight-desktop')):
    face = home / '.face.icon'
    source = desktop / 'usr/share/pixmaps/steamos.png'
    legacy = desktop / 'usr/share/steamos/steam_icon.png'
    default_face = not face.exists() or (legacy.is_file() and face.read_bytes() == legacy.read_bytes())
    if default_face and source.is_file():
        temporary = face.with_suffix('.next')
        temporary.write_bytes(source.read_bytes())
        temporary.replace(face)
    header = desktop / 'usr/share/plasma/plasmoids/org.kde.plasma.kickoff/contents/ui/Header.qml'
    if header.is_file():
        source = header.read_text()
        gate = 'visible: KConfig.KAuthorized.authorizeControlModule("kcm_users")'
        if gate in source:
            # The account editor is unavailable in PRoot; the real KUser
            # identity/avatar should still be visible as a read-only profile.
            source = source.replace(gate,
                'visible: true\n            enabled: KConfig.KAuthorized.authorizeControlModule("kcm_users")')
        # Kirigami's compiled avatar uses a shader unsupported by Qt Quick's
        # software renderer. Override only this menu's image, preserving the
        # native KUser identity and the user's selected face icon.
        if '// Stearlight profile image' not in source:
            source = source.replace('source: kuser.faceIconUrl', '''source: kuser.faceIconUrl

            // Stearlight profile image
            contentItem: Image {
                source: kuser.faceIconUrl
                fillMode: Image.PreserveAspectFit
                cache: false
            }''')
        header.write_text(source)
    profile = home / '.local/share/konsole/Stearlight.profile'
    if not profile.exists():
        profile.parent.mkdir(parents=True, exist_ok=True)
        profile.write_text('[Appearance]\nColorScheme=Vapor\n[General]\n'
            'Name=Stearlight\nParent=Vapor.profile\n'
            'Command=/bin/bash /opt/quest/desktop-shell.sh\n'
            'Directory=/home/stearlight\n')
    path = settings / 'konsolerc'
    value = read(path)
    current = value.get('Desktop Entry', 'DefaultProfile', fallback='')
    if current in ('', 'Vapor.profile', 'FALLBACK/'):
        if not value.has_section('Desktop Entry'):
            value.add_section('Desktop Entry')
        value.set('Desktop Entry', 'DefaultProfile', 'Stearlight.profile')
        write(path, value)
    # Upgrade only the original minimal panel, not a user's rearranged layout.
    path = settings / 'plasma-org.kde.plasma.desktop-appletsrc'
    value = read(path)
    panel = 'Containments][2'
    general = panel + '][General'
    if value.get(general, 'AppletOrder', fallback='') == '3;4;5':
        identifiers = [int(x) for section in value.sections()
                       for x in re.findall(r'\]\[(\d+)', '[' + section)]
        tray = str(max(identifiers + [5]) + 1)
        section = panel + '][Applets][' + tray
        value.add_section(section)
        value.set(section, 'plugin', 'org.kde.plasma.systemtray')
        value.set(general, 'AppletOrder', '3;4;' + tray + ';5')
        write(path, value)


def locale(settings):
    value = read(settings / 'plasma-localerc')
    language = value.get('Formats', 'LANG', fallback='C.UTF-8')
    translations = value.get('Translations', 'LANGUAGE', fallback='')
    if not re.fullmatch(r'[A-Za-z0-9_.@-]+', language):
        language = 'C.UTF-8'
    if not re.fullmatch(r'[A-Za-z0-9_.@:-]*', translations):
        translations = ''
    return {'LANG': language, 'LC_ALL': language, 'LANGUAGE': translations}


if __name__ == '__main__':
    home = Path(os.environ['HOME'])
    settings = Path(os.environ['XDG_CONFIG_HOME'])
    if sys.argv[1:] == ['locale']:
        print('\n'.join('export ' + key + '=' + shlex.quote(value)
                        for key, value in locale(settings).items()))
    else:
        configure(home, settings)
