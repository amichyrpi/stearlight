#!/usr/bin/python3
"""Refresh the platform room-view/input bridge in an already-installed OS."""
from pathlib import Path
import os


def refresh(runtime, source):
    path = runtime / "resources/webinterface/dashboard/systemui.js"
    data = path.read_bytes()
    start = data.find(b"// Platform adapter: Valve owns the dock button and room-view state.")
    end = data.find(b"\n})();", start)
    if start < 0 or start > 16384 or end < 0 or end - start > 8192:
        raise ValueError("Unknown SteamVR platform bridge; leaving it unchanged")
    end += len(b"\n})();")
    bridge = source.read_bytes().replace(b"\r\n", b"\n").rstrip()
    if data[start:end] == bridge:
        return
    temporary = path.with_suffix(".js.quest-input")
    temporary.write_bytes(data[:start] + bridge + data[end:])
    temporary.chmod(path.stat().st_mode & 0o777)
    temporary.replace(path)


if __name__ == "__main__":
    refresh(Path(os.environ.get("STEARLIGHT_STEAMVR_ROOT", "/opt/steamvr")), Path("/opt/quest/roomview-bridge.js"))
