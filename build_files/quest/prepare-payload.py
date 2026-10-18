#!/usr/bin/env python3
"""Prepare the ARM64 Quest APK payload on Ubuntu 24.04 or an equivalent Linux host."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

REPO = Path(__file__).resolve().parents[2]
LOCK = json.loads((Path(__file__).with_name("dependencies.json")).read_text())
BUILD_LOG = None


def run(*args):
    command = [str(arg) for arg in args]
    if BUILD_LOG is None:
        subprocess.run(command, check=True)
        return
    with BUILD_LOG.open("a") as log:
        log.write("\n" + " ".join(command) + "\n")
        log.flush()
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print("\n".join(BUILD_LOG.read_text().splitlines()[-25:]), file=sys.stderr)
        raise subprocess.CalledProcessError(result.returncode, command)


def fetch(spec, downloads):
    target = downloads / (spec["sha256"][:16] + "-" + spec["url"].rsplit("/", 1)[1])
    if not target.exists():
        temporary = target.with_suffix(target.suffix + ".partial")
        with urllib.request.urlopen(spec["url"], timeout=90) as source, temporary.open("wb") as out:
            shutil.copyfileobj(source, out)
        with temporary.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != spec["sha256"]:
                raise RuntimeError(f"Checksum mismatch: {spec['url']}")
        temporary.replace(target)
    with target.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != spec["sha256"]:
            raise RuntimeError(f"Cached dependency checksum mismatch: {target}")
    return target


def verify_musl_preload(library, root):
    """The socket preload is shared by Alpine tools and Valve's glibc clients."""
    musl = root / "lib/ld-musl-aarch64.so.1"
    if not musl.is_file():
        return

    def symbols(path, undefined):
        output = subprocess.check_output(
            ["aarch64-linux-gnu-readelf", "--dyn-syms", "--wide", str(path)], text=True)
        result = set()
        for line in output.splitlines():
            fields = line.split()
            if len(fields) < 8 or not fields[0].endswith(":"):
                continue
            if (fields[6] == "UND") == undefined and (not undefined or fields[4] == "GLOBAL"):
                result.add(fields[7].split("@")[0])
        return result

    missing = symbols(library, True) - symbols(musl, False)
    if missing:
        raise RuntimeError("Socket preload requires symbols absent from musl: " + ", ".join(sorted(missing)))


def stage_glx_dependencies(root, library_dir):
    """Use host X11 headers with the clean rootfs's ARM64 link libraries."""
    pending = ["x11", "x11-xcb", "xext", "xrandr", "xxf86vm", "glproto", "libglvnd",
               "xcb-glx", "xcb-shm", "xcb-dri2", "xcb-dri3", "xcb-present", "xcb-sync",
               "xcb-randr", "xcb-render", "xcb-shape", "xrender", "xcb-xfixes", "xshmfence"]
    required = set(pending)
    seen = set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        try:
            directory = subprocess.check_output(
                ["pkg-config", "--variable=pcfiledir", name], text=True).strip()
            source = Path(directory) / f"{name}.pc"
            data = source.read_text()
        except (subprocess.CalledProcessError, OSError) as error:
            raise RuntimeError(f"Install the Quest X11 build headers; missing {name}.pc") from error
        data = re.sub(r"^libdir=.*$", f"libdir={library_dir}", data, flags=re.M)
        (library_dir / "pkgconfig" / source.name).write_text(data)
        flags = subprocess.check_output(["pkg-config", "--libs-only-l", name], text=True).split()
        for flag in flags:
            if not flag.startswith("-l"):
                continue
            target = library_dir / f"lib{flag[2:]}.so"
            candidates = sorted((root / "usr/lib/aarch64-linux-gnu").glob(f"lib{flag[2:]}.so.*"))
            if not candidates:
                if name in required:
                    raise RuntimeError(f"The clean ARM64 rootfs must supply {target.name}")
                # Private header dependencies need metadata, but are not
                # linked into this GLX build. Its linker checks actual needs.
                continue
            target.unlink(missing_ok=True)
            shutil.copyfile(candidates[0], target)
        requirements = subprocess.check_output(
            ["pkg-config", "--print-requires", "--print-requires-private", name], text=True)
        pending.extend(line.split()[0] for line in requirements.splitlines() if line.strip())


