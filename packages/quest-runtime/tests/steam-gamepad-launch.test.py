"""Keep physical input in Steam across repeated runtime preparation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class SteamGamepadLaunchTest(unittest.TestCase):
    def test_preparation_is_idempotent_and_preserves_steam_command(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'steam'
            quest = Path(folder) / 'quest'
            (root / 'steamrtarm64').mkdir(parents=True)
            quest.mkdir()
            for name in ('steamwebhelper-launcher.sh', 'prepare-store-browser.sh', 'prepare-cef.sh',
                         'prepare-steam-input.py', 'frame-steam-ipc', 'prepare-streaming-client.sh',
                         'prepare-game-runtime.sh', 'steam_channels.py', 'prepare-desktop-ui.py'):
                (quest / name).write_text('')
            launcher = root / 'RUNSTEAM.sh'
            command = 'exec "${STEAM_COMMAND[@]}" >"$HOME/steam.log" 2>&1'
            launcher.write_text('#!/bin/bash\nSTEAM_COMMAND=(\n /bin/true -gamepadui\n)\n' + command + '\n')
            launcher.chmod(0o755)
            env = dict(os.environ, STEARLIGHT_STEAM_ROOT=str(root), STEARLIGHT_QUEST_ROOT=str(quest))
            prepare = ROOT / 'packages/quest-runtime/linux/prepare-steam-client.sh'
            subprocess.run(['sh', str(prepare)], env=env, check=True)
            first = launcher.read_bytes()
            subprocess.run(['sh', str(prepare)], env=env, check=True)
            self.assertEqual(launcher.read_bytes(), first)
            text = first.decode()
            self.assertEqual(text.count('# STEARLIGHT_ANDROID_GAMEPAD'), 1)
            self.assertIn(command, text)
            self.assertIn('${LD_PRELOAD:+:$LD_PRELOAD}', text)
            subprocess.run(['bash', '-n', str(launcher)], check=True)


if __name__ == '__main__':
    unittest.main()
