#!/usr/bin/env python3
"""Build auditable native Rust release archives (Python standard library only)."""
import argparse
import hashlib
from functools import lru_cache
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import zipfile

TARGETS = {'linux-x86_64': 'x86_64-unknown-linux-gnu', 'windows-x64': 'x86_64-pc-windows-msvc', 'macos-intel': 'x86_64-apple-darwin', 'macos-apple-silicon': 'aarch64-apple-darwin'}
ROOT = Path(__file__).resolve().parents[1]

def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, text=True, capture_output=True, **kwargs)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def safe(value):
    if not re.fullmatch(r'[A-Za-z0-9_.+-]+', value):
        raise ValueError('Unsafe archive/version component: ' + value)
    return value

def license_files(directory):
    # Include nested vendor notices and every text file in font directories.
    return sorted(p for p in directory.rglob('*') if p.is_file() and (
        re.search(r'license|licence|copying|copyright|notice', p.name, re.I)
        or p.name.upper() in ('OFL.TXT', 'UFL.TXT')
        or ('fonts' in p.parts and p.suffix.lower() in ('.txt', '.md'))
        or ('vendor' in p.parts and p.suffix.lower() in ('.c', '.h') and b'copyright' in p.read_bytes().lower())))

def upstream_licenses(package, directory):
    """Retrieve omitted workspace licenses from the crate's recorded exact revision."""
    vcs = directory / '.cargo_vcs_info.json'
    repository_url = package.get('repository') or ''
    match = re.fullmatch(r'https?://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)(?:/tree/[A-Za-z0-9_./-]+)?/?', repository_url)
    repository = ('https://github.com/' + match[1] + '/' + match[2].removesuffix('.git')) if match else ''
    if not vcs.exists() or not re.fullmatch(r'https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('No verifiable license source for ' + package['name'])
    info = json.loads(vcs.read_text())
    revision = info['git']['sha1']
    if not re.fullmatch('[0-9a-f]{40}', revision):
        raise ValueError('Invalid crate source revision')
    exceptional = {('hexf-parse', '0.2.1'): 'CC0-1.0', ('dispatch', '0.2.0'): 'MIT', ('r-efi', '5.3.0'): 'MIT', ('r-efi', '6.0.0'): 'MIT'}
    chosen = exceptional.get((package['name'], package['version']))
    if chosen:
        if chosen not in (package.get('license') or ''):
            raise ValueError('Audited license exception no longer matches declaration')
        url = 'https://raw.githubusercontent.com/spdx/license-list-data/31846481de7ea17d92f2f46fad76abe58b2e9bb0/text/' + chosen + '.txt'
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read(4 * 1024 * 1024 + 1)
        if not data.strip() or len(data) > 4 * 1024 * 1024:
            raise ValueError('Invalid canonical license')
        result = [(chosen + '.txt', data, url), ('Cargo.toml', (directory / 'Cargo.toml').read_bytes(), 'crate:Cargo.toml')]
        if (directory / 'Cargo.toml.orig').is_file():
            result.append(('Cargo.toml.orig', (directory / 'Cargo.toml.orig').read_bytes(), 'crate:Cargo.toml.orig'))
        if package['name'] == 'r-efi':
            url = 'https://raw.githubusercontent.com/' + repository.removeprefix('https://github.com/') + '/' + revision + '/AUTHORS'
            with urllib.request.urlopen(url, timeout=30) as response:
                authors = response.read(4 * 1024 * 1024 + 1)
            if not authors.strip() or len(authors) > 4 * 1024 * 1024:
                raise ValueError('Missing r-efi attribution')
            result.append(('AUTHORS', authors, url))
        return result
    return fetch_licenses(repository, revision)

@lru_cache(maxsize=None)
def fetch_licenses(repository, revision):
    base = 'https://raw.githubusercontent.com/' + repository.removeprefix('https://github.com/') + '/' + revision + '/'
    result = []
    for filename in ('LICENSE', 'LICENSE-MIT', 'LICENSE-APACHE', 'LICENSE.txt', 'LICENSE.md', 'LICENSE-MIT.txt', 'LICENSE-APACHE2.txt', 'LICENSE-CC0', 'COPYING', 'COPYRIGHT', 'UNLICENSE'):
        url = base + filename
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                data = response.read(4 * 1024 * 1024 + 1)
            if len(data) > 4 * 1024 * 1024:
                raise ValueError('Upstream license exceeds size limit: ' + url)
            if not data.strip() or b'<!DOCTYPE html' in data[:100]:
                raise ValueError('Invalid upstream license: ' + url)
            result.append((filename, data, url))
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise
    if not result:
        raise ValueError('Missing actual license text for ' + repository + '@' + revision)
    return result

def collect_licenses(metadata, destination):
    inventory = []
    for package in sorted(metadata['packages'], key=lambda p: (p['name'], p['version'])):
        if package['id'] == metadata['resolve']['root']:
            continue
        directory = Path(package['manifest_path']).parent.resolve()
        target = destination / safe(package['name'] + '-' + package['version'])
        files = license_files(directory)
        declared = package.get('license_file')
        if declared:
            path = (directory / declared).resolve()
            if not path.is_relative_to(directory) or not path.is_file():
                raise ValueError('Invalid declared license-file: ' + str(path))
            if path not in files:
                files.append(path)
        records = []
        for path in files:
            if not path.resolve().is_relative_to(directory):
                raise ValueError('License symlink escapes crate: ' + str(path))
            relative = path.relative_to(directory)
            out = target / relative
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, out)
            records.append({'path': out.relative_to(destination.parent).as_posix(), 'sha256': sha(out), 'source': 'crate:' + relative.as_posix()})
        # Font-only notices do not cover the crate's own MIT/Apache code.
        if not files or (package['name'] == 'epaint_default_fonts' and not any(p.parent == directory for p in files)):
            for filename, data, source in upstream_licenses(package, directory):
                out = target / 'upstream' / filename
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(data)
                records.append({'path': out.relative_to(destination.parent).as_posix(), 'sha256': sha(out), 'source': source})
        if not records:
            raise ValueError('License inventory is empty: ' + package['name'])
        inventory.append({'name': package['name'], 'version': package['version'], 'license': package.get('license'), 'files': records})
    return inventory

