#!/usr/bin/python3
"""Run Steam's x86 games in its installed runtime without privileged containers."""
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import sys


def unescape(value):
    return re.sub(r"\\([0-7]{3})", lambda m: chr(int(m[1], 8)), value)


def prepare_rootfs(platform, destination):
    files = platform / "files"
    manifest = platform / "usr-mtree.txt.gz"
    stamp = destination / "manifest.json"
    signature = [5, str(platform), manifest.stat().st_size, manifest.stat().st_mtime_ns]
    if not stamp.exists() or json.loads(stamp.read_text()) != signature:
        records = []
        with gzip.open(manifest, "rt") as source:
            for line in source:
                fields = line.split()
                if not fields or fields[0].startswith("#"):
                    continue
                relative = Path(unescape(fields[0]))
                if relative.is_absolute() or ".." in relative.parts:
                    raise ValueError("Invalid Steam runtime manifest path")
                attributes = dict(f.split("=", 1) for f in fields[1:] if "=" in f)
                records.append((files / relative, attributes))
        # Steam downloads canonical files; pressure-vessel normally creates
        # these aliases. Keep existing files and Valve's update ownership.
        for path, attributes in records:
            if attributes.get("type") == "dir":
                path.mkdir(parents=True, exist_ok=True)
        for path, attributes in records:
            if attributes.get("type") == "link" and not os.path.lexists(path):
                path.symlink_to(unescape(attributes["link"]))
            elif attributes.get("type") == "file" and "contents" in attributes and not os.path.lexists(path):
                content = Path(unescape(attributes["contents"]))
                if content.is_absolute() or ".." in content.parts:
                    raise ValueError("Invalid Steam runtime content path")
                os.link(files / content, path)
        destination.mkdir(parents=True, exist_ok=True)
        # FEX's GPU guest bridges require glibc >= 2.34. Older games still
        # select soldier (2.31), so overlay the compatible x86 loader and C++
        # libraries in our per-game root without changing Valve's runtime.
        usr = destination / "usr"
        if usr.is_symlink():
            usr.unlink()
        usr.mkdir(exist_ok=True)
        for directory, source, excluded in ((usr, files, "lib"), (usr / "lib", files / "lib", "x86_64-linux-gnu")):
            directory.mkdir(exist_ok=True)
            for item in source.iterdir():
                link = directory / item.name
                if item.name != excluded and not os.path.lexists(link):
                    link.symlink_to(item)
        libraries = usr / "lib/x86_64-linux-gnu"
        libraries.mkdir(exist_ok=True)
        for source in (files / "lib/x86_64-linux-gnu", Path("/opt/quest/fex-runtime/lib/x86_64-linux-gnu"), Path("/opt/quest/fex-runtime/usr/lib/x86_64-linux-gnu")):
            if not source.is_dir():
                continue
            for item in source.iterdir():
                link = libraries / item.name
                if link.is_symlink() or link.is_file():
                    link.unlink()
                if item.is_symlink():
                    link.symlink_to(os.readlink(item))
                elif item.is_file():
                    shutil.copy2(item, link)
                elif item.name == "pulseaudio":
                    if link.is_dir():
                        shutil.rmtree(link)
                    shutil.copytree(item, link)
                else:
                    if link.is_dir() and not link.is_symlink():
                        shutil.rmtree(link)
                    link.symlink_to(item)
        for name, target in {"bin": "usr/bin", "sbin": "usr/sbin",
                             "lib": "usr/lib", "lib64": "usr/lib/x86_64-linux-gnu",
                             "etc": "usr/etc"}.items():
            link = destination / name
            if link.is_symlink():
                if os.readlink(link) == target:
                    continue
                link.unlink()
            link.symlink_to(target)
        stamp.write_text(json.dumps(signature))
    return destination


def unwrap(arguments):
    args = list(arguments)
    if args and args[0] == "--":
        args.pop(0)
    # These Steam wrappers only establish a mount namespace. PRoot already
    # supplies our filesystem; FEX uses the same official runtime directly.
    while args:
        if args[0] == "--":
            args.pop(0)
            continue
        executable = Path(args[0])
        if executable.name == "_v2-entry-point" and executable.parent.name.startswith("SteamLinuxRuntime"):
            try:
                end = args.index("--", 1)
            except ValueError:
                raise ValueError("Missing Steam runtime command")
            args = args[end + 1:]
        elif executable.name == "scout-on-soldier-entry-point-v2" and executable.parent.name == "SteamLinuxRuntime":
            args = args[1:]
        else:
            break
    if not args:
        raise ValueError("Missing game command")
    return args


