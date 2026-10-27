#!/usr/bin/python3
"""Keep Flatpak's signed installer; launch GUI apps in the Quest runtime."""
import configparser
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time
import uuid

NATIVE = Path('/opt/stearlight-desktop/usr/bin/flatpak.native')
STATE = Path('/run/stearlight/quest')
HOME = Path('/home/stearlight')


def config(path):
    value = configparser.ConfigParser(interpolation=None, strict=False)
    value.optionxform = str
    value.read(path)
    return value


def run_arguments(args):
    command = branch = arch = None
    index = 0
    while index < len(args) and args[index].startswith('-'):
        arg = args[index]
        if arg in ('--user', '--file-forwarding', '--verbose', '-v'):
            pass
        elif arg.startswith('--command='):
            command = arg.split('=', 1)[1]
        elif arg.startswith('--branch='):
            branch = arg.split('=', 1)[1]
        elif arg.startswith('--arch='):
            arch = arg.split('=', 1)[1]
        else:
            raise ValueError('Unsupported Quest launch option: ' + arg)
        index += 1
    if index == len(args):
        raise ValueError('Specify an installed Flatpak application')
    app = args[index]
    if not re.fullmatch(r'[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+){2,}(?:/aarch64/[^/]+)?', app):
        raise ValueError('Invalid application identifier')
    if arch and arch != 'aarch64':
        raise ValueError('This desktop runtime supports ARM64 applications')
    if branch:
        if '/' in app or not re.fullmatch(r'[A-Za-z0-9._-]+', branch):
            raise ValueError('Invalid application branch')
        app += '/aarch64/' + branch
    return app, command, [x for x in args[index + 1:] if x not in ('@@', '@@u')]


def location(ref):
    path = subprocess.check_output([str(NATIVE), '--user', 'info', '--show-location', ref], text=True).strip()
    result = Path(path).resolve()
    allowed = (HOME / '.local/share/flatpak').resolve()
    if not result.is_relative_to(allowed) or not (result / 'files').is_dir():
        raise ValueError('The installed app/runtime is not available in the user installation')
    return result


def consent(app):
    path = HOME / '.config/stearlight/flatpak-compatibility.json'
    try:
        accepted = json.loads(path.read_text())
    except (OSError, ValueError):
        accepted = {}
    if accepted.get('desktop-compatibility-v1'):
        return
    message = ('Quest runs installed Flatpak apps using Stearlight compatibility mode. '
               'Apps share your desktop files and session; Flatpak\'s Linux sandbox '
               'permissions are not enforced. Run ' + app + '?')
    result = subprocess.run(['/opt/stearlight-desktop/usr/bin/kdialog', '--title',
                             'Desktop application compatibility', '--yesno', message,
                             '--yes-label', 'Run application', '--no-label', 'Cancel'])
    if result.returncode:
        raise ValueError('Application launch cancelled')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({'desktop-compatibility-v1': True}) + '\n')
    path.chmod(0o600)


def copy_configuration(source, destination):
    """Stat paths explicitly: PRoot's emulated hardlinks have a symlink d_type."""
    destination.mkdir(parents=True, exist_ok=True)
    for entry in source.iterdir():
        target = destination / entry.name
        if entry.is_symlink():
            if target.is_symlink() or target.is_file():
                target.unlink()
            target.symlink_to(os.readlink(entry))
        elif entry.is_dir():
            if target.is_symlink():
                target.unlink()
            copy_configuration(entry, target)
        elif entry.is_file():
            if target.is_symlink():
                target.unlink()
            shutil.copyfile(entry, target)
            target.chmod(entry.stat().st_mode & 0o777)


def prepare_root(root, runtime):
    root.mkdir(parents=True, exist_ok=True)
    for name in ['usr', 'app', 'dev', 'proc', 'sys', 'tmp', 'run', 'home', 'var', 'opt']:
        (root / name).mkdir(exist_ok=True)
    for name, target in [('bin', 'usr/bin'), ('sbin', 'usr/sbin'), ('lib', 'usr/lib'), ('lib64', 'usr/lib')]:
        path = root / name
        if not path.is_symlink() and not path.exists():
            path.symlink_to(target)
    etc = root / 'etc'
    etc.mkdir(exist_ok=True)
    source = runtime / 'files/etc'
    if source.is_dir():
        copy_configuration(source, etc)
    for name in ['resolv.conf', 'hosts', 'nsswitch.conf']:
        target = etc / name
        if target.is_symlink():
            target.unlink()
        if Path('/etc', name).is_file():
            shutil.copyfile(Path('/etc', name), target)
    for name in ['passwd', 'group', 'machine-id']:
        if (etc / name).is_symlink():
            (etc / name).unlink()
    (etc / 'passwd').write_text(f'stearlight:x:{os.getuid()}:{os.getgid()}:Stearlight:/home/stearlight:/bin/sh\n')
    (etc / 'group').write_text(f'stearlight:x:{os.getgid()}:\n')
    (etc / 'machine-id').write_text(uuid.uuid5(uuid.NAMESPACE_URL, str(root)).hex + '\n')
    host = root / 'run/host'
    host.mkdir(parents=True, exist_ok=True)
    (host / 'font-dirs.xml').write_text('<?xml version="1.0"?><fontconfig>'
        '<dir>/run/host/fonts</dir><dir>/run/host/frame-fonts</dir></fontconfig>\n')


