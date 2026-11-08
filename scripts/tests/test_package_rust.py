import importlib.util
import json
import struct
import sys
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('package_rust', Path(__file__).parents[1] / 'package-rust.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

def pe_fixture(name='KERNEL32.dll', delayed=False):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 1)
    struct.pack_into('<H', data, 0x94, 240)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b)
    struct.pack_into('<I', data, optional + 60, 512)
    struct.pack_into('<I', data, optional + 108, 16)
    directory = 13 if delayed else 1
    struct.pack_into('<II', data, optional + 112 + 8 * directory, 0x1000, 64 if delayed else 40)
    section = optional + 240
    data[section:section + 8] = b'.rdata\0\0'
    struct.pack_into('<IIII', data, section + 8, 512, 0x1000, 512, 512)
    if delayed:
        struct.pack_into('<II', data, 512, 1, 0x1080)
    else:
        struct.pack_into('<I', data, 524, 0x1080)
    encoded = name.encode('ascii') + b'\0'
    data[640:640 + len(encoded)] = encoded
    return bytes(data)

class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('LICENSE', 'README.md', 'examples/Horizon.pdeck', 'scripts/install-user.sh', 'packaging/photodeck.svg', 'packaging/photodeck.png'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture ' + name)
        self.binary = self.root / 'binary'
        self.binary.write_bytes(b'fixture native binary')
        self.crate = self.root / 'crate'
        self.crate.mkdir()
        (self.crate / 'LICENSE-MIT').write_text('Copyright actual license fixture')
        (self.crate / 'vendor').mkdir()
        (self.crate / 'vendor/COPYING').write_text('Vendor copyright fixture')
        (self.crate / 'vendor/lcms.h').write_text('// Copyright vendor; actual MIT permission notice fixture')
        (self.crate / 'fonts').mkdir()
        (self.crate / 'fonts/OFL.txt').write_text('Font license fixture')
        self.metadata = {'resolve': {'root': 'root'}, 'packages': [
            {'id': 'root', 'name': 'photodeck', 'version': '0.11.0-ea.1'},
            {'id': 'dep', 'name': 'dep', 'version': '1.0.0', 'manifest_path': str(self.crate / 'Cargo.toml'), 'license': 'MIT'}]}

    def build(self, platform):
        if platform == 'windows-x64' and self.binary.read_bytes() == b'fixture native binary':
            self.binary.write_bytes(pe_fixture())
        return mod.package(self.root, self.metadata, platform, 'a' * 40, self.binary, self.root / 'dist')

    def test_linux_layout_and_manifest(self):
        archive = self.build('linux-x86_64')
        with tarfile.open(archive) as stream:
            members = {m.name.split('/', 1)[1]: m for m in stream.getmembers() if '/' in m.name}
            self.assertIn('scripts/install-user.sh', members)
            self.assertIn('THIRD-PARTY-LICENSES/dep-1.0.0/vendor/COPYING', members)
            self.assertIn('THIRD-PARTY-LICENSES/dep-1.0.0/fonts/OFL.txt', members)
            self.assertTrue(members['photodeck'].mode & 0o111)
            launcher = stream.extractfile(members['launch.sh']).read().decode()
            self.assertIn('exec "$photodeck_root/photodeck" "$@"', launcher)
            manifest = json.load(stream.extractfile(members['PACKAGE-MANIFEST.json']))
            self.assertEqual(manifest['version'], '0.11.0-ea.1')
            self.assertEqual(manifest['sourceSha'], 'a' * 40)
            self.assertEqual(manifest['binarySha256'], mod.sha(self.binary))
            self.assertEqual(len(manifest['licenses'][0]['files']), 4)

    def test_windows_no_linux_launcher(self):
        with zipfile.ZipFile(self.build('windows-x64')) as stream:
            names = [n.split('/', 1)[1] for n in stream.namelist()]
            self.assertIn('photodeck.exe', names)
            self.assertNotIn('launch.sh', names)
            self.assertNotIn('scripts/install-user.sh', names)
            manifest = json.loads(stream.read(next(n for n in stream.namelist() if n.endswith('/PACKAGE-MANIFEST.json'))))
            self.assertEqual(manifest['windowsImports'], ['KERNEL32.dll'])

    def test_dynamic_msvc_runtime_rejected(self):
        for delayed in (False, True):
            for name in ('VCRUNTIME140.dll', 'MSVCP140.dll', 'CONCRT140.dll'):
                self.binary.write_bytes(pe_fixture(name, delayed))
                with self.assertRaisesRegex(ValueError, 'private MSVC runtime'):
                    self.build('windows-x64')

    def test_malformed_pe_rejected(self):
        for mutation in ('signature', 'machine', 'optional', 'rva', 'name', 'unterminated', 'truncated'):
            data = bytearray(pe_fixture())
            if mutation == 'signature': data[0] = 0
            elif mutation == 'machine': struct.pack_into('<H', data, 0x84, 0x14c)
            elif mutation == 'optional': struct.pack_into('<H', data, 0x98, 0x10b)
            elif mutation == 'rva': struct.pack_into('<I', data, 524, 0x9000)
            elif mutation == 'name': data[640] = 0xff
            elif mutation == 'unterminated': data[640:896] = b'A' * 256
            else: data = data[:530]
            self.binary.write_bytes(data)
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                mod.windows_imports(self.binary)

    def test_subprocess_decodes_utf8(self):
        result = mod.run([sys.executable, '-c', "import sys; sys.stdout.buffer.write('PhotoDeck · Rust'.encode('utf-8'))"])
        self.assertEqual(result.stdout, 'PhotoDeck · Rust')

    def test_missing_license_fails_closed(self):
        for path in self.crate.rglob('*'):
            if path.is_file(): path.unlink()
        with self.assertRaisesRegex(ValueError, 'verifiable license source'):
            self.build('windows-x64')

    def test_declared_license_escape_rejected(self):
        self.metadata['packages'][1]['license_file'] = '../LICENSE'
        with self.assertRaisesRegex(ValueError, 'Invalid declared'):
            self.build('windows-x64')

    def test_version_and_sha_are_path_safe(self):
        self.metadata['packages'][0]['version'] = '../escape'
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            self.build('windows-x64')
        with self.assertRaisesRegex(ValueError, 'exact'):
            mod.package(self.root, self.metadata, 'windows-x64', 'main', self.binary, self.root / 'dist')

    def test_pinned_upstream_source_and_cache(self):
        (self.crate / '.cargo_vcs_info.json').write_text(json.dumps({'git': {'sha1': 'b' * 40}}))
        package = {'name': 'omitted', 'version': '1.0.0', 'repository': 'https://github.com/org/repo.git'}
        with patch.object(mod, 'fetch_licenses', return_value=[('LICENSE', b'actual text', 'pinned')]) as fetch:
            self.assertTrue(mod.upstream_licenses(package, self.crate))
            fetch.assert_called_once_with('https://github.com/org/repo', 'b' * 40)
        package['repository'] = 'https://github.com.evil/org/repo'
        with self.assertRaisesRegex(ValueError, 'verifiable'):
            mod.upstream_licenses(package, self.crate)

if __name__ == '__main__': unittest.main()
