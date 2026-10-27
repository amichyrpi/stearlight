#!/usr/bin/env python3
"""Rasterize the installed Frame icons for OpenVR's PNG thumbnail API."""
import configparser
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile


def stage(overlay):
    desktop = overlay / 'opt/stearlight-desktop'
    target = overlay / 'opt/quest/desktop-icons'
    target.mkdir(parents=True, exist_ok=True)
    icons = {}
    for prefix in (desktop, overlay / 'opt/stearlight-browser'):
        for root in (prefix / 'usr/share/icons', prefix / 'usr/share/pixmaps'):
            if root.is_dir():
                for path in sorted(root.rglob('*')):
                    if path.is_file() and path.suffix in ('.png', '.svg', '.svgz'):
                        icons.setdefault(path.stem, path)
    entries = {'desktop': desktop / 'usr/share/icons/breeze/apps/48/kwin.svg'}
    for prefix in (desktop, overlay / 'opt/stearlight-browser'):
        for path in (prefix / 'usr/share/applications').glob('*.desktop'):
            config = configparser.RawConfigParser(strict=False)
            try:
                config.read(path)
                icon = config['Desktop Entry'].get('Icon', '')
                source = icons.get(icon)
                if icon.startswith('/'):
                    source = prefix / icon.lstrip('/')
                if source and source.is_file():
                    entries[path.stem] = source
            except (configparser.Error, OSError, KeyError):
                continue
    for name, source in entries.items():
        if not source.is_file():
            if name == 'desktop':
                raise ValueError('Frame KWin desktop icon is missing')
            continue
        with tempfile.TemporaryDirectory() as folder:
            if source.suffix == '.svgz':
                expanded = Path(folder) / 'icon.svg'
                expanded.write_bytes(gzip.decompress(source.read_bytes()))
                source = expanded
            subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(source),
                            '-vf', 'scale=128:128:flags=lanczos', '-frames:v', '1', str(target / (name + '.png'))], check=True)
    print('Staged Frame desktop/application icons:', len(entries))


if __name__ == '__main__':
    stage(Path(sys.argv[1]))
