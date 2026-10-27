#!/usr/bin/python3
"""Own optional Plasma and authenticated developer access inside the app."""
import argparse
import ctypes
import ctypes.util
import getpass
import hmac
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import secrets
import re
import signal
import socket
import select
import subprocess
import time
import urllib.request
import urllib.parse
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import steam_channels
from frame_menus import FrameMenus

RUNTIME = Path('/opt/stearlight-desktop')
STATE = Path('/home/stearlight/.local/state/stearlight-desktop')
CONFIG = STATE / 'settings.json'
TOKEN = STATE / 'api-token'
SHADOW = Path('/etc/shadow')
MODE = Path('/run/stearlight/quest/desktop-mode.active')
READY = Path('/run/stearlight/quest/desktop-mode.ready')
FOCUS = Path('/run/stearlight/quest/desktop-focus.request')
BOOST = Path('/run/stearlight/quest/performance-boost.request')
INTERACTION_BOOST = Path('/run/stearlight/quest/interaction-boost.request')
PORT = 27771


def atomic(path, value, mode=0o600):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.next')
    descriptor = os.open(temporary, os.O_CREAT | os.O_TRUNC | os.O_WRONLY, mode)
    with os.fdopen(descriptor, 'w') as stream:
        stream.write(value)
    os.chmod(temporary, mode)
    temporary.replace(path)


def password_hash(password, salt=None):
    candidate = ctypes.util.find_library('crypt')
    library = ctypes.CDLL(candidate) if candidate else ctypes.CDLL(None)
    if not hasattr(library, 'crypt'):
        library = ctypes.CDLL(str(RUNTIME / 'usr/lib/libcrypt.so.2'))
    library.crypt.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    library.crypt.restype = ctypes.c_char_p
    result = library.crypt(password.encode(), (salt or '$6$' + secrets.token_hex(8) + '$').encode())
    if not result or result.startswith(b'*'):
        raise ValueError('Password hashing failed')
    return result.decode()


