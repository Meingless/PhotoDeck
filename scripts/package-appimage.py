#!/usr/bin/env python3
"""Wrap an already deployed Linux portable package in an AppImage."""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--appimagetool', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    package = args.package.resolve()
    tool = args.appimagetool.resolve()
    output = args.output.resolve()
    for entry in ['launch.sh', 'bin/photodeck', 'lib', 'plugins']:
        if not (package / entry).exists():
            parser.error(f'Missing portable package entry: {entry}')
    if output.exists():
        parser.error(f'Output already exists: {output}')
    output.parent.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='photodeck-appimage-') as temp:
        appdir = Path(temp) / 'PhotoDeck.AppDir'
        shutil.copytree(package, appdir, symlinks=True)
        (appdir / 'AppRun').write_text(
            '#!/usr/bin/env bash\nset -euo pipefail\n'
            'photodeck_appdir=$(cd -- "$(dirname -- "$0")" && pwd)\n'
            'exec "$photodeck_appdir/launch.sh" "$@"\n'
        )
        (appdir / 'AppRun').chmod(0o755)
        (appdir / 'photodeck.desktop').write_text(
            '[Desktop Entry]\nType=Application\nName=PhotoDeck\n'
            'Comment=Local photo and layer editor\nExec=photodeck %F\n'
            'Icon=photodeck\nTerminal=false\nCategories=Graphics;2DGraphics;RasterGraphics;\n'
            'MimeType=image/png;image/jpeg;image/bmp;image/webp;\n'
        )
        shutil.copy2(root / 'packaging/photodeck.png', appdir / 'photodeck.png')
        (appdir / 'licenses').mkdir(exist_ok=True)
        shutil.copy2(root / 'packaging/licenses/AppImage-runtime.txt',
                     appdir / 'licenses/AppImage-runtime.txt')
        (appdir / '.DirIcon').symlink_to('photodeck.png')
        # Some hosts expose many CPUs but insufficient memory for parallel squashfs.
        subprocess.run(
            [str(tool), '--no-appstream', '--mksquashfs-opt', '-processors',
             '--mksquashfs-opt', '2', str(appdir), str(output)],
            env={**os.environ, 'ARCH': 'x86_64'}, check=True,
        )
    output.chmod(0o755)
    print(output)


if __name__ == '__main__':
    main()
