import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('channels', Path(__file__).resolve().parents[1] / 'linux/steam_channels.py')
channels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(channels)


class ChannelStateTests(unittest.TestCase):
    def test_clean_recovery_uses_moving_feed_and_retains_profile_name(self):
        with tempfile.TemporaryDirectory() as folder:
            channels.STEAM = Path(folder)
            package = channels.STEAM / 'package'
            channels.write(package / 'beta', 'linux_arm64_beta_1234abcd')
            channels.prepare_client()
            self.assertEqual(channels.client_channel(), 'beta')
            self.assertEqual(channels.read(package / 'stearlight-frame-beta'), 'linux_arm64_beta_1234abcd')
            self.assertEqual(channels.read(package / 'stearlight-channel'), 'beta')

    def test_frame_profile_tracks_committed_native_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            channels.STEAM = Path(folder)
            package = channels.STEAM / 'package'
            channels.write(package / 'stearlight-channel', 'beta')
            channels.write(package / 'stearlight-frame-beta', 'linux_arm64_beta_1234abcd')
            alias = package / 'steam_client_linux_arm64_beta_1234abcd_linuxarm64.manifest'
            alias.write_text('"version" "1"')
            channels.prepare_manifest_alias()
            self.assertFalse(alias.is_symlink())  # No committed public installation.
            target = package / 'steam_client_publicbeta_linuxarm64.manifest'
            target.write_text('"version" "2"')
            channels.prepare_manifest_alias()
            self.assertTrue(alias.is_symlink())
            self.assertEqual(alias.read_text(), target.read_text())
            self.assertEqual(alias.with_name(alias.name + '.stearlight-snapshot').read_text(), '"version" "1"')
            replacement = target.with_suffix('.next')
            replacement.write_text('"version" "3"')
            replacement.replace(target)
            self.assertEqual(alias.read_text(), '"version" "3"')
            channels.prepare_manifest_alias()
            channels.write(package / 'stearlight-frame-beta', '../outside')
            channels.prepare_manifest_alias()
            self.assertEqual(alias.read_text(), '"version" "3"')

    def test_client_change_preserves_os_choice_and_uses_public_feed(self):
        with tempfile.TemporaryDirectory() as folder:
            channels.STEAM = Path(folder)
            channels.write(channels.STEAM / 'package/stearlight-channel', 'beta')
            channels.select_client('rel')
            self.assertEqual(channels.os_channel(), 'beta')
            self.assertEqual(channels.client_channel(), 'rel')
            channels.select_client('beta')
            self.assertEqual(channels.read(channels.STEAM / 'package/beta'), 'publicbeta')
            self.assertEqual(channels.read(channels.STEAM / 'package/stearlight-channel-update'), 'beta')
            with self.assertRaises(ValueError):
                channels.select_client('../private')

    def test_only_confirmed_runtime_versions_are_reported(self):
        with tempfile.TemporaryDirectory() as folder:
            channels.CACHE = Path(folder)
            self.assertEqual(channels.status()['frame_version'], '')
            (channels.CACHE / 'pending.json').write_text(json.dumps({'version': '9.9.9'}))
            self.assertEqual(channels.status()['frame_version'], '')
            (channels.CACHE / 'installed.json').write_text(json.dumps({'version': '0.4.5', 'buildid': '20261007.6125817'}))
            self.assertEqual(channels.status()['frame_version'], '0.4.5')


if __name__ == '__main__':
    unittest.main()