def make_archive(stage, archive, platform):
    if platform.startswith('linux'):
        with tarfile.open(archive, 'w:gz') as stream:
            stream.add(stage, arcname=stage.name)
    elif sys.platform == 'darwin':
        run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', stage, archive])
    else:
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as stream:
            for path in sorted(stage.rglob('*')):
                if path.is_file():
                    stream.write(path, (Path(stage.name) / path.relative_to(stage)).as_posix())

def package(root, metadata, platform, source_sha, binary, output, smoke=False):
    if not re.fullmatch('[0-9a-fA-F]{40}', source_sha):
        raise ValueError('--source-sha must be an exact 40-character Git SHA')
    main = next(p for p in metadata['packages'] if p['id'] == metadata['resolve']['root'])
    version = safe(main['version'])
    name = 'PhotoDeck-' + version + '-' + platform
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='photodeck-package-') as work:
        stage = Path(work) / name
        stage.mkdir()
        shutil.copyfile(root / 'LICENSE', stage / 'LICENSE')
        shutil.copyfile(root / 'README.md', stage / 'README.md')
        (stage / 'examples').mkdir()
        shutil.copyfile(root / 'examples/Horizon.pdeck', stage / 'examples/Horizon.pdeck')
        if platform.startswith('macos'):
            app = stage / 'PhotoDeck.app' / 'Contents'
            executable = app / 'MacOS/photodeck'
            executable.parent.mkdir(parents=True)
            (app / 'Resources').mkdir()
            shutil.copyfile(root / 'packaging/photodeck.icns', app / 'Resources/photodeck.icns')
            with (app / 'Info.plist').open('wb') as stream:
                plistlib.dump({'CFBundleExecutable': 'photodeck', 'CFBundleIdentifier': 'io.github.meingless.photodeck', 'CFBundleName': 'PhotoDeck', 'CFBundlePackageType': 'APPL', 'CFBundleIconFile': 'photodeck.icns', 'CFBundleShortVersionString': version.split('-')[0], 'CFBundleVersion': version.split('-')[0], 'PhotoDeckReleaseVersion': version, 'LSMinimumSystemVersion': '12.0', 'NSHighResolutionCapable': True}, stream)
        else:
            executable = stage / ('photodeck.exe' if platform == 'windows-x64' else 'photodeck')
        shutil.copyfile(binary, executable)
        executable.chmod(0o755)
        if platform.startswith('linux'):
            (stage / 'launch.sh').write_text('#!/usr/bin/env sh\nset -eu\nphotodeck_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$photodeck_root/photodeck" "$@"\n')
            (stage / 'launch.sh').chmod(0o755)
            (stage / 'scripts').mkdir()
            shutil.copyfile(root / 'scripts/install-user.sh', stage / 'scripts/install-user.sh')
            (stage / 'scripts/install-user.sh').chmod(0o755)
            (stage / 'packaging').mkdir()
            for icon in ('photodeck.svg', 'photodeck.png'):
                shutil.copyfile(root / 'packaging' / icon, stage / 'packaging' / icon)
        for document in ('EARLY-ACCESS.md', 'packaging/THIRD-PARTY.md', 'packaging/RUST-THIRD-PARTY.md', 'docs/RUST-MIGRATION.md', 'docs/releases/' + version + '.md'):
            source = root / document
            if source.is_file():
                (stage / document).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, stage / document)
        inventory = collect_licenses(metadata, stage / 'THIRD-PARTY-LICENSES')
        if platform.startswith('macos'):
            shutil.copytree(stage / 'THIRD-PARTY-LICENSES', app / 'Resources/THIRD-PARTY-LICENSES')
            shutil.copyfile(root / 'LICENSE', app / 'Resources/LICENSE')
        signing = 'not applicable'
        if platform.startswith('macos'):
            if sys.platform != 'darwin' or not shutil.which('codesign'):
                raise ValueError('macOS packages require native codesign')
            run(['codesign', '--force', '--deep', '--sign', '-', stage / 'PhotoDeck.app'])
            run(['codesign', '--verify', '--deep', '--strict', stage / 'PhotoDeck.app'])
            signing = 'ad-hoc; codesign --verify --deep --strict passed'
        manifest = {'version': version, 'sourceSha': source_sha.lower(), 'platform': platform, 'target': TARGETS[platform], 'binarySha256': sha(executable), 'licenses': inventory, 'signing': signing, 'releaseBaseline': 'Ubuntu 24.04' if platform.startswith('linux') else ('macOS 12.0' if platform.startswith('macos') else 'Windows runner native build')}
        (stage / 'PACKAGE-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
        archive = output / (name + ('.tar.gz' if platform.startswith('linux') else '.zip'))
        make_archive(stage, archive, platform)
        if smoke:
            extracted = Path(work) / 'extracted'
            extracted.mkdir()
            if archive.suffix == '.zip':
                if sys.platform == 'darwin':
                    run(['ditto', '-x', '-k', archive, extracted])
                else:
                    with zipfile.ZipFile(archive) as stream:
                        stream.extractall(extracted)
            else:
                with tarfile.open(archive) as stream:
                    stream.extractall(extracted, filter='data')
            tested = extracted / name / executable.relative_to(stage)
            result = run([tested, '--version'])
            if version not in result.stdout:
                raise ValueError('Extracted binary version mismatch: ' + result.stdout)
            demo = extracted / 'smoke-demo.pdeck'
            run([tested, '--write-demo', demo])
            if not demo.is_file() or not demo.stat().st_size:
                raise ValueError('Extracted binary did not create demo')
            (output / (name + '-smoke.json')).write_text(json.dumps({'platform': platform, 'archiveSha256': sha(archive), 'versionOutput': result.stdout.strip(), 'checks': ['extracted --version', 'extracted --write-demo'], 'guiVerified': False}, indent=2) + '\n')
        return archive

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=TARGETS, required=True)
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    actual_sha = run(['git', 'rev-parse', 'HEAD'], cwd=ROOT).stdout.strip()
    if args.source_sha.lower() != actual_sha:
        raise ValueError('--source-sha differs from checkout HEAD')
    host = next(line.split(': ', 1)[1] for line in run(['rustc', '-vV']).stdout.splitlines() if line.startswith('host: '))
    if host != TARGETS[args.platform]:
        raise ValueError('Native packaging target mismatch: ' + host + ' vs ' + TARGETS[args.platform])
    metadata = json.loads(run(['cargo', 'metadata', '--format-version', '1', '--locked', '--offline'], cwd=ROOT).stdout)
    binary = args.binary or ROOT / 'target/release' / ('photodeck.exe' if args.platform == 'windows-x64' else 'photodeck')
    print(package(ROOT, metadata, args.platform, args.source_sha, binary, args.output.resolve(), args.smoke))

if __name__ == '__main__':
    main()
