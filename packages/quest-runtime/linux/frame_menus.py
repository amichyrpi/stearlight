"""Frame menu data from the installed desktop and the real Quest runtime."""
import configparser
import base64
from contextlib import closing
import json
import hashlib
from pathlib import Path
import re
import shlex
import shutil
import sqlite3
import struct
import subprocess
import time

APPLICATIONS = (Path('/opt/stearlight-desktop/usr/share/applications'),
                Path('/opt/stearlight-browser/usr/share/applications'),
                Path('/home/stearlight/.local/share/flatpak/exports/share/applications'))
RUNTIME = Path('/run/stearlight/quest')
WINDOW_BASE = 2100000000
ICON_ROOT = Path('/opt/quest/desktop-icons')
ACTIVITY_DB = Path('/home/stearlight/.local/share/kactivitymanagerd/resources/database')
PERF_DEFAULTS = {'filter': 1, 'scaler': 1, 'sharpness': 2, 'overlay': 0}
PERF_RANGES = {'filter': range(1, 4), 'scaler': range(1, 6),
               'sharpness': range(21), 'overlay': range(5)}


def app_number(app_id):
    return 100000000 + int(hashlib.sha256(app_id.encode()).hexdigest()[:8], 16) % 1800000000


def enable_performance_tab(source):
    # Scope the native QAM gate to its own module. Our Steam window is on
    # Xvfb while games/Plasma use Gamescope's OpenVR backend, so the global
    # IN_GAMESCOPE flag must retain its real value for other Steam features.
    pattern = re.compile(r'\(![\w$]+\|\|(?P<config>\(0,[\w$]+\.[\w$]+\)\(!0\))\.IN_GAMESCOPE\)(?=&&)')
    matches = [match for match in pattern.finditer(source)
               if '#QuickAccess_Tab_Perf_Title' in source[match.end():match.end() + 6000]]
    if len(matches) != 1:
        return source
    match = matches[0]
    replacement = match[0][:-1] + '||' + match['config'] + '.ON_FRAME)'
    return source[:match.start()] + replacement + source[match.end():]


def enable_window_icons(source):
    marker = 'globalThis.__stearlightFrameWindowIcons?.['
    if marker in source or '#VRDashboard_AddDesktopWindow' not in source:
        return source
    start = source.index('#VRDashboard_AddDesktopWindow')
    pattern = re.compile(r'children:(?P<jsx>\(0,[\w$]+\.jsx\))\((?P<component>[\w$]+),\{hwnd:(?P<window>[\w$]+)\.hwnd\}\)')
    matches = list(pattern.finditer(source, start, start + 1800))
    if len(matches) != 1:
        return source
    match = matches[0]
    icon = marker + match['window'] + '.window_id]'
    replacement = 'children:' + icon + '?' + match['jsx'] + '("img",{src:' + icon + '}):' + match[0].removeprefix('children:')
    return source[:match.start()] + replacement + source[match.end():]


def enable_playspace_icon(source):
    # Quest owns boundary setup, so the native action remains Recenter. Frame
    # uses the Playspace artwork for this dock control; both icons are shipped
    # by Valve. Change only the dashboard's enum renderer, not the action.
    pattern = re.compile(r'(case [\w$]+\.[\w$]+\.[\w$]+:return\(0,[\w$]+\.jsx\)\([\w$]+\.)'
                         r'Recenter(,\{\}\);case [\w$]+\.[\w$]+\.[\w$]+:return\(0,[\w$]+\.jsx\)\([\w$]+\.Playspace,\{\}\))')
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        return source
    match = matches[0]
    return source[:match.start()] + match[1] + 'Playspace' + match[2] + source[match.end():]