def prepare_game_script(command):
    # MonoKickstart's stock Linux launcher leaves an emulated Bash waiting
    # beside the game. Replacing that final child launch preserves its setup
    # and arguments while saving a process on Android's limited budget.
    path = Path(command[0])
    try:
        with path.open("rb") as source:
            header = source.read(65537)
        if len(header) > 65536 or not header.startswith(b"#!/bin/bash") or b"# MonoKickstart picks the right libfolder" not in header:
            return
        text = header.decode("utf-8")
        patched = re.sub(r'(?m)^(\s*)(\./[\w.-]+\.bin\.x86(?:_64)?) \$@$', r'\1exec \2 "$@"', text)
        if text != patched:
            path.write_text(patched)
    except (OSError, UnicodeError):
        pass


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("run", "waitforexitandrun"):
        raise ValueError("Unsupported FEX launch command")
    tool = Path(sys.argv[0]).resolve().parent
    common = tool.parent
    runtime = common / "SteamLinuxRuntime_soldier"
    for argument in sys.argv[2:]:
        path = Path(argument)
        if path.name == "_v2-entry-point" and path.parent.name.startswith("SteamLinuxRuntime_"):
            runtime = path.parent
            break
    candidates = sorted(runtime.glob("*_platform_*/usr-mtree.txt.gz"))
    if not candidates:
        raise RuntimeError(f"{runtime.name} must finish downloading before launching this game")
    data = Path(os.environ.get("STEAM_COMPAT_DATA_PATH", str(Path.home() / ".local/share/stearlight/fex")))
    rootfs = prepare_rootfs(candidates[-1].parent, data / "quest-rootfs")
    config = data / "quest-fex.json"
    config.write_text(json.dumps({"Config": {}, "ThunksDB": {"GL": 1, "Vulkan": 1}}))
    environment = dict(os.environ)
    # Native glibc needs rseq disabled in PRoot, but soldier's older x86
    # glibc cannot parse that tunable while initializing its allocator.
    for key in ("GLIBC_TUNABLES", "LD_PRELOAD", "LD_LIBRARY_PATH"):
        environment.pop(key, None)
    environment.update({
        "FEX_PORTABLE": "1", "FEX_ROOTFS": str(rootfs), "FEX_APP_CONFIG": str(config),
        "FEX_THUNKHOSTLIBS": str(tool / "usr/lib/aarch64-linux-gnu/fex-emu/HostThunks"),
        "FEX_THUNKGUESTLIBS": str(tool / "usr/share/fex-emu/GuestThunks"),
        "FEX_THUNKCONFIG": str(config),
        "PATH": str(tool / "usr/bin") + ":" + environment.get("PATH", "/usr/bin:/bin"),
        "VK_ICD_FILENAMES": "/opt/quest/turnip_icd.json",
        "VK_DRIVER_FILES": "/opt/quest/turnip_icd.json",
        # Xvfb has no DRI3 scanout. Mesa renders on Turnip and presents the
        # completed game image through X11's shared-memory transport.
        "MESA_VK_WSI_DEBUG": "sw",
        "GALLIUM_DRIVER": "zink", "MESA_LOADER_DRIVER_OVERRIDE": "zink",
        "LIBGL_KOPPER_DISABLE": "true", "LIBGL_KOPPER_DRI2": "1",
    })
    command = unwrap(sys.argv[2:])
    prepare_game_script(command)
    environment.setdefault("FNA3D_FORCE_DRIVER", "Vulkan")
    environment.setdefault("SDL_AUDIODRIVER", "pulseaudio")
    if environment.get("STEARLIGHT_FEX_GAMESCOPE") != "1" and environment.get("SteamAppId"):
        app = environment["SteamAppId"]
        environment["STEARLIGHT_FEX_GAMESCOPE"] = "1"
        environment["GLIBC_TUNABLES"] = "glibc.pthread.rseq=0"
        environment["XKB_CONFIG_ROOT"] = "/opt/frame-gamescope/share/X11/xkb"
        environment["PATH"] = "/opt/quest:" + environment["PATH"]
        environment.pop("MESA_VK_WSI_DEBUG", None)
        loader = "/opt/frame-gamescope/lib/ld-linux-aarch64.so.1"
        libraries = "/usr/lib/aarch64-linux-gnu/libpipewire-0.3.so.0"
        preload = ["--preload", libraries]
        compositor = [loader, *preload, "--library-path", "/opt/frame-gamescope/lib:/usr/lib/aarch64-linux-gnu",
                      "/opt/frame-gamescope/bin/gamescope", "--backend", "openvr", "--expose-wayland",
                      "--xwayland-count", "1", "-w", "1280", "-h", "720", "-W", "1280", "-H", "720", "-r", "60",
                      "--steam", "--virtual-connector-strategy", "SingleApplication",
                      "--vr-overlay-key", "valve.steam.desktopgame." + app,
                      "--vr-app-overlay-key", "valve.steam.desktopgame",
                      "--vr-overlay-show-immediately", "--vr-overlay-enable-control-bar", "--vr-overlay-enable-control-bar-keyboard",
                      "--", str(tool / "fex-compat-tool"), "run", *command]
        os.execve(loader, compositor, environment)
        return
    print("Stearlight FEX: launching " + command[0], file=sys.stderr, flush=True)
    os.execve(tool / "usr/bin/FEX", [str(tool / "usr/bin/FEX"), *command], environment)


if __name__ == "__main__":
    main()
