#!/usr/bin/python3
"""Adapt the user's installed Standable driver; preserve SteamVR add-on choices."""
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    steam = Path(os.environ.get("STEARLIGHT_STEAM_ROOT", "/opt/steam"))
    quest = Path(os.environ.get("STEARLIGHT_QUEST_ROOT", "/opt/quest"))
    bridge = quest / "standable"
    game = steam / "steamapps/common/Standable Full Body Estimation"
    if not (game / "bin/win64/driver_standable.dll").is_file():
        return
    if not (bridge / "driver_standable.so").is_file():
        return
    native = game / "bin/linuxarm64"
    native.mkdir(parents=True, exist_ok=True)
    target = native / "driver_standable.so"
    if not target.is_file() or target.read_bytes() != (bridge / target.name).read_bytes():
        temporary = target.with_suffix(".so.next")
        shutil.copyfile(bridge / target.name, temporary)
        temporary.replace(target)
    sdk = game / "bin/win64/steam_api64.dll"
    if not sdk.is_file():
        dependency = bridge / "steam_api64.dll"
        if not dependency.is_file():
            raise RuntimeError("Standable needs the genuine SteamVR Windows steam_api64.dll dependency")
        shutil.copyfile(dependency, sdk)
    # This existing Windows server hosts the real Linux vrserver's driver
    # RPCs. Standable's Windows UI discovers it by the SteamVR server name.
    server = bridge / "vrserver.exe"
    if not server.is_file() or server.read_bytes() != (bridge / "ignition_server.exe").read_bytes():
        temporary = server.with_suffix(".exe.next")
        shutil.copyfile(bridge / "ignition_server.exe", temporary)
        temporary.replace(server)
    config = {"server_exe": str(server),
              "driver_dll": "Z:" + str(game / "bin/win64/driver_standable.dll").replace("/", "\\"),
              "wine_cmd": ["/usr/bin/python3", str(quest / "standable-host.py"), "--game", str(game)],
              "wait_for_debugger": False}
    (native / "ignition.json").write_text(json.dumps(config) + "\n")
    subprocess.run(["/opt/steamvr/bin/linuxarm64/vrpathreg", "adddriver", str(game)], check=True)


if __name__ == "__main__":
    main()