def build_streaming_allocator(linux, quest, work):
    # libdrm's public headers are architecture neutral; libc remains the
    # cross compiler's ARM64 sysroot. Never add all of /usr/include to it.
    headers = work / "streaming-drm-headers"
    headers.mkdir(exist_ok=True)
    for name in ("xf86drm.h", "xf86drmMode.h"):
        shutil.copyfile(Path("/usr/include") / name, headers / name)
    run("aarch64-linux-gnu-g++", "-O2", "-shared", "-fPIC", "-std=c++17",
        f"-I{headers}", "-I/usr/include/libdrm", linux / "streaming-drm.cpp",
        "-pthread", "-o", quest / "libquest_streaming_drm.so")


def build_mesa(root, work, downloads):
    source_archive = fetch(LOCK["mesa"], downloads)
    sources = work / "mesa"
    sources.mkdir(exist_ok=True)
    mesa = sources / "mesa-26.1.6"
    if not mesa.exists():
        with tarfile.open(source_archive) as archive:
            archive.extractall(sources, filter="data")
    run(sys.executable, REPO / "build_files/quest/patch-mesa.py", mesa)
    sysroot = work / "sysroot/lib"
    (sysroot / "pkgconfig").mkdir(parents=True, exist_ok=True)
    zlib = sorted((root / "usr/lib/aarch64-linux-gnu").glob("libz.so.1.*"))
    if not zlib:
        raise RuntimeError("The clean rootfs must supply ARM64 libz.so.1")
    shutil.copyfile(zlib[-1], sysroot / "libz.so.1")
    (sysroot / "libz.so").unlink(missing_ok=True)
    (sysroot / "libz.so").symlink_to("libz.so.1")
    (sysroot / "pkgconfig/zlib.pc").write_text(
        f"Name: zlib\nDescription: Stearlight ARM64 zlib\nVersion: 1.3.1\nLibs: -L{sysroot} -lz\nCflags: -I/usr/include\n")
    stage_glx_dependencies(root, sysroot)
    cross = work / "turnip.cross"
    cross.write_text("""[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'
pkg-config = 'pkg-config'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8-a'
endian = 'little'
[properties]
needs_exe_wrapper = true
""" + f"pkg_config_libdir = '{sysroot / 'pkgconfig'}'\n")
    build = work / "zink-build"
    setup = [sys.executable, "-m", "mesonbuild.mesonmain", "setup"]
    if (build / "build.ninja").exists():
        setup += ["--reconfigure"]
    run(*setup, build, mesa, "--cross-file", cross, "--buildtype=release", "--wrap-mode=nofallback",
        "-Dvulkan-drivers=freedreno", "-Dfreedreno-kmds=kgsl", "-Dgallium-drivers=zink", "-Dplatforms=x11",
        "-Dopengl=true", "-Degl=enabled", "-Dgbm=disabled", "-Dglx=dri", "-Dglvnd=enabled", "-Dllvm=disabled",
        "-Dvideo-codecs=", "-Dbuild-tests=false", "-Dtools=", "-Dvalgrind=disabled", "-Dlibunwind=disabled",
        "-Ddisplay-info=disabled", "-Dzstd=disabled", "-Dandroid-libbacktrace=disabled", "-Dzlib=enabled",
        "-Dexpat=disabled", "-Dxmlconfig=disabled", "-Dspirv-tools=disabled", "-Dshader-cache=enabled",
        "-Dshader-cache-max-size=128M")
    run("ninja", "-C", build, "-j", min(os.cpu_count() or 2, 8),
        "src/freedreno/vulkan/libvulkan_freedreno.so",
        "src/gallium/targets/dri/libgallium-26.1.6.so", "src/glx/libGLX_mesa.so.0.0.0",
        "src/egl/libEGL_mesa.so.0.0.0")
    return build


def overlay_archive(overlay, target):
    with target.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw,
            compresslevel=1, mtime=0) as compressed, tarfile.open(fileobj=compressed, mode="w|") as archive:
        for directory, dirs, files in os.walk(overlay, followlinks=False):
            dirs.sort()
            for name in sorted(dirs + files):
                path = Path(directory) / name
                info = archive.gettarinfo(str(path), path.relative_to(overlay).as_posix())
                info.uid = info.gid = info.mtime = 0
                info.uname = info.gname = ""
                if info.issym() and info.linkname.startswith("/"):
                    info.linkname = os.path.relpath(info.linkname.lstrip("/"), str(Path(info.name).parent))
                if info.isfile():
                    with path.open("rb") as source:
                        archive.addfile(info, source)
                else:
                    archive.addfile(info)


