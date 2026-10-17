import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


def load(name):
    path = Path(__file__).resolve().parents[1] / 'linux' / (name + '.py')
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


launcher = load('flatpak-launcher')
defaults = load('desktop-defaults')


class DesktopAppsTests(unittest.TestCase):
    def test_file_forwarding_and_runtime_selection(self):
        app, command, args = launcher.run_arguments([
            '--branch=stable', '--command=calculator', '--file-forwarding',
            'org.gnome.Calculator', '@@u', 'file:///home/stearlight/test', '@@'])
        self.assertEqual(app, 'org.gnome.Calculator/aarch64/stable')
        self.assertEqual(command, 'calculator')
        self.assertEqual(args, ['file:///home/stearlight/test'])
        for args in [['../../bad'], ['--arch=x86_64', 'org.gnome.Calculator'],
                     ['--branch=../../bad', 'org.gnome.Calculator']]:
            with self.assertRaises(ValueError):
                launcher.run_arguments(args)

    def test_reject_install_outside_flatpak_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'outside/files').mkdir(parents=True)
            with mock.patch.object(launcher, 'HOME', root), mock.patch.object(
                    launcher.subprocess, 'check_output', return_value=str(root / 'outside')):
                with self.assertRaises(ValueError):
                    launcher.location('org.gnome.Calculator')

    def test_runtime_configuration_preserves_symlinks_without_following_them(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target = root / 'runtime', root / 'private'
            source.mkdir()
            (source / 'text').write_text('runtime data')
            (source / 'guest-link').symlink_to('/usr/share/example')
            target.mkdir()
            outside = root / 'outside'
            outside.write_text('keep')
            (target / 'text').symlink_to(outside)
            launcher.copy_configuration(source, target)
            self.assertEqual((target / 'text').read_text(), 'runtime data')
            self.assertEqual(outside.read_text(), 'keep')
            self.assertEqual(str((target / 'guest-link').readlink()), '/usr/share/example')

    def test_defaults_are_repeatable_and_preserve_custom_profiles(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            settings = home / '.config'
            settings.mkdir()
            panel = settings / 'plasma-org.kde.plasma.desktop-appletsrc'
            panel.write_text('[Containments][2][General]\nAppletOrder=3;4;5\n')
            defaults.configure(home, settings)
            first = panel.read_text()
            defaults.configure(home, settings)
            self.assertEqual(panel.read_text(), first)
            self.assertIn('org.kde.plasma.systemtray', first)
            profile = settings / 'konsolerc'
            profile.write_text('[Desktop Entry]\nDefaultProfile=Personal.profile\n')
            defaults.configure(home, settings)
            self.assertIn('Personal.profile', profile.read_text())

    def test_locale_uses_kde_choice_and_rejects_shell_text(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = Path(directory)
            path = settings / 'plasma-localerc'
            path.write_text('[Formats]\nLANG=fr_FR.UTF-8\n[Translations]\nLANGUAGE=fr:en_US\n')
            self.assertEqual(defaults.locale(settings)['LANG'], 'fr_FR.UTF-8')
            path.write_text('[Formats]\nLANG=$(bad)\n')
            self.assertEqual(defaults.locale(settings)['LANG'], 'C.UTF-8')


if __name__ == '__main__':
    unittest.main()