def applications():
    result = {}
    for directory in APPLICATIONS:
        for path in sorted(directory.glob('*.desktop')):
            parser = configparser.RawConfigParser(strict=False)
            try:
                parser.read(path)
                entry = parser['Desktop Entry']
                if (entry.get('Type') != 'Application' or
                    entry.get('NoDisplay', '').lower() == 'true' or
                    entry.get('Hidden', '').lower() == 'true' or not entry.get('Exec')):
                    continue
                command = []
                for word in shlex.split(entry['Exec']):
                    if word in ('%f', '%F', '%u', '%U', '%i'):
                        continue
                    command.append(word.replace('%c', entry.get('Name', path.stem)).replace('%k', str(path)).replace('%%', '%'))
                if not command or any('%' in word for word in command):
                    continue
                search = '/opt/stearlight-desktop/usr/bin:/opt/stearlight-browser/usr/bin:/usr/local/bin:/usr/bin:/bin'
                executable = shutil.which(command[0], path=search)
                if executable is None:
                    executable = shutil.which(command[0].rsplit('/', 1)[-1], path=search)
                if executable is None:
                    continue
                command[0] = executable
                if entry.get('Terminal', '').lower() == 'true':
                    terminal = shutil.which('konsole', path=search)
                    if terminal is None:
                        continue
                    command = [terminal, '-e', *command]
                # Launch actual applications, not settings submodules or Steam
                # itself. They remain accessible in Plasma's Settings/menu.
                if command[0].rsplit('/', 1)[-1] in ('steam', 'kcmshell6', 'kcm', 'steamos-nested-desktop'):
                    continue
                key = path.stem
                if key == 'chromium' and 'org.chromium.Chromium' in result:
                    continue
                if any(app['command'] == command for app in result.values()):
                    continue
                result[key] = {'id': key, 'name': entry.get('Name', key), 'command': command,
                               'icon': entry.get('Icon', '')}
            except (OSError, ValueError, KeyError, configparser.Error):
                continue
    return result


