"""Check argument boundaries, exit propagation and update-safe installation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

LINUX = Path(__file__).resolve().parents[1] / 'linux'


class GameLaunchWrapperTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.build.name) / 'game-launch-wrapper'
        subprocess.run(['cc', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
                        str(LINUX / 'game-launch-wrapper.c'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_options_and_command_arguments(self):
        with tempfile.TemporaryDirectory() as folder:
            wrapper = Path(folder) / 'steam-launch-wrapper'
            shutil.copyfile(self.binary, wrapper)
            wrapper.chmod(0o755)
            real = wrapper.with_name(wrapper.name + '.quest-real')
            real.write_text(f'#!{sys.executable}\nimport json,os,sys\nprint(json.dumps([os.getpid(),sys.argv[1:]]))\nsys.exit(37)\n')
            real.chmod(0o755)
            cases = [
                (['--oom-score-adjust', '900', '--', 'a game', '--oom-score-adjust', '900'],
                 ['--', 'a game', '--oom-score-adjust', '900']),
                (['--oom-score-adjust=900', '--', 'game'], ['--', 'game']),
                (['--oom-score-adjust', '0', '--', 'game'], ['--oom-score-adjust', '0', '--', 'game']),
                (['--oom-score-adjust'], ['--oom-score-adjust']),
                (['--', '--oom-score-adjust=900'], ['--', '--oom-score-adjust=900']),
            ]
            for arguments, expected in cases:
                with subprocess.Popen([str(wrapper), *arguments], stdout=subprocess.PIPE, text=True) as child:
                    output, _ = child.communicate(timeout=10)
                    pid, received = json.loads(output)
                    self.assertEqual(received, expected)
                    self.assertEqual(pid, child.pid)  # exec, rather than another resident helper.
                    self.assertEqual(child.returncode, 37)

    def test_installation_idempotence_and_upstream_update(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'steam'
            quest = Path(folder) / 'quest'
            wrapper = root / 'linuxarm64/steam-launch-wrapper'
            wrapper.parent.mkdir(parents=True)
            quest.mkdir()
            shutil.copyfile(self.binary, quest / 'game-launch-wrapper')
            preparer = quest / 'prepare-game-runtime.sh'
            preparer.write_bytes((LINUX / 'prepare-game-runtime.sh').read_bytes().replace(b'\r\n', b'\n'))
            environment = dict(os.environ, STEARLIGHT_STEAM_ROOT=str(root), STEARLIGHT_QUEST_ROOT=str(quest))
            for version in (b'original', b'updated'):
                binary = b'\x7fELF' + version
                wrapper.write_bytes(binary)
                for _ in range(2):
                    subprocess.run(['bash', str(preparer)], env=environment, check=True)
                    self.assertEqual(wrapper.with_name(wrapper.name + '.quest-real').read_bytes(), binary)
                    self.assertEqual(wrapper.read_bytes(), (quest / 'game-launch-wrapper').read_bytes())

    def test_incompatible_preloads_do_not_enter_adapter(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            wrapper = folder / 'steam-launch-wrapper'
            shutil.copyfile(self.binary, wrapper)
            wrapper.chmod(0o755)
            # If a dynamic adapter loads this first, it exits before forwarding.
            preload = folder / 'preload.c'
            preload.write_text('#include <unistd.h>\n__attribute__((constructor)) void init(void){_exit(69);}\n')
            library = folder / 'preload.so'
            subprocess.run(['cc', '-shared', '-fPIC', str(preload), '-o', str(library)], check=True)
            vendor = folder / 'vendor.c'
            vendor.write_text('#include <stdio.h>\n#include <stdlib.h>\nint main(void){puts(getenv("LD_PRELOAD"));return 37;}\n')
            subprocess.run(['cc', '-static', str(vendor), '-o', str(wrapper) + '.quest-real'], check=True)
            result = subprocess.run([str(wrapper), '--oom-score-adjust', '900', '--', 'game'],
                                    env=dict(os.environ, LD_PRELOAD=str(library)), capture_output=True, text=True)
            self.assertEqual(result.returncode, 37)
            self.assertEqual(result.stdout.strip(), str(library))


if __name__ == '__main__':
    unittest.main()