def stage_fonts(overlay, downloads, work):
    for spec in LOCK["fonts"].values():
        run("dpkg-deb", "-x", fetch(spec, downloads), overlay)
    quest = overlay / "opt/quest"
    quest.mkdir(parents=True, exist_ok=True)
    fna_spec = importlib.util.spec_from_file_location("fna_build", Path(__file__).with_name("build-fna3d.py"))
    fna_build = importlib.util.module_from_spec(fna_spec)
    fna_spec.loader.exec_module(fna_build)
    fna_build.build(work, quest / "fna3d", {name: fetch(spec, downloads) for name, spec in LOCK["fna3d"].items()})
    version = hashlib.sha256(json.dumps(LOCK["fonts"], sort_keys=True).encode()).hexdigest()
    (quest / "fonts.version").write_text(version + "\n")
    config = Path("etc/fonts/conf.d/66-stearlight-cjk.conf")
    (overlay / config).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(REPO / "system_files" / config, overlay / config)


def stage_update_helpers(overlay, downloads):
    for spec in LOCK["updates"].values():
        run("dpkg-deb", "-x", fetch(spec, downloads), overlay)
    quest = overlay / "opt/quest"
    tools = quest / "update-tools"
    tools.mkdir(parents=True, exist_ok=True)
    for name in ("openssl", "btrfs", "unsquashfs"):
        shutil.copy2(overlay / "usr/bin" / name, tools / name)
        # Only our loader boundary should run these glibc tools.
        (overlay / "usr/bin" / name).unlink()
    for name in ("steam-os-update.py", "prepare-os-runtime.py", "activate-os-update.py",
                 "steamdeck-images.pem", "steamos-dev-images.pem",
                 "systemui-overlay-bridge.js", "roomview-bridge.js"):
        shutil.copy2(REPO / "packages/quest-runtime/linux" / name, quest / name)
    for name in ["steamos-select-branch", "steamos-update",
                 "steamos-polkit-helpers/steamos-select-branch", "steamos-polkit-helpers/steamos-update"]:
        relative = Path("usr/bin") / name
        target = overlay / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / "system_files" / relative, target)
        target.chmod(0o755)


def roomview_system_ui(native):
    # The hidden-floor branch still draws a reflective floor in theater mode.
    # Observe native room-view state without mutating saved floor preferences.
    floors = [b'o&&ne.JJ.isTheaterMode&&r.createElement(s.B4o',
              b'o&&c.H.m_bShowFloor&&(l||ne.JJ.isTheaterMode)&&r.createElement(s.B4o',
              b'o&&c.H.m_bShowFloorFar&&!ne.JJ.isTheaterMode&&!l&&r.createElement(s.B4o']
    if floors[0] not in native:
        floors = [b'r&&de.JJ.isTheaterMode&&n.createElement(s.B4o',
                  b'r&&c.H.m_bShowFloor&&(l||de.JJ.isTheaterMode)&&n.createElement(s.B4o',
                  b'r&&c.H.m_bShowFloorFar&&!de.JJ.isTheaterMode&&!l&&n.createElement(s.B4o']
    if any(native.count(condition) != 1 for condition in floors):
        raise RuntimeError("Quest room-view adapter does not match this SteamVR construct")
    for condition in floors:
        native = native.replace(condition, b'!(globalThis.RoomViewStore?.roomViewShowing||globalThis.RoomViewStore?.roomViewEnabled)&&' + condition)
    return native


