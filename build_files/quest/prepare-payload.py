#!/usr/bin/env python3
"""Prepare the ARM64 Quest APK payload on Ubuntu 24.04 or an equivalent Linux host."""
import argparse
import gzip
import hashlib
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


def stage_glx_dependencies(root, library_dir):
    """Use host X11 headers with the clean rootfs's ARM64 link libraries."""
    pending = ["x11", "x11-xcb", "xext", "xrandr", "xxf86vm", "glproto", "libglvnd",
               "xcb-glx", "xcb-shm", "xcb-dri3", "xcb-present", "xcb-sync",
               "xcb-xfixes", "xshmfence"]
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
        "-Dopengl=true", "-Degl=disabled", "-Dgbm=disabled", "-Dglx=dri", "-Dglvnd=enabled", "-Dllvm=disabled",
        "-Dvideo-codecs=", "-Dbuild-tests=false", "-Dtools=", "-Dvalgrind=disabled", "-Dlibunwind=disabled",
        "-Ddisplay-info=disabled", "-Dzstd=disabled", "-Dandroid-libbacktrace=disabled", "-Dzlib=enabled",
        "-Dexpat=disabled", "-Dxmlconfig=disabled", "-Dspirv-tools=disabled", "-Dshader-cache=disabled")
    run("ninja", "-C", build, "-j", min(os.cpu_count() or 2, 8),
        "src/freedreno/vulkan/libvulkan_freedreno.so",
        "src/gallium/targets/dri/libgallium-26.1.6.so", "src/glx/libGLX_mesa.so.0.0.0")
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