class FrameMenus:
    def __init__(self, manager, state, atomic):
        self.manager, self.state, self.atomic = manager, state, atomic
        self.config = state / 'frame-menus.json'
        try:
            self.settings = json.loads(self.config.read_text())
        except (OSError, ValueError):
            self.settings = {}
        self.settings = {'advanced': bool(self.settings.get('advanced', False)),
                         'profile': self.settings.get('profile', 'Performance')}
        if self.settings['profile'] not in ('Performance', 'Powersave'):
            self.settings['profile'] = 'Performance'
        try:
            saved = json.loads(self.config.read_text())
        except (OSError, ValueError):
            saved = {}
        self.settings.update({key: saved.get(key, default) if saved.get(key, default) in PERF_RANGES[key] else default
                              for key, default in PERF_DEFAULTS.items()})
        self.compositor_key, self.compositor_retry, self.displays = None, 0, []
        self.tagged_instances = set()
        self.overlay_key = None
        self.overlay_retry = 0
        self.catalog, self.catalog_at = {}, 0
        self.history_path = state / 'launched-apps.json'
        try:
            history = json.loads(self.history_path.read_text())
            self.history = {key: float(value) for key, value in history.items() if isinstance(key, str)}
        except (OSError, ValueError, TypeError, AttributeError):
            self.history = {}
        self.history_at = 0
        self.icons = {}
        self.focus_overlay, self.focus_token = '', 0
        self.windows, self.window_ids = {}, {}
        self.log_offset, self.script_ready, self.script_retry = 0, False, 0
        if RUNTIME.is_dir():
            self.publish_profile()

    def publish_profile(self):
        self.atomic(RUNTIME / 'performance-profile', self.settings['profile'] + '\n')

    def apps(self):
        if time.monotonic() >= self.catalog_at:
            self.catalog = applications()
            self.catalog_at = time.monotonic() + 30
        return self.catalog

    def remember(self, app_id, timestamp=None):
        if app_id not in self.apps():
            return
        timestamp = time.time() if timestamp is None else timestamp
        if timestamp <= self.history.get(app_id, 0):
            return
        self.history[app_id] = timestamp
        self.atomic(self.history_path, json.dumps(self.history))

    def launched_apps(self):
        # KDE already records launcher use. Import actual launches, never its
        # favorites or the installed catalogue, then retain our own history.
        if time.monotonic() >= self.history_at:
            self.history_at = time.monotonic() + 30
            if ACTIVITY_DB.is_file():
                try:
                    with closing(sqlite3.connect('file:' + str(ACTIVITY_DB) + '?mode=ro', uri=True, timeout=.1)) as db:
                        rows = db.execute("SELECT targettedResource, MAX(start) FROM ResourceEvent "
                                          "WHERE targettedResource LIKE 'applications:%' GROUP BY targettedResource")
                        for resource, stamp in rows:
                            self.remember(resource.removeprefix('applications:').removesuffix('.desktop'), stamp)
                except (OSError, sqlite3.Error):
                    pass
        apps = self.apps()
        return [apps[key] for key in sorted(self.history, key=self.history.get, reverse=True) if key in apps]

    def icon(self, app_id):
        if not re.fullmatch(r'[\w.-]+', app_id):
            app_id = 'desktop'
        if app_id not in self.icons:
            paths = [ICON_ROOT / (app_id + '.png')]
            app = self.apps().get(app_id)
            icon = app.get('icon', '') if app else ''
            if icon and re.fullmatch(r'[\w.-]+', icon):
                exports = Path('/home/stearlight/.local/share/flatpak/exports/share/icons/hicolor')
                paths.extend(exports / size / 'apps' / (icon + '.png')
                             for size in ('128x128', '64x64', '256x256', '48x48'))
            paths.append(ICON_ROOT / 'desktop.png')
            for path in paths:
                try:
                    data = path.read_bytes()
                    if data.startswith(b'\x89PNG\r\n\x1a\n') and len(data) <= 262144:
                        self.icons[app_id] = base64.b64encode(data).decode()
                        break
                except OSError:
                    pass
            else:
                self.icons[app_id] = ''
        return self.icons[app_id]

    def status(self):
        refresh = None
        try:
            data = (RUNTIME / 'tracking').read_bytes()
            if len(data) >= 24 and struct.unpack_from('<II', data) == (0x53515231, 2):
                rate = struct.unpack_from('<f', data, 20)[0]
                if 50 <= rate <= 150:
                    refresh = rate
        except OSError:
            pass
        return {**self.settings, 'refresh_hz': refresh,
                'focus_overlay': self.focus_overlay, 'focus_token': self.focus_token,
                'apps': [{'id': app['id'], 'name': app['name'], 'icon_base64': self.icon(app['id'])}
                         for app in self.launched_apps()],
                'windows': self.open_windows()}

    def open_windows(self):
        windows = [{'window_id': key, 'hwnd': 0, 'title': value['title'],
                    'icon_base64': self.icon(value.get('app_id', 'desktop'))}
                   for key, value in self.windows.items()] if self.manager.running('workspace') else []
        for app_id, app in self.apps().items():
            if self.manager.running('app-' + app_id):
                key = 'app:' + app_id
                if key not in self.window_ids:
                    self.window_ids[key] = WINDOW_BASE + len(self.window_ids)
                windows.append({'window_id': self.window_ids[key], 'hwnd': 0, 'title': app['name'],
                                'icon_base64': self.icon(app_id)})
        return windows

    def script(self, source, name, keep=False):
        path = self.state / (name + '.js')
        self.atomic(path, source)
        base = ['/usr/bin/dbus-send', '--session', '--type=method_call', '--print-reply', '--dest=org.kde.KWin']
        def call(*args):
            return subprocess.check_output(base + list(args), text=True, stderr=subprocess.DEVNULL, timeout=3)
        try:
            call('/Scripting', 'org.kde.kwin.Scripting.unloadScript', 'string:' + name)
        except subprocess.SubprocessError:
            pass
        reply = call('/Scripting', 'org.kde.kwin.Scripting.loadScript', 'string:' + str(path), 'string:' + name)
        match = re.search(r'int32\s+(\d+)', reply)
        if not match:
            raise ValueError('KWin did not load the window bridge')
        call('/Scripting/Script' + match[1], 'org.kde.kwin.Script.run')
        if not keep:
            call('/Scripting', 'org.kde.kwin.Scripting.unloadScript', 'string:' + name)

    def poll(self):
        self.poll_performance()
        if not self.manager.running('workspace'):
            self.windows.clear()
            self.script_ready, self.log_offset = False, 0
            return
        now = time.monotonic()
        environment = RUNTIME / 'desktop-shell.env'
        if not environment.exists():
            return
        if not self.script_ready and now >= self.script_retry:
            self.script_retry = now + 10
            try:
                self.script(Path('/opt/quest/desktop-windows.js').read_text(), 'stearlight-windows', keep=True)
                self.script_ready = True
            except (OSError, ValueError, subprocess.SubprocessError):
                pass
        try:
            with (self.state / 'workspace.log').open('rb') as log:
                if log.seek(0, 2) < self.log_offset:
                    self.log_offset = 0
                log.seek(self.log_offset)
                lines = log.read(131072).splitlines(keepends=True)
                for line in lines:
                    if not line.endswith(b'\n'):
                        break
                    self.log_offset += len(line)
                    marker = line.find(b'STEARLIGHT_WINDOWS ')
                    if marker < 0:
                        continue
                    value = json.loads(line[marker + len(b'STEARLIGHT_WINDOWS '):])
                    current = {}
                    for window in value[:128]:
                        uid = str(window['id'])
                        if uid not in self.window_ids:
                            self.window_ids[uid] = WINDOW_BASE + len(self.window_ids)
                        app_id = str(window.get('app_id', '')).removesuffix('.desktop')
                        current[self.window_ids[uid]] = {'id': uid, 'title': str(window['title'])[:512], 'app_id': app_id}
                        if app_id not in self.history:
                            self.remember(app_id)
                    self.windows = current
        except (OSError, ValueError, KeyError, TypeError):
            pass

    def poll_performance(self):
        now = time.monotonic()
        if now < self.compositor_retry:
            return
        self.compositor_retry = now + 2
        sockets = []
        for path in Path('/tmp/.X11-unix').glob('X*'):
            try:
                sockets.append((str(path), path.stat().st_ino, path.stat().st_mtime_ns))
            except OSError:
                pass
        managed = {}
        for name, instance in self.manager.instances.items():
            if self.manager.running(name):
                if name == 'workspace':
                    managed[instance] = 2000000000
                elif name.startswith('app-'):
                    managed[instance] = app_number(name[4:])
        key = (tuple(sorted(sockets)), tuple(sorted(managed.items())), *(self.settings[k] for k in PERF_DEFAULTS))
        if key != self.compositor_key or not managed.keys() <= self.tagged_instances:
            try:
                output = subprocess.check_output(['/usr/bin/python3', '/opt/quest/gamescope-perf.py',
                    json.dumps({**self.settings, 'windows': managed})], text=True, stderr=subprocess.DEVNULL, timeout=4)
                result = json.loads(output)
                self.displays = result['displays']
                self.tagged_instances = set(result['tagged'])
                self.compositor_key = key
            except (OSError, ValueError, subprocess.SubprocessError):
                return
        overlay = self.settings['overlay']
        desired = (self.displays[-1], overlay) if overlay and self.displays else None
        if desired == self.overlay_key and (desired is None or self.manager.running('performance-overlay')):
            return
        if desired == self.overlay_key and now < self.overlay_retry:
            return
        self.manager.stop('performance-overlay')
        self.overlay_key = desired
        self.overlay_retry = now + 60
        if desired:
            display, level = desired
            config = self.state / 'MangoHud.conf'
            # Steam's legacy protobuf enum is not MangoHud's preset order.
            preset = {4: 1, 1: 2, 2: 3, 3: 4}[level]
            self.atomic(config, f'preset={preset}\ngpu_stats=0\ncpu_stats=0\ncpu_temp=0\ngpu_temp=0\n')
            empty = self.state / 'unavailable-sensors'
            empty.mkdir(exist_ok=True)
            self.manager.start('performance-overlay',
                ['/opt/frame-gamescope/lib/ld-linux-aarch64.so.1', '--preload',
                 '/opt/quest/libquest_socket.so:/opt/quest/libquest_mango_sensors.so', '--library-path',
                 '/opt/frame-gamescope/lib:/usr/lib/aarch64-linux-gnu', '/opt/frame-gamescope/bin/mangoapp'],
                {'DISPLAY': display, 'WAYLAND_DISPLAY': '', 'MANGOHUD_CONFIGFILE': str(config),
                 'QUEST_MANGO_EMPTY_SENSORS': str(empty),
                 'VK_ICD_FILENAMES': '/opt/quest/turnip_icd.json', 'VK_DRIVER_FILES': '/opt/quest/turnip_icd.json',
                 'MESA_VK_WSI_DEBUG': 'sw',
                 'GALLIUM_DRIVER': 'zink', 'MESA_LOADER_DRIVER_OVERRIDE': 'zink',
                 'GLIBC_TUNABLES': 'glibc.pthread.rseq=0'})

    def call(self, operation, args):
        if operation == 'frame-settings':
            updated = dict(self.settings)
            if args.get('reset'):
                updated.update(PERF_DEFAULTS, advanced=False)
            for key, valid in PERF_RANGES.items():
                if key in args:
                    if type(args[key]) is not int or args[key] not in valid:
                        raise ValueError('Invalid performance setting: ' + key)
                    updated[key] = args[key]
            if 'advanced' in args:
                updated['advanced'] = bool(args['advanced'])
            if 'profile' in args:
                if args['profile'] not in ('Performance', 'Powersave'):
                    raise ValueError('Unknown performance profile')
                updated['profile'] = args['profile']
            self.settings = updated
            self.publish_profile()
            self.compositor_retry = 0
            self.atomic(self.config, json.dumps(self.settings))
        elif operation == 'launch-app':
            app_id = args['id']
            if app_id not in self.apps():
                raise ValueError('Application is not installed')
            if (not self.manager.running('app-' + app_id) and
                sum(name.startswith('app-') and self.manager.running(name) for name in self.manager.processes) >= 6):
                raise ValueError('Close a desktop application first')
            self.start_app(self.apps()[app_id])
        elif operation == 'focus-window':
            window_id = int(args['window_id'])
            for app_id, app in self.apps().items():
                if self.window_ids.get('app:' + app_id) == window_id and self.manager.running('app-' + app_id):
                    self.start_app(app)
                    return self.status()
            window = self.windows.get(window_id)
            if not window:
                raise ValueError('Window has closed')
            self.script('for (const w of workspace.stackingOrder) { if (String(w.internalId) === ' +
                        json.dumps(window['id']) + ') { w.minimized = false; workspace.activeWindow = w; break; } }',
                        'stearlight-focus')
            self.manager.focus_at = time.monotonic() + .3
        else:
            raise ValueError('Unknown Frame operation')
        return self.status()

    def start_app(self, app):
        environment = self.manager.ensure_display()
        number = app_number(app['id'])
        environment.update(SteamAppId=str(number), SteamGameId=str(number))
        self.manager.start('app-' + app['id'], ['/bin/bash', '/opt/quest/desktop-run-app.sh',
                          app['id'], app['name'], *app['command']], environment)
        self.remember(app['id'])
        self.focus_overlay = 'stearlight.desktop.app.' + str(number)
        self.focus_token = time.time_ns()
