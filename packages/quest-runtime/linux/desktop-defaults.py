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


def configure(home, settings):
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
