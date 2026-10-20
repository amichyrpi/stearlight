"""Check the adapter against real SDL2, including embedded-SDL API dispatch."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class SDLAdapterTest(unittest.TestCase):
    def test_real_sdl_input_pacing_and_opt_out(self):
        with tempfile.TemporaryDirectory() as folder:
            library = Path(folder) / 'adapter.so'
            test = Path(folder) / 'test'
            include = ROOT / 'packages/quest-runtime/app/src/main/cpp'
            subprocess.run(['g++', '-O2', '-std=c++17', '-shared', '-fPIC', '-Wl,-Bsymbolic-functions',
                            '-I', str(include), str(ROOT / 'packages/quest-runtime/linux/sdl-gamepad.cpp'),
                            '-ldl', '-o', str(library)], check=True)
            subprocess.run(['g++', '-O2', '-std=c++17', '-I', str(include),
                            str(Path(__file__).with_suffix('.cpp')), '-ldl', '-o', str(test)], check=True)
            base = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', LD_PRELOAD=str(library))
            for key in ('SDL_DYNAMIC_API', 'STEARLIGHT_RENPY_SDL', 'STEARLIGHT_WINE_GAMEPAD', 'STEARLIGHT_STEAM_INPUT'):
                base.pop(key, None)
            for label, extra in (
                ('normal gamepad', {}),
                ('Wine haptics', {'STEARLIGHT_WINE_GAMEPAD': '1'}),
                ('RenPy embedded SDL', {'STEARLIGHT_RENPY_SDL': '1', 'SDL_DYNAMIC_API': str(library)}),
                ('RenPy input disabled', {'STEARLIGHT_RENPY_SDL': '1', 'SDL_DYNAMIC_API': str(library), 'STEARLIGHT_STEAM_INPUT': '0'}),
            ):
                with self.subTest(mode=label):
                    subprocess.run([str(test)], env={**base, **extra}, check=True, timeout=15)


if __name__ == '__main__':
    unittest.main()
