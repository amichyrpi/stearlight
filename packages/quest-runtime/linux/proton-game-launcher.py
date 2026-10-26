#!/usr/bin/python3
"""Run Valve's ARM64 Proton in the app's existing Linux filesystem."""
import gzip
import runpy
import subprocess
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time


def unescape(value):
    return re.sub(r"\\([0-7]{3})", lambda m: chr(int(m[1], 8)), value)


def prepare_platform(platform):
    files = platform / "files"
    manifest = platform / "usr-mtree.txt.gz"
    stamp = platform / ".quest-aliases.json"
    signature = [4, manifest.stat().st_size, manifest.stat().st_mtime_ns]
    managed = set()
    try:
        previous = json.loads(stamp.read_text())
        if isinstance(previous, dict) and previous.get("signature") == signature:
            return files
        if isinstance(previous, dict):
            managed = set(previous.get("aliases", []))
    except (OSError, ValueError):
        pass
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
    for path, attributes in records:
        if attributes.get("type") == "dir":
            path.mkdir(parents=True, exist_ok=True)
    aliases = []
    for path, attributes in records:
        relative = str(path.relative_to(files))
        if relative in managed and (path.is_file() or path.is_symlink()):
            path.unlink()
        if attributes.get("type") == "link":
            if os.path.lexists(path):
                continue
            path.symlink_to(unescape(attributes["link"]))
            aliases.append(relative)
        elif attributes.get("type") == "file" and "contents" in attributes:
            content = Path(unescape(attributes["contents"]))
            if content.is_absolute() or ".." in content.parts:
                raise ValueError("Invalid Steam runtime content path")
            canonical = files / content
            is_alias = False
            if os.path.lexists(path):
                try:
                    is_alias = path.samefile(canonical)
                except FileNotFoundError:
                    # PRoot can report an emulated hard link as an existing
                    # regular file even though its old absolute target moved.
                    path.unlink()
                else:
                    if path.is_symlink() and not is_alias:
                        path.unlink()
            if not os.path.lexists(path):
                # Relative links stay inside this platform across Steam moves
                # and avoid PRoot's absolute hard-link backing-file records.
                path.symlink_to(os.path.relpath(canonical, path.parent))
                is_alias = True
            if is_alias:
                aliases.append(relative)
    temporary = stamp.with_suffix(".next")
    temporary.write_text(json.dumps({"signature": signature, "aliases": aliases}))
    temporary.replace(stamp)
    return files


def prepare_vtol_textures(game):
    # Unity 2020's textureQuality setting omits the two largest mips at load time.
    # This changes graphics configuration, not game code or stereo geometry.
    asset = game / "VTOLVR_Data/globalgamemanagers"
    original = "141f912ee5e463e962fe81e96009d93b41d3bbead220e4249b814f8644253709"
    previous = "a8eef375dff3597024700f89a56cec91ed916c31e2cdfbcf4a12db711083e3e3"
    adjusted = "437b512ae393213790f787961ae2d827e40da6934ef891aa6798f37534ef41a8"
    data = asset.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest == adjusted:
        return
    if digest not in (original, previous):
        raise ValueError("VTOL graphics profile does not match this game build")
    backup = asset.with_name(asset.name + ".quest-original")
    if digest == original and not backup.exists():
        backup.write_bytes(data)
    updated = data[:259448] + b"\x02\x00\x00\x00" + data[259452:]
    if hashlib.sha256(updated).hexdigest() != adjusted:
        raise ValueError("VTOL graphics profile verification failed")
    temporary = asset.with_name(asset.name + ".quest-next")
    temporary.write_bytes(updated)
    temporary.replace(asset)


