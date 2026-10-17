#!/usr/bin/env python3
"""Extract Valve's ARM64 Plasma workspace into a private runtime prefix."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess

BINARIES = """plasmashell kwin_wayland kwin_wayland_wrapper kwin_x11
startplasma-wayland startplasma-x11 plasma_session ksmserver kded6
kactivitymanagerd kglobalacceld kaccess ksplashqml plasma_waitforname
kcminit kcminit_startup kapplymousetheme
plasma-apply-lookandfeel plasma-apply-desktoptheme plasma-apply-colorscheme
plasma-apply-wallpaperimage dolphin konsole systemsettings
kbuildsycoca6 kreadconfig6 kwriteconfig6 kquitapp6 kstart
dbus-daemon dbus-run-session dbus-launch sshd ssh-keygen ssh sftp openssl unix_chkpwd
xrdp xrdp-sesman xrdp-sesexec xrdp-chansrv xrdp-keygen
Xorg Xvnc xauth xrdb xset xprop xrandr xdpyinfo xkbcomp
steamos-nested-desktop""".split()
BINARIES += ['flatpak', 'bwrap', 'xdg-open', 'xdg-mime', 'kcmshell6', 'kdialog']
BINARIES += ['gpg', 'gpgv', 'gpgconf', 'gpg-agent', 'gpg-connect-agent',
             'gpgsm', 'dirmngr', 'pinentry', 'pinentry-qt', 'ostree', 'xdg-dbus-proxy']
TREES = """usr/lib/qt6/plugins usr/lib/qt6/qml usr/lib/kf6 usr/lib/plasma
usr/lib/xorg/modules usr/lib/xrdp usr/lib/security usr/lib/krb5/plugins
usr/share/plasma usr/share/kpackage usr/share/kservices6 usr/share/kservicetypes6
usr/share/kf6 usr/share/kio usr/share/knotifications6 usr/share/kconf_update
usr/share/konsole usr/share/dolphin usr/share/kwin usr/share/ksmserver
usr/share/color-schemes usr/share/icons/breeze usr/share/icons/breeze-dark
usr/share/icons/hicolor usr/share/wallpapers usr/share/steamos
usr/share/icons/breeze_cursors usr/share/icons/Adwaita usr/share/icons/default
usr/share/Qt6 usr/share/X11/xkb usr/share/xrdp usr/share/wayland-sessions
usr/share/xsessions usr/share/dbus-1/services usr/share/kglobalaccel
usr/share/mime usr/share/locale/en_US usr/share/locale/fr
usr/share/licenses usr/lib/locale/C.utf8 usr/lib/locale/locale-archive
usr/share/desktop-directories
etc/xdg etc/xrdp etc/X11/xrdp etc/fonts""".split()
TREES += ['usr/lib/vlc', 'usr/lib/qt/plugins', 'usr/share/vlc', 'usr/share/vim',
          'usr/share/terminfo', 'usr/share/kmenuedit', 'usr/share/discover',
          'usr/share/glib-2.0/schemas', 'usr/share/applications/steam/steamos-nested-desktop']
TREES += ['usr/lib/gnupg', 'usr/share/gnupg', 'etc/gnupg']
TREES += ['usr/lib/spa-0.2/support']
UNAVAILABLE_INDEXING_PLUGINS = {
    # KWin probes this service even when its splash is disabled. The default
    # waits indefinitely for a splash process that this core session omits.
    'usr/share/dbus-1/services/org.kde.KSplash.service',
    'usr/lib/vlc/plugins/plugins.dat',
    'usr/lib/qt6/plugins/kf6/kio/tags.so',
    'usr/lib/qt6/plugins/kf6/kio/timeline.so',
    'usr/lib/qt6/plugins/kf6/kio/baloosearch.so',
    'usr/lib/qt6/plugins/kf6/kded/baloosearchmodule.so',
    'usr/lib/qt6/plugins/kf6/kfileitemaction/tagsfileitemaction.so',
}


def stage(root, destination, append=False):
    root, destination = root.resolve(), destination.resolve()
    if not (root / "usr/bin/plasmashell").is_file():
        raise ValueError("Frame OS root must contain KDE Plasma")
    destination.mkdir(parents=True, exist_ok=True)
    pending, seen = [], set()
    copied = set()
    previous_manifest = destination / "manifest.json"
    previous_files = set(json.loads(previous_manifest.read_text()).get("files", {})) if previous_manifest.exists() else set()
    if append:
        copied.update(previous_files)
    library_directories = {Path("usr/lib"), Path("usr/lib/xrdp")}
    libraries = {}
    for file in (root / "usr/lib").rglob("*.so*"):
        if file.is_file():
            libraries.setdefault(file.name, file.relative_to(root))
    dependency_cache = {}
    unavailable_plugins = []

    def dependencies_available(source, visiting=None):
        visiting = set() if visiting is None else visiting
        if source in visiting:
            return True
        if source in dependency_cache:
            return dependency_cache[source]
        text = subprocess.check_output(["readelf", "-d", str(source)], text=True)
        names = re.findall(r"\(NEEDED\).*?\[(.*?)\]", text)
        result = all(name in libraries and dependencies_available(root / libraries[name], visiting | {source})
                     for name in names)
        dependency_cache[source] = result
        return result

    def copy(relative):
        relative = Path(relative)
        # These protocols require a running Baloo index. KIO probes tags:/
        # during startup and otherwise displays an error in every desktop.
        if str(relative) in UNAVAILABLE_INDEXING_PLUGINS:
            (destination / relative).unlink(missing_ok=True)
            return
        if str(relative) in copied:
            return
        if any(part.startswith(("QtQuick3D", "Qt3D")) for part in relative.parts):
            return
        # Frame's minimized image omits PackageKit's Qt library. This optional
        # package-install handler is not needed by the Plasma workspace.
        if str(relative) == "usr/lib/kf6/kpackagehandlers/appstreamhandler":
            (destination / relative).unlink(missing_ok=True)
            return
        source = root / relative
        if not source.exists():
            return
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            for item in source.rglob("*"):
                if item.is_file():
                    copy(item.relative_to(root))
            return
        # Resolve aliases inside the image, not against the build host's /usr.
        while source.is_symlink():
            link = source.readlink()
            source = root / str(link).lstrip("/") if link.is_absolute() else source.parent / link
        if not source.is_file():
            raise ValueError(f"Missing Frame desktop alias target: {relative}")
        with source.open("rb") as stream:
            header = stream.read(18)
            elf = header[:4] == b"\x7fELF"
        if elf and int.from_bytes(header[16:18], "little") not in (2, 3):
            target.unlink(missing_ok=True)
            return
        if elf and str(relative).startswith(("usr/lib/qt6/", "usr/lib/qt/plugins/", "usr/lib/vlc/plugins/", "usr/lib/kf6/")) and not dependencies_available(source):
            unavailable_plugins.append(str(relative))
            target.unlink(missing_ok=True)
            return
        shutil.copy2(source, target)
        copied.add(str(relative))
        with target.open("rb") as stream:
            if stream.read(4) == b"\x7fELF":
                pending.append(target)

    for name in BINARIES:
        copy("usr/bin/" + name)
        if not (root / "usr/bin" / name).is_file():
            copy("usr/lib/" + name)
    # The recovery image's application catalog is authoritative. Include its
    # native desktop programs rather than selecting only three KDE launchers.
    # Python/system-daemon tools need a full system login and are not bundled.
    applications = []
    for entry in sorted((root / 'usr/share/applications').glob('*.desktop')):
        config = configparser.ConfigParser(interpolation=None, strict=False)
        config.read(entry)
        if config.get('Desktop Entry', 'NoDisplay', fallback='false').lower() == 'true':
            continue
        command = config.get('Desktop Entry', 'Exec', fallback='')
        if not command:
            continue
        executable = shlex.split(command)[0]
        if executable in ('sh', 'bash'):
            continue
        binary = root / executable.lstrip('/') if executable.startswith('/') else root / 'usr/bin' / executable
        if binary.is_file() and binary.read_bytes()[:4] == b'\x7fELF':
            copy(binary.relative_to(root))
            copy(entry.relative_to(root))
            applications.append(entry.name)
    for name in ['deckard-nested-desktop.desktop', 'org.chromium.Chromium.desktop', 'uxterm.desktop']:
        copy('usr/share/applications/' + name)
    copy("usr/share/wallpapers/Steam Deck Logo Default.jpg")
    for relative in TREES:
        copy(relative)
    for name in ['org.kde.konsole.desktop', 'org.kde.dolphin.desktop',
                 'org.kde.plasmashell.desktop', 'systemsettings.desktop', 'org.kde.systemsettings.desktop']:
        copy('usr/share/applications/' + name)
    for relative in ["usr/lib/Xorg", "usr/lib/ld-linux-aarch64.so.1", "usr/lib/ssh/sftp-server",
                     "usr/lib/openssh/sftp-server", "usr/lib/openssh/sshd-session",
                     "usr/lib/ssh/sshd-session", "usr/lib/libGL.so.1",
                     "usr/lib/libEGL.so.1", "usr/lib/libvulkan.so.1"]:
        copy(relative)
    # All libraries, Qt plugins, and KDE workers are checked recursively.
    while pending:
        binary = pending.pop()
        if binary in seen:
            continue
        seen.add(binary)
        text = subprocess.check_output(["readelf", "-d", str(binary)], text=True)
        for name in re.findall(r"\(NEEDED\).*?\[(.*?)\]", text):
            locations = [Path("usr/lib") / name, Path("usr/lib/xrdp") / name,
                         Path("usr/lib/qt6") / name, Path("usr/lib/xorg/modules") / name]
            if name in libraries and libraries[name] not in locations:
                locations.append(libraries[name])
            relative = next((path for path in locations if (root / path).is_file()), None)
            if relative is None:
                raise ValueError(f"Missing desktop dependency {name} for {binary.name}")
            library_directories.add(relative.parent)
            copy(relative)
    # Relocate executable interpreters without changing the shared OS loader.
    prefix = "/opt/stearlight-desktop/usr"
    rpath = ":".join("/opt/stearlight-desktop/" + str(path) for path in sorted(library_directories))
    for binary in seen:
        if binary.name == "ld-linux-aarch64.so.1":
            continue
        program = subprocess.check_output(["readelf", "-l", str(binary)], text=True)
        subprocess.run(["patchelf", "--force-rpath", "--set-rpath",
                        rpath, str(binary)], check=True)
        if "Requesting program interpreter" in program:
            subprocess.run(["patchelf", "--set-interpreter", prefix + "/lib/ld-linux-aarch64.so.1",
                            str(binary)], check=True)
    (destination / "usr/bin/qt.conf").write_text(
        "[Paths]\nPrefix=..\nLibraries=lib\nPlugins=lib/qt6/plugins\nQmlImports=lib/qt6/qml\nData=share\n")
    copied.add("usr/bin/qt.conf")
    for name in previous_files - copied:
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Invalid cached desktop manifest path")
        (destination / relative).unlink(missing_ok=True)
    manifest = json.loads(previous_manifest.read_text()).get('files', {}) if append and previous_manifest.exists() else {}
    for file in sorted(destination.rglob("*")):
        if file.is_file() and file != previous_manifest:
            with file.open("rb") as stream:
                manifest[str(file.relative_to(destination))] = hashlib.file_digest(stream, "sha256").hexdigest()
    for name in UNAVAILABLE_INDEXING_PLUGINS:
        manifest.pop(name, None)
    (destination / "manifest.json").write_text(json.dumps({"source": "Steam Frame OS", "files": manifest,
                                                           "applications": applications,
                                                           "unavailable_optional_plugins": unavailable_plugins}, indent=2) + "\n")
    print(f"Staged Frame Plasma: {len(manifest)} files, {len(seen)} ELF objects")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--append", action='store_true', help='Extend a cached bundle without recopying its files')
    args = parser.parse_args()
    stage(args.root, args.output, args.append)
