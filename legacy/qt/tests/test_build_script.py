"""Offline standard-library checks for build.sh's qmake test inventory."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BASH = shutil.which('bash')
STUB = r'''
import json
import os
from pathlib import Path
import re
import sys

tool = Path(sys.argv[0]).name
if tool in ('qmake6', 'qmake'):
    stage = 'qmake'
    project = Path(sys.argv[1])
    if not project.is_file():
        sys.exit('Missing project: ' + str(project))
    Path('.stub-project').write_text(str(project))
else:
    stage = 'make' if tool == 'make' else 'test'
    project = Path('.stub-project').read_text()
    project = Path(project)
event = {'stage': stage, 'project': project.name, 'tool': tool,
         'args': sys.argv[1:], 'platform': os.environ.get('QT_QPA_PLATFORM')}
with open(os.environ['STUB_LOG'], 'a') as log:
    log.write(json.dumps(event) + '\n')
if (stage, project.name) == (os.environ.get('FAIL_STAGE'), os.environ.get('FAIL_PROJECT')):
    sys.exit(37)
if stage == 'make':
    target = re.search(r'^TARGET\s*=\s*(\S+)', project.read_text(), re.M).group(1)
    binary = Path('bin/photodeck') if project.name == 'PhotoDeck.pro' else Path(target)
    binary.parent.mkdir(parents=True, exist_ok=True)
    if not binary.exists():
        binary.symlink_to(os.environ['STUB_TOOL'])
'''


# Executable symlinks and shebang stubs require POSIX, even if Bash exists elsewhere.
@unittest.skipUnless(os.name == 'posix' and BASH, 'POSIX and bash are required')
class QmakeBuildTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / 'project with spaces'
        (self.root / 'scripts').mkdir(parents=True)
        (self.root / 'tests').mkdir()
        shutil.copyfile(ROOT / 'scripts/build.sh', self.root / 'scripts/build.sh')
        shutil.copyfile(ROOT / 'PhotoDeck.pro', self.root / 'PhotoDeck.pro')
        self.projects = sorted((ROOT / 'tests').glob('*.pro'))
        self.assertTrue(self.projects, 'No qmake test projects found')
        for project in self.projects:
            shutil.copyfile(project, self.root / 'tests' / project.name)
        self.tools = Path(temporary.name) / 'tools'
        self.tools.mkdir()
        self.stub = self.tools / 'stub'
        self.stub.write_text(f'#!{sys.executable}\n' + STUB)
        self.stub.chmod(0o755)
        for tool in ('qmake6', 'make'):
            (self.tools / tool).symlink_to(self.stub)
        # Deliberately exclude cmake and strip, even when installed on the host.
        for tool in ('dirname', 'mkdir', 'cp'):
            (self.tools / tool).symlink_to(shutil.which(tool))
        self.log = Path(temporary.name) / 'events.jsonl'
        self.installed = self.root / 'bin/photodeck'
        self.expected = [('qmake', 'PhotoDeck.pro'), ('make', 'PhotoDeck.pro')]
        self.expected += [(stage, project.name) for project in self.projects
                          for stage in ('qmake', 'make', 'test')]

    def run_script(self, stage='', project=''):
        self.log.write_text('')
        self.installed.unlink(missing_ok=True)
        environment = dict(os.environ, PATH=str(self.tools), LC_ALL='C',
                           STUB_LOG=str(self.log), STUB_TOOL=str(self.stub),
                           FAIL_STAGE=stage, FAIL_PROJECT=project)
        result = subprocess.run([BASH, str(self.root / 'scripts/build.sh'),
                                 str(self.root / 'app build')],
                                env=environment, capture_output=True, text=True, timeout=30)
        events = [json.loads(line) for line in self.log.read_text().splitlines()]
        return result, events

    def check_complete_inventory(self, qmake):
        result, events = self.run_script()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([(event['stage'], event['project']) for event in events], self.expected)
        targets = {project.name: re.search(r'^TARGET\s*=\s*(\S+)', project.read_text(), re.M).group(1)
                   for project in self.projects}
        for event in events:
            if event['stage'] == 'qmake':
                self.assertEqual(event['tool'], qmake)
                folder = 'tests' if event['project'] != 'PhotoDeck.pro' else ''
                self.assertEqual(event['args'], [str(self.root / folder / event['project'])])
            elif event['stage'] == 'make':
                self.assertEqual(event['args'], ['-j4'])
            else:
                self.assertEqual(event['tool'], targets[event['project']])
                self.assertEqual(event['args'], [])
                self.assertEqual(event['platform'], 'offscreen')
        self.assertTrue(self.installed.is_file(), 'Successful build must install the app')

    def test_qmake6_builds_and_runs_every_project_once(self):
        self.check_complete_inventory('qmake6')

    def test_legacy_qmake_fallback_keeps_the_same_inventory(self):
        (self.tools / 'qmake6').rename(self.tools / 'qmake')
        self.check_complete_inventory('qmake')

    def test_failure_stops_the_build_before_later_suites_or_installation(self):
        # Exercise every stage, including an app build failure and a test near the end.
        failures = [('qmake', 'PhotoDeck.pro'), ('make', 'PhotoDeck.pro')]
        failures += [(stage, self.projects[-2].name) for stage in ('qmake', 'make', 'test')]
        for stage, project in failures:
            with self.subTest(stage=stage, project=project):
                result, events = self.run_script(stage, project)
                self.assertEqual(result.returncode, 37, result.stdout + result.stderr)
                stop = self.expected.index((stage, project)) + 1
                self.assertEqual([(event['stage'], event['project']) for event in events],
                                 self.expected[:stop])
                self.assertFalse(self.installed.exists(), 'Failed builds must not install the app')


if __name__ == '__main__':
    unittest.main()
