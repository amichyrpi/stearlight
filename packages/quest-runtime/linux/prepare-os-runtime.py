#!/usr/bin/python3
"""Retain the Quest compositor and UI adapters when staging Valve's OS runtime."""
from pathlib import Path
import shutil
import sys

runtime = Path(sys.argv[1])
quest = Path('/opt/quest')
ui = runtime / 'resources/webinterface/dashboard/systemui.js'
data = ui.read_bytes()
patterns = [b'r&&de.JJ.isTheaterMode&&n.createElement(s.B4o',
            b'r&&c.H.m_bShowFloor&&(l||de.JJ.isTheaterMode)&&n.createElement(s.B4o',
            b'r&&c.H.m_bShowFloorFar&&!de.JJ.isTheaterMode&&!l&&n.createElement(s.B4o']
if patterns[0] not in data:
    patterns = [b'o&&ne.JJ.isTheaterMode&&r.createElement(s.B4o',
                b'o&&c.H.m_bShowFloor&&(l||ne.JJ.isTheaterMode)&&r.createElement(s.B4o',
                b'o&&c.H.m_bShowFloorFar&&!ne.JJ.isTheaterMode&&!l&&r.createElement(s.B4o']
for pattern in patterns:
    if data.count(pattern) != 1:
        raise ValueError('Quest room-view adapter does not match this SteamVR version')
    data = data.replace(pattern, b'!(globalThis.RoomViewStore?.roomViewShowing||globalThis.RoomViewStore?.roomViewEnabled)&&' + pattern)
ui.write_bytes((quest / 'systemui-overlay-bridge.js').read_bytes() + b'\n' +
               (quest / 'roomview-bridge.js').read_bytes() + b'\n' + data)
binary = runtime / 'bin/linuxarm64/vrcompositor'
binary.rename(binary.with_suffix('.bin'))
shutil.copy2('/opt/steamvr/bin/linuxarm64/vrcompositor', binary)
shutil.copy2('/usr/local/bin/stearlight-frame-vrwebhelper', runtime / 'bin/vrwebhelper/linuxarm64/vrwebhelper.sh')
for component in ('drivers/stearlight-fake', 'resources/stearlight-session'):
    source = Path('/opt/steamvr') / component
    if source.exists():
        shutil.copytree(source, runtime / component, symlinks=True, dirs_exist_ok=True)
