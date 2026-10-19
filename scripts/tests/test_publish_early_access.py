"""Local-only regression tests: no network, token, or GitHub writes."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'publish-early-access.py'
spec = importlib.util.spec_from_file_location('publish_early_access', SCRIPT)
publisher = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = publisher
spec.loader.exec_module(publisher)

SHA = 'a' * 40
HEAD = 'b' * 40
TREE = 'c' * 40

RECOVERY_WINDOWS_SKIPS = {
    'acceptsCanonicalDirectoryAliases':
        'Creating native directory symlinks may require Windows developer privileges.',
    'rejectsSymlinksWithoutFollowingThem':
        'QFile::link creates a Windows shortcut, not a native symlink.',
    'preservesForeignOrIncompleteActualOwnership(foreign-machine-matching-boot)':
        'This platform does not expose a boot identity.',
    'preservesForeignOrIncompleteActualOwnership(matching-boot-changed-host-without-machine)':
        'This platform does not expose a boot identity.',
    'localBootIdentityStillRequiresDeadPid(dead-pid)':
        'This platform does not expose a boot identity.',
    'localBootIdentityStillRequiresDeadPid(live-pid)':
        'This platform does not expose a boot identity.',
}


def environment():
    return {'GITHUB_REPOSITORY': publisher.REPOSITORY, 'GITHUB_REF': 'refs/heads/main',
            'GITHUB_SHA': SHA, 'GITHUB_EVENT_NAME': 'workflow_dispatch',
            'REQUESTED_TAG': publisher.TAG}


def event():
    return {'inputs': {'version': publisher.TAG, 'expected_sha': SHA}}


def ci_run():
    return {'id': 42, 'name': 'Build and test', 'path': '.github/workflows/validate.yml',
            'head_sha': SHA, 'event': 'push', 'head_branch': 'main', 'status': 'completed',
            'conclusion': 'success', 'head_repository': {'full_name': publisher.REPOSITORY}}


def junit_xml(count, names=None):
    return (f'<testsuite tests="{count}" failures="0" errors="0" skipped="0">' +
            ''.join(f'<testcase name="{name}"/>' for name in
                    (names if names is not None else [f'test{index}' for index in range(count)])) + '</testsuite>')


def qt_xml(executable, count=3):
    classname = publisher.TEST_CLASSES[executable]
    root = publisher.ET.Element('testsuite', name=classname, tests=str(count),
                                failures='0', errors='0', skipped='0')
    for index in range(count):
        publisher.ET.SubElement(root, 'testcase', name=f'test{index}', classname=classname)
    return publisher.ET.tostring(root, encoding='unicode')


def save_safety_xml(skipped=True):
    # Exact 17-case inventory and skip node observed in publisher run 38743440250.
    names = ['initTestCase', 'nativeSaveAndLoadTrackVersionAndFreshBaseline',
             'independentEditorCannotBeSilentlyOverwritten',
             'hashDetectsSameSizeSameTimestampInPlaceEdit',
             'deletionNeedsExplicitOverwriteAndRecoveryKeepsConflict',
             'snapshotCannotOverwriteSourceEvenWithOptions',
             'recoveredCopyCanReuseSnapshotWithoutAdoptingItAsProject',
             'saveAsEscapesConflictAndTracksNewDestination',
             'canonicalAliasesCannotBypassConflict',
             'symbolicAliasesAndRetargetingAreProtected',
             'legacyOriginalRequiresConsent(v1)', 'legacyOriginalRequiresConsent(v2)',
             'legacyOriginalRequiresConsent(v3)', 'legacyOriginalRequiresConsent(v4)',
             'externalLegacyEditRequiresBothIndependentPermissions',
             'failuresKeepIdentityHistoryAndDirtyState', 'cleanupTestCase']
    root = publisher.ET.Element('testsuite', name='SaveSafetyTests', tests='17',
                                failures='0', errors='0', skipped=str(int(skipped)))
    for name in names:
        case = publisher.ET.SubElement(root, 'testcase', name=name, classname='SaveSafetyTests')
        if name == 'symbolicAliasesAndRetargetingAreProtected' and skipped:
            publisher.ET.SubElement(case, 'skipped', message='This test requires Unix symbolic links.')
    return publisher.ET.tostring(root, encoding='unicode')


def recovery_files_xml(skipped=True):
    names = list(RECOVERY_WINDOWS_SKIPS)
    names += [f'otherRecoveryCase{index}' for index in range(70 - len(names))]
    root = publisher.ET.Element('testsuite', name='RecoveryFilesTests', tests='70',
                                failures='0', errors='0', skipped=str(len(RECOVERY_WINDOWS_SKIPS) if skipped else 0))
    for name in names:
        case = publisher.ET.SubElement(root, 'testcase', name=name, classname='RecoveryFilesTests')
        if skipped and name in RECOVERY_WINDOWS_SKIPS:
            publisher.ET.SubElement(case, 'skipped', message=RECOVERY_WINDOWS_SKIPS[name])
    return publisher.ET.tostring(root, encoding='unicode')


class FakeAPI:
    def __init__(self):
        self.main = SHA
        self.pull = {'number': publisher.PR_NUMBER, 'merged': True, 'merge_commit_sha': SHA,
                     'base': {'ref': 'main', 'repo': {'full_name': publisher.REPOSITORY}},
                     'head': {'sha': HEAD, 'ref': publisher.PR_HEAD_REF,
                              'repo': {'full_name': publisher.REPOSITORY}}}
        self.runs = [ci_run()]
        self.pr_runs = [dict(ci_run(), id=41, event='pull_request', head_sha=HEAD,
                             pull_requests=[{'number': publisher.PR_NUMBER, 'head': {'sha': HEAD}}])]
        self.jobs = [{'name': name, 'status': 'completed', 'conclusion': 'success'}
                     for name in publisher.CI_JOBS]
        self.tag = None
        self.releases = []
        self.remote = []
        self.writes = []
        self.reads = []
        self.bad_upload = False

    def api(self, path, method='GET', data=None, optional=False):
        if method != 'GET':
            self.writes.append((method, path, copy.deepcopy(data)))
        if path == '/git/ref/heads/main':
            return {'object': {'type': 'commit', 'sha': self.main}}
        if path == f'/pulls/{publisher.PR_NUMBER}':
            return copy.deepcopy(self.pull)
        if path == '/git/ref/tags/' + publisher.TAG:
            return None if self.tag is None else {'object': {'type': 'commit', 'sha': self.tag}}
        if path == '/git/refs' and method == 'POST':
            self.tag = data['sha']
            return {'object': {'type': 'commit', 'sha': self.tag}}
        if path == '/releases' and method == 'POST':
            self.releases.append(dict(data, id=7, html_url='https://github.com/example/release'))
            return copy.deepcopy(self.releases[-1])
        if path == '/releases/7':
            release = next(item for item in self.releases if item['id'] == 7)
            if method == 'PATCH':
                release.update(data)
            return copy.deepcopy(release)
        raise AssertionError(f'Unexpected fake API call {method} {path}')

    def pages(self, path, key=None):
        self.reads.append(path)
        if path.startswith('/actions/workflows/validate.yml/runs?'):
            return copy.deepcopy(self.pr_runs if 'event=pull_request' in path else self.runs)
        if re.fullmatch(r'/actions/runs/[0-9]+/jobs\?filter=latest', path):
            return copy.deepcopy(self.jobs)
        if path == '/releases':
            return copy.deepcopy(self.releases)
        if path == '/releases/7/assets':
            return copy.deepcopy(self.remote)
        raise AssertionError('Unexpected fake list ' + path)

    def upload(self, release_id, asset):
        self.writes.append(('UPLOAD', asset.path.name, None))
        self.remote.append({'name': asset.path.name, 'size': asset.size,
                            'state': 'uploaded', 'digest': 'sha256:' +
                            ('0' * 64 if self.bad_upload else asset.sha256)})


def fake_git(*args):
    if args == ('rev-parse', 'HEAD'):
        return SHA
    if args[0] == 'rev-parse' and args[1].endswith('^{tree}'):
        return TREE
    if args[:2] == ('merge-base', '--is-ancestor'):
        return ''
    raise AssertionError('Unexpected git ' + str(args))


class GuardTests(unittest.TestCase):
    def check(self, api=None, env=None, payload=None, git_fn=fake_git):
        with patch.object(publisher, 'verify_source_version'):
            return publisher.verify_guard(api or FakeAPI(), SHA, env or environment(),
                                          payload or event(), git_fn)

    def test_valid_exact_merge(self):
        self.assertEqual(self.check(), '42')

    def test_publisher_is_pinned_to_release_pr_and_reviewed_main(self):
        self.assertEqual(publisher.PR_NUMBER, 6)
        self.assertEqual(publisher.PR_HEAD_REF, 'integration/next-release-qa-fixes')
        self.assertEqual(publisher.TAG, 'v0.10.0-ea.4')
        self.assertEqual(publisher.VERSION, '0.10.0-ea.4')
        self.assertEqual(set(publisher.TEST_CLASSES), publisher.TEST_EXECUTABLES)
        self.assertEqual(len(publisher.TEST_EXECUTABLES), 24)
        self.assertEqual(publisher.TEST_SUITE_COUNT, 24)
        self.assertEqual(publisher.NOTES_PATH, Path('docs/releases/0.10.0-ea.4.md'))
        self.assertEqual(publisher.TEST_CLASSES['photodeck-smart-objects-tests'], 'SmartObjectTests')
        self.assertEqual(publisher.TEST_CLASSES['photodeck-export-dialog-tests'], 'ExportDialogTests')
        self.assertEqual(publisher.BASE_SHA, '580b1896f883acbf6c5591092d5045f60d017826')
        calls = []
        def recording_git(*args):
            calls.append(args)
            return fake_git(*args)
        self.check(git_fn=recording_git)
        ancestry = [args for args in calls if args[:2] == ('merge-base', '--is-ancestor')]
        self.assertEqual(ancestry, [
            ('merge-base', '--is-ancestor', publisher.BASE_SHA, HEAD),
            ('merge-base', '--is-ancestor', publisher.BASE_SHA, SHA),
            ('merge-base', '--is-ancestor', HEAD, SHA),
        ])

    def test_windows_skip_authorization_remains_exactly_scoped(self):
        self.assertEqual(publisher.ALLOWED_SKIPS, {
            ('windows-x64', 'photodeck-save-safety-tests'): {
                'suite': 'SaveSafetyTests', 'tests': 17, 'cases': {
                    'symbolicAliasesAndRetargetingAreProtected':
                        'This test requires Unix symbolic links.',
                },
            },
            ('windows-x64', 'photodeck-recovery-files-tests'): {
                'suite': 'RecoveryFilesTests', 'tests': 70,
                'cases': RECOVERY_WINDOWS_SKIPS,
            },
        })

    def test_repository_metadata_matches_the_authorized_release(self):
        root = SCRIPT.parents[1]
        publisher.verify_source_version(root)
        self.assertTrue((root / publisher.NOTES_PATH).is_file())
        build = (root / '.github/workflows/build.yml').read_text()
        self.assertIn('PHOTO_VERSION: ' + publisher.VERSION, build)
        self.assertIn('PHOTO_TEST_SUITES: 24', build)
        self.assertIn(str(publisher.NOTES_PATH), build)
        workflow = (root / '.github/workflows/publish-0.10.0.yml').read_text()
        self.assertIn("options: ['" + publisher.TAG + "']", workflow)
        self.assertIn("default: '" + publisher.TAG + "'", workflow)
        self.assertIn("REQUESTED_TAG: '" + publisher.TAG + "'", workflow)
        package = (root / 'scripts/package-linux-portable.py').read_text()
        self.assertIn("default='" + publisher.VERSION + "'", package)

    def test_authorized_inventory_keeps_every_qmake_suite(self):
        projects = (SCRIPT.parents[1] / 'tests').glob('*.pro')
        targets = [re.search(r'^TARGET\s*=\s*(\S+)', project.read_text(), re.M).group(1)
                   for project in projects]
        self.assertEqual(len(targets), 24)
        self.assertEqual(set(targets), publisher.TEST_EXECUTABLES)

    def test_full_sha_only(self):
        for value in ('a' * 7, 'A' * 40, 'main', None):
            with self.subTest(value=value), self.assertRaises(publisher.GuardError):
                publisher.full_sha(value)

    def test_reject_wrong_repository_ref_sha_version_and_event(self):
        for key, value in [('GITHUB_REPOSITORY', 'someone/PhotoDeck'),
                           ('GITHUB_REF', 'refs/heads/feature'),
                           ('GITHUB_SHA', HEAD), ('REQUESTED_TAG', 'v0.9.2'),
                           ('GITHUB_EVENT_NAME', 'pull_request')]:
            env = environment()
            env[key] = value
            with self.subTest(key=key), self.assertRaises(publisher.GuardError):
                self.check(env=env)

    def test_reject_manual_input_mismatch(self):
        payload = event()
        payload['inputs']['expected_sha'] = HEAD
        with self.assertRaises(publisher.GuardError):
            self.check(payload=payload)

    def test_prior_ea3_tag_is_rejected_in_both_manual_authorization_fields(self):
        for target in ('environment', 'input', 'both'):
            env, payload, api = environment(), event(), FakeAPI()
            if target in {'environment', 'both'}:
                env['REQUESTED_TAG'] = 'v0.10.0-ea.3'
            if target in {'input', 'both'}:
                payload['inputs']['version'] = 'v0.10.0-ea.3'
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.check(api=api, env=env, payload=payload)
            self.assertFalse(api.writes)

    def test_prior_pr5_identity_and_branch_cannot_authorize_this_release(self):
        for target in ('number', 'branch', 'both'):
            api = FakeAPI()
            if target in {'number', 'both'}:
                api.pull['number'] = 5
            if target in {'branch', 'both'}:
                api.pull['head']['ref'] = 'feature/embedded-smart-objects-20261017'
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.check(api=api)
            self.assertFalse(api.writes)

    def test_workflow_run_identity(self):
        env = environment()
        env['GITHUB_EVENT_NAME'] = 'workflow_run'
        payload = {'workflow_run': ci_run()}
        self.assertEqual(self.check(env=env, payload=payload), '42')
        for field, value in [('event', 'pull_request'), ('conclusion', 'failure'),
                             ('head_sha', HEAD), ('head_branch', 'feature'), ('name', 'Other')]:
            bad = copy.deepcopy(payload)
            bad['workflow_run'][field] = value
            with self.subTest(field=field), self.assertRaises(publisher.GuardError):
                self.check(env=env, payload=bad)

    def test_reject_moved_main_and_wrong_pr_merge(self):
        for target in ('main', 'merge', 'unmerged', 'headrepo', 'headbranch'):
            api = FakeAPI()
            if target == 'main':
                api.main = HEAD
            elif target == 'merge':
                api.pull['merge_commit_sha'] = HEAD
            elif target == 'unmerged':
                api.pull['merged'] = False
            elif target == 'headrepo':
                api.pull['head']['repo']['full_name'] = 'attacker/PhotoDeck'
            else:
                api.pull['head']['ref'] = 'unreviewed-branch'
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.check(api=api)
            self.assertFalse(api.writes)

    def test_reject_missing_failed_or_wrong_sha_ci(self):
        for target in ('missing', 'failed', 'wrongsha', 'skippedjob', 'missingjob'):
            api = FakeAPI()
            if target == 'missing':
                api.runs = []
            elif target == 'failed':
                api.runs[0]['conclusion'] = 'failure'
            elif target == 'wrongsha':
                api.runs[0]['head_sha'] = HEAD
            elif target == 'skippedjob':
                api.jobs[0]['conclusion'] = 'skipped'
            else:
                api.jobs.pop()
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.check(api=api)

    def test_reject_missing_failed_wrong_sha_or_other_pr_head_ci(self):
        for target in ('missing', 'failed', 'wrongsha', 'otherpr', 'priorpr'):
            api = FakeAPI()
            if target == 'missing':
                api.pr_runs = []
            elif target == 'failed':
                api.pr_runs[0]['conclusion'] = 'failure'
            elif target == 'wrongsha':
                api.pr_runs[0]['head_sha'] = SHA
            else:
                api.pr_runs[0]['pull_requests'] = [{'number': 5 if target == 'priorpr' else 1}]
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.check(api=api)
            self.assertFalse(api.writes)

    def test_ci_queries_cannot_hide_newer_non_successful_runs(self):
        api = FakeAPI()
        self.check(api=api)
        queries = [path for path in api.reads if '/validate.yml/runs?' in path]
        self.assertEqual(len(queries), 2)
        self.assertTrue(all('status=' not in path for path in queries))

    def test_latest_pr_and_main_ci_must_succeed_despite_older_green_evidence(self):
        for target in ('pr', 'main'):
            for status, conclusion in [('completed', 'failure'), ('completed', 'cancelled'),
                                       ('completed', 'skipped'), ('in_progress', None),
                                       ('queued', None)]:
                api = FakeAPI()
                runs = api.pr_runs if target == 'pr' else api.runs
                runs.append(dict(runs[0], id=43, status=status, conclusion=conclusion))
                with self.subTest(target=target, status=status, conclusion=conclusion), \
                        self.assertRaisesRegex(publisher.GuardError, 'Latest .* must succeed'):
                    self.check(api=api)
                self.assertFalse(api.writes)

    def test_latest_successful_main_ci_is_selected_and_pinned(self):
        api = FakeAPI()
        api.runs.append(dict(ci_run(), id=43))
        self.assertEqual(self.check(api=api), '43')
        self.assertIn('/actions/runs/43/jobs?filter=latest', api.reads)
        env = dict(environment(), EXPECTED_CI_RUN_ID='42')
        with self.assertRaisesRegex(publisher.GuardError, 'not the latest'):
            self.check(api=api, env=env)
        env['EXPECTED_CI_RUN_ID'] = '43'
        self.assertEqual(self.check(api=api, env=env), '43')

    def test_workflow_run_trigger_cannot_pin_older_green_main_ci(self):
        api = FakeAPI()
        api.runs.append(dict(ci_run(), id=43))
        env = dict(environment(), GITHUB_EVENT_NAME='workflow_run')
        with self.assertRaisesRegex(publisher.GuardError, 'not the latest'):
            self.check(api=api, env=env, payload={'workflow_run': ci_run()})

    def test_latest_successful_final_pr_ci_is_selected(self):
        api = FakeAPI()
        api.pr_runs.append(dict(api.pr_runs[0], id=43))
        self.check(api=api)
        self.assertIn('/actions/runs/43/jobs?filter=latest', api.reads)

    def test_newer_unrelated_ci_does_not_change_latest_relevant_validation(self):
        identities = [('name', 'Other workflow'), ('path', '.github/workflows/other.yml'),
                      ('head_sha', 'd' * 40),
                      ('head_repository', {'full_name': 'someone/PhotoDeck'})]
        for target in ('pr', 'main'):
            extras = [('pull_requests', [{'number': 5}])] if target == 'pr' else [
                ('event', 'pull_request'), ('head_branch', 'other-branch')]
            for field, value in [*identities, *extras]:
                api = FakeAPI()
                runs = api.pr_runs if target == 'pr' else api.runs
                newer = dict(runs[0], id=43, conclusion='failure')
                newer[field] = value
                runs.append(newer)
                with self.subTest(target=target, field=field):
                    self.assertEqual(self.check(api=api), '42')
                self.assertNotIn('/actions/runs/43/jobs?filter=latest', api.reads)

    def test_reject_missing_ancestry_different_tree_or_checkout(self):
        ancestry_edges = [(publisher.BASE_SHA, HEAD), (publisher.BASE_SHA, SHA), (HEAD, SHA)]
        for target in [*ancestry_edges, 'tree', 'checkout']:
            api = FakeAPI()
            def invalid_git(*args):
                if isinstance(target, tuple) and args == ('merge-base', '--is-ancestor', *target):
                    raise publisher.GuardError('Not an ancestor')
                if target == 'tree' and args == ('rev-parse', HEAD + '^{tree}'):
                    return 'd' * 40
                if target == 'checkout' and args == ('rev-parse', 'HEAD'):
                    return HEAD
                return fake_git(*args)
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.check(api=api, git_fn=invalid_git)
            self.assertFalse(api.writes)

    def test_reject_wrong_source_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'src').mkdir()
            (root / 'CMakeLists.txt').write_text('project(PhotoDeck VERSION 0.10.0 LANGUAGES CXX)')
            for version in ('0.9.0-ea.1', '0.10.0-ea.3'):
                (root / 'src/version.h').write_text(f'inline const char *Version = "{version}";')
                with self.subTest(version=version), self.assertRaises(publisher.GuardError):
                    publisher.verify_source_version(root)


class APITransportTests(unittest.TestCase):
    def test_redirects_are_rejected_before_authentication_can_cross_hosts(self):
        handler = publisher.NoRedirect()
        with self.assertRaisesRegex(publisher.GuardError, 'redirect refused'):
            handler.redirect_request(None, None, 302, 'redirect', {},
                                     'https://untrusted.example/asset')


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.artifacts = self.root / 'artifacts'
        self.artifacts.mkdir()
        self.output = self.root / 'out'
        for label, names in publisher.PLATFORMS.items():
            folder = self.artifacts / ('PhotoDeck-' + label)
            (folder / 'dist').mkdir(parents=True)
            lines = []
            for name in names:
                path = folder / 'dist' / name
                path.write_bytes(('package:' + name).encode())
                lines.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + name)
            (folder / 'dist/SHA256SUMS.txt').write_text('\n'.join(lines) + '\n')
            platform = 'Linux' if label == 'linux-x86_64' else 'Windows' if label == 'windows-x64' else 'macOS'
            (folder / 'release-source.json').write_text(json.dumps({
                'version': publisher.VERSION, 'source_commit': SHA, 'platform': platform}))
            (folder / 'package-version.txt').write_text('PhotoDeck ' + publisher.VERSION + '\n')
            (folder / 'packaged-test-list.txt').write_text('\n'.join(sorted(publisher.TEST_EXECUTABLES)) + '\n')
            (folder / 'packaged-test-count.txt').write_text('24\n')
            (folder / 'build-release').mkdir()
            (folder / 'build-release/tests.xml').write_text(junit_xml(24, sorted(publisher.CTEST_NAMES)))
            for name in publisher.TEST_EXECUTABLES:
                (folder / 'build-release' / (name + '-native.xml')).write_text(qt_xml(name))
            if label in {'linux-x86_64', 'windows-x64'}:
                for name in publisher.TEST_EXECUTABLES:
                    (folder / (name + '-packaged.xml')).write_text(qt_xml(name))
            startup = 'linux' if label == 'linux-x86_64' else 'windows' if label == 'windows-x64' else 'macos'
            (folder / (startup + '-startup.png')).write_bytes(b'fake-startup-image')
            if label == 'linux-x86_64':
                (folder / 'appimage-startup.png').write_bytes(b'fake-startup-image')
            notes = folder / publisher.NOTES_PATH
            notes.parent.mkdir(parents=True)
            notes.write_bytes(publisher.NOTES_PATH.read_bytes())

    def tearDown(self):
        self.temp.cleanup()

    def verify(self):
        return publisher.verify_artifacts(self.artifacts, self.output, SHA,
                                          ci_run_id='42', package_run_id='99')

    def test_complete_five_packages_release_proof_and_combined_checksum(self):
        assets = self.verify()
        self.assertEqual(len(assets), 7)
        self.assertEqual(len((self.output / 'SHA256SUMS.txt').read_text().splitlines()), 6)
        self.assertIn('RELEASE-PROOF.json', assets)
        proof = json.loads(assets['RELEASE-PROOF.json'].path.read_text())
        self.assertEqual(proof['source_commit'], SHA)
        self.assertEqual(proof['source_ci_run_id'], '42')
        self.assertEqual(proof['package_run_id'], '99')
        self.assertEqual(set(proof['packages']), {name for names in publisher.PLATFORMS.values() for name in names})
        self.assertEqual(len(proof['test_executables']), 24)
        for label, result in proof['platforms'].items():
            self.assertEqual(result['native_ctest']['executed'], 24)
            self.assertEqual(len(result['native_qt']), 24)
            expected = 24 if label in {'linux-x86_64', 'windows-x64'} else 0
            self.assertEqual(len(result['bundled_runtime']), expected)

    def test_reject_missing_granular_native_qt_proof_on_every_platform(self):
        for label in publisher.PLATFORMS:
            proof = self.artifacts / ('PhotoDeck-' + label) / 'build-release/photodeck-recovery-files-tests-native.xml'
            content = proof.read_text()
            proof.unlink()
            with self.subTest(platform=label), self.assertRaisesRegex(publisher.GuardError, 'Incomplete native QtTest'):
                self.verify()
            proof.write_text(content)

    def test_granular_native_results_record_only_the_explicit_windows_skips(self):
        folder = self.artifacts / 'PhotoDeck-windows-x64/build-release'
        (folder / 'photodeck-save-safety-tests-native.xml').write_text(save_safety_xml())
        (folder / 'photodeck-recovery-files-tests-native.xml').write_text(recovery_files_xml())
        assets = self.verify()
        proof = json.loads(assets['RELEASE-PROOF.json'].path.read_text())
        windows = proof['platforms']['windows-x64']['native_qt']
        self.assertEqual(sum(suite['skipped'] for suite in windows.values()), 7)
        linux = self.artifacts / 'PhotoDeck-linux-x86_64/build-release/photodeck-recovery-files-tests-native.xml'
        linux.write_text(recovery_files_xml())
        with self.assertRaisesRegex(publisher.GuardError, 'Unexpected JUnit skip'):
            self.verify()

    def test_reject_copied_native_report_from_another_suite(self):
        folder = self.artifacts / 'PhotoDeck-macos-intel/build-release'
        (folder / 'photodeck-recovery-files-tests-native.xml').write_text(qt_xml('photodeck-tests'))
        with self.assertRaisesRegex(publisher.GuardError, 'suite identity'):
            self.verify()

    def test_reject_duplicate_or_substituted_native_suite_inventory(self):
        proof = self.artifacts / 'PhotoDeck-windows-x64/build-release/tests.xml'
        names = sorted(publisher.CTEST_NAMES)
        names[-1] = names[0]
        proof.write_text(junit_xml(24, names))
        with self.assertRaises(publisher.GuardError):
            self.verify()
        names[-1] = 'unrelated-test-suite'
        proof.write_text(junit_xml(24, names))
        with self.assertRaisesRegex(publisher.GuardError, 'suite inventory'):
            self.verify()

    def test_reject_incomplete_twenty_four_suite_inventory(self):
        folder = self.artifacts / 'PhotoDeck-windows-x64'
        (folder / 'packaged-test-count.txt').write_text('14\n')
        with self.assertRaisesRegex(publisher.GuardError, '24-suite inventory'):
            self.verify()

    def test_release_proof_reports_all_seven_explicit_windows_skips(self):
        folder = self.artifacts / 'PhotoDeck-windows-x64'
        (folder / 'photodeck-save-safety-tests-packaged.xml').write_text(save_safety_xml())
        (folder / 'photodeck-recovery-files-tests-packaged.xml').write_text(recovery_files_xml())
        assets = self.verify()
        proof = json.loads(assets['RELEASE-PROOF.json'].path.read_text())
        windows = proof['platforms']['windows-x64']['bundled_runtime']
        self.assertEqual(sum(suite['skipped'] for suite in windows.values()), 7)
        self.assertEqual(windows['photodeck-recovery-files-tests']['testcases'], 70)
        self.assertEqual(windows['photodeck-recovery-files-tests']['executed'], 64)
        self.assertEqual(len(windows['photodeck-recovery-files-tests']['skipped_cases']), 6)
        checksums = publisher.parse_checksums((self.output / 'SHA256SUMS.txt').read_text(),
                                             set(assets) - {'SHA256SUMS.txt'})
        self.assertEqual(checksums['RELEASE-PROOF.json'], assets['RELEASE-PROOF.json'].sha256)

    def test_missing_or_unexpected_package_and_checksum_mismatch(self):
        folder = self.artifacts / 'PhotoDeck-windows-x64/dist'
        package = folder / publisher.PLATFORMS['windows-x64'][0]
        package.write_bytes(b'modified')
        with self.assertRaisesRegex(publisher.GuardError, 'checksum mismatch'):
            self.verify()
        package.unlink()
        with self.assertRaisesRegex(publisher.GuardError, 'file set'):
            self.verify()

    def test_reject_missing_platform(self):
        (self.artifacts / 'unexpected-platform').mkdir()
        with self.assertRaises(publisher.GuardError):
            self.verify()

    def test_reject_wrong_package_version_and_provenance(self):
        folder = self.artifacts / 'PhotoDeck-windows-x64'
        (folder / 'package-version.txt').write_text('PhotoDeck 0.9.0-ea.1\n')
        with self.assertRaisesRegex(publisher.GuardError, 'version mismatch'):
            self.verify()
        (folder / 'package-version.txt').write_text('PhotoDeck ' + publisher.VERSION)
        proof = json.loads((folder / 'release-source.json').read_text())
        proof['source_commit'] = HEAD
        (folder / 'release-source.json').write_text(json.dumps(proof))
        with self.assertRaisesRegex(publisher.GuardError, 'provenance mismatch'):
            self.verify()

    def test_reject_incomplete_inventory_or_packaged_results(self):
        folder = self.artifacts / 'PhotoDeck-windows-x64'
        proof = folder / 'photodeck-ui-resilience-tests-packaged.xml'
        proof.unlink()
        with self.assertRaisesRegex(publisher.GuardError, 'Incomplete packaged-runtime'):
            self.verify()
        proof.write_text('<testsuite tests="3" failures="1"><failure/></testsuite>')
        with self.assertRaisesRegex(publisher.GuardError, 'Failing JUnit'):
            self.verify()

    def test_reject_skipped_disabled_or_non_run_junit(self):
        proof = self.artifacts / 'PhotoDeck-windows-x64/build-release/tests.xml'
        for value in ('<testsuite tests="24" skipped="1"/>',
                      '<testsuite tests="24" disabled="1"/>',
                      '<testsuite tests="24"><testcase><skipped/></testcase></testsuite>',
                      '<testsuite tests="24"><testcase status="notrun"/></testsuite>'):
            proof.write_text(value)
            with self.subTest(value=value), self.assertRaises(publisher.GuardError):
                self.verify()

    def test_windows_notes_crlf_checkout_is_accepted(self):
        notes = self.artifacts / 'PhotoDeck-windows-x64' / publisher.NOTES_PATH
        source = publisher.NOTES_PATH.read_bytes()
        self.assertNotIn(b'\r', source)
        notes.write_bytes(source.replace(b'\n', b'\r\n'))
        self.assertEqual(len(self.verify()), 7)

    def test_notes_content_bom_bare_cr_and_whitespace_changes_are_rejected(self):
        notes = self.artifacts / 'PhotoDeck-windows-x64' / publisher.NOTES_PATH
        source = publisher.NOTES_PATH.read_bytes()
        variants = {'content': source.replace(b'PhotoDeck', b'OtherApp', 1),
                    'bom': b'\xef\xbb\xbf' + source,
                    'barecr': source.replace(b'\n', b'\r', 1),
                    'space': source + b' ', 'removednewline': source.rstrip(b'\n')}
        for target, content in variants.items():
            notes.write_bytes(content)
            with self.subTest(target=target), self.assertRaisesRegex(publisher.GuardError, 'notes differ'):
                self.verify()

    def test_artifact_context_accepts_only_known_windows_bundled_skip(self):
        proof = self.artifacts / 'PhotoDeck-windows-x64/photodeck-save-safety-tests-packaged.xml'
        proof.write_text(save_safety_xml())
        self.assertEqual(len(self.verify()), 7)
        linux_proof = self.artifacts / 'PhotoDeck-linux-x86_64/photodeck-save-safety-tests-packaged.xml'
        linux_proof.write_text(save_safety_xml())
        with self.assertRaisesRegex(publisher.GuardError, 'Unexpected JUnit skip'):
            self.verify()

    def test_checksum_parser_rejects_duplicates_paths_and_wrong_set(self):
        digest = 'a' * 64
        for value in (digest + '  ../file.zip', digest + '  file.zip\n' + digest + '  file.zip',
                      digest + '  other.zip', 'not a checksum'):
            with self.subTest(value=value), self.assertRaises(publisher.GuardError):
                publisher.parse_checksums(value, {'file.zip'})


class JUnitSkipTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'photodeck-save-safety-tests-packaged.xml'

    def tearDown(self):
        self.temp.cleanup()

    def verify(self, xml=None, platform='windows-x64', executable='photodeck-save-safety-tests'):
        self.path.write_text(save_safety_xml() if xml is None else xml)
        return publisher.verify_junit(self.path, platform=platform, executable=executable)

    def test_exact_observed_windows_skip_is_accepted(self):
        self.verify()

    def test_linux_same_inventory_without_skip_is_accepted(self):
        self.verify(save_safety_xml(skipped=False), platform='linux-x86_64')

    def test_skip_is_rejected_on_other_platforms_executables_and_native_ctest(self):
        for platform, executable in [('linux-x86_64', 'photodeck-save-safety-tests'),
                                     ('macos-intel', 'photodeck-save-safety-tests'),
                                     ('macos-apple-silicon', 'photodeck-save-safety-tests'),
                                     ('windows-x64', 'photodeck-parser-safety-tests'), (None, None)]:
            with self.subTest(platform=platform, executable=executable), self.assertRaises(publisher.GuardError):
                self.verify(platform=platform, executable=executable)

    def test_unexpected_skip_identity_reason_attributes_or_suite_is_rejected(self):
        for target in ('test', 'class', 'suite', 'reason', 'type', 'text', 'nested'):
            root = publisher.ET.fromstring(save_safety_xml())
            case = root.find("testcase[@name='symbolicAliasesAndRetargetingAreProtected']")
            skipped = case.find('skipped')
            if target == 'test':
                case.set('name', 'differentTest')
            elif target == 'class':
                case.set('classname', 'OtherTests')
            elif target == 'suite':
                root.set('name', 'OtherTests')
            elif target == 'reason':
                skipped.set('message', 'Different reason')
            elif target == 'type':
                skipped.set('type', 'skip')
            elif target == 'text':
                skipped.text = 'unexpected text'
            else:
                publisher.ET.SubElement(skipped, 'skipped')
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.verify(publisher.ET.tostring(root, encoding='unicode'))

    def test_unexpected_extra_or_duplicate_skip_is_rejected(self):
        for target in ('extra', 'duplicate'):
            root = publisher.ET.fromstring(save_safety_xml())
            root.set('skipped', '2')
            if target == 'extra':
                publisher.ET.SubElement(root.find('testcase'), 'skipped', message='Unexpected')
            else:
                case = root.find("testcase[@name='symbolicAliasesAndRetargetingAreProtected']")
                publisher.ET.SubElement(case, 'skipped', message='This test requires Unix symbolic links.')
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.verify(publisher.ET.tostring(root, encoding='unicode'))

    def test_failure_error_disabled_non_run_and_count_contradictions_are_rejected(self):
        for target in ('failure', 'error', 'failures', 'errors', 'disabled', 'status',
                       'skipped0', 'skipped2', 'tests16', 'missingcase', 'duplicatecase',
                       'orphan', 'negative', 'aggregate'):
            root = publisher.ET.fromstring(save_safety_xml())
            if target in {'failure', 'error'}:
                publisher.ET.SubElement(root.find('testcase'), target)
            elif target in {'failures', 'errors', 'disabled'}:
                root.set(target, '1')
            elif target == 'status':
                root.find('testcase').set('status', 'notrun')
            elif target.startswith('skipped'):
                root.set('skipped', target[-1])
            elif target == 'tests16':
                root.set('tests', '16')
            elif target == 'missingcase':
                root.remove(root.find('testcase'))
            elif target == 'duplicatecase':
                cases = root.findall('testcase')
                cases[1].set('name', cases[0].get('name'))
            elif target == 'orphan':
                publisher.ET.SubElement(root, 'skipped', message='This test requires Unix symbolic links.')
            elif target == 'negative':
                root.set('tests', '-17')
            else:
                wrapper = publisher.ET.Element('testsuites', tests='18', skipped='1')
                wrapper.append(root)
                root = wrapper
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.verify(publisher.ET.tostring(root, encoding='unicode'))


class RecoveryFilesSkipTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'photodeck-recovery-files-tests-packaged.xml'

    def tearDown(self):
        self.temp.cleanup()

    def verify(self, xml=None, platform='windows-x64', executable='photodeck-recovery-files-tests'):
        self.path.write_text(recovery_files_xml() if xml is None else xml)
        return publisher.verify_junit(self.path, platform=platform, executable=executable)

    def test_exact_six_windows_recovery_exceptions_are_accepted_and_reported(self):
        self.assertEqual(
            publisher.ALLOWED_SKIPS[('windows-x64', 'photodeck-recovery-files-tests')],
            {'suite': 'RecoveryFilesTests', 'tests': 70, 'cases': RECOVERY_WINDOWS_SKIPS})
        proof = self.verify()
        self.assertEqual(proof['testcases'], 70)
        self.assertEqual(proof['executed'], 64)
        self.assertEqual(proof['skipped'], 6)

    def test_linux_full_recovery_inventory_without_skips_is_accepted(self):
        self.assertEqual(self.verify(recovery_files_xml(False), platform='linux-x86_64')['executed'], 70)

    def test_reject_windows_skips_on_other_targets(self):
        for platform, executable in [('linux-x86_64', 'photodeck-recovery-files-tests'),
                                     ('macos-intel', 'photodeck-recovery-files-tests'),
                                     ('macos-apple-silicon', 'photodeck-recovery-files-tests'),
                                     ('windows-x64', 'photodeck-recovery-tests'), (None, None)]:
            with self.subTest(platform=platform, executable=executable), self.assertRaises(publisher.GuardError):
                self.verify(platform=platform, executable=executable)

    def test_reject_extra_conditional_skip_and_changed_context(self):
        for target in ('machine-identity', 'hard-link', 'case', 'reason', 'count', 'class', 'suite'):
            root = publisher.ET.fromstring(recovery_files_xml())
            case = root.find('testcase')
            skipped = case.find('skipped')
            if target in {'machine-identity', 'hard-link'}:
                case = root.find("testcase[@name='otherRecoveryCase0']")
                case.set('name', 'matchingMachineAndChangedHostnameStillRequireDeadPid'
                         if target == 'machine-identity' else 'rejectsHardLinkedSameFile')
                publisher.ET.SubElement(case, 'skipped', message=
                    'This platform does not expose a machine identity.' if target == 'machine-identity'
                    else 'The temporary filesystem does not support creating hard links.')
                root.set('skipped', '7')
            elif target == 'case':
                case.set('name', 'unrelatedCase')
            elif target == 'reason':
                skipped.set('message', 'Different reason')
            elif target == 'count':
                root.set('tests', '71')
                publisher.ET.SubElement(root, 'testcase', name='extra', classname='RecoveryFilesTests')
            elif target == 'class':
                case.set('classname', 'OtherTests')
            else:
                root.set('name', 'OtherTests')
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.verify(publisher.ET.tostring(root, encoding='unicode'))


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.assets = {}
        self.package_name = publisher.PLATFORMS['windows-x64'][0]
        for name in sorted(publisher.RELEASE_ASSET_NAMES):
            path = root / name
            path.write_text(name)
            self.assets[name] = publisher.inspect_asset(path)
        self.api = FakeAPI()
        self.guards = 0
        self.body = publisher.release_body('Reviewed notes', SHA, '42', '99', self.assets)

    def tearDown(self):
        self.temp.cleanup()

    def reguard(self):
        self.guards += 1

    def publish(self):
        return publisher.publish(self.api, SHA, self.assets, self.body, self.reguard)

    def existing(self, draft=True):
        self.api.tag = SHA
        self.api.releases = [{'id': 7, 'tag_name': publisher.TAG,
                              'target_commitish': SHA, 'name': 'PhotoDeck ' + publisher.VERSION,
                              'body': self.body, 'draft': draft, 'prerelease': True,
                              'html_url': 'https://github.com/example/release'}]

    def test_rejects_incomplete_release_asset_set_before_any_write(self):
        self.assets.pop('RELEASE-PROOF.json')
        with self.assertRaisesRegex(publisher.GuardError, 'complete five packages'):
            self.publish()
        self.assertFalse(self.api.writes)

    def test_prior_ea3_packages_cannot_be_published_as_ea4(self):
        assets = {name.replace('0.10.0-ea.4', '0.10.0-ea.3'): asset
                  for name, asset in self.assets.items()}
        with self.assertRaisesRegex(publisher.GuardError, 'complete five packages'):
            publisher.publish(self.api, SHA, assets, self.body, self.reguard)
        self.assertFalse(self.api.writes)

    def test_previous_ea3_publication_remains_untouched(self):
        previous = {'id': 5, 'tag_name': 'v0.10.0-ea.3', 'draft': False,
                    'target_commitish': publisher.BASE_SHA}
        self.api.releases = [copy.deepcopy(previous)]
        self.publish()
        self.assertEqual(self.api.releases[0], previous)
        self.assertEqual(self.api.releases[1]['tag_name'], 'v0.10.0-ea.4')
        self.assertFalse(self.api.releases[1]['draft'])
        self.assertFalse(any(write[1] == '/releases/5' for write in self.api.writes))

    def test_release_body_describes_exact_proof_scope(self):
        self.assertIn('exact 24-suite native CTest', self.body)
        self.assertIn('Linux and Windows additionally reran all 24 suites', self.body)
        self.assertIn('RELEASE-PROOF.json', self.body)
        self.assertIn('/pull/6', self.body)
        self.assertNotIn('/pull/5', self.body)

    def test_creates_draft_uploads_verified_set_then_publishes(self):
        result = self.publish()
        self.assertFalse(result['draft'])
        self.assertTrue(result['prerelease'])
        self.assertEqual(self.guards, 2)
        self.assertEqual(self.api.writes[0][1], '/git/refs')
        self.assertEqual(self.api.writes[1][2]['draft'], True)
        self.assertEqual(self.api.writes[-1][0], 'PATCH')
        self.assertEqual(set(item['name'] for item in self.api.remote), set(self.assets))

    def test_rejects_wrong_existing_tag_without_any_write(self):
        self.api.tag = HEAD
        with self.assertRaisesRegex(publisher.GuardError, 'never moved'):
            self.publish()
        self.assertFalse(self.api.writes)

    def test_rejects_unrelated_existing_draft(self):
        self.existing()
        self.api.releases[0]['body'] = 'unrelated'
        with self.assertRaisesRegex(publisher.GuardError, 'never overwritten'):
            self.publish()
        self.assertFalse(self.api.writes)

    def test_draft_resume_uploads_only_missing_assets(self):
        self.existing()
        first = self.assets[self.package_name]
        self.api.upload(7, first)
        self.api.writes.clear()
        self.publish()
        self.assertEqual({write[1] for write in self.api.writes if write[0] == 'UPLOAD'},
                         set(self.assets) - {self.package_name})

    def test_same_published_release_is_read_only_idempotent(self):
        self.publish()
        self.api.writes.clear()
        self.publish()
        self.assertFalse(self.api.writes)

    def test_rejects_changed_or_unrelated_assets_without_overwrite(self):
        for target in ('digest', 'size', 'unrelated'):
            api = FakeAPI()
            self.api = api
            self.existing()
            api.upload(7, self.assets[self.package_name])
            api.writes.clear()
            if target == 'digest':
                api.remote[0]['digest'] = 'sha256:' + '0' * 64
            elif target == 'size':
                api.remote[0]['size'] += 1
            else:
                api.remote[0]['name'] = 'unrelated.txt'
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.publish()
            self.assertFalse(api.writes)

    def test_bad_server_digest_leaves_draft_unpublished(self):
        self.api.bad_upload = True
        with self.assertRaisesRegex(publisher.GuardError, 'SHA256 mismatch'):
            self.publish()
        self.assertTrue(self.api.releases[0]['draft'])
        self.assertFalse(any(write[0] == 'PATCH' for write in self.api.writes))

    def test_main_moving_after_packaging_prevents_first_write(self):
        def fail_guard():
            raise publisher.GuardError('main moved')
        with self.assertRaises(publisher.GuardError):
            publisher.publish(self.api, SHA, self.assets, self.body, fail_guard)
        self.assertFalse(self.api.writes)

    def test_main_moving_during_upload_leaves_draft(self):
        def fail_second_guard():
            self.guards += 1
            if self.guards == 2:
                raise publisher.GuardError('main moved')
        with self.assertRaises(publisher.GuardError):
            publisher.publish(self.api, SHA, self.assets, self.body, fail_second_guard)
        self.assertTrue(self.api.releases[0]['draft'])
        self.assertFalse(any(write[0] == 'PATCH' for write in self.api.writes))

    def test_final_tree_or_latest_ci_regression_during_upload_leaves_draft(self):
        for target in ('tree', 'prci', 'mainci'):
            self.api = FakeAPI()
            self.guards = 0
            def recheck_guard():
                self.guards += 1
                if self.guards == 2:
                    if target == 'prci':
                        self.api.pr_runs.append(dict(self.api.pr_runs[0], id=43,
                                                     conclusion='failure'))
                    elif target == 'mainci':
                        self.api.runs.append(dict(ci_run(), id=43, status='in_progress',
                                                 conclusion=None))
                def current_git(*args):
                    if self.guards == 2 and target == 'tree' and args == (
                            'rev-parse', HEAD + '^{tree}'):
                        return 'd' * 40
                    return fake_git(*args)
                with patch.object(publisher, 'verify_source_version'):
                    publisher.verify_guard(self.api, SHA, environment(), event(), current_git)
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                publisher.publish(self.api, SHA, self.assets, self.body, recheck_guard)
            self.assertTrue(self.api.releases[0]['draft'])
            self.assertFalse(any(write[0] == 'PATCH' for write in self.api.writes))


if __name__ == '__main__':
    unittest.main()
