#!/usr/bin/python3
"""Refresh Steam's desktop API adapter without replacing its interface."""
import json
from pathlib import Path
import secrets
import sys
from frame_menus import enable_performance_tab, enable_window_icons

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/opt/steam')
state = Path('/home/stearlight/.local/state/stearlight-desktop')
state.mkdir(parents=True, exist_ok=True)
state.chmod(0o700)
token = state / 'api-token'
if not token.exists():
    token.write_text(secrets.token_hex(32))
    token.chmod(0o600)
begin = b'/* STEARLIGHT_DESKTOP_BEGIN */\n'
end = b'/* STEARLIGHT_DESKTOP_END */\n'
library = next((root / name for name in ['steamui/sp.js', 'steamui/library.js']
                if (root / name).is_file() and b'SteamOSManager.SetUserPassword#1' in (root / name).read_bytes()), None)
if library is None:
    print('Stearlight desktop adapter: unrecognized Steam client; keeping its interface', file=sys.stderr)
    sys.exit(0)
data = library.read_bytes()
if data.startswith(begin):
    data = data.split(end, 1)[1]
data = enable_window_icons(enable_performance_tab(data.decode())).encode()
bridge = '\n'.join(Path('/opt/quest', name).read_text() for name in
                   ('desktop-ui-bridge.js', 'frame-menu-bridge.js'))
bridge = bridge.replace('__STEARLIGHT_DESKTOP_TOKEN__', json.dumps(token.read_text().strip())).encode()
temporary = library.with_suffix('.desktop-next')
temporary.write_bytes(begin + bridge + b'\n' + end + data)
temporary.chmod(library.stat().st_mode & 0o777)
temporary.replace(library)
