import gzip
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

LINUX = Path(__file__).resolve().parents[1] / "linux"
spec = importlib.util.spec_from_file_location("launcher", LINUX / "fex-game-launcher.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class GameRuntimeTest(unittest.TestCase):
    def test_gamepad_preload_is_scoped_to_the_fex_guest(self):
        with tempfile.TemporaryDirectory() as folder:
            library = Path(folder) / "libquest_gamepad_x86_64.so"
            root = Path(folder) / "root"
            self.assertEqual(launcher.guest_gamepad(root, library), [])
            library.write_bytes(b"adapter")
            self.assertEqual(launcher.guest_gamepad(root, library), ["LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libquest_gamepad.so"])
            self.assertEqual((root / "usr/lib/x86_64-linux-gnu/libquest_gamepad.so").read_bytes(), b"adapter")

    def test_runtime_update_retargets_shell_and_support_directories(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            destination = base / "quest-rootfs"
            for version in ("old", "new"):
                platform = base / version
                files = platform / "files"
                (files / "bin").mkdir(parents=True)
                (files / "lib/x86_64-linux-gnu").mkdir(parents=True)
                (files / "etc").mkdir()
                (files / "bin/bash").write_text(version)
                with gzip.open(platform / "usr-mtree.txt.gz", "wt") as manifest:
                    manifest.write("./bin type=dir\n./lib type=dir\n./etc type=dir\n")
                launcher.prepare_rootfs(platform, destination)
                self.assertEqual((destination / "bin/bash").read_text(), version)
                self.assertEqual((destination / "etc").resolve(), files / "etc")
            launcher.prepare_rootfs(platform, destination)
            self.assertEqual((destination / "bin/bash").read_text(), "new")

    def test_monokickstart_replaces_shell_without_changing_setup(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder) / "Game"
            original = '#!/bin/bash\n# MonoKickstart picks the right libfolder\ncd "`dirname "$0"`"\n\t./Game.bin.x86_64 $@\n'
            game.write_text(original)
            launcher.prepare_game_script([str(game)])
            self.assertEqual(game.read_text(), original.replace('./Game.bin.x86_64 $@', 'exec ./Game.bin.x86_64 "$@"'))
            launcher.prepare_game_script([str(game)])
            self.assertEqual(game.read_text().count('exec '), 1)
            game.write_text('#!/bin/bash\n./Other.bin.x86_64 $@\n')
            launcher.prepare_game_script([str(game)])
            self.assertNotIn('exec ', game.read_text())

    def test_monokickstart_selects_only_a_verified_x86_64_binary(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder) / "Celeste"
            game.write_text('#!/bin/bash\n# MonoKickstart picks the right libfolder\n./Game.bin.x86_64 $@\n')
            binary = game.with_name("Game.bin.x86_64")
            elf = bytearray(20)
            elf[:6] = b"\x7fELF\x02\x01"
            elf[18:20] = b"\x3e\x00"
            binary.write_bytes(elf)
            self.assertEqual(launcher.prepare_game_script([str(game)]), binary)
            elf[18:20] = b"\xb7\x00"
            binary.write_bytes(elf)
            self.assertIsNone(launcher.prepare_game_script([str(game)]))

    def test_gamescope_owns_local_game_panel_and_uses_matching_pipewire(self):
        with tempfile.TemporaryDirectory() as folder:
            tool = Path(folder) / "FEX-Emu/fex-compat-tool"
            runtime = tool.parent.parent / "SteamLinuxRuntime_soldier/soldier_platform_test"
            runtime.mkdir(parents=True)
            (runtime / "usr-mtree.txt.gz").touch()
            with patch.object(launcher.sys, "argv", [str(tool), "run", "/games/Game", "two words"]), patch.dict(os.environ, {"SteamAppId": "123", "STEAM_COMPAT_DATA_PATH": folder}, clear=True), patch.object(launcher, "prepare_rootfs", return_value=Path(folder)), patch.object(launcher.os, "setsid"), patch.object(launcher.os, "execve") as execute:
                launcher.main()
            executable, arguments, environment = execute.call_args.args
            self.assertIn("--backend", arguments)
            self.assertIn("openvr", arguments)
            self.assertIn("valve.steam.desktopgame.123", arguments)
            self.assertEqual(arguments[arguments.index("--virtual-connector-strategy") + 1], "SingleApplication")
            self.assertEqual(arguments[arguments.index("-w") + 1], "960")
            self.assertEqual(arguments[arguments.index("-h") + 1], "540")
            self.assertIn("/usr/lib/aarch64-linux-gnu/libpipewire-0.3.so.0", arguments)
            self.assertEqual(arguments[-4:], [str(tool), "run", "/games/Game", "two words"])
            self.assertEqual(environment["STEARLIGHT_FEX_GAMESCOPE"], "1")
            self.assertNotIn("LD_PRELOAD", environment)
            self.assertNotIn("MESA_VK_WSI_DEBUG", environment)

    def test_launch_keeps_steam_identity_and_scopes_native_loader_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            common = Path(folder) / "common"
            tool = common / "FEX-Emu"
            platform = common / "SteamLinuxRuntime_sniper/sniper_platform_test"
            (platform / "files/lib/x86_64-linux-gnu").mkdir(parents=True)
            with gzip.open(platform / "usr-mtree.txt.gz", "wt") as stream:
                stream.write("./bin type=dir\n")
            command = [str(tool / "fex-compat-tool"), "waitforexitandrun", "--",
                       str(platform.parent / "_v2-entry-point"), "--verb=waitforexitandrun", "--", "/games/Game", "two words"]
            environment = {"STEAM_COMPAT_DATA_PATH": str(Path(folder) / "compat"),
                           "SteamAppId": "123", "GLIBC_TUNABLES": "glibc.pthread.rseq=0",
                           "LD_PRELOAD": "native.so", "LD_LIBRARY_PATH": "/native/libs",
                           "PULSE_SERVER": "unix:/tmp/pulse", "STEARLIGHT_FEX_GAMESCOPE": "1"}
            with patch.object(launcher.sys, "argv", command), patch.dict(os.environ, environment, clear=True), patch.object(launcher, "guest_gamepad", return_value=["LD_PRELOAD=/guest/input.so"]), patch.object(launcher.os, "execve") as execute:
                launcher.main()
                self.assertEqual(os.environ["GLIBC_TUNABLES"], "glibc.pthread.rseq=0")
            executable, arguments, actual = execute.call_args.args
            self.assertEqual(executable, tool / "usr/bin/FEX")
            self.assertEqual(arguments[1:], ["/games/Game", "two words"])
            self.assertEqual(actual["SteamAppId"], "123")
            self.assertEqual(actual["PULSE_SERVER"], "unix:/tmp/pulse")
            self.assertEqual(actual["VK_ICD_FILENAMES"], "/opt/quest/turnip_icd.json")
            self.assertNotIn("GLIBC_TUNABLES", actual)
            self.assertNotIn("LD_PRELOAD", actual)
            self.assertEqual(actual["FEX_ENV"], "LD_PRELOAD=/guest/input.so")
            self.assertNotIn("LD_LIBRARY_PATH", actual)
            self.assertEqual(actual["MESA_VK_WSI_DEBUG"], "sw")
            self.assertEqual(json.loads(Path(actual["FEX_APP_CONFIG"]).read_text())["ThunksDB"], {"GL": 1, "Vulkan": 1})

    def test_runtime_aliases_survive_repeat_and_refresh_on_update(self):
        with tempfile.TemporaryDirectory() as folder:
            platform = Path(folder) / "platform"
            files = platform / "files"
            (files / "lib/x86_64-linux-gnu").mkdir(parents=True)
            (files / "existing").write_text("Valve file")
            pulse = files / "lib/x86_64-linux-gnu/pulseaudio"
            pulse.mkdir()
            (pulse / "libpulsecommon.so").write_text("audio dependency")
            manifest = platform / "usr-mtree.txt.gz"
            entries = "./bin type=dir\n./bin/game\\040name type=link link=../existing\n"
            with gzip.open(manifest, "wt") as stream:
                stream.write(entries)
            root = Path(folder) / "root"
            launcher.prepare_rootfs(platform, root)
            self.assertEqual((root / "bin/game name").read_text(), "Valve file")
            self.assertEqual((files / "existing").read_text(), "Valve file")
            self.assertFalse((root / "usr/lib/x86_64-linux-gnu/pulseaudio").is_symlink())
            self.assertEqual((root / "usr/lib/x86_64-linux-gnu/pulseaudio/libpulsecommon.so").read_text(), "audio dependency")
            stamp = (root / "manifest.json").stat().st_mtime_ns
            launcher.prepare_rootfs(platform, root)
            self.assertEqual((root / "manifest.json").stat().st_mtime_ns, stamp)
            with gzip.open(manifest, "wt") as stream:
                stream.write(entries + "./new type=link link=existing\n")
            launcher.prepare_rootfs(platform, root)
            self.assertEqual((root / "usr/new").read_text(), "Valve file")

    def test_manifest_cannot_escape_runtime(self):
        with tempfile.TemporaryDirectory() as folder:
            platform = Path(folder)
            (platform / "files").mkdir()
            with gzip.open(platform / "usr-mtree.txt.gz", "wt") as stream:
                stream.write("./../escape type=dir\n")
            with self.assertRaises(ValueError):
                launcher.prepare_rootfs(platform, platform / "root")

    def test_content_addressed_runtime_files_are_restored(self):
        with tempfile.TemporaryDirectory() as folder:
            platform = Path(folder) / "platform"
            files = platform / "files"
            (files / "lib/x86_64-linux-gnu").mkdir(parents=True)
            (files / "aa").mkdir()
            (files / "aa/entry.bin").write_text("runtime executable")
            with gzip.open(platform / "usr-mtree.txt.gz", "wt") as stream:
                stream.write("./bin type=dir\n./bin/python type=file contents=./aa/entry.bin\n")
            launcher.prepare_rootfs(platform, Path(folder) / "root")
            self.assertEqual((files / "bin/python").read_text(), "runtime executable")

    def test_only_known_container_wrappers_are_removed(self):
        game = ["/games/Celeste", "a b", "--", "--verb=keep"]
        command = ["--", "/steam/SteamLinuxRuntime_soldier/_v2-entry-point",
                   "--verb=waitforexitandrun", "--",
                   "/steam/SteamLinuxRuntime/scout-on-soldier-entry-point-v2", "--", *game]
        self.assertEqual(launcher.unwrap(command), game)
        self.assertEqual(launcher.unwrap(["/custom/_v2-entry-point", *game]),
                         ["/custom/_v2-entry-point", *game])

    def test_shader_worker_limit_preserves_other_arguments(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "fossilize_replay"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(LINUX / "shader-replay-launcher.c"), "-o", str(binary)], check=True)
            real = binary.with_name(binary.name + ".real")
            real.write_text("#!/usr/bin/python3\nimport json,os,sys\nprint(json.dumps([sys.argv[1:],os.environ['VK_DRIVER_FILES']]))\n")
            real.chmod(0o755)
            output = subprocess.check_output([str(binary), "cache with spaces.foz", "--num-threads", "8", "--num-threads=16", "--quiet"], text=True)
            arguments, icd = json.loads(output)
            self.assertEqual(arguments, ["cache with spaces.foz", "--quiet", "--num-threads", "1"])
            self.assertEqual(icd, "/opt/quest/turnip_icd.json")


if __name__ == "__main__":
    unittest.main()
