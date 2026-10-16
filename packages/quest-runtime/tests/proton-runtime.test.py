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
spec = importlib.util.spec_from_file_location("proton_launcher", LINUX / "proton-game-launcher.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class ProtonRuntimeTest(unittest.TestCase):
    def test_native_gamepad_adapter_is_scoped_to_wine_and_can_be_disabled(self):
        with patch.object(Path, "is_file", return_value=True):
            environment = launcher.launch_environment(Path("/runtime"), {"SteamAppId": "123"})
            self.assertIn("/opt/quest/libquest_gamepad_aarch64.so", environment["LD_PRELOAD"])
            disabled = launcher.launch_environment(Path("/runtime"), {"STEARLIGHT_STEAM_INPUT": "0"})
            self.assertNotIn("gamepad", disabled["LD_PRELOAD"])

    def test_migrated_proot_alias_retargets_current_canonical_file(self):
        with tempfile.TemporaryDirectory() as folder:
            platform = Path(folder)
            files = platform / "files"
            (files / "bin").mkdir(parents=True)
            (files / "current").write_text("current runtime")
            alias = files / "bin/command"
            alias.symlink_to(platform / "removed-platform/old")
            with gzip.open(platform / "usr-mtree.txt.gz", "wt") as manifest:
                manifest.write("./bin type=dir\n./bin/command type=file contents=./current\n")
            # PRoot presents its hard-link aliases as regular files to lstat.
            with patch.object(Path, "is_symlink", return_value=False):
                launcher.prepare_platform(platform)
            self.assertEqual(alias.read_text(), "current runtime")
            self.assertTrue(alias.samefile(files / "current"))
            launcher.prepare_platform(platform)
            self.assertEqual(alias.read_text(), "current runtime")

    def test_runtime_aliases_refresh_after_a_steam_update(self):
        with tempfile.TemporaryDirectory() as folder:
            platform = Path(folder)
            files = platform / "files"
            files.mkdir()
            (files / "old").write_text("old runtime")
            (files / "new").write_text("updated runtime")
            manifest = platform / "usr-mtree.txt.gz"
            with gzip.open(manifest, "wt") as output:
                output.write("./lib type=dir\n./lib/library.so type=file contents=./old\n./lib/alias.so type=link link=library.so\n")
            launcher.prepare_platform(platform)
            self.assertEqual((files / "lib/alias.so").read_text(), "old runtime")
            stamp = (platform / ".quest-aliases.json").stat().st_mtime_ns
            launcher.prepare_platform(platform)
            self.assertEqual((platform / ".quest-aliases.json").stat().st_mtime_ns, stamp)
            with gzip.open(manifest, "wt") as output:
                output.write("./lib type=dir\n./lib/library.so type=file contents=./new\n./lib/alias.so type=link link=library.so\n")
            os.utime(manifest, ns=(manifest.stat().st_atime_ns, manifest.stat().st_mtime_ns + 1))
            launcher.prepare_platform(platform)
            self.assertEqual((files / "lib/alias.so").read_text(), "updated runtime")
            self.assertEqual((files / "old").read_text(), "old runtime")

    def test_manifest_cannot_escape_the_runtime(self):
        with tempfile.TemporaryDirectory() as folder:
            platform = Path(folder)
            with gzip.open(platform / "usr-mtree.txt.gz", "wt") as output:
                output.write("../outside type=dir\n")
            with self.assertRaises(ValueError):
                launcher.prepare_platform(platform)

    def test_official_proton_keeps_steam_identity_audio_and_arguments(self):
        with tempfile.TemporaryDirectory() as folder:
            runtime = Path(folder)
            platform = runtime / "steamrt4_platform_test"
            platform.mkdir()
            (platform / "usr-mtree.txt.gz").touch()
            command = ["helper", str(runtime / "_v2-entry-point"), "--verb=waitforexitandrun", "--",
                       "/common/Proton 11.0 (ARM64)/proton", "waitforexitandrun", "/games/Game.exe", "two words"]
            environment = {"SteamAppId": "123", "STEAM_COMPAT_DATA_PATH": "/prefix/123",
                           "PULSE_SERVER": "unix:/tmp/pulse", "PROOT_QUEST_ROBUST": "1",
                           "VK_INSTANCE_LAYERS": "streaming-layer"}
            with patch.object(launcher.sys, "argv", command), patch.dict(os.environ, environment, clear=True), \
                 patch.object(launcher, "prepare_platform", return_value=platform / "files"), \
                 patch.object(launcher.os, "open", return_value=50), patch.object(launcher.os, "dup2"), \
                 patch.object(launcher.os, "close"), patch.object(launcher.os, "execve") as execute:
                launcher.main()
            executable, arguments, actual = execute.call_args.args
            self.assertEqual(executable, "/usr/bin/python3")
            self.assertEqual(arguments, [executable, *command[4:]])
            for key in ("SteamAppId", "STEAM_COMPAT_DATA_PATH", "PULSE_SERVER", "PROOT_QUEST_ROBUST"):
                self.assertEqual(actual[key], environment[key])
            self.assertEqual(actual["VK_DRIVER_FILES"], "/opt/quest/turnip_icd.json")
            self.assertNotIn("VK_INSTANCE_LAYERS", actual)

    def test_vtol_defaults_preserve_later_user_choices_and_rpc_services(self):
        with tempfile.TemporaryDirectory() as folder:
            compat = Path(folder)
            settings = compat / "pfx/drive_c/users/steamuser/AppData/Roaming/Boundless Dynamics, LLC/VTOLVR/SaveData/gameSettings.cfg"
            settings.parent.mkdir(parents=True)
            settings.write_text("GAMESETTINGS\n{\n MSAA = 1\n SHADOWS = 1\n BGM_VOLUME = 90\n}\n")
            registry = compat / "pfx/system.reg"
            registry.write_text('[System\\\\ControlSet001\\\\Services\\\\Eventlog] 0\n"Start"=dword:00000002\n\n[System\\\\ControlSet001\\\\Services\\\\RpcSs] 0\n"Start"=dword:00000002\n')
            launcher.prepare_vtol_prefix(compat)
            self.assertIn("MSAA = -1", settings.read_text())
            self.assertIn("BGM_VOLUME = 90", settings.read_text())
            self.assertIn('Eventlog] 0\n"Start"=dword:00000004', registry.read_text())
            self.assertIn('RpcSs] 0\n"Start"=dword:00000002', registry.read_text())
            settings.write_text("user choices")
            launcher.prepare_vtol_prefix(compat)
            self.assertEqual(settings.read_text(), "user choices")

    def test_vtol_maps_real_quest_inputs_without_replacing_vendor_bindings(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            root = game / "VTOLVR_Data/StreamingAssets/SteamVR"
            root.mkdir(parents=True)
            controller = {"controller_type": "oculus_touch", "bindings": {"/actions/default": {
                "sources": [{"path": "/user/hand/left/input/joystick", "mode": "joystick"},
                            {"path": "/user/hand/right/input/grip", "mode": "button",
                             "parameters": {"force_input": "force", "click_activate_threshold": "0.8"}}]}}}
            headset = {"controller_type": "rift", "bindings": {"/actions/default": {
                "sources": [{"path": "/user/head/proximity", "mode": "button",
                             "inputs": {"click": {"output": "/actions/default/in/headsetonhead"}}}]}}}
            for name, content in (("touch.json", controller), ("head.json", headset)):
                (root / name).write_text(json.dumps(content))
            defaults = [{"controller_type": "oculus_touch", "binding_url": "touch.json"},
                        {"controller_type": "rift", "binding_url": "head.json"}]
            manifest = root / "actions.json"
            manifest.write_text(json.dumps({"default_bindings": defaults, "actions": ["original actions"]}))
            original = manifest.read_bytes()
            launcher.prepare_vtol_bindings(game)
            actual = json.loads(manifest.read_text())
            self.assertEqual(actual["default_bindings"][:2], defaults)
            self.assertEqual(actual["actions"], ["original actions"])
            self.assertEqual((root / "actions.json.quest-original").read_bytes(), original)
            self.assertEqual(json.loads((root / "touch.json").read_text()), controller)
            mapped = json.loads((root / "bindings_stearlight_quest.json").read_text())
            sources = mapped["bindings"]["/actions/default"]["sources"]
            self.assertEqual(sources[0]["path"], "/user/hand/left/input/thumbstick")
            self.assertNotIn("force_input", sources[1]["parameters"])
            head = json.loads((root / "binding_stearlight_quest_hmd.json").read_text())
            self.assertEqual(head["controller_type"], "stearlight-quest_hmd")
            self.assertEqual(head["bindings"], headset["bindings"])
            stamp = manifest.stat().st_mtime_ns
            launcher.prepare_vtol_bindings(game)
            self.assertEqual(manifest.stat().st_mtime_ns, stamp)

    @unittest.skipIf(os.name == "nt", "Needs a POSIX shell")
    def test_adapters_keep_vendor_backups_and_refresh_after_steam_updates(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / "steam"
            quest = Path(folder) / "quest"
            quest.mkdir()
            for name in ("proton-runtime-entry.sh", "audio-namespace-launcher.sh"):
                (quest / name).write_bytes((LINUX / name).read_bytes())
            entry = root / "steamapps/common/SteamLinuxRuntime_4-arm64/_v2-entry-point"
            audio = root / "linuxarm64/pw-audio-namespace"
            for path in (entry, audio):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("original Valve file")
            env = dict(os.environ, STEARLIGHT_STEAM_ROOT=str(root), STEARLIGHT_QUEST_ROOT=str(quest))
            def prepare():
                subprocess.run(["/bin/sh", str(LINUX / "prepare-game-runtime.sh")], env=env, check=True)
            prepare(); prepare()
            self.assertEqual(Path(str(entry) + ".quest-original").read_text(), "original Valve file")
            entry.write_text("updated Valve file")
            prepare()
            self.assertEqual(Path(str(entry) + ".quest-original").read_text(), "updated Valve file")
            self.assertEqual(entry.read_bytes(), (quest / "proton-runtime-entry.sh").read_bytes())


if __name__ == "__main__":
    unittest.main()
