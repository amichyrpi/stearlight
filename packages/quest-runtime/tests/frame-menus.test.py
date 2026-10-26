"""Exercise real desktop entries, stale-window handling and scoped UI gating."""
import importlib.util
import json
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
        self.pins = patch.multiple(menus, APPLICATIONS=(self.apps,), RUNTIME=self.root)
        self.pins.start()
        self.addCleanup(self.pins.stop)
        self.manager = type('Manager', (), {'processes': {}, 'instances': {}, 'running': lambda self, name: False})()
        def atomic(path, text):
            path.write_text(text)
        self.frame = menus.FrameMenus(self.manager, self.root, atomic)

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
        with patch.object(menus.subprocess, 'check_output', return_value='[":1"]'):
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
