"""Check coupled Touch identity/axis adaptation, restoration and update safety."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import struct
import subprocess
import unittest
from unittest.mock import patch

source = Path(__file__).resolve().parents[1] / 'linux/prepare-streaming-input.py'
spec = importlib.util.spec_from_file_location('stream_input', source)
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class StreamingLauncherTest(unittest.TestCase):
    def test_usb_preference_preserves_session_and_only_uses_advertised_peers(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            launcher = source.with_name('streaming-client-launcher.c')
            fixture = root / 'test.c'
            fixture.write_text('#define main launcher_main\n#include "' + str(launcher) +
                               '"\n#undef main\nint main(int n,char** a){prefer_usb(n,a);'
                               'for(int i=1;a[i];i++)puts(a[i]);return 0;}\n')
            binary = root / 'test'
            subprocess.run(['gcc', '-O2', '-Wall', '-Wextra', '-Werror', str(fixture), '-o', str(binary)], check=True)
            scope = root / 'usb-network-scope'
            endpoint = '192.168.4.10~10400~10400~1,0,1~~~~session-token'
            args = ['-c', '192.168.4.10', '-c', '10.86.13.38', '-e', endpoint]
            environment = dict(os.environ, QUEST_SOCKET_REGISTRY=str(root / 'socket-registry.sock'))
            environment.pop('STEARLIGHT_VRLINK_PREFER_USB', None)

            def run(arguments=args, state='14 10.86.13.37 29 123', env=environment):
                scope.write_text(state)
                result = subprocess.run([str(binary), *arguments], env=env, check=True, capture_output=True, text=True)
                return result.stdout.splitlines()

            expected = args[:-1] + [endpoint.replace('192.168.4.10', '10.86.13.38', 1)]
            self.assertEqual(run(), expected)
            self.assertEqual(run(expected), expected)
            self.assertEqual(run(args+['-c', 'fe80::1234', '-q', 'test']), expected+['-c', 'fe80::1234', '-q', 'test'])
            self.assertEqual(run(state='0 - 0 0'), args)  # Disconnected.
            self.assertEqual(run(state='14 10.0.0.2 24 123'), args)  # Different subnet.
            self.assertEqual(run(state='14 10.86.13.37 33 123'), args)  # Invalid prefix.
            self.assertEqual(run(env=dict(environment, STEARLIGHT_VRLINK_PREFER_USB='0')), args)
            ambiguous = args + ['-c', '10.86.13.39']
            self.assertEqual(run(ambiguous), ambiguous)
            unknown = args[:-1] + [endpoint.replace('192.168.4.10', '192.168.4.99', 1)]
            self.assertEqual(run(unknown), unknown)
            ipv6 = args[:-1] + ['[fe80::1234]~10400~session-token']
            self.assertEqual(run(ipv6), ipv6)


class StreamingInputTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        root = Path(self.folder.name)
        self.binary = root / 'vrlink.real'
        fixture = bytearray(max(offset for offset, _, _ in adapter.LENGTHS) + 4)
        for offset, old, _ in adapter.LENGTHS:
            struct.pack_into('<I', fixture, offset, 0xd2800004 | (old << 5))
        self.original = bytes(fixture) + adapter.OLD_TYPE + b'prefix'
        for hand in ('left', 'right'):
            for name in ('thumbstick/x', 'thumbstick/y', 'trigger/value'):
                self.original += f'/user/hand/{hand}/input/{name}'.encode().ljust(40, b'\0')
        self.original += b'suffix'
        self.binary.write_bytes(self.original)
        self.binary.chmod(0o755)
        self.bindings = [root / name for name in ('player.json', 'profile.json')]
        self.binding = {'controller_type': 'frame_controller', 'bindings': {'/actions/legacy': {}}, 'options': {'mirror_actions': False}}
        for path in self.bindings:
            path.write_text(json.dumps(self.binding))
        self.modified = adapter.transform(self.original)
        pins = patch.multiple(adapter, ORIGINAL=hashlib.sha256(self.original).hexdigest(), PATCHED=hashlib.sha256(self.modified).hexdigest())
        pins.start()
        self.addCleanup(pins.stop)

    def test_patch_and_restore_are_coupled_and_idempotent(self):
        self.assertTrue(adapter.prepare(self.binary, self.bindings))
        self.assertEqual(self.binary.read_bytes(), self.modified)
        self.assertFalse(adapter.prepare(self.binary, self.bindings))
        self.assertTrue(adapter.prepare(self.binary, self.bindings, restore=True))
        self.assertEqual(self.binary.read_bytes(), self.original)
        for path in self.bindings:
            self.assertEqual(json.loads(path.read_text()), self.binding)
        self.assertFalse(adapter.prepare(self.binary, self.bindings, restore=True))
        self.assertEqual(self.binary.stat().st_mode & 0o777, 0o755)

    def test_touch_wire_order(self):
        for hand in ('left', 'right'):
            first = self.original.index(f'/user/hand/{hand}/input/thumbstick/x'.encode())
            fields = [self.modified[first+i*40:first+(i+1)*40].rstrip(b'\0').decode().rsplit('/input/', 1)[1] for i in range(3)]
            self.assertEqual(fields, ['trigger/value', 'thumbstick/x', 'thumbstick/y'])
        self.assertEqual(adapter.transform(self.modified, restore=True), self.original)

    def test_updated_player_restores_only_our_emulation(self):
        adapter.prepare(self.binary, self.bindings)
        unknown = b'new Valve runtime'
        self.binary.write_bytes(unknown)
        current = json.loads(self.bindings[0].read_text())
        current['options']['custom_setting'] = 7
        self.bindings[0].write_text(json.dumps(current))
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertTrue(adapter.prepare(self.binary, self.bindings))
        self.assertEqual(self.binary.read_bytes(), unknown)
        options = json.loads(self.bindings[0].read_text())['options']
        self.assertEqual(options, {'mirror_actions': False, 'custom_setting': 7})

    def test_bad_binding_never_installs_the_binary_patch(self):
        self.bindings[1].write_text('[]')
        with self.assertRaises(ValueError):
            adapter.prepare(self.binary, self.bindings)
        self.assertEqual(self.binary.read_bytes(), self.original)
        self.assertEqual(json.loads(self.bindings[0].read_text()), self.binding)

    def test_corrupt_output_pin_preserves_both_bindings_and_player(self):
        with patch.object(adapter, 'PATCHED', '0'*64), self.assertRaises(ValueError):
            adapter.prepare(self.binary, self.bindings)
        self.assertEqual(self.binary.read_bytes(), self.original)
        for path in self.bindings:
            self.assertEqual(json.loads(path.read_text()), self.binding)


if __name__ == '__main__':
    unittest.main()
