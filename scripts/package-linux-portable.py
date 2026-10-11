#!/usr/bin/env python3
"""Package a built Qt Widgets app with shared libraries; retain system glibc and graphics drivers."""
import argparse, pathlib, subprocess, re, shutil, tarfile, os, json
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--qt',required=True);p.add_argument('--output',required=True);p.add_argument('--version',default='0.2.1-ea.1');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
package=out/f'PhotoDeck-{a.version}-linux-x86_64';package.mkdir(exist_ok=True)
for folder in ['bin','lib','plugins/platforms','plugins/imageformats','plugins/wayland-shell-integration','plugins/wayland-decoration-client','plugins/platforminputcontexts','licenses/system']:(package/folder).mkdir(parents=True,exist_ok=True)
shutil.copy2(a.binary,package/'bin/photodeck');qt=pathlib.Path(a.qt);plugins=qt/'plugins'
files=[package/'bin/photodeck']
for category,names in {'platforms':['libqxcb.so','libqwayland*.so','libqoffscreen.so'],'imageformats':['libqjpeg.so','libqwebp.so','libqgif.so','libqtiff.so'],'wayland-shell-integration':['*.so'],'wayland-decoration-client':['*.so'],'platforminputcontexts':['*.so']}.items():
 for pattern in names:
  for source in (plugins/category).glob(pattern):
   dest=package/'plugins'/category/source.name;shutil.copy2(source,dest);files.append(dest)
# Qt plugin libraries can depend on additional Qt libraries in the SDK directory.
env={**os.environ,'LD_LIBRARY_PATH':str(qt/'lib')+':'+os.environ.get('LD_LIBRARY_PATH','')}
base={'libc.so.6','libm.so.6','libpthread.so.0','libdl.so.2','librt.so.1','libresolv.so.2','ld-linux-x86-64.so.2'}
bundled={};queue=files[:]
while queue:
 file=queue.pop();result=subprocess.run(['ldd',str(file)],env=env,text=True,capture_output=True,check=True)
 for line in result.stdout.splitlines():
  if 'not found' in line:raise RuntimeError('Missing package dependency: '+line)
  match=re.search(r'=>\s+(/\S+)',line)
  if not match:continue
  dep=pathlib.Path(match.group(1));name=dep.name
  if name in base or name in bundled:continue
  shutil.copy2(dep,package/'lib'/name);bundled[name]=str(dep);queue.append(dep)
  # Capture the actual distribution's license text for every system shared library.
  if pathlib.Path('/usr/bin/dpkg-query').exists():
   ownership=subprocess.run(['dpkg-query','-S',str(dep)],text=True,capture_output=True)
   if ownership.returncode==0:
    owner=ownership.stdout.split(': ')[0].split(':')[0];notice=pathlib.Path('/usr/share/doc')/owner/'copyright'
    if notice.exists():shutil.copy2(notice,package/'licenses/system'/f'{owner}.txt')
# Keep fontconfig's parser and configuration at the same version. System/user font directories
# remain discoverable, while generated font caches stay in PhotoDeck's namespace.
font_configuration=pathlib.Path('/etc/fonts')
if font_configuration.exists():
 shutil.copytree(font_configuration,package/'etc/fonts',dirs_exist_ok=True)
 for config in (package/'etc/fonts').rglob('*.conf'):
  text=config.read_text(errors='replace').replace('<cachedir prefix="xdg">fontconfig</cachedir>','<cachedir prefix="xdg">PhotoDeck/fontconfig</cachedir>')
  config.write_text(text)
(package/'bin/qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=plugins\nLibraries=lib\n')
(package/'launch.sh').write_text('''#!/usr/bin/env bash
set -euo pipefail
photodeck_root=$(cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$photodeck_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$photodeck_root/plugins"
export FONTCONFIG_PATH="${FONTCONFIG_PATH:-$photodeck_root/etc/fonts}"
export FONTCONFIG_FILE="${FONTCONFIG_FILE:-$photodeck_root/etc/fonts/fonts.conf}"
if [[ -n "${WAYLAND_DISPLAY:-}" && -z "${QT_QPA_PLATFORM:-}" ]] && ! compgen -G "$photodeck_root/plugins/platforms/libqwayland*.so" >/dev/null; then export QT_QPA_PLATFORM=xcb; fi
exec "$photodeck_root/bin/photodeck" "$@"
''');(package/'launch.sh').chmod(0o755)
for name in ['README.md','EARLY-ACCESS.md','LICENSE']:shutil.copy2(root/name,package/name)
shutil.copy2(root/'packaging/THIRD-PARTY.md',package/'THIRD-PARTY.md');shutil.copytree(root/'packaging/licenses',package/'licenses',dirs_exist_ok=True)
shutil.copytree(root/'examples',package/'examples',dirs_exist_ok=True)
(package/'runtime-manifest.json').write_text(json.dumps({'version':a.version,'qt':str(qt),'libraries':bundled,'glibcBundled':False},indent=2))
archive=out/(package.name+'.tar.gz')
with tarfile.open(archive,'w:gz') as tf:tf.add(package,arcname=package.name)
print(archive)
