#!/usr/bin/python3
"""Launch the optional Windows driver host in Standable's existing prefix."""
import argparse
import importlib.util
import os
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--steam", default="/opt/steam", type=Path)
    parser.add_argument("--proton", default="/opt/steam/steamapps/common/Proton 11.0 (ARM64)/proton", type=Path)
    parser.add_argument("--logs", default="/tmp/quest-standable", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.command:
        parser.error("Missing Windows driver-host command")
    game = args.game.resolve()
    steamapps = game.parent.parent
    if steamapps.name != "steamapps" or not (game / "bin/win64/driver_standable.dll").is_file():
        parser.error("Expected the installed Steam copy of Standable")
    compat = steamapps / "compatdata/2370570"
    if not (compat / "pfx/system.reg").is_file():
        parser.error("Launch Standable through Steam first to prepare its prefix")
    spec = importlib.util.spec_from_file_location("quest_proton", "/opt/quest/proton-game-launcher.py")
    launcher = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(launcher)
    platforms = sorted((args.steam / "steamapps/common/SteamLinuxRuntime_4-arm64").glob("*_platform_*/usr-mtree.txt.gz"))
    if not platforms:
        parser.error("Steam ARM64 runtime is not installed")
    files = launcher.prepare_platform(platforms[-1].parent)
    # The driver reads Steam's config and chaperone before the GUI starts.
    launcher.prepare_standable_prefix(compat)
    args.logs.mkdir(parents=True, exist_ok=True)
    env = launcher.launch_environment(files, dict(os.environ, STEARLIGHT_STEAM_INPUT="0"))
    env.update(STEAM_COMPAT_DATA_PATH=str(compat), STEAM_COMPAT_CLIENT_INSTALL_PATH=str(args.steam),
               # This DLL normally lives inside SteamVR's vrserver process.
               # Using Standable's app ID here falsely marks its GUI running
               # and prevents Steam from launching that GUI at startup.
               STEAM_COMPAT_INSTALL_PATH=str(game), SteamAppId="250820", SteamGameId="250820",
               PROTON_VR_RUNTIME=os.environ.get("PROTON_VR_RUNTIME", "/opt/steamvr"),
               PROTON_LOG="-fex", PROTON_LOG_DIR=str(args.logs), WINEDEBUG="err+all",
               STEARLIGHT_IGNITION_LOG="Z:" + str(args.logs / "driver-host.log").replace("/", "\\"))
    env["WINEDLLOVERRIDES"] = ("wineusb.sys=d;winebth.sys=d;winemenubuilder.exe=d;tabtip.exe=d;"
                              + env.get("WINEDLLOVERRIDES", ""))
    # runinprefix shares the GUI's Wine server and named pipes; normal `run`
    # starts another Steam application wrapper and duplicates startup work.
    launcher.exec_proton([str(args.proton), "runinprefix", *args.command], env, compat, direct=True)


if __name__ == "__main__":
    main()
