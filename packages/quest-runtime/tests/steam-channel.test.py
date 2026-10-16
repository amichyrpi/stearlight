import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[3] / "system_files/usr/bin/steamos-select-branch"


class SteamChannelTest(unittest.TestCase):
    def test_native_contract_and_branch_persistence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            env = dict(os.environ, STEARLIGHT_STEAM_ROOT=folder)
            def call(*args):
                return subprocess.run(["sh", str(SCRIPT), *args], env=env, capture_output=True, text=True)
            self.assertEqual(call("-l").stdout, "rel\nbeta\n")
            self.assertEqual(call("-c").stdout, "rel\n")
            (root / "package").mkdir()
            beta = root / "package/beta"
            beta.write_text("partner-branch\n")
            self.assertEqual(call("-c").stdout, "beta\n")
            self.assertEqual(beta.read_text(), "partner-branch\n")
            self.assertEqual(call("beta").returncode, 0)
            self.assertEqual(beta.read_text(), "partner-branch\n")
            pending = root / "package/stearlight-channel-update"
            self.assertFalse(pending.exists())
            beta.write_text("partner-bootstrap-override\n")
            self.assertEqual(call("-c").stdout, "beta\n")
            self.assertEqual(call("preview").returncode, 2)
            self.assertEqual(call("../invalid").returncode, 2)
            self.assertEqual(beta.read_text(), "partner-bootstrap-override\n")
            self.assertEqual(call("rel").returncode, 0)
            self.assertEqual(beta.read_text(), "partner-bootstrap-override\n")
            self.assertEqual(call("-c").stdout, "rel\n")
            self.assertFalse(pending.exists())
            self.assertEqual(call("rel").returncode, 0)
            self.assertFalse(pending.exists())

    def test_launcher_arguments_preserve_explicit_channel(self):
        args_script = SCRIPT.parents[3] / "packages/quest-runtime/linux/steam-channel-args.sh"
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "package").mkdir()
            def arguments(precache="0"):
                return subprocess.check_output(["bash", "-c",
                    'STEAMROOT=$1; STEARLIGHT_SHADER_PRECACHE=$3; STEAM_ARGS=(-deckard); . "$2"; printf "[%s]" "${STEAM_ARGS[@]}"',
                    "test", folder, str(args_script), precache], text=True)
            self.assertEqual(arguments(), "[-deckard][-noshaders]")
            self.assertEqual(arguments("1"), "[-deckard]")
            (root / "package/stearlight-channel").write_text("beta\n")
            self.assertEqual(arguments(), "[-deckard][-noshaders][-clientbeta][publicbeta]")
            injected = subprocess.check_output(["bash", "-c",
                'STEAMROOT=$1; STEAM_ARGS=(-deckard -clientbeta linux_arm64_beta_old -other); . "$2"; printf "[%s]" "${STEAM_ARGS[@]}"',
                "test", folder, str(args_script)], text=True)
            self.assertEqual(injected, "[-deckard][-other][-noshaders][-clientbeta][publicbeta]")
            (root / "package/beta").write_text("linux_arm64_beta_test\n")
            self.assertEqual(arguments(), "[-deckard][-noshaders][-clientbeta][publicbeta]")
            self.assertEqual((root / "package/beta").read_text(), "publicbeta\n")
            (root / "package/beta").write_text("publicbeta\n")
            self.assertEqual(arguments(), "[-deckard][-noshaders][-clientbeta][publicbeta]")
            (root / "package/stearlight-channel").write_text("rel\n")
            self.assertEqual(arguments(), "[-deckard][-noshaders][-clientbeta][steamdeck_stable]")
            self.assertEqual((root / "package/beta").read_text(), "steamdeck_stable\n")
            pending = root / "package/stearlight-channel-update"
            pending.write_text("rel\n")
            self.assertEqual(arguments(), "[-deckard][-noshaders][-clientbeta][steamdeck_stable][-forcesteamupdate][-forcepackagedownload]")
            self.assertFalse(pending.exists())
            self.assertEqual(arguments(), "[-deckard][-noshaders][-clientbeta][steamdeck_stable]")


if __name__ == "__main__":
    unittest.main()