def main():
    global BUILD_LOG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Mounted clean Stearlight ARM64 rootfs")
    parser.add_argument("--work", type=Path, required=True, help="Linux build/cache directory")
    parser.add_argument("--output", type=Path, required=True, help="New Android payload directory")
    parser.add_argument("--rootfs-archive", type=Path, help="Reuse an existing clean rootfs.tar.gz")
    parser.add_argument("--frame-desktop-root", type=Path, help="Read-only Steam Frame OS mount containing Plasma")
    parser.add_argument("--android-ndk", type=Path, required=True, help="Linux Android NDK for the standalone PRoot adapter")
    args = parser.parse_args()
    root, work, output = (path.resolve() for path in (args.root, args.work, args.output))
    if not (args.android_ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang").is_file():
        parser.error("--android-ndk must contain the Linux Android NDK toolchain")
    if not (root / "opt/steam/steamrtarm64/steam").is_file():
        parser.error("--root must be the clean ARM64 Frame rootfs")
    if output.exists() and any(output.iterdir()):
        parser.error("--output must be empty; choose a new directory to avoid stale APK assets")
    downloads = work / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    BUILD_LOG = work / "quest-payload-build.log"
    assets = output / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    run(sys.executable, REPO / "build_files/quest/prepare-animation.py",
        "--root", root, "--assets", assets)
    overlay = work / "quest-overlay"
    overlay.mkdir(exist_ok=True)
    stage_fonts(overlay, downloads, work)
    stage_update_helpers(overlay, downloads)
    run(sys.executable, REPO / "build_files/quest/patch-gamescope.py",
        root / "opt/frame-gamescope/bin/gamescope", overlay / "opt/frame-gamescope/bin/gamescope")
    for spec in LOCK["audio"].values():
        run("dpkg-deb", "-x", fetch(spec, downloads), overlay)
    for spec in LOCK["gaming"].values():
        run("dpkg-deb", "-x", fetch(spec, downloads), overlay)
    for spec in LOCK["fex_libraries"].values():
        run("dpkg-deb", "-x", fetch(spec, downloads), overlay / "opt/quest/fex-runtime")
    # Steam starts game launch commands through /bin/sh with its glibc
    # overlay preloads. Alpine's musl shell rejects those before FEX runs.
    shell = overlay / "bin/sh"
    shell.parent.mkdir(parents=True, exist_ok=True)
    shell.unlink(missing_ok=True)
    shell.symlink_to("../usr/bin/bash")
    for spec in LOCK["linux"].values():
        with tarfile.open(fetch(spec, downloads), "r:gz") as archive:
            archive.extractall(overlay, members=[m for m in archive
                if not m.name.startswith(".")], filter="data")
    quest = overlay / "opt/quest"
    quest.mkdir(parents=True, exist_ok=True)
    cli = quest / 'desktop-cli'
    cli.mkdir(exist_ok=True)
    for spec in LOCK['desktop_cli'].values():
        shutil.copyfile(fetch(spec, downloads), cli / spec['url'].rsplit('/', 1)[1])
    desktop = root / "opt/stearlight-desktop"
    if args.frame_desktop_root:
        run(sys.executable, REPO / "build_files/stage-frame-desktop.py",
            "--root", args.frame_desktop_root, "--output", overlay / "opt/stearlight-desktop")
    elif (desktop / "usr/bin/plasmashell").is_file():
        shutil.copytree(desktop, overlay / "opt/stearlight-desktop", dirs_exist_ok=True)
    else:
        parser.error("Provide --frame-desktop-root, or stage Plasma in the clean rootfs first")
    browser = root / "opt/stearlight-browser"
    if args.frame_desktop_root:
        run("python3", REPO / "build_files/quest/stage-chromium.py", "--frame", args.frame_desktop_root,
            "--desktop", overlay / "opt/stearlight-desktop", "--output", overlay / "opt/stearlight-browser",
            "--downloads", downloads)
    elif (browser / "usr/lib/chromium/chromium").is_file():
        shutil.copytree(browser, overlay / "opt/stearlight-browser", dirs_exist_ok=True)
    else:
        parser.error("Provide --frame-desktop-root to stage Chromium and its ARM64 dependencies")
    for name in ("desktop-session.sh", "desktop-headset.sh", "desktop-launcher.sh", "desktop-manager.py", "steam_channels.py", "chromium-launcher.sh",
                 "desktop-ui-bridge.js", "plasma-core-session.sh", "prepare-desktop.sh",
                 "prepare-desktop-ui.py", "prepare-desktop-kio.py", "desktop-shell.sh", "adb-command.py", "desktop-remote.sh", "desktop-fonts.conf",
                 "flatpak-launcher.py", "prepare-flatpak.py", "flathub.flatpakrepo", "prepare-desktop-cli.sh", "desktop-defaults.py", "desktop-device-settings.sh"):
        shutil.copyfile(REPO / "packages/quest-runtime/linux" / name, quest / name)
        (quest / name).chmod(0o755)
    pam_headers = work / "desktop-pam-headers"
    pam_headers.mkdir(exist_ok=True)
    shutil.copytree('/usr/include/security', pam_headers / 'security', dirs_exist_ok=True)
    run("aarch64-linux-gnu-gcc", "-O2", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
        f"-I{pam_headers}", REPO / "packages/quest-runtime/linux/desktop-pam.c",
        f"-L{overlay / 'opt/stearlight-desktop/usr/lib'}", "-l:libpam.so.0", "-l:libcrypt.so.2",
        "-Wl,--disable-new-dtags,-rpath,/opt/stearlight-desktop/usr/lib", "-o", quest / "desktop-pam.so")
    run("aarch64-linux-gnu-gcc", "-O2", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
        REPO / "packages/quest-runtime/linux/desktop-access-runtime.c", "-ldl",
        "-o", quest / "libdesktop_access.so")
    mesa_build = build_mesa(root, work, downloads)
    shutil.copy2(mesa_build / "src/freedreno/vulkan/libvulkan_freedreno.so", quest / "libvulkan_freedreno.so")
    run("aarch64-linux-gnu-strip", quest / "libvulkan_freedreno.so")
    gl_libraries = overlay / "usr/lib/aarch64-linux-gnu"
    gl_libraries.mkdir(parents=True, exist_ok=True)
    # EGL and GLX must share the matching Mesa driver for opaque-fd textures.
    # The older rootfs EGL driver can render locally while Vulkan sees black.
    egl_vendors = overlay / "usr/share/glvnd/egl_vendor.d"
    egl_vendors.mkdir(parents=True, exist_ok=True)
    (egl_vendors / "50_mesa.json").write_text(json.dumps({
        "file_format_version": "1.0.0",
        "ICD": {"library_path": "/usr/lib/aarch64-linux-gnu/libEGL_mesa.so.0"}}))
    for source in ["src/gallium/targets/dri/libgallium-26.1.6.so", "src/glx/libGLX_mesa.so.0.0.0",
                   "src/egl/libEGL_mesa.so.0.0.0"]:
        destination = gl_libraries / Path(source).name
        shutil.copyfile(mesa_build / source, destination)
        run("aarch64-linux-gnu-strip", "--strip-unneeded", destination)
        run("patchelf", "--set-rpath", "$ORIGIN", destination)
    egl_alias = gl_libraries / "libEGL_mesa.so.0"
    egl_alias.unlink(missing_ok=True)
    egl_alias.symlink_to("libEGL_mesa.so.0.0.0")
    includes = REPO / "packages/quest-runtime/app/src/main/cpp"
    linux = REPO / "packages/quest-runtime/linux"
    driver = quest / "stearlight-quest"
    (driver / "bin/linuxarm64").mkdir(parents=True, exist_ok=True)
    (driver / "resources").mkdir(exist_ok=True)
    shutil.copytree(linux / "resources", driver / "resources", dirs_exist_ok=True)
    run("aarch64-linux-gnu-g++", "-O2", "-shared", "-fPIC", "-Wl,-Bsymbolic", "-std=c++17",
        f"-I{includes}", linux / "quest_wsi.cpp", "-ldl", "-o", quest / "libquest_vulkan.so")
    run("aarch64-linux-gnu-g++", "-O2", "-shared", "-fPIC", "-std=c++17",
        f"-I{includes}", f"-I{REPO / 'packages/openvr/headers'}", linux / "quest_driver.cpp",
        "-o", driver / "bin/linuxarm64/driver_stearlight-quest.so")
    for name, libs in [("gpu-probe", ["-ldl"]), ("angle-probe", ["-ldl"]), ("glx-probe", ["-ldl"]), ("ipc-probe", ["-pthread"])]:
        run("aarch64-linux-gnu-gcc", "-O2", linux / f"{name}.c", *libs, "-o", quest / name)
    run("aarch64-linux-gnu-gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
        linux / "socket-report.c", "-ldl", "-o", quest / "libquest_socket.so")
    verify_musl_preload(quest / "libquest_socket.so", root)
    run("aarch64-linux-gnu-gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
        linux / "wine-pe-memory.c", "-ldl", "-o", quest / "libquest_wine_pe.so")
    for compiler, architecture in (("x86_64-linux-gnu-g++", "x86_64"), ("aarch64-linux-gnu-g++", "aarch64")):
        run(compiler, "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC", "-Wl,-Bsymbolic-functions", "-std=c++17",
            f"-I{includes}", linux / "sdl-gamepad.cpp", "-ldl", "-o", quest / f"libquest_gamepad_{architecture}.so")
    run("aarch64-linux-gnu-g++", "-O2", "-shared", "-fPIC", "-std=c++17",
        f"-I{includes}", linux / "video-bridge.cpp", "-ldl", "-pthread",
        "-o", quest / "libquest_video.so")
    build_streaming_allocator(linux, quest, work)
    run("aarch64-linux-gnu-gcc", "-O2", linux / "streaming-client-launcher.c",
        "-o", quest / "streaming-client-launcher")
    shutil.copyfile(linux / "prepare-streaming-client.sh", quest / "prepare-streaming-client.sh")
    run("aarch64-linux-gnu-gcc", "-O2", linux / "shader-replay-launcher.c",
        "-o", quest / "shader-replay-launcher")
    run("aarch64-linux-gnu-gcc", "-O2", linux / "gamescopereaper.c",
        "-o", quest / "gamescopereaper")
    gamescope_scripts = overlay / "etc/gamescope/scripts"
    gamescope_scripts.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(linux / "gamescope-local-games.lua", gamescope_scripts / "90-quest-local-games.lua")
    shutil.copyfile(linux / "prepare-game-runtime.sh", quest / "prepare-game-runtime.sh")
    shutil.copyfile(linux / "prepare-fex.py", quest / "prepare-fex.py")
    shutil.copyfile(linux / "fex-game-launcher.py", quest / "fex-game-launcher.py")
    shutil.copyfile(linux / "prepare-gamepad-ui.py", quest / "prepare-gamepad-ui.py")
    for name in ("proton-game-launcher.py", "proton-runtime-entry.sh", "audio-namespace-launcher.sh"):
        shutil.copyfile(linux / name, quest / name)
    launcher = overlay / "opt/steamvr/bin/linuxarm64/vrcompositor"
    launcher.parent.mkdir(parents=True, exist_ok=True)
    run("aarch64-linux-gnu-gcc", "-O2", linux / "compositor-launcher.c", "-o", launcher)
    dbus_flags = subprocess.check_output(["pkg-config", "--cflags", "dbus-1"], text=True).split()
    run("aarch64-linux-gnu-g++", "-O2", "-std=c++17", "-pthread", "-DQUEST_SERVICES_EMBEDDED", *dbus_flags,
        f"-I{REPO / 'packages/openvr/headers'}", linux / "runtime-monitor.cpp",
        linux / "bluetooth-bridge.cpp", f"-L{root / 'opt/steamvr/bin/linuxarm64'}", "-lopenvr_api",
        f"-L{root / 'usr/lib/aarch64-linux-gnu'}",
        f"-Wl,-rpath-link,{root / 'usr/lib/aarch64-linux-gnu'}",
        f"-Wl,-rpath-link,{root / 'lib/aarch64-linux-gnu'}",
        "-l:libdbus-1.so.3", "-Wl,--allow-shlib-undefined",
        "-Wl,-rpath,/opt/steamvr/bin/linuxarm64", "-o", quest / "runtime-monitor")
    (quest / "bluetooth-bridge").unlink(missing_ok=True)
    (driver / "driver.vrdrivermanifest").write_text(json.dumps(
        {"alwaysActivate": True, "name": "stearlight-quest", "directory": "", "resourceOnly": False}))
    shutil.copy2(root / "opt/steamvr/drivers/stearlight-fake/resources/frame_hmd_additional.vrsettings",
                 driver / "resources/frame_hmd_additional.vrsettings")
    for name, library in [("quest", "libquest_vulkan.so"), ("turnip", "libvulkan_freedreno.so")]:
        (quest / f"{name}_icd.json").write_text(json.dumps({"file_format_version": "1.0.0",
            "ICD": {"library_path": f"/opt/quest/{library}", "api_version": "1.3.0"}}))
    session = (REPO / "system_files/usr/local/libexec/stearlight/steamframe-session").read_text()
    (quest / "frame-session").write_text(session.replace(
        "export STEARLIGHT_FAKE_TRACKING=1", "export STEARLIGHT_FAKE_TRACKING=0"))
    (quest / "start-session.sh").write_text((linux / "start-session.sh").read_text())
    shutil.copyfile(linux / "system-bus.conf", quest / "system-bus.conf")
    shutil.copyfile(linux / "audio.pa", quest / "audio.pa")
    shutil.copyfile(linux / "pipewire.conf", quest / "pipewire.conf")
    wireplumber_config = overlay / "etc/wireplumber/wireplumber.conf.d/20-quest.conf"
    wireplumber_config.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(linux / "wireplumber-quest.conf", wireplumber_config)
    webhelper = overlay / "usr/local/bin/stearlight-frame-vrwebhelper"
    webhelper.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(linux / "webhelper-launcher.sh", webhelper)
    webhelper.chmod(0o755)
    # Observe Valve's native room-view action without replacing its UI.
    system_ui = Path("opt/steamvr/resources/webinterface/dashboard/systemui.js")
    (overlay / system_ui).parent.mkdir(parents=True, exist_ok=True)
    (overlay / system_ui).write_bytes((linux / "systemui-overlay-bridge.js").read_bytes() + b"\n" +
                                    (linux / "roomview-bridge.js").read_bytes() + b"\n" +
                                    roomview_system_ui((root / system_ui).read_bytes()))
    shutil.copyfile(linux / "store-browser-bridge.js", quest / "store-browser-bridge.js")
    shutil.copyfile(linux / "steam-update-bridge.js", quest / "steam-update-bridge.js")
    shutil.copyfile(linux / "prepare-store-browser.sh", quest / "prepare-store-browser.sh")
    shutil.copyfile(linux / "prepare-steam-client.sh", quest / "prepare-steam-client.sh")
    shutil.copyfile(linux / "steam-channel-args.sh", quest / "steam-channel-args.sh")
    shutil.copyfile(linux / "prepare-cef.sh", quest / "prepare-cef.sh")
    shutil.copyfile(linux / "prepare-steam-input.py", quest / "prepare-steam-input.py")
    run("aarch64-linux-gnu-gcc", "-O2", "-static", "-Wall", "-Wextra", "-Werror",
        linux / "game-launch-wrapper.c", "-o", quest / "game-launch-wrapper")
    shutil.copyfile(linux / "prepare-frame-controls.sh", quest / "prepare-frame-controls.sh")
    shutil.copyfile(linux / "steamwebhelper-launcher.sh", quest / "steamwebhelper-launcher.sh")
    shutil.copyfile(linux / "cef-arguments.sh", quest / "cef-arguments.sh")
    shutil.copyfile(linux / "prepare-vrclient.sh", quest / "prepare-vrclient.sh")
    shutil.copyfile(REPO / "system_files/usr/local/libexec/stearlight/frame-steam-ipc",
                    quest / "frame-steam-ipc")
    run("aarch64-linux-gnu-gcc", "-O2", "-Wall", "-Wextra",
        linux / "store-browser-watch.c", "-o", quest / "store-browser-watch")
    shutil.copyfile(root / "usr/bin/lsof", quest / "lsof")
    (quest / "lsof").chmod(0o755)
    run("aarch64-linux-gnu-gcc", "-O2", "-Wall", "-Wextra", "-Werror",
        linux / "lsof-launcher.c", "-o", overlay / "usr/bin/lsof")
    # Drop the generated library from older cached overlays. Startup adapts
    # the installed version instead of replacing Valve's matching UI files.
    (overlay / "opt/steam/steamui/library.js").unlink(missing_ok=True)
    for name in ["frame-session", "start-session.sh"]:
        (quest / name).chmod(0o755)
    # Several launchers are executed directly by Steam/KDE rather than bash.
    for file in quest.iterdir():
        if file.is_file() and file.suffix in (".sh", ".py"):
            file.chmod(0o755)
    overlay_archive(overlay, assets / "quest-overlay.bin")
    proot = work / "android-proot"
    for spec in LOCK["android"].values():
        run("dpkg-deb", "-x", fetch(spec, downloads), proot)
    prefix = proot / "data/data/com.termux/files/usr"
    jni = output / "jniLibs/arm64-v8a"
    jni.mkdir(parents=True, exist_ok=True)
    for source, name in [("bin/proot", "libproot_exec.so"), ("libexec/proot/loader", "libproot_loader.so"),
                          ("lib/libtalloc.so.2.5.0", "libtalloc.so"), ("lib/libandroid-shmem.so", "libandroid-shmem.so")]:
        shutil.copyfile(prefix / source, jni / name)
    run(sys.executable, REPO / "build_files/quest/build-proot.py",
        "--archive", fetch(LOCK["sources"]["proot"], downloads), "--prefix", prefix,
        "--android-ndk", args.android_ndk.resolve(), "--output", jni / "libproot_exec.so")
    headers = output / "include/openxr"
    headers.mkdir(parents=True, exist_ok=True)
    for name, spec in LOCK["openxr"].items():
        shutil.copyfile(fetch(spec, downloads), headers / name)
    source_files = [fetch(spec, downloads) for spec in LOCK["sources"].values()]
    source_files.append(fetch(LOCK["mesa"], downloads))
    source_files.extend(fetch(spec, downloads) for spec in LOCK["fna3d"].values())
    source_files.extend(fetch(spec, downloads) for spec in LOCK["fonts"].values())
    source_files.extend(fetch(spec, downloads) for spec in LOCK["audio"].values())
    source_files.extend(fetch(spec, downloads) for spec in LOCK["updates"].values())
    source_files.extend(fetch(spec, downloads) for spec in LOCK["gaming"].values())
    source_files.extend(fetch(spec, downloads) for spec in LOCK["fex_libraries"].values())
    source_files.extend(fetch(spec, downloads) for spec in LOCK["chromium"].values())
    source_files.extend(fetch(spec, downloads) for spec in LOCK['desktop_cli'].values())
    (assets / "quest-dependencies.json").write_text(json.dumps(LOCK, indent=2) + "\n")
    source_bundle = output.parent / (output.name + "-sources.zip")
    with zipfile.ZipFile(source_bundle, "w", compression=zipfile.ZIP_STORED) as bundle:
        for source in source_files:
            bundle.write(source, "upstream/" + source.name)
        for source in (REPO / "build_files/quest").glob("*"):
            if source.is_file():
                bundle.write(source, "build_files/quest/" + source.name)
        for source in linux.glob("*"):
            if source.is_file():
                bundle.write(source, "packages/quest-runtime/linux/" + source.name)
        for source in (REPO / "packages/quest-runtime/proot").glob("*"):
            if source.is_file():
                bundle.write(source, "packages/quest-runtime/proot/" + source.name)
        for directory in ["packages/quest-runtime/app/src", "packages/quest-runtime/tests"]:
            for source in (REPO / directory).rglob("*"):
                if source.is_file() and "__pycache__" not in source.parts:
                    bundle.write(source, source.relative_to(REPO))
        for name in ["build_files/stage-frame-desktop.py", "post_process/quest/shell.ps1",
                     "post_process/quest/shell.sh", "post_process/quest/usb-network.ps1",
                     "packages/quest-runtime/README.md",
                     "system_files/usr/local/libexec/stearlight/frame-steam-ipc",
                     "system_files/usr/local/libexec/stearlight/steamframe-session",
                     "system_files/usr/bin/steamos-select-branch", "system_files/usr/bin/steamos-update",
                     "system_files/usr/bin/steamos-polkit-helpers/steamos-select-branch",
                     "system_files/usr/bin/steamos-polkit-helpers/steamos-update",
                     "system_files/etc/fonts/conf.d/66-stearlight-cjk.conf"]:
            bundle.write(REPO / name, name)
    archive = args.rootfs_archive
    if archive is None:
        archive = work / "rootfs.tar.gz"
        run(sys.executable, REPO / "build_files/quest/package-rootfs.py", "--root", root,
            "--overlay", overlay, "--output", archive)
    run(sys.executable, REPO / "build_files/quest/split-rootfs.py", archive, assets)
    print(f"Quest payload: {output}\nDependency sources: {source_bundle}", flush=True)


if __name__ == "__main__":
    main()
