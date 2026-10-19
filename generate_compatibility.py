#!/usr/bin/env python3
"""Generate Stearlight OS standalone game compatibility Markdown.

Run: python generate_compatibility.py
Dependencies: standard library only.
"""
import json
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, urlopen

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'games.json'
OUTPUT = HERE / 'STANDALONE_COMPATIBILITY.md'
STEAM_DOC_ICONS = 'https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/'
BADGES = {
    'verified': ('Verified', 'verified_50.png'),
    'playable': ('Playable', 'playable_50_1.png'),
    'unsupported': ('Unsupported', 'unsupported_50.png'),
    'unknown': ('Unknown', 'unknown_50_1.png'),
}

def steam_name(appid, fallback):
    """Get the public Steam name; gracefully fall back when Steam is unavailable."""
    url = 'https://store.steampowered.com/api/appdetails?' + urlencode({'appids': appid, 'l': 'english'})
    try:
        request = Request(url, headers={'User-Agent': 'StearlightOSCompatibility/1.0'})
        with urlopen(request, timeout=8) as response:
            payload = json.load(response)
        item = payload.get(str(appid), {})
        if item.get('success'):
            name = item.get('data', {}).get('name')
            if name:
                return name
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return fallback or f'Steam App {appid}'

def escape_cell(value):
    return str(value).replace('|', '&#124;').replace('\n', ' ').strip()

def main():
    games = json.loads(SOURCE.read_text(encoding='utf-8'))
    lines = [
        '<h1 align="center">Stearlight OS</h1>',
        '',
        '<p align="center"><strong>Standalone Game Compatibility</strong></p>',
        '',
        '<p align="center">',
        '  Community testing results for standalone gaming on Stearlight OS.',
        '</p>',
        '',
        '## Compatibility ratings',
        '',
    ]
    for key, (label, icon) in BADGES.items():
        lines.append(f'<img src="{STEAM_DOC_ICONS}{icon}" alt="{label}" height="24"> **{label}**  ')
    lines += [
        '',
        '## Tested games',
        '',
        '| Game | Compatibility | Notes |',
        '| :--- | :--- | :--- |',
    ]
    for game in games:
        appid = int(game['appid'])
        status = game['status'].lower()
        if status not in BADGES:
            raise ValueError(f'Unknown status {status!r} for app {appid}')
        label, icon = BADGES[status]
        name = escape_cell(steam_name(appid, game.get('name', '')))
        notes = escape_cell(game.get('notes', ''))
        store_url = f'https://store.steampowered.com/app/{appid}/'
        cover_url = game.get('artwork_url') or f'https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/{appid}/library_600x900.jpg'
        cover = f'<img src="{cover_url}" alt="{name} cover" width="62">'
        badge = f'<img src="{STEAM_DOC_ICONS}{icon}" alt="{label}" height="28"> **{label}**'
        lines.append(f'| {cover}<br>[{name}]({store_url}) | {badge} | {notes} |')
    lines += [
        '',
        '> **Note:** Steam artwork is fetched from Steam’s public CDN. Some newer titles use hashed image paths and might need a manually specified artwork URL.',
        '',
        '### Contributing',
        '',
        'Add a game to `games.json` using its Steam App ID, `status`, and `notes`.',
        'Run `python generate_compatibility.py` to refresh this table.',
        'If Steam is unreachable, the generator uses the optional `name` fallback.',
        '',
    ]
    OUTPUT.write_text('\n'.join(lines), encoding='utf-8')
    print(f'Created {OUTPUT} ({len(games)} games)')

if __name__ == '__main__':
    main()