class Manager:
    def __init__(self):
        STATE.mkdir(parents=True, exist_ok=True)
        os.chmod(STATE, 0o700)
        # The APK maps passwd to Android's app UID. Keep its primary group
        # aligned too, so XRDP's private session sockets retain real ownership.
        group = Path('/etc/group')
        if os.environ.get('STEARLIGHT_QUEST_RUNTIME') == '1' and group.is_file():
            rows = group.read_text().splitlines()
            rows = [':'.join([row.split(':')[0], row.split(':')[1], str(os.getgid()), row.split(':')[3]])
                    if row.startswith('stearlight:') else row for row in rows]
            atomic(group, '\n'.join(rows) + '\n', 0o644)
        try:
            self.settings = json.loads(CONFIG.read_text())
        except FileNotFoundError:
            self.settings = {'enabled': False, 'password_hash': '', 'default_session': 'plasma'}
        self.processes = {}
        self.logs = {}
        self.instances = {}
        self.error = ''
        self.remote = False
        self.remote_deadline = 0
        self.remote_pid = 0
        self.ssh_socket = None
        self.ssh_sequence = 0
        self.focus_at = 0
        self.focus_id = ''
        self.menus = FrameMenus(self, STATE, atomic)

    def save(self):
        atomic(CONFIG, json.dumps(self.settings) + '\n')

    def status(self):
        return {'enabled': self.settings['enabled'], 'has_password': bool(self.settings['password_hash']),
                'desktop_available': (RUNTIME / 'usr/bin/plasmashell').is_file(),
                'desktop_running': self.running('workspace') or self.remote, 'ssh_running': self.running('ssh'),
                'rdp_running': self.running('rdp'), 'ssh_port': 2222, 'rdp_port': 3389,
                'username': 'stearlight', 'default_session': self.settings['default_session'],
                'focus_id': self.focus_id, 'error': self.error, 'updates': steam_channels.status(),
                'frame': self.menus.status()}

    def running(self, name):
        if name == 'ssh':
            return self.ssh_socket is not None
        return name in self.processes and self.processes[name].poll() is None

    def ensure_display(self):
        if not self.running('desktop'):
            self.start('desktop', ['/bin/bash', '/opt/quest/desktop-headset.sh'])
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                log = (STATE / 'desktop.log').read_text(errors='replace')
                wayland = re.search(r"Running compositor on wayland display '(gamescope-\d+)'", log)
                display = re.search(r'Starting Xwayland on (:\d+)', log)
                if (wayland and display and Path('/tmp/.X11-unix/X' + display[1][1:]).is_socket()
                    and (Path(os.environ.get('XDG_RUNTIME_DIR', '/tmp/stearlight-runtime')) / wayland[1]).is_socket()):
                    return {'DISPLAY': display[1], 'WAYLAND_DISPLAY': wayland[1]}
            except OSError:
                pass
            if not self.running('desktop'):
                break
            time.sleep(.1)
        raise ValueError('The desktop display did not start')

    def start(self, name, command, environment=None, connection=None):
        if self.running(name):
            return
        log = (STATE / (name + '.log')).open('w')
        env = dict(os.environ)
        env.pop('LD_LIBRARY_PATH', None)
        env.pop('LD_PRELOAD', None)
        if environment:
            env.update(environment)
        instance = env.get('STEARLIGHT_DESKTOP_INSTANCE') or secrets.token_hex(16)
        env['STEARLIGHT_DESKTOP_INSTANCE'] = instance
        self.instances[name] = instance
        self.processes[name] = subprocess.Popen(command, env=env, stdin=connection,
            stdout=connection if connection else log, stderr=log if connection else subprocess.STDOUT,
            start_new_session=True)
        self.logs[name] = log

    def stop(self, name):
        if name == 'ssh':
            if self.ssh_socket:
                self.ssh_socket.close()
                self.ssh_socket = None
            for session in list(self.processes):
                if session.startswith('ssh-session-'):
                    self.stop(session)
            return
        instance = self.instances.pop(name, None)
        process = self.processes.pop(name, None)
        if process and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        # KDE's D-Bus activation helpers can detach from the original group.
        # Clean only processes carrying this manager's inherited instance ID.
        if instance:
            marker = ('STEARLIGHT_DESKTOP_INSTANCE=' + instance).encode()
            owned = []
            for path in Path('/proc').iterdir():
                if not path.name.isdigit():
                    continue
                try:
                    uid = next(row.split()[1] for row in (path / 'status').read_text().splitlines() if row.startswith('Uid:'))
                    if int(uid) == os.getuid() and marker in (path / 'environ').read_bytes().split(b'\0'):
                        pid = int(path.name)
                        owned.append(pid)
                        os.kill(pid, signal.SIGTERM)
                except (OSError, ProcessLookupError, StopIteration):
                    pass
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and any(Path('/proc', str(pid)).exists() for pid in owned):
                time.sleep(0.05)
            for pid in owned:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        log = self.logs.pop(name, None)
        if log:
            log.close()

    def clear_headset_activation(self):
        self.focus_at = 0
        self.focus_id = ''
        FOCUS.unlink(missing_ok=True)
        Path('/run/stearlight/quest/desktop-shell.env').unlink(missing_ok=True)
        for service in Path('/run/stearlight/quest/desktop-services').glob('*.service'):
            service.unlink()

    def poll(self):
        if (self.focus_at and time.monotonic() >= self.focus_at and
                Path('/tmp/stearlight-runtime/stearlight-plasma/wayland-0').is_socket()):
            self.focus_at = 0
            if self.running('desktop') and MODE.exists():
                atomic(FOCUS, 'desktop\n')
                self.focus_id = str(time.time_ns())
        if self.running('workspace') and not MODE.exists():
            self.call('stop', {})
        for name in list(self.processes):
            if name.startswith('app-') and not self.running(name):
                self.stop(name)
            if name.startswith('ssh-session-') and not self.running(name):
                self.stop(name)
        if (self.running('desktop') and not self.running('workspace') and not MODE.exists()
            and not any(name.startswith('app-') and self.running(name) for name in self.processes)):
            self.stop('desktop')
        display_closed = 'desktop' in self.processes and not self.running('desktop')
        workspace_closed = 'workspace' in self.processes and not self.running('workspace')
        if display_closed or workspace_closed:
            self.clear_headset_activation()
            self.stop('workspace')
            # An independent app still owns the shared display when Plasma
            # exits. Only a failed display requires closing its clients.
            if display_closed:
                for name in list(self.processes):
                    if name.startswith('app-'):
                        self.stop(name)
            if display_closed or not any(name.startswith('app-') and self.running(name) for name in self.processes):
                self.stop('desktop')
            self.reconcile()
            MODE.unlink(missing_ok=True)
            self.error = 'The desktop closed; Steam is restarting.'
        if self.remote and self.remote_deadline and time.monotonic() > self.remote_deadline:
            self.remote = False
            self.remote_deadline = 0
            MODE.unlink(missing_ok=True)
            self.error = 'The remote desktop did not start; Steam is restarting.'
        if self.remote and self.remote_pid:
            try:
                alive = Path('/proc', str(self.remote_pid), 'stat').read_text().rsplit(')', 1)[1].split()[0] != 'Z'
            except OSError:
                alive = False
            if not alive:
                self.call('remote-stop', {})

    def set_password(self, current, new):
        if not isinstance(current, str):
            raise ValueError('The current password must be text')
        previous = self.settings['password_hash']
        if previous and not hmac.compare_digest(password_hash(current, previous), previous):
            raise ValueError('The current password is incorrect')
        if not isinstance(new, str) or len(new) < 8 or len(new) > 1024 or '\x00' in new:
            raise ValueError('Use a password of at least eight characters')
        value = password_hash(new)
        self.settings['password_hash'] = value
        self.save()
        shadow = SHADOW
        rows = shadow.read_text().splitlines() if shadow.exists() else []
        rows = [row for row in rows if not row.startswith('stearlight:')]
        rows.append('stearlight:' + value + ':' + str(int(time.time() / 86400)) + ':0:99999:7:::')
        atomic(shadow, '\n'.join(rows) + '\n')
        self.reconcile()

    def prepare_access(self):
        private = RUNTIME / 'usr/bin'
        hostkey = STATE / 'ssh-host-key'
        if not hostkey.exists():
            subprocess.run([str(private / 'ssh-keygen'), '-q', '-t', 'ed25519', '-N', '', '-f', str(hostkey)], check=True)
        ssh = f'''Port 2222
ListenAddress 0.0.0.0
HostKey {hostkey}
PidFile {STATE}/ssh.pid
UsePAM no
PermitRootLogin no
PasswordAuthentication yes
KbdInteractiveAuthentication no
PubkeyAuthentication yes
PermitEmptyPasswords no
AllowUsers stearlight
AuthorizedKeysFile .ssh/authorized_keys
StrictModes yes
UseDNS no
X11Forwarding no
SetEnv PATH=/opt/stearlight-desktop/usr/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
Subsystem sftp internal-sftp
'''
        atomic(STATE / 'sshd_config', ssh)
        if self.ssh_socket is None:
            listener = socket.socket()
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                listener.bind(('0.0.0.0', 2222))
                listener.listen(8)
                listener.setblocking(False)
            except OSError:
                listener.close()
                raise
            self.ssh_socket = listener
        if self.running('desktop'):
            return
        # XRDP uses its normal PAM login and Xorg backend. No services listen
        # until the user has enabled developer mode and set a password.
        for directory in ['/run/xrdp', '/run/xrdp/sockdir', '/etc/xrdp', '/etc/pam.d', '/usr/lib/security']:
            Path(directory).mkdir(parents=True, exist_ok=True)
        rsa = STATE / 'rdp-rsa-keys.ini'
        if not rsa.exists():
            subprocess.run([str(private / 'xrdp-keygen'), 'xrdp', str(rsa)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.chmod(rsa, 0o600)
        rsa_link = Path('/etc/xrdp/rsakeys.ini')
        if not rsa_link.exists():
            rsa_link.symlink_to(rsa)
        for module in ['pam_unix.so', 'pam_permit.so']:
            target = Path('/usr/lib/security') / module
            if not target.exists():
                target.symlink_to(RUNTIME / 'usr/lib/security' / module)
        atomic(Path('/etc/pam.d/xrdp-sesman'),
               'auth required /opt/quest/desktop-pam.so\n'
               f'account required {RUNTIME}/usr/lib/security/pam_permit.so\n'
               f'session required {RUNTIME}/usr/lib/security/pam_permit.so\n', 0o644)
        certificate, key = STATE / 'rdp-cert.pem', STATE / 'rdp-key.pem'
        if not certificate.exists():
            subprocess.run([str(private / 'openssl'), 'req', '-new', '-x509', '-nodes', '-days', '3650',
                            '-newkey', 'rsa:2048', '-subj', '/CN=Stearlight Desktop', '-keyout', str(key),
                            '-out', str(certificate)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.chmod(key, 0o600)
        atomic(STATE / 'xrdp.ini', f'''[Globals]
port=tcp://:3389
security_layer=tls
certificate={certificate}
key_file={key}
ssl_protocols=TLSv1.2,TLSv1.3
bitmap_cache=true
bitmap_compression=true
max_bpp=32
use_fastpath=both
fork=true
autorun=
allow_channels=true

[Logging]
LogFile={STATE}/xrdp-protocol.log
LogLevel=WARNING
EnableSyslog=false

[Channels]
rdpdr=true
rdpsnd=true
cliprdr=true
drdynvc=true

[Xorg]
name=Plasma
lib=libxup.so
username=ask
password=ask
port=-1
code=20
''')
        sesman_instance = self.instances.get('sesman') or secrets.token_hex(16)
        atomic(STATE / 'sesman.ini', f'''[Globals]
EnableUserWindowManager=false
DefaultWindowManager=/opt/quest/desktop-remote.sh
ReconnectScript=/bin/true

[Security]
AllowRootLogin=false
MaxLoginRetry=3
AlwaysGroupCheck=false
SessionSockdirGroup=stearlight

[Sessions]
MaxSessions=1
KillDisconnected=true
DisconnectedTimeLimit=5
IdleTimeLimit=0

[Logging]
LogFile={STATE}/sesman-protocol.log
LogLevel=WARNING
EnableSyslog=false

[SessionVariables]
STEARLIGHT_DESKTOP_INSTANCE={sesman_instance}

[Xorg]
param={RUNTIME}/usr/lib/Xorg
param=-config
param={STATE}/xorg.conf
param=-modulepath
param={RUNTIME}/usr/lib/xorg/modules
param=-noreset
param=-nolisten
param=tcp
param=-logfile
param={STATE}/xorg.%s.log
''')
        xorg = (RUNTIME / 'etc/X11/xrdp/xorg.conf').read_text()
        xorg = '\n'.join(row for row in xorg.splitlines() if 'Option "DRMDevice"' not in row)
        xorg = xorg.replace('Option "DRI3" "1"', 'Option "DRI3" "0"')
        atomic(STATE / 'xorg.conf', xorg + '\n', 0o644)
        self.start('sesman', [str(private / 'xrdp-sesman'), '--nodaemon', '--config', str(STATE / 'sesman.ini')],
                   {'LD_PRELOAD': '/opt/quest/libdesktop_access.so', 'STEARLIGHT_DESKTOP_ACCESS': '1',
                    'STEARLIGHT_DESKTOP_PAM_RESERVE': '1',
                    'STEARLIGHT_DESKTOP_INSTANCE': sesman_instance,
                    'PATH': '/opt/stearlight-desktop/usr/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin'})
        self.start('rdp', [str(private / 'xrdp'), '--nodaemon', '--config', str(STATE / 'xrdp.ini')])

    def reconcile(self):
        if self.settings['enabled'] and self.settings['password_hash']:
            self.prepare_access()
        else:
            for name in ['rdp', 'sesman', 'ssh']:
                self.stop(name)
            if self.remote:
                self.remote = False
                MODE.unlink(missing_ok=True)

    def accept_ssh(self):
        # OpenSSH's inetd mode uses the accepted socket directly. The existing
        # manager owns the listener instead of another permanent sshd process.
        try:
            connection, _ = self.ssh_socket.accept()
        except BlockingIOError:
            return
        with connection:
            connection.setblocking(True)
            active = sum(name.startswith('ssh-session-') for name in self.processes)
            if active >= 4:
                return
            self.ssh_sequence += 1
            self.start('ssh-session-' + str(self.ssh_sequence),
                [str(RUNTIME / 'usr/bin/sshd'), '-i', '-e', '-f', str(STATE / 'sshd_config')],
                connection=connection)

    def call(self, operation, args):
        if operation == 'status':
            return self.status()
        if operation in ('frame-settings', 'launch-app', 'focus-window'):
            return self.menus.call(operation, args)
        if operation == 'performance-boost':
            atomic(INTERACTION_BOOST, f'{time.monotonic() + 2:.6f}\n')
            return {}
        if operation == 'client-channel':
            steam_channels.select_client(args['channel'])
            return steam_channels.status()
        if operation == 'password':
            self.set_password(args.get('current_password', ''), args.get('new_password', ''))
        elif operation == 'developer':
            self.settings['enabled'] = bool(args['enabled'])
            self.save()
            self.reconcile()
        elif operation in ('start', 'remote-start', 'remote-reserve'):
            if not self.status()['desktop_available']:
                raise ValueError('Plasma desktop is not installed')
            if operation == 'start' and self.remote:
                raise ValueError('Close the Remote Desktop session before opening the headset desktop')
            atomic(BOOST, f'{time.monotonic() + 15:.6f}\n')
            if operation == 'start':
                # Leave room for applications within Android's child-process
                # budget. SSH remains usable; RDP resumes in Gaming Mode.
                self.stop('rdp')
                self.stop('sesman')
            atomic(MODE, 'remote\n' if operation in ('remote-start', 'remote-reserve') else 'desktop\n')
            deadline = time.monotonic() + 15
            while not READY.exists() and time.monotonic() < deadline:
                time.sleep(0.1)
            if not READY.exists():
                MODE.unlink(missing_ok=True)
                raise ValueError('Steam did not release the desktop process budget')
            if operation in ('remote-start', 'remote-reserve'):
                self.clear_headset_activation()
                self.stop('workspace')
                self.stop('desktop')
                self.remote = True
                self.remote_deadline = time.monotonic() + 30 if operation == 'remote-reserve' else 0
                self.remote_pid = 0
                if operation == 'remote-start':
                    for path in Path('/proc').iterdir():
                        if not path.name.isdigit():
                            continue
                        try:
                            # Sesman sanitizes the display-server environment.
                            # Its private configuration path identifies ours.
                            args = (path / 'cmdline').read_bytes().split(b'\0')
                            uid = next(row.split()[1] for row in (path / 'status').read_text().splitlines() if row.startswith('Uid:'))
                            if (path / 'comm').read_text().strip() == 'Xorg' and int(uid) == os.getuid() and str(STATE / 'xorg.conf').encode() in args:
                                self.remote_pid = int(path.name)
                                break
                        except (OSError, StopIteration):
                            pass
            else:
                environment = self.ensure_display()
                environment.update(SteamAppId='2000000000', SteamGameId='2000000000')
                self.start('workspace', ['/bin/bash', '/opt/quest/desktop-session.sh', 'headset'],
                           environment)
                # Selecting Desktop must also raise an existing workspace.
                # Let Steam finish closing its launcher menu before raising
                # the overlay; its completion otherwise steals focus back.
                self.focus_at = time.monotonic() + 2
            self.error = ''
        elif operation in ('stop', 'remote-stop'):
            self.clear_headset_activation()
            self.stop('workspace')
            if not any(name.startswith('app-') and self.running(name) for name in self.processes):
                self.stop('desktop')
            if self.remote:
                self.remote = False
                # Stop detached KDE workers before the headset runtime starts.
                self.stop('sesman')
            self.reconcile()
            MODE.unlink(missing_ok=True)
        elif operation == 'session':
            if args.get('session_name') != 'plasma':
                raise ValueError('Unsupported desktop session')
        else:
            raise ValueError('Unsupported desktop operation')
        return self.status()

    def close(self):
        self.clear_headset_activation()
        self.stop('ssh')
        for name in list(self.processes):
            self.stop(name)
        MODE.unlink(missing_ok=True)


def serve():
    manager = Manager()
    if not TOKEN.exists():
        atomic(TOKEN, secrets.token_hex(32))
    token = TOKEN.read_text().strip()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def origin(self):
            origin = self.headers.get('Origin', '')
            if origin:
                parsed = urllib.parse.urlsplit(origin)
                if parsed.scheme not in ('http', 'https') or parsed.hostname not in ('steamloopback.host', '127.0.0.1'):
                    return None
            return origin

        def do_OPTIONS(self):
            origin = self.origin()
            if origin is None:
                self.send_error(403)
                return
            self.send_response(204)
            self.send_header('Access-Control-Allow-Origin', origin)
            self.send_header('Access-Control-Allow-Headers', 'Authorization, Content-Type')
            self.send_header('Access-Control-Allow-Methods', 'POST')
            self.end_headers()

        def do_POST(self):
            origin = self.origin()
            if origin is None:
                self.send_error(403)
                return
            if not hmac.compare_digest(self.headers.get('Authorization', ''), 'Bearer ' + token):
                self.send_error(403)
                return
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 8192:
                self.send_error(400)
                return
            try:
                args = json.loads(self.rfile.read(length))
                value = manager.call(args.pop('operation'), args)
                payload = {'ok': True, 'value': value}
            except (ValueError, KeyError, OSError, subprocess.SubprocessError):
                payload = {'ok': False, 'error': 'The desktop operation failed; check the password and desktop logs.'}
            data = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            if origin:
                self.send_header('Access-Control-Allow-Origin', origin)
            self.end_headers()
            self.wfile.write(data)

    server = HTTPServer(('127.0.0.1', PORT), Handler)
    server.timeout = 1
    stopped = False

    def stop(*args):
        nonlocal stopped
        stopped = True

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        manager.reconcile()
        while not stopped:
            readable, _, _ = select.select([server.socket] + ([manager.ssh_socket] if manager.ssh_socket else []), [], [], 1)
            if server.socket in readable:
                server.handle_request()
            if manager.ssh_socket and manager.ssh_socket in readable:
                manager.accept_ssh()
            manager.poll()
            try:
                manager.menus.poll()
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                print(f'Frame menu polling failed: {error}', file=sys.stderr)
    finally:
        manager.close()
        server.server_close()


def client(operation, **args):
    request = urllib.request.Request(f'http://127.0.0.1:{PORT}/',
        data=json.dumps({'operation': operation, **args}).encode(),
        headers={'Authorization': 'Bearer ' + TOKEN.read_text().strip(), 'Content-Type': 'application/json'})
    result = json.load(urllib.request.urlopen(request, timeout=30))
    if not result['ok']:
        raise RuntimeError(result['error'])
    return result['value']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['serve', 'start', 'stop', 'status', 'passwd', 'enable', 'disable', 'remote-start', 'remote-stop'])
    args = parser.parse_args()
    if args.operation == 'serve':
        serve()
    elif args.operation == 'passwd':
        current = getpass.getpass('Current desktop password (blank for first setup): ')
        new = getpass.getpass('New desktop password: ')
        if new != getpass.getpass('Confirm new password: '):
            parser.error('Passwords do not match')
        client('password', current_password=current, new_password=new)
        print('Desktop password updated')
    else:
        operation = 'developer' if args.operation in ('enable', 'disable') else args.operation
        print(json.dumps(client(operation, enabled=args.operation == 'enable'), indent=2))