def main():
    global BUILD_LOG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Mounted clean Stearlight ARM64 rootfs")
    parser.add_argument("--work", type=Path, required=True, help="Linux build/cache directory")
    parser.add_argument("--output", type=Path, required=True, help="New Android payload directory")
    parser.add_argument("--rootfs-archive", type=Path, help="Reuse an existing clean rootfs.tar.gz")
    args = parser.parse_args()
    root, work, output = (path.resolve() for path in (args.root, args.work, args.output))
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
    for spec in LOCK["linux"].values():
        with tarfile.open(fetch(spec, downloads), "r:gz") as archive:
            archive.extractall(overlay, members=[m for m in archive
                if not m.name.startswith(".")], filter="data")
    quest = overlay / "opt/quest"
    quest.mkdir(parents=True, exist_ok=True)
    mesa_build = build_mesa(root, work, downloads)
    shutil.copy2(mesa_build / "src/freedreno/vulkan/libvulkan_freedreno.so", quest / "libvulkan_freedreno.so")
    run("aarch64-linux-gnu-strip", quest / "libvulkan_freedreno.so")
    gl_libraries = overlay / "usr/lib/aarch64-linux-gnu"
    gl_libraries.mkdir(parents=True, exist_ok=True)
    for source in ["src/gallium/targets/dri/libgallium-26.1.6.so", "src/glx/libGLX_mesa.so.0.0.0"]:
        destination = gl_libraries / Path(source).name
        shutil.copyfile(mesa_build / source, destination)
        run("aarch64-linux-gnu-strip", "--strip-unneeded", destination)
        run("patchelf", "--set-rpath", "$ORIGIN", destination)
    includes = REPO / "packages/quest-runtime/app/src/main/cpp"
    linux = REPO / "packages/quest-runtime/linux"
    driver = quest / "stearlight-quest"
    (driver / "bin/linuxarm64").mkdir(parents=True, exist_ok=True)
    (driver / "resources").mkdir(exist_ok=True)
    run("aarch64-linux-gnu-g++", "-O2", "-shared", "-fPIC", "-Wl,-Bsymbolic", "-std=c++17",
        f"-I{includes}", linux / "quest_wsi.cpp", "-ldl", "-o", quest / "libquest_vulkan.so")
    run("aarch64-linux-gnu-g++", "-O2", "-shared", "-fPIC", "-std=c++17",
        f"-I{includes}", f"-I{REPO / 'packages/openvr/headers'}", linux / "quest_driver.cpp",
        "-o", driver / "bin/linuxarm64/driver_stearlight-quest.so")
    for name, libs in [("gpu-probe", ["-ldl"]), ("angle-probe", ["-ldl"]), ("glx-probe", ["-ldl"]), ("ipc-probe", ["-pthread"])]:
        run("aarch64-linux-gnu-gcc", "-O2", linux / f"{name}.c", *libs, "-o", quest / name)
    launcher = overlay / "opt/steamvr/bin/linuxarm64/vrcompositor"
    launcher.parent.mkdir(parents=True, exist_ok=True)
    run("aarch64-linux-gnu-gcc", "-O2", linux / "compositor-launcher.c", "-o", launcher)
    run("aarch64-linux-gnu-g++", "-O2", f"-I{REPO / 'packages/openvr/headers'}",
        linux / "runtime-monitor.cpp", f"-L{root / 'opt/steamvr/bin/linuxarm64'}", "-lopenvr_api",
        "-Wl,-rpath,/opt/steamvr/bin/linuxarm64", "-o", quest / "runtime-monitor")
    dbus_flags = subprocess.check_output(["pkg-config", "--cflags", "dbus-1"], text=True).split()
    run("aarch64-linux-gnu-g++", "-O2", "-std=c++17", *dbus_flags,
        linux / "bluetooth-bridge.cpp", f"-L{root / 'usr/lib/aarch64-linux-gnu'}",
        f"-Wl,-rpath-link,{root / 'usr/lib/aarch64-linux-gnu'}",
        f"-Wl,-rpath-link,{root / 'lib/aarch64-linux-gnu'}",
        "-l:libdbus-1.so.3", "-o", quest / "bluetooth-bridge")
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
    webhelper = overlay / "usr/local/bin/stearlight-frame-vrwebhelper"
    webhelper.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(linux / "webhelper-launcher.sh", webhelper)
    webhelper.chmod(0o755)
    # Observe Valve's native room-view action without replacing its UI.
    system_ui = Path("opt/steamvr/resources/webinterface/dashboard/systemui.js")
    (overlay / system_ui).parent.mkdir(parents=True, exist_ok=True)
    (overlay / system_ui).write_bytes((linux / "roomview-bridge.js").read_bytes() + b"\n" +
                                    (root / system_ui).read_bytes())
    shutil.copyfile(linux / "store-browser-bridge.js", quest / "store-browser-bridge.js")
    shutil.copyfile(linux / "prepare-store-browser.sh", quest / "prepare-store-browser.sh")
    shutil.copyfile(linux / "prepare-cef.sh", quest / "prepare-cef.sh")
    shutil.copyfile(linux / "prepare-vrclient.sh", quest / "prepare-vrclient.sh")
    shutil.copyfile(REPO / "system_files/usr/local/libexec/stearlight/frame-steam-ipc",
                    quest / "frame-steam-ipc")
    run("aarch64-linux-gnu-gcc", "-O2", "-Wall", "-Wextra",
        linux / "store-browser-watch.c", "-o", quest / "store-browser-watch")
    shutil.copyfile(root / "usr/bin/lsof", quest / "lsof")
    (quest / "lsof").chmod(0o755)
    shutil.copyfile(linux / "lsof-launcher.sh", overlay / "usr/bin/lsof")
    (overlay / "usr/bin/lsof").chmod(0o755)
    # Drop the generated library from older cached overlays. Startup adapts
    # the installed version instead of replacing Valve's matching UI files.
    (overlay / "opt/steam/steamui/library.js").unlink(missing_ok=True)
    for name in ["frame-session", "start-session.sh"]:
        (quest / name).chmod(0o755)
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
    run("patchelf", "--replace-needed", "libtalloc.so.2", "libtalloc.so", "--set-rpath", "$ORIGIN",
        jni / "libproot_exec.so")
    headers = output / "include/openxr"
    headers.mkdir(parents=True, exist_ok=True)
    for name, spec in LOCK["openxr"].items():
        shutil.copyfile(fetch(spec, downloads), headers / name)
    source_files = [fetch(spec, downloads) for spec in LOCK["sources"].values()]
    source_files.append(fetch(LOCK["mesa"], downloads))
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
        for name in ["quest_bridge.h", "gpu_bridge.h"]:
            bundle.write(REPO / "packages/quest-runtime/app/src/main/cpp" / name,
                         "packages/quest-runtime/app/src/main/cpp/" + name)
        bundle.write(REPO / "system_files/usr/local/libexec/stearlight/frame-steam-ipc",
                     "system_files/usr/local/libexec/stearlight/frame-steam-ipc")
    archive = args.rootfs_archive
    if archive is None:
        archive = work / "rootfs.tar.gz"
        run(sys.executable, REPO / "build_files/quest/package-rootfs.py", "--root", root,
            "--overlay", overlay, "--output", archive)
    run(sys.executable, REPO / "build_files/quest/split-rootfs.py", archive, assets)
    print(f"Quest payload: {output}\nDependency sources: {source_bundle}", flush=True)


if __name__ == "__main__":
    main()
