import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("fex_launcher", Path(__file__).resolve().parents[1] / "linux/fex-game-launcher.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class CelesteMemoryTest(unittest.TestCase):
    def test_updated_game_without_old_renderer_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            launcher.prepare_celeste_memory(Path(folder))
            launcher.prepare_celeste_memory(Path(folder), False)
            self.assertEqual(list(Path(folder).iterdir()), [])

    def test_unknown_game_update_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            library = game / "lib64/libFNA3D.so.0"
            library.parent.mkdir()
            library.write_bytes(b"unrecognized Steam game update")
            launcher.prepare_celeste_memory(game)
            launcher.prepare_celeste_memory(game, False)
            self.assertEqual(library.read_bytes(), b"unrecognized Steam game update")
            self.assertFalse(library.with_name(library.name + ".quest-original").exists())

    def test_managed_renderer_upgrade_restore_and_unknown_game_update(self):
        original = b"original fixture"
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            library = game / "lib64/libFNA3D.so.0"
            library.parent.mkdir()
            library.write_bytes(original)
            library.chmod(0o755)
            runtime = game / "runtime"
            runtime.mkdir()
            real_hash = launcher.hashlib.sha256
            # Keep the launcher pinned to its real game's checksum while the
            # test uses a tiny original, rather than distributing game files.
            def digest(data):
                if data == original:
                    from types import SimpleNamespace
                    return SimpleNamespace(hexdigest=lambda: "547990f643e09bf4ab68c755f89dad7548b8d07803a843c2e18c08ef2e9d3300")
                return real_hash(data)
            def bundle(data):
                (runtime / "libFNA3D.so.0").write_bytes(data)
                (runtime / "profile.json").write_text(json.dumps({"sha256": real_hash(data).hexdigest()}))
            with patch.object(launcher.hashlib, "sha256", side_effect=digest):
                bundle(b"renderer one")
                launcher.prepare_celeste_memory(game, runtime=runtime)
                self.assertEqual(library.read_bytes(), b"renderer one")
                self.assertEqual(library.stat().st_mode & 0o777, 0o755)
                launcher.prepare_celeste_memory(game, runtime=runtime)
                bundle(b"renderer two")
                launcher.prepare_celeste_memory(game, runtime=runtime)
                self.assertEqual(library.read_bytes(), b"renderer two")
                launcher.prepare_celeste_memory(game, False, runtime)
                self.assertEqual(library.read_bytes(), original)
                launcher.prepare_celeste_memory(game, runtime=runtime)
                library.write_bytes(b"Steam update")
                launcher.prepare_celeste_memory(game, runtime=runtime)
                launcher.prepare_celeste_memory(game, False, runtime)
                self.assertEqual(library.read_bytes(), b"Steam update")

    def test_corrupt_bundle_does_not_touch_game(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            library = game / "lib64/libFNA3D.so.0"
            library.parent.mkdir()
            library.write_bytes(b"original")
            runtime = game / "runtime"
            runtime.mkdir()
            (runtime / "libFNA3D.so.0").write_bytes(b"corrupt")
            (runtime / "profile.json").write_text('{"sha256":"wrong"}')
            with self.assertRaisesRegex(ValueError, "checksum"):
                launcher.prepare_celeste_memory(game, runtime=runtime)
            self.assertEqual(library.read_bytes(), b"original")


if __name__ == "__main__":
    unittest.main()