def prepare_vtol_prefix(compat):
    # Apply defaults once. Subsequent choices in the game's settings survive.
    stamp = compat / "quest-vtol-profile.json"
    if stamp.exists():
        return
    settings = compat / "pfx/drive_c/users/steamuser/AppData/Roaming/Boundless Dynamics, LLC/VTOLVR/SaveData/gameSettings.cfg"
    if settings.exists():
        backup = settings.with_name(settings.name + ".quest-original")
        if not backup.exists():
            backup.write_bytes(settings.read_bytes())
        data = settings.read_text()
        for key, value in {"MSAA": "-1", "SHADOWS": "-1", "WINGMAN_VOICES": "DefaultWingmanVoice;"}.items():
            data = re.sub(r"(?m)^(\s*" + key + r" = ).*", lambda m: m[1] + value, data)
        settings.write_text(data)
    else:
        settings.parent.mkdir(parents=True, exist_ok=True)
        settings.write_text("GAMESETTINGS\n{\n\tMSAA = -1\n\tSHADOWS = -1\n\tTHUMBSTICK_MODE = 1\n\tWINGMAN_VOICES = DefaultWingmanVoice;\n}\n")
    registry = compat / "pfx/system.reg"
    if registry.exists():
        data = registry.read_text()
        # OpenVR supplies Touch input; VTOL does not need Wine's desktop HID
        # discovery or Event Log service. Retain its RPC and network drivers.
        for name in ("PlugPlay", "Eventlog"):
            pattern = r'(\[System\\\\ControlSet001\\\\Services\\\\' + name + r'\][^\n]*\n)(.*?)(?=\n\[|\Z)'
            data = re.sub(pattern, lambda m: m[1] + re.sub(r'"Start"=dword:[0-9a-f]+', '"Start"=dword:00000004', m[2]), data, flags=re.S | re.I)
        registry.write_text(data)
    stamp.write_text(json.dumps({"version": 1}))


def prepare_outerwilds_textures(game):
    # Unity 2019's default full-size textures exhaust Quest 2's shared RAM.
    # Omit the two largest mips and use smaller shadow maps; retain game code.
    asset = game / "OuterWilds_Data/globalgamemanagers"
    original = "60af32f30f99e9e539a0bd19c1425f3ee3fb7c57172acbc1b556a6d0a81d6267"
    adjusted = "778ac7dc5a1654b1f6708a57d582c533daf60357ea0e0888b1b4c4d209a98dbf"
    data = asset.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest == adjusted:
        return
    if digest != original:
        print("Stearlight: unrecognized Outer Wilds graphics profile; keeping its settings", file=sys.stderr)
        return
    updated = bytearray(data)
    for offset, before, after in (
        (248972, b"\x03\x00\x00\x00", b"\x00\x00\x00\x00"),  # shadowResolution
        (249016, b"\x00\x00\x00\x00", b"\x02\x00\x00\x00"),  # textureQuality
        (249048, b"\x00\x00\x00\x44", b"\x00\x00\x00\x43"),  # mip budget: 512 -> 128 MiB
    ):
        if data[offset:offset + 4] != before:
            raise ValueError("Outer Wilds graphics profile verification failed")
        updated[offset:offset + 4] = after
    if hashlib.sha256(updated).hexdigest() != adjusted:
        raise ValueError("Outer Wilds graphics profile checksum mismatch")
    backup = asset.with_name(asset.name + ".quest-original")
    if not backup.exists():
        backup.write_bytes(data)
    temporary = asset.with_name(asset.name + ".quest-next")
    temporary.write_bytes(updated)
    temporary.replace(asset)


def prepare_outerwilds_settings(compat):
    # The game replaces Unity's initial quality settings when a profile loads.
    # Apply a Quest default once, retaining saves and subsequent user choices.
    saves = compat / "pfx/drive_c/users/steamuser/AppData/LocalLow/Mobius Digital/Outer Wilds/SteamSaves"
    defaults = dict(fullScreen=False, displayResWidth=960, displayResHeight=540, refreshRate=30,
                    antiAlias=0, antiAliasQuality=0, textureQuality=2, oceanQuality=0,
                    shadowQuality=0, ambientOcclusionQuality=0, lightingQuality=0, vSyncEnabled=True)
    for player in saves.glob("*/player.owsett"):
        settings = player.with_name("graphics.owsett")
        stamp = settings.with_name(settings.name + ".quest-profile")
        previous = stamp.read_text().strip() if stamp.exists() else None
        if previous == "2":
            continue
        original = settings.read_bytes() if settings.exists() else None
        try:
            data = json.loads(original) if original is not None else {}
            if not isinstance(data, dict):
                raise ValueError("Unknown graphics settings format")
        except (ValueError, UnicodeError):
            print("Stearlight: unrecognized Outer Wilds settings; leaving them unchanged", file=sys.stderr)
            continue
        backup = settings.with_name(settings.name + ".quest-original")
        if original is not None and not backup.exists():
            backup.write_bytes(original)
        if previous != "1":
            data.update(defaults)
        # Unity 2019 JsonUtility does not run this class's constructor. Supply
        # brightness and camera defaults explicitly when creating partial JSON.
        for key, value in dict(displayNumber=0, aspectRatio=4, gammaValue=0.5,
                               fieldOfView=70.0, dithering=0.2, consolePerformanceMode=0).items():
            data.setdefault(key, value)
        temporary = settings.with_name(settings.name + ".quest-next")
        temporary.write_text(json.dumps(data))
        temporary.replace(settings)
        stamp.write_text("2\n")


