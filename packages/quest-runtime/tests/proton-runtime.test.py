import gzip
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

LINUX = Path(__file__).resolve().parents[1] / "linux"
spec = importlib.util.spec_from_file_location("proton_launcher", LINUX / "proton-game-launcher.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class ProtonRuntimeTest(unittest.TestCase):
    def test_garden_boot_preserves_other_settings_and_original(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            boot = game / "Garden of Witches_Data/boot.config"
            boot.parent.mkdir()
            original = "gfx-enable-gfx-jobs=1\ngfx-enable-native-gfx-jobs=1\nbuild-guid=keep\n"
            boot.write_text(original)
            launcher.prepare_garden_boot(game)
            self.assertEqual(boot.read_text(), original.replace("jobs=1", "jobs=0"))
            self.assertEqual(boot.with_name("boot.config.quest-original").read_text(), original)
            launcher.prepare_garden_boot(game)
            self.assertEqual(boot.with_name("boot.config.quest-original").read_text(), original)

    def test_garden_prefix_retains_hid_drivers_and_rpc(self):
        with tempfile.TemporaryDirectory() as folder:
            compat = Path(folder)
            registry = compat / "pfx/system.reg"
            registry.parent.mkdir()
            original = ''.join(
                '[System\\\\ControlSet001\\\\Services\\\\' + name + '] 0\n' + value + '\n\n'
                for name, value in [
                    ("PlugPlay", '"Start"=dword:00000002'),
                    ("Eventlog", '"Start"=dword:00000002'),
                    ("RpcSs", '"Start"=dword:00000002'),
                    ("winebus", '"Group"="WinePlugPlay"\n"Start"=dword:00000003'),
                    ("winehid", '"Group"="WinePlugPlay"\n"Start"=dword:00000003'),
                ])
            registry.write_text(original)
            launcher.prepare_garden_prefix(compat)
            updated = registry.read_text()
            self.assertIn('PlugPlay] 0\n"Start"=dword:00000004', updated)
            self.assertIn('Eventlog] 0\n"Start"=dword:00000004', updated)
            self.assertIn('RpcSs] 0\n"Start"=dword:00000002', updated)
            self.assertEqual(updated.count('"Group"="System Bus Extender"\n"Start"=dword:00000003'), 2)
            launcher.prepare_garden_prefix(compat)
            self.assertEqual(registry.with_name("system.reg.quest-garden-original").read_text(), original)

    def test_proton_exec_keeps_prefix_setup_imports_arguments_and_log(self):
        class Executed(Exception):
            pass
        with tempfile.TemporaryDirectory() as folder:
            tool = Path(folder)
            (tool / "quest_test_dependency.py").write_text("value = 42\n")
            proton = tool / "proton"
            proton.write_text('''import os, subprocess, sys
import quest_test_dependency
assert quest_test_dependency.value == 42
assert subprocess.call(["wineserver", "-w"], env=dict(os.environ)) == 0
with open(sys.argv[2], "w") as log:
    subprocess.call(["/runtime/wine", r"c:\\windows\\system32\\steam.exe", sys.argv[3]],
                    env=dict(os.environ), stdout=log, stderr=log)
''')
            environment = {"SteamAppId": "2530470", "PULSE_SERVER": "unix:/pulse"}
            with patch.dict(os.environ, clear=True), patch.object(sys, "argv", []), \
                 patch.object(sys, "path", list(sys.path)), \
                 patch.object(launcher, "prepare_garden_prefix") as prepare, \
                 patch.object(launcher.subprocess, "call", return_value=0) as call, \
                 patch.object(launcher.os, "dup2") as duplicate, \
                 patch.object(launcher.os, "execvpe", side_effect=Executed) as execute:
                with self.assertRaises(Executed):
                    launcher.exec_proton([str(proton), "waitforexitandrun", str(tool / "log"), "/game/two words.exe"], environment, tool)
                self.assertIs(launcher.subprocess.call, call)
                self.assertEqual(call.call_args.args[0], ["wineserver", "-w"])
                prepare.assert_called_once_with(tool)
                self.assertEqual(execute.call_args.args, ("/runtime/wine", ["/runtime/wine", r"c:\windows\system32\steam.exe", "/game/two words.exe"], environment))
                self.assertEqual([c.args[1] for c in duplicate.call_args_list], [1, 2])
            sys.modules.pop("quest_test_dependency", None)

    def test_flat_panel_separates_wine_libraries_and_preserves_arguments(self):
        environment = {"PATH": "/usr/bin", "LD_PRELOAD": "/wine-only.so", "LD_LIBRARY_PATH": "/wine",
                       "MESA_VK_WSI_DEBUG": "sw", "PULSE_SERVER": "unix:/pulse", "SteamAppId": "2530470"}
        with patch.object(launcher.os, "getpgrp", return_value=10), \
             patch.object(launcher.os, "getpid", return_value=20), \
             patch.object(launcher.os, "setsid") as isolate, \
             patch.object(launcher.os, "execve") as execute:
            arguments = ["--verb=waitforexitandrun", "--", "/Proton/proton", "waitforexitandrun", "/game/two words.exe"]
            launcher.launch_flat_panel(Path("/runtime/entry"), arguments, environment, "2530470")
        _, command, actual = execute.call_args.args
        isolate.assert_called_once()
        self.assertEqual(command[-len(arguments):], arguments)
        self.assertIn("valve.steam.desktopgame.2530470", command)
        self.assertNotIn("LD_PRELOAD", actual)
        self.assertNotIn("LD_LIBRARY_PATH", actual)
        self.assertNotIn("MESA_VK_WSI_DEBUG", actual)
        self.assertEqual(actual["PULSE_SERVER"], environment["PULSE_SERVER"])
        self.assertEqual(actual["STEARLIGHT_PROTON_DIRECT"], "1")

    def test_native_gamepad_adapter_is_scoped_to_wine_and_can_be_disabled(self):
        with patch.object(Path, "is_file", return_value=True):
            environment = launcher.launch_environment(Path("/runtime"), {"SteamAppId": "123"})
            self.assertIn("/opt/quest/libquest_gamepad_aarch64.so", environment["LD_PRELOAD"])
            disabled = launcher.launch_environment(Path("/runtime"), {"STEARLIGHT_STEAM_INPUT": "0"})
            self.assertNotIn("gamepad", disabled["LD_PRELOAD"])

    def test_garden_retains_window_and_gamepad_services_without_changing_vtol(self):
        with tempfile.TemporaryDirectory() as folder:
            runtime = Path(folder)
            platform = runtime / "steamrt4_platform_test"
            platform.mkdir()
            (platform / "usr-mtree.txt.gz").touch()
            command = ["helper", str(runtime / "_v2-entry-point"), "--", "/Proton/proton",
                       "waitforexitandrun", "/game/Game.exe"]
            for appid in ("2530470", "667970"):
                environment = {"SteamAppId": appid, "STEAM_COMPAT_DATA_PATH": "/prefix",
                               "STEAM_COMPAT_INSTALL_PATH": "/game", "STEARLIGHT_PROTON_GAMESCOPE": "1",
                               "SDL_GAMECONTROLLER_IGNORE_DEVICES": "0x28de/0x11ff,0x0000/0x0000,0X0000/0X0000",
                               "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT": "0x045e/0x028e"}
                with patch.object(launcher.sys, "argv", command), patch.dict(os.environ, environment, clear=True), \
                     patch.object(launcher, "prepare_platform", return_value=platform / "files"), \
                     patch.object(launcher.os, "open", return_value=50), patch.object(launcher.os, "dup2"), \
                     patch.object(launcher.os, "close"), patch.object(launcher.os, "execve") as execute, \
                     patch.object(launcher, "exec_proton") as proton, \
                     patch.object(launcher, "prepare_garden_boot"), patch.object(launcher, "prepare_vtol_bindings"), \
                     patch.object(launcher, "prepare_vtol_textures"), patch.object(launcher, "prepare_vtol_prefix"), \
                     patch.object(launcher, "release_streaming_audio"):
                    launcher.main()
                actual = proton.call_args.args[1] if appid == "2530470" else execute.call_args.args[2]
                overrides = actual["WINEDLLOVERRIDES"].split(";")
                self.assertNotIn("explorer.exe=d", overrides)
                self.assertNotIn("rpcss.exe=d", overrides)
                if appid == "2530470":
                    self.assertNotIn("winebus.sys=d", overrides)
                    self.assertEqual(actual["STEARLIGHT_WINE_GAMEPAD"], "1")
                    self.assertEqual(actual["PROTON_USE_WINED3D"], "1")
                    self.assertEqual(actual["SDL_GAMECONTROLLER_IGNORE_DEVICES"], "0x28de/0x11ff")
                    self.assertEqual(actual["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"], "0x045e/0x028e,0x0000/0x0000")
                else:
                    self.assertIn("winebus.sys=d", overrides)
                    self.assertNotIn("STEARLIGHT_WINE_GAMEPAD", actual)
                    self.assertEqual(actual["SDL_GAMECONTROLLER_IGNORE_DEVICES"], environment["SDL_GAMECONTROLLER_IGNORE_DEVICES"])

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
