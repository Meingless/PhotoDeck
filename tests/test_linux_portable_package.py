"""Offline inventory and desktop-install checks for the Linux portable archive."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BASH = shutil.which('bash')


@unittest.skipUnless(os.name == 'posix' and BASH, 'POSIX and bash are required')
class LinuxPortablePackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        tools = self.root / 'tools'
        tools.mkdir()
        # This is an archive-layout test, with no host Qt or ELF dependencies.
        ldd = tools / 'ldd'
        ldd.write_text('#!/bin/sh\nexit 0\n')
        ldd.chmod(0o755)
        binary = self.root / 'photodeck'
        binary.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
        binary.chmod(0o755)
        qt = self.root / 'Qt'
        (qt / 'plugins').mkdir(parents=True)
        self.output = self.root / 'dist'
        self.package_name = 'PhotoDeck-test-linux-x86_64'
        self.environment = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ['PATH'])
        result = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/package-linux-portable.py'),
             '--binary', str(binary), '--qt', str(qt), '--output', str(self.output),
             '--version', 'test'],
            env=self.environment, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.archive = self.output / (self.package_name + '.tar.gz')

    def unpack(self, destination):
        with tarfile.open(self.archive, 'r:gz') as archive:
            # Only this test's freshly generated package is extracted.
            archive.extractall(destination, filter='data')
        return destination / self.package_name

    def test_archive_contains_the_advertised_executable_installer_and_icon(self):
        with tarfile.open(self.archive, 'r:gz') as archive:
            installer = archive.getmember(self.package_name + '/scripts/install-user.sh')
            self.assertTrue(installer.mode & 0o111)
            icon = archive.extractfile(self.package_name + '/packaging/photodeck.svg').read()
        self.assertEqual(icon, (ROOT / 'packaging/photodeck.svg').read_bytes())

    def test_extracted_installer_only_writes_the_selected_xdg_directory(self):
        package = self.unpack(self.root / 'extracted package % with spaces')
        home = self.root / 'temporary home'
        home.mkdir()
        data = self.root / 'temporary XDG data'
        environment = dict(self.environment, HOME=str(home), XDG_DATA_HOME=str(data))
        result = subprocess.run(
            [BASH, str(package / 'scripts/install-user.sh')], env=environment,
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        # Qt uses this exact basename to associate windows with their launcher.
        main = (ROOT / 'src/main.cpp').read_text()
        desktop_name = re.search(r'setDesktopFileName\("([^"]+)"\)', main)
        self.assertIsNotNone(desktop_name, 'Application must identify its desktop entry')
        desktop = (data / 'applications' / (desktop_name.group(1) + '.desktop')).read_text()
        escaped = str(package).replace('%', '%%')
        self.assertIn(f'Exec="{escaped}/launch.sh" %F\n', desktop)
        self.assertIn('Icon=photodeck\n', desktop)
        self.assertEqual((data / 'icons/hicolor/scalable/apps/photodeck.svg').read_bytes(),
                         (ROOT / 'packaging/photodeck.svg').read_bytes())
        self.assertEqual(list(home.iterdir()), [])

    def test_extracted_launcher_preserves_arguments(self):
        package = self.unpack(self.root / 'extracted package')
        result = subprocess.run(
            [str(package / 'launch.sh'), '--no-settings', 'image with spaces.png'],
            env=self.environment, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.splitlines(), ['--no-settings', 'image with spaces.png'])


if __name__ == '__main__':
    unittest.main()