def request(app, custom_command=None, arguments=()):
    installed = location(app)
    metadata = config(installed / 'metadata')
    runtime_ref = metadata.get('Application', 'runtime')
    runtime = location(runtime_ref)
    app_id = metadata.get('Application', 'name')
    if not re.fullmatch(r'[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+){2,}', app_id):
        raise ValueError('Invalid installed app metadata')
    command = shlex.split(custom_command or metadata.get('Application', 'command'))
    if not command:
        raise ValueError('This application has no launch command')
    root = HOME / '.local/share/stearlight/desktop-runs' / app_id
    prepare_root(root, runtime)
    data = HOME / '.var/app' / app_id
    for name in ['config', 'data', 'cache']:
        (data / name).mkdir(parents=True, exist_ok=True)
    environment = {key: os.environ[key] for key in [
        'DISPLAY', 'WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR', 'XDG_SESSION_TYPE',
        'DBUS_SESSION_BUS_ADDRESS', 'PULSE_SERVER', 'LANG', 'LC_ALL'] if key in os.environ}
    environment.update({'HOME': str(HOME), 'USER': 'stearlight', 'LOGNAME': 'stearlight',
        'PATH': '/app/bin:/usr/bin:/bin', 'TMPDIR': '/tmp', 'FLATPAK_ID': app_id,
        'XDG_CONFIG_HOME': str(data / 'config'), 'XDG_DATA_HOME': str(data / 'data'),
        'XDG_CACHE_HOME': str(data / 'cache'), 'XDG_DATA_DIRS': '/app/share:/usr/share',
        'XDG_CURRENT_DESKTOP': 'KDE', 'QT_QUICK_BACKEND': 'software',
        'QT_XCB_GL_INTEGRATION': 'none', 'QT_XCB_NO_MITSHM': '1',
        'GSK_RENDERER': 'cairo', 'GTK_USE_PORTAL': '0', 'GIO_USE_VFS': 'local',
        'LIBGL_ALWAYS_SOFTWARE': '1',
        'LD_LIBRARY_PATH': '/app/lib:/app/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu:/usr/lib'})
    bindings = [
        {'source': str(runtime / 'files'), 'target': '/usr'},
        {'source': str(installed / 'files'), 'target': '/app'}]
    for source, target in [('/usr/share/fonts', '/run/host/fonts'),
                           ('/opt/stearlight-desktop/usr/share/fonts', '/run/host/frame-fonts')]:
        if Path(source).is_dir():
            bindings.append({'source': source, 'target': target})
    return {'root': str(root), 'bindings': bindings,
        'environment': environment, 'command': command + list(arguments), 'cwd': str(HOME)}


def launch(value):
    if not (STATE / 'desktop-mode.active').is_file() and os.environ.get('STEARLIGHT_DESKTOP_APP_WINDOW') != '1':
        raise ValueError('Open Desktop before starting a desktop application')
    directory = STATE / 'desktop-app-requests'
    directory.mkdir(exist_ok=True)
    identifier = uuid.uuid4().hex
    target = directory / (identifier + '.request')
    temporary = target.with_suffix('.next')
    temporary.write_text(json.dumps(value))
    temporary.chmod(0o600)
    temporary.replace(target)
    result = directory / (identifier + '.result')
    deadline = time.monotonic() + 15
    running_since = None
    while time.monotonic() < deadline:
        if result.is_file():
            state = json.loads(result.read_text())
            if state['state'] == 'running':
                running_since = running_since or time.monotonic()
                if time.monotonic() - running_since >= 1:
                    return
            elif state['state'] == 'exited' and not state.get('code'):
                return
            else:
                raise ValueError(state.get('error') or 'The desktop application exited during startup; '
                                 'see /run/stearlight/quest/desktop-app-logs')
        time.sleep(.1)
    target.unlink(missing_ok=True)
    raise ValueError('The desktop application runtime did not respond')


def main(args):
    if '--system' in args:
        os.execv(str(NATIVE), [str(NATIVE)] + args)
    filtered = [x for x in args if x != '--user']
    if not filtered or '--version' in filtered or filtered[0] in ('--help', '-h'):
        os.execv(str(NATIVE), [str(NATIVE)] + filtered)
    if not filtered or filtered[0] != 'run':
        os.execv(str(NATIVE), [str(NATIVE), '--user'] + args)
    app, command, arguments = run_arguments(filtered[1:])
    consent(app.split('/')[0])
    launch(request(app, command, arguments))


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except (ValueError, OSError, KeyError, configparser.Error, subprocess.SubprocessError) as error:
        print('Desktop application: ' + str(error)[:1500], file=sys.stderr)
        sys.exit(1)