def prepare_vtol_bindings(game):
    root = game / "VTOLVR_Data/StreamingAssets/SteamVR"
    manifest = root / "actions.json"
    data = json.loads(manifest.read_text())
    defaults = data.get("default_bindings", [])
    added = False
    for source, target, filename in (
        ("oculus_touch", "frame_controller", "bindings_stearlight_quest.json"),
        ("rift", "stearlight-quest_hmd", "binding_stearlight_quest_hmd.json"),
    ):
        if any(entry.get("controller_type") == target for entry in defaults):
            continue
        entry = next((entry for entry in defaults if entry.get("controller_type") == source), None)
        if not entry:
            raise ValueError("VTOL is missing its " + source + " input binding")
        relative = Path(entry["binding_url"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Invalid VTOL input binding path")
        binding = json.loads((root / relative).read_text())
        binding["controller_type"] = target
        binding["name"] = "Stearlight Quest " + source
        for actions in binding.get("bindings", {}).values():
            for input_source in actions.get("sources", []):
                path = input_source.get("path", "")
                if path.endswith("/input/joystick"):
                    input_source["path"] = path.removesuffix("joystick") + "thumbstick"
                if path.endswith("/input/grip"):
                    # Quest's driver supplies a real grip click from the
                    # analog squeeze; the Frame profile has no force sensor.
                    input_source.get("parameters", {}).pop("force_input", None)
        destination = root / filename
        temporary = destination.with_suffix(".quest-next")
        temporary.write_text(json.dumps(binding) + "\n")
        temporary.replace(destination)
        defaults.append({"controller_type": target, "binding_url": filename})
        added = True
    if added:
        backup = manifest.with_name(manifest.name + ".quest-original")
        if not backup.exists():
            backup.write_bytes(manifest.read_bytes())
        data["default_bindings"] = defaults
        temporary = manifest.with_suffix(".quest-next")
        temporary.write_text(json.dumps(data) + "\n")
        temporary.replace(manifest)


def launch_environment(files, environment):
    env = dict(environment)
    env["PATH"] = "/usr/bin:/bin"
    env["LD_LIBRARY_PATH"] = "/usr/lib/aarch64-linux-gnu:/opt/frame-gamescope/lib:" + str(files / "lib/aarch64-linux-gnu")
    env["LD_PRELOAD"] = "/opt/quest/libquest_wine_pe.so:/opt/quest/libquest_socket.so"
    gamepad = Path("/opt/quest/libquest_gamepad_aarch64.so")
    if gamepad.is_file() and env.get("STEARLIGHT_STEAM_INPUT", "1") != "0":
        env["LD_PRELOAD"] += ":" + str(gamepad)
    env["GLIBC_TUNABLES"] = "glibc.pthread.rseq=0"
    env["PROTON_USE_XALIA"] = "0"
    env["PROTON_NO_ESYNC"] = env["PROTON_NO_FSYNC"] = "1"
    # Mono creates many handled exceptions during startup. Wine's default
    # Proton trace writes tens of megabytes for these under PRoot.
    env.setdefault("WINEDEBUG", "-all")
    env.setdefault("PROTON_LOG", "-fex")
    env.setdefault("PROTON_LOG_DIR", "/tmp")
    env["GALLIUM_DRIVER"] = env["MESA_LOADER_DRIVER_OVERRIDE"] = "zink"
    env["LIBGL_KOPPER_DISABLE"] = "true"
    env["LIBGL_KOPPER_DRI2"] = "1"
    env["VK_ICD_FILENAMES"] = env["VK_DRIVER_FILES"] = "/opt/quest/turnip_icd.json"
    env["MESA_VK_WSI_DEBUG"] = "sw"
    env["MESA_SHADER_CACHE_MAX_SIZE"] = env["MESA_GLSL_CACHE_MAX_SIZE"] = env.get("STEARLIGHT_SHADER_CACHE_MAX_SIZE", "128M")
    # Streaming's injected layers are unnecessary for a local VR scene.
    for name in ("VK_INSTANCE_LAYERS", "STEAM_COMPAT_MEDIA_PATH", "STEAM_COMPAT_TRANSCODED_MEDIA_PATH"):
        env.pop(name, None)
    return env


def release_streaming_audio():
    state = Path("/run/stearlight/quest")
    if not (state / "local-vr-audio.supported").exists():
        return
    (state / "local-vr.pid").write_text(str(os.getpid()))
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if (state / "local-vr-audio.paused").exists():
            return
        time.sleep(0.05)
    raise RuntimeError("Streaming audio did not release the local VR process budget")


def prepare_flat_unity_boot(game, directory):
    # Keep flat Unity games on the direct graphics path inside Gamescope.
    boot = game / directory / "boot.config"
    data = boot.read_text()
    updated = re.sub(r"(?m)^(gfx-enable-(?:native-)?gfx-jobs)=1$", r"\1=0", data)
    if updated != data:
        backup = boot.with_name(boot.name + ".quest-original")
        if not backup.exists():
            backup.write_text(data)
        temporary = boot.with_name(boot.name + ".quest-next")
        temporary.write_text(updated)
        temporary.replace(boot)


def prepare_flat_prefix(compat):
    # Called after Proton has finished and waited for prefix initialization.
    # Keep Wine's SDL/HID input and RPC; omit desktop-only autostart services.
    registry = compat / "pfx/system.reg"
    data = registry.read_text()
    updated = data
    for name in ("PlugPlay", "Eventlog"):
        pattern = r'(\[System\\\\ControlSet001\\\\Services\\\\' + name + r'\][^\n]*\n)(.*?)(?=\n\[|\Z)'
        updated = re.sub(pattern, lambda m: m[1] + re.sub(r'"Start"=dword:[0-9a-f]+', '"Start"=dword:00000004', m[2]), updated, flags=re.S | re.I)
    for name in ("winebus", "winehid"):
        pattern = r'(\[System\\\\ControlSet001\\\\Services\\\\' + name + r'\][^\n]*\n)(.*?)(?=\n\[|\Z)'
        updated = re.sub(pattern, lambda m: m[1] + m[2].replace('"Group"="WinePlugPlay"', '"Group"="System Bus Extender"'), updated, flags=re.S | re.I)
    if updated != data:
        backup = registry.with_name(registry.name + ".quest-flat-original")
        if not backup.exists():
            backup.write_text(data)
        temporary = registry.with_name(registry.name + ".quest-next")
        temporary.write_text(updated)
        temporary.replace(registry)


def launch_flat_panel(entry, arguments, environment, appid):
    env = dict(environment)
    env["STEARLIGHT_PROTON_GAMESCOPE"] = "1"
    env["STEARLIGHT_PROTON_DIRECT"] = "1"
    env["XKB_CONFIG_ROOT"] = "/opt/frame-gamescope/share/X11/xkb"
    env["PATH"] = "/opt/quest:" + env["PATH"]
    env.pop("LD_PRELOAD", None)
    env.pop("LD_LIBRARY_PATH", None)
    env.pop("MESA_VK_WSI_DEBUG", None)
    loader = "/opt/frame-gamescope/lib/ld-linux-aarch64.so.1"
    profile = env.get("STEARLIGHT_QUEST_GAME_PROFILE", "1") != "0"
    width, height = (960, 540) if profile else (1280, 720)
    rate = "30" if profile and appid == "753640" else "60"
    command = [loader, "--preload", "/usr/lib/aarch64-linux-gnu/libpipewire-0.3.so.0",
               "--library-path", "/opt/frame-gamescope/lib:/usr/lib/aarch64-linux-gnu",
               "/opt/frame-gamescope/bin/gamescope", "--backend", "openvr", "--expose-wayland",
               "--xwayland-count", "1", "-w", str(width), "-h", str(height), "-W", str(width), "-H", str(height), "-r", rate,
               "--steam", "--virtual-connector-strategy", "SingleApplication",
               "--vr-overlay-key", "valve.steam.desktopgame." + appid,
               "--vr-app-overlay-key", "valve.steam.desktopgame",
               "--vr-overlay-show-immediately", "--vr-overlay-enable-control-bar", "--vr-overlay-enable-control-bar-keyboard",
               "--", "/usr/bin/python3", "/opt/quest/proton-game-launcher.py", str(entry), *arguments]
    if os.getpgrp() != os.getpid():
        os.setsid()
    os.execve(loader, command, env)


def exec_proton(arguments, environment, compat):
    # Steam's outer reaper and Gamescope already own the game's lifetime.
    # Keep Valve's prefix setup, then replace the redundant Python waiter.
    original = subprocess.call
    def call(command, *extra, **options):
        if (isinstance(command, (list, tuple)) and len(command) > 2
                and Path(command[0]).name == "wine"
                and command[1].lower() == r"c:\windows\system32\steam.exe"):
            prepare_flat_prefix(compat)
            for key, fd in (("stdout", 1), ("stderr", 2)):
                target = options.get(key)
                if target is not None:
                    os.dup2(target.fileno(), fd)
            os.execvpe(command[0], command, options.get("env", os.environ))
        return original(command, *extra, **options)
    subprocess.call = call
    sys.argv = list(arguments)
    sys.path.insert(0, str(Path(arguments[0]).parent))
    os.environ.clear()
    os.environ.update(environment)
    try:
        runpy.run_path(arguments[0], run_name="__main__")
    finally:
        subprocess.call = original


def main():
    entry = Path(sys.argv[1])
    args = sys.argv[2:]
    if "--" not in args:
        raise ValueError("Missing Steam runtime command")
    args = args[args.index("--") + 1:]
    if not args:
        raise ValueError("Empty Steam runtime command")
    platforms = sorted(entry.parent.glob("*_platform_*/usr-mtree.txt.gz"))
    if not platforms:
        raise ValueError("Steam ARM64 platform is not installed")
    appid = os.environ.get("SteamAppId", os.environ.get("STEAM_COMPAT_APP_ID", "0"))
    if not appid.isdecimal():
        raise ValueError("Invalid Steam application ID")
    # Steam can pass an epoll descriptor as stdout. Always give Wine ordinary
    # log descriptors; writing a line to epoll fails with EINVAL.
    fd = os.open("/tmp/quest-proton-" + appid + ".log", os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    os.dup2(fd, 1)
    os.dup2(fd, 2)
    if fd > 2:
        os.close(fd)
    files = prepare_platform(platforms[-1].parent)
    env = launch_environment(files, os.environ)
    # Outer Wilds remains unsupported; see docs/outer-wilds-investigation.md.
    flat_unity = {"2530470": "Garden of Witches_Data", "753640": "OuterWilds_Data"}
    if appid in flat_unity:
        if env.get("STEARLIGHT_PROTON_GAMESCOPE") != "1":
            release_streaming_audio()
            launch_flat_panel(entry, sys.argv[2:], env, appid)
            return
        prepare_flat_unity_boot(Path(env["STEAM_COMPAT_INSTALL_PATH"]), flat_unity[appid])
        if appid == "753640" and env.get("STEARLIGHT_QUEST_GAME_PROFILE", "1") != "0":
            prepare_outerwilds_textures(Path(env["STEAM_COMPAT_INSTALL_PATH"]))
            prepare_outerwilds_settings(Path(env["STEAM_COMPAT_DATA_PATH"]))
            env.setdefault("MALLOC_ARENA_MAX", "2")
            # Limit compilation concurrency and queued frames on Quest 2.
            # Later user DXVK settings retain precedence over these defaults.
            defaults = ("dxvk.numCompilerThreads = 2; dxgi.maxDeviceMemory = 512; "
                        "dxgi.maxSharedMemory = 512; dxgi.maxFrameLatency = 1; dxgi.maxFrameRate = 30")
            env["DXVK_CONFIG"] = defaults + "; " + env.get("DXVK_CONFIG", "")
        if appid == "2530470" and env.get("STEARLIGHT_QUEST_GAME_PROFILE", "1") != "0":
            # Use the tested D3D11 Vulkan path instead of WineD3D -> Zink.
            # Bound worker/allocator pressure alongside the full VR dashboard.
            env.setdefault("MALLOC_ARENA_MAX", "2")
            defaults = ("dxvk.numCompilerThreads = 2; dxgi.maxDeviceMemory = 1024; "
                        "dxgi.maxSharedMemory = 512; dxgi.maxFrameLatency = 1")
            env["DXVK_CONFIG"] = defaults + "; " + env.get("DXVK_CONFIG", "")
        env.setdefault("PROTON_USE_WINED3D", "0")
        if appid == "2530470":
            # Requires the payload's software-X11 PresentWait2 correction.
            env["MESA_VK_WSI_DEBUG"] = "sw,noshm"
        if appid == "753640":
            # XPutImage avoids software MIT-SHM Present bookkeeping. The
            # payload's Mesa patch disables unsupported PresentWait2 here.
            env["MESA_VK_WSI_DEBUG"] = "sw,noshm"
            env["MESA_VK_ENABLE_SUBMIT_THREAD"] = "true"
        env["STEARLIGHT_WINE_GAMEPAD"] = "1"
        # SDL's virtual joystick has no USB vendor/product ID. Steam's list
        # of controllers handled through Steam Input includes 0000/0000;
        # Wine applies that list too, hiding our process-local virtual pad.
        ignored = "SDL_GAMECONTROLLER_IGNORE_DEVICES"
        env[ignored] = ",".join(pair for pair in env.get(ignored, "").split(",")
                                if pair.strip().lower() != "0x0000/0x0000")
        allowed = "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"
        if allowed in env:
            env[allowed] += ",0x0000/0x0000"
        overrides = "plugplay.exe=d;wevtsvc=d;wineusb.sys=d;winebth.sys=d;UnityCrashHandler64.exe=d;tabtip.exe=d;winemenubuilder.exe=d;gameoverlayrenderer64=d;gameoverlayrenderer=d"
        env["WINEDLLOVERRIDES"] = overrides + ";" + env.get("WINEDLLOVERRIDES", "")
    if appid == "667970" and env.get("STEARLIGHT_QUEST_GAME_PROFILE", "1") != "0":
        game = Path(env["STEAM_COMPAT_INSTALL_PATH"])
        prepare_vtol_bindings(game)
        prepare_vtol_textures(game)
        prepare_vtol_prefix(Path(env["STEAM_COMPAT_DATA_PATH"]))
        release_streaming_audio()
        overrides = "winebus.sys=d;wineusb.sys=d;winebth.sys=d;winemenubuilder.exe=d;tabtip.exe=d;UnityCrashHandler64.exe=d"
        env["WINEDLLOVERRIDES"] = overrides + ";" + env.get("WINEDLLOVERRIDES", "")
    if appid in flat_unity:
        width, height = (960, 540) if env.get("STEARLIGHT_QUEST_GAME_PROFILE", "1") != "0" else (1280, 720)
        graphics_thread = "-force-gfx-mt" if appid == "2530470" else "-force-gfx-direct"
        args += [graphics_thread, "-screen-fullscreen", "0", "-screen-width", str(width), "-screen-height", str(height)]
        if env.get("STEARLIGHT_QUEST_GAME_PROFILE", "1") != "0":
            args += ["-job-worker-count", "2"]
    # The official Proton Python script still owns Wine, Steam identity,
    # per-game prefixes, and game exit. No extra supervisor is kept alive.
    if appid in flat_unity and Path(args[0]).name == "proton":
        exec_proton(args, env, Path(env["STEAM_COMPAT_DATA_PATH"]))
    elif Path(args[0]).name == "proton":
        os.execve("/usr/bin/python3", ["/usr/bin/python3", *args], env)
    else:
        os.execvpe(args[0], args, env)


if __name__ == "__main__":
    main()
