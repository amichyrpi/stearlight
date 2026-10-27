"""Exercise real desktop entries, stale-window handling and scoped UI gating."""
import importlib.util
import json
import base64
from contextlib import closing
import sqlite3
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from unittest.mock import Mock

path = Path(__file__).resolve().parents[1] / 'linux/frame_menus.py'
spec = importlib.util.spec_from_file_location('menus', path)
menus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(menus)


class FrameMenusTests(unittest.TestCase):
    def test_desktop_profile_uses_default_avatar_without_replacing_user_customizations(self):
        spec = importlib.util.spec_from_file_location('desktop_defaults', path.with_name('desktop-defaults.py'))
        defaults = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(defaults)
        home, settings, desktop = self.root / 'home', self.root / 'config', self.root / 'desktop'
        home.mkdir();settings.mkdir()
        icon = desktop / 'usr/share/pixmaps/steamos.png'
        icon.parent.mkdir(parents=True);icon.write_bytes(b'native Steam icon')
        header = desktop / 'usr/share/plasma/plasmoids/org.kde.plasma.kickoff/contents/ui/Header.qml'
        header.parent.mkdir(parents=True)
        header.write_text('visible: KConfig.KAuthorized.authorizeControlModule("kcm_users")\nsource: kuser.faceIconUrl')
        defaults.configure(home, settings, desktop)
        self.assertEqual((home / '.face.icon').read_bytes(), icon.read_bytes())
        self.assertIn('visible: true', header.read_text())
        self.assertIn('enabled: KConfig.KAuthorized.authorizeControlModule("kcm_users")', header.read_text())
        self.assertIn('source: kuser.faceIconUrl', header.read_text())
        self.assertIn('contentItem: Image', header.read_text())
        (home / '.face.icon').write_bytes(b'user avatar')
        defaults.configure(home, settings, desktop)
        self.assertEqual((home / '.face.icon').read_bytes(), b'user avatar')
        self.assertEqual(header.read_text().count('// Stearlight profile image'), 1)
        legacy = desktop / 'usr/share/steamos/steam_icon.png'
        legacy.parent.mkdir(parents=True);legacy.write_bytes(b'monochrome overlay icon')
        (home / '.face.icon').write_bytes(legacy.read_bytes())
        defaults.configure(home, settings, desktop)
        self.assertEqual((home / '.face.icon').read_bytes(), icon.read_bytes())

    def test_returning_from_kde_keeps_independent_app_and_shared_display_alive(self):
        spec = importlib.util.spec_from_file_location('desktop_manager_stop', path.with_name('desktop-manager.py'))
        desktop = importlib.util.module_from_spec(spec);spec.loader.exec_module(desktop)
        manager = desktop.Manager.__new__(desktop.Manager)
        manager.remote = False
        manager.processes = dict(desktop=object(), workspace=object(), **{'app-terminal': object()})
        manager.running = lambda name: name in manager.processes
        manager.clear_headset_activation, manager.reconcile, manager.status = Mock(), Mock(), Mock(return_value={})
        stopped = []
        def stop(name):
            stopped.append(name);manager.processes.pop(name, None)
        manager.stop = stop
        with patch.object(desktop, 'MODE', self.root / 'mode'):
            desktop.Manager.call(manager, 'stop', {})
        self.assertEqual(stopped, ['workspace'])
        self.assertIn('app-terminal', manager.processes)
        self.assertIn('desktop', manager.processes)

    def test_plasma_failure_does_not_close_independent_app_display(self):
        spec = importlib.util.spec_from_file_location('desktop_manager_poll', path.with_name('desktop-manager.py'))
        desktop = importlib.util.module_from_spec(spec);spec.loader.exec_module(desktop)
        manager = desktop.Manager.__new__(desktop.Manager)
        manager.remote, manager.focus_at = False, 0
        manager.processes = dict(desktop=object(), workspace=object(), **{'app-terminal': object()})
        manager.running = lambda name: name in manager.processes and name != 'workspace'
        manager.clear_headset_activation, manager.reconcile = Mock(), Mock()
        stopped = []
        def stop(name):
            stopped.append(name);manager.processes.pop(name, None)
        manager.stop = stop
        mode = self.root / 'mode';mode.write_text('desktop')
        with patch.object(desktop, 'MODE', mode):
            manager.poll()
        self.assertEqual(stopped, ['workspace'])
        self.assertIn('app-terminal', manager.processes)
        self.assertIn('desktop', manager.processes)
        self.assertFalse(mode.exists())

    def test_window_identity_retries_until_native_window_exists_then_stops(self):
        self.manager.instances = {'workspace': 'workspace-instance', 'app-terminal': 'app-instance', 'desktop': 'backend-instance'}
        self.manager.running = lambda name: True
        self.manager.stop = Mock()
        with patch.object(menus.subprocess, 'check_output', side_effect=[
            '{"displays":[":1"],"tagged":[]}',
            '{"displays":[":1"],"tagged":["workspace-instance","app-instance"]}'
        ]) as worker:
            self.frame.poll_performance()
            settings = json.loads(worker.call_args.args[0][-1])
            self.assertEqual(settings['windows'], {'workspace-instance': 2000000000,
                                                   'app-instance': menus.app_number('terminal')})
            self.frame.compositor_retry = 0
            self.frame.poll_performance()
            self.frame.compositor_retry = 0
            self.frame.poll_performance()
            self.assertEqual(worker.call_count, 2)

    def test_menu_failures_do_not_close_desktop_ssh_or_rdp(self):
        spec = importlib.util.spec_from_file_location('desktop_manager', path.with_name('desktop-manager.py'))
        desktop = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(desktop)
        for error in (OSError('disk full'), ValueError('invalid settings'), menus.subprocess.SubprocessError('launch failed')):
            with self.subTest(error=error), tempfile.TemporaryDirectory() as folder:
                token = Path(folder) / 'token'
                token.write_text('test-token')
                manager, server = Mock(ssh_socket=None), Mock()
                handlers = {}
                def register(number, handler):
                    handlers[number] = handler
                def poll():
                    if manager.menus.poll.call_count == 1:
                        # A second iteration proves a menu failure did not exit
                        # the service or close its SSH/RDP/desktop processes.
                        manager.close.assert_not_called()
                        handlers[desktop.signal.SIGTERM]()
                manager.poll.side_effect = poll
                manager.menus.poll.side_effect = [error, None]
                with patch.object(desktop, 'TOKEN', token), \
                     patch.object(desktop, 'Manager', return_value=manager), \
                     patch.object(desktop, 'HTTPServer', return_value=server), \
                     patch.object(desktop.signal, 'signal', side_effect=register), \
                     patch.object(desktop.select, 'select', return_value=([], [], [])), \
                     patch.object(desktop.sys, 'stderr'):
                    desktop.serve()
                self.assertEqual(manager.menus.poll.call_count, 2)
                manager.close.assert_called_once()
                server.server_close.assert_called_once()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.apps = self.root / 'applications'
        self.apps.mkdir()
        self.pins = patch.multiple(menus, APPLICATIONS=(self.apps,), RUNTIME=self.root,
                                   ACTIVITY_DB=self.root / 'activities.sqlite', ICON_ROOT=self.root / 'icons')
        self.pins.start()
        self.addCleanup(self.pins.stop)
        self.manager = type('Manager', (), {'processes': {}, 'instances': {}, 'running': lambda self, name: False})()
        self.manager.ensure_display = lambda: {'DISPLAY': ':1', 'WAYLAND_DISPLAY': 'gamescope-0'}
        def atomic(path, text):
            path.write_text(text)
        self.frame = menus.FrameMenus(self.manager, self.root, atomic)

    def application(self, name='terminal'):
        (self.apps / (name + '.desktop')).write_text(
            f'[Desktop Entry]\nType=Application\nName={name}\nExec={sys.executable} "two words"\n')

    def test_only_used_apps_are_listed_and_history_survives_restart(self):
        self.application(); self.application('unused')
        self.assertEqual(self.frame.status()['apps'], [])
        self.manager.start, self.manager.call = Mock(), Mock()
        self.frame.call('launch-app', {'id': 'terminal'})
        self.manager.call.assert_not_called()  # No full desktop launch.
        self.assertEqual(self.manager.start.call_args.args[0], 'app-terminal')
        self.assertEqual(self.manager.start.call_args.args[1][-2:], [sys.executable, 'two words'])
        self.assertEqual([app['id'] for app in self.frame.status()['apps']], ['terminal'])
        reopened = menus.FrameMenus(self.manager, self.root, lambda p, s: p.write_text(s))
        self.assertEqual([app['id'] for app in reopened.status()['apps']], ['terminal'])

    def test_kde_actual_launch_history_is_imported_without_favorites_or_uninstalled_apps(self):
        self.application(); self.application('unused')
        with closing(sqlite3.connect(menus.ACTIVITY_DB)) as database, database:
            database.execute('CREATE TABLE ResourceEvent(targettedResource TEXT, start INTEGER)')
            database.executemany('INSERT INTO ResourceEvent VALUES(?,?)', [
                ('applications:terminal.desktop', 10), ('applications:not-installed.desktop', 20), ('file:///a/document', 30)])
        self.assertEqual([app['id'] for app in self.frame.status()['apps']], ['terminal'])

    def test_app_icons_and_separate_running_window_can_be_selected_without_kde(self):
        self.application()
        menus.ICON_ROOT.mkdir()
        png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a4XcAAAAASUVORK5CYII=')
        (menus.ICON_ROOT / 'desktop.png').write_bytes(png)
        self.manager.start = Mock()
        self.manager.running = lambda name: name == 'app-terminal'
        self.frame.remember('terminal')
        state = self.frame.status()
        self.assertEqual(base64.b64decode(state['apps'][0]['icon_base64']), png)
        window = state['windows'][0]
        self.assertEqual(window['title'], 'terminal')
        with patch.object(self.frame, 'script') as script:
            self.frame.call('focus-window', {'window_id': window['window_id']})
            script.assert_not_called()
        self.assertTrue(self.frame.focus_overlay.startswith('stearlight.desktop.app.'))
        self.assertEqual(base64.b64decode(window['icon_base64']), png)

    def test_window_icon_adapter_preserves_native_capture_and_is_scoped(self):
        source = 'other({hwnd:a.hwnd});"#VRDashboard_AddDesktopWindow";children:(0,e.jsx)(lr,{hwnd:zt.hwnd});native()'
        actual = menus.enable_window_icons(source)
        self.assertIn('__stearlightFrameWindowIcons?.[zt.window_id]', actual)
        self.assertIn(':(0,e.jsx)(lr,{hwnd:zt.hwnd})', actual)
        self.assertTrue(actual.startswith('other({hwnd:a.hwnd});'))
        self.assertEqual(menus.enable_window_icons(actual), actual)
        self.assertEqual(menus.enable_window_icons('unknown client'), 'unknown client')

    def test_installed_catalog_and_exec_argument_boundaries(self):
        (self.apps / 'terminal.desktop').write_text(
            f'[Desktop Entry]\nType=Application\nName=Terminal\nExec={sys.executable} "a file" %U\n')
        (self.apps / 'hidden.desktop').write_text(
            f'[Desktop Entry]\nType=Application\nName=Hidden\nNoDisplay=true\nExec={sys.executable}\n')
        (self.apps / 'missing.desktop').write_text('[Desktop Entry]\nType=Application\nExec=not-installed-quest-test\n')
        catalog = self.frame.apps()
        self.assertEqual(list(catalog), ['terminal'])
        self.assertEqual(catalog['terminal']['command'], [sys.executable, 'a file'])
        with self.assertRaises(ValueError):
            self.frame.call('launch-app', {'id': '../missing'})

    def test_settings_round_trip_and_unsupported_profile_refused(self):
        self.frame.call('frame-settings', {'advanced': True, 'profile': 'Powersave',
                                          'filter': 3, 'scaler': 5, 'overlay': 4, 'sharpness': 8})
        again = menus.FrameMenus(self.manager, self.root, lambda p, s: p.write_text(s))
        self.assertTrue(again.status()['advanced'])
        self.assertEqual(again.status()['profile'], 'Powersave')
        self.assertEqual((self.root / 'performance-profile').read_text(), 'Powersave\n')
        self.assertEqual([again.status()[k] for k in ('filter', 'scaler', 'overlay', 'sharpness')], [3, 5, 4, 8])
        with self.assertRaises(ValueError):
            again.call('frame-settings', {'profile': 'overclock'})
        before = dict(again.settings)
        with self.assertRaises(ValueError):
            again.call('frame-settings', {'filter': 2, 'overlay': 100})
        self.assertEqual(again.settings, before)
        again.call('frame-settings', {'reset': True})
        self.assertEqual({k: again.settings[k] for k in menus.PERF_DEFAULTS}, menus.PERF_DEFAULTS)

    def test_overlay_stops_when_disabled_and_does_not_restart_in_a_crash_loop(self):
        self.manager.start, self.manager.stop = Mock(), Mock()
        self.frame.settings['overlay'] = 4
        with patch.object(menus.subprocess, 'check_output', return_value='{"displays":[":1"],"tagged":[]}'):
            self.frame.poll_performance()
            self.assertIn('/opt/frame-gamescope/bin/mangoapp', self.manager.start.call_args[0][1])
            self.assertEqual(self.manager.start.call_args[0][0], 'performance-overlay')
            self.frame.compositor_retry = 0
            self.frame.poll_performance()
            self.assertEqual(self.manager.start.call_count, 1)
            self.frame.call('frame-settings', {'overlay': 0})
            self.frame.poll_performance()
            self.manager.stop.assert_called_with('performance-overlay')
            self.assertIsNone(self.frame.overlay_key)
            self.assertEqual(self.manager.start.call_count, 1)

    def test_actual_window_changes_and_closed_window_refused(self):
        self.manager.running = lambda name: True
        (self.root / 'desktop-shell.env').write_text('environment')
        self.frame.script_ready = True
        log = self.root / 'workspace.log'
        log.write_text('js: STEARLIGHT_WINDOWS [{"id":"uuid","title":"Terminal"}]\n')
        self.frame.poll()
        window = self.frame.status()['windows'][0]
        with patch.object(self.frame, 'script') as activate:
            self.frame.call('focus-window', {'window_id': window['window_id']})
            self.assertIn('uuid', activate.call_args[0][0])
        with log.open('a') as stream:
            stream.write('js: STEARLIGHT_WINDOWS []\n')
        self.frame.poll()
        self.assertEqual(self.frame.status()['windows'], [])
        with self.assertRaises(ValueError):
            self.frame.call('focus-window', {'window_id': window['window_id']})

    def test_only_the_native_performance_tab_gate_changes(self):
        gate = '(!pr||(0,Mt.j3)(!0).IN_GAMESCOPE)&&X'
        source = gate + ';tab("#QuickAccess_Tab_Perf_Title");other.IN_GAMESCOPE'
        result = menus.enable_performance_tab(source)
        self.assertIn('IN_GAMESCOPE||(0,Mt.j3)(!0).ON_FRAME', result)
        self.assertTrue(result.endswith('other.IN_GAMESCOPE'))
        self.assertEqual(menus.enable_performance_tab(result), result)
        self.assertEqual(menus.enable_performance_tab(gate), gate)


if __name__ == '__main__':
    unittest.main()
