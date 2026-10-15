"""Local-only regression tests: no network, token, or GitHub writes."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
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


class FakeAPI:
    def __init__(self):
        self.main = SHA
        self.pull = {'number': 1, 'merged': True, 'merge_commit_sha': SHA,
                     'base': {'ref': 'main', 'repo': {'full_name': publisher.REPOSITORY}},
                     'head': {'sha': HEAD, 'repo': {'full_name': publisher.REPOSITORY}}}
        self.runs = [ci_run()]
        self.pr_runs = [dict(ci_run(), id=41, event='pull_request', head_sha=HEAD,
                             pull_requests=[{'number': 1, 'head': {'sha': HEAD}}])]
        self.jobs = [{'name': name, 'status': 'completed', 'conclusion': 'success'}
                     for name in publisher.CI_JOBS]
        self.tag = None
        self.releases = []
        self.remote = []
        self.writes = []
        self.bad_upload = False

    def api(self, path, method='GET', data=None, optional=False):
        if method != 'GET':
            self.writes.append((method, path, copy.deepcopy(data)))
        if path == '/git/ref/heads/main':
            return {'object': {'type': 'commit', 'sha': self.main}}
        if path == '/pulls/1':
            return copy.deepcopy(self.pull)
        if path == '/git/ref/tags/' + publisher.TAG:
            return None if self.tag is None else {'object': {'type': 'commit', 'sha': self.tag}}
        if path == '/git/refs' and method == 'POST':
            self.tag = data['sha']
            return {'object': {'type': 'commit', 'sha': self.tag}}
        if path == '/releases' and method == 'POST':
            self.releases = [dict(data, id=7, html_url='https://github.com/example/release')]
            return copy.deepcopy(self.releases[0])
        if path == '/releases/7':
            if method == 'PATCH':
                self.releases[0].update(data)
            return copy.deepcopy(self.releases[0])
        raise AssertionError(f'Unexpected fake API call {method} {path}')

    def pages(self, path, key=None):
        if path.startswith('/actions/workflows/validate.yml/runs?'):
            return copy.deepcopy(self.pr_runs if 'event=pull_request' in path else self.runs)
        if path in {'/actions/runs/42/jobs?filter=latest', '/actions/runs/41/jobs?filter=latest'}:
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
        for target in ('main', 'merge', 'unmerged', 'headrepo'):
            api = FakeAPI()
            if target == 'main':
                api.main = HEAD
            elif target == 'merge':
                api.pull['merge_commit_sha'] = HEAD
            elif target == 'unmerged':
                api.pull['merged'] = False
            else:
                api.pull['head']['repo']['full_name'] = 'attacker/PhotoDeck'
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
        for target in ('missing', 'failed', 'wrongsha', 'otherpr'):
            api = FakeAPI()
            if target == 'missing':
                api.pr_runs = []
            elif target == 'failed':
                api.pr_runs[0]['conclusion'] = 'failure'
            elif target == 'wrongsha':
                api.pr_runs[0]['head_sha'] = SHA
            else:
                api.pr_runs[0]['pull_requests'] = [{'number': 2}]
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.check(api=api)
            self.assertFalse(api.writes)

    def test_reject_missing_ancestry_different_tree_or_checkout(self):
        for target in ('ancestry', 'tree', 'checkout'):
            def invalid_git(*args):
                if target == 'ancestry' and args[0] == 'merge-base':
                    raise publisher.GuardError('Not an ancestor')
                if target == 'tree' and args == ('rev-parse', HEAD + '^{tree}'):
                    return 'd' * 40
                if target == 'checkout' and args == ('rev-parse', 'HEAD'):
                    return HEAD
                return fake_git(*args)
            with self.subTest(target=target), self.assertRaises(publisher.GuardError):
                self.check(git_fn=invalid_git)

    def test_reject_wrong_source_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'src').mkdir()
            (root / 'src/version.h').write_text('inline const char *Version = "0.9.0-ea.1";')
            (root / 'CMakeLists.txt').write_text('project(PhotoDeck VERSION 0.9.1 LANGUAGES CXX)')
            with self.assertRaises(publisher.GuardError):
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
            (folder / 'packaged-test-count.txt').write_text('14\n')
            (folder / 'build-release').mkdir()
            (folder / 'build-release/tests.xml').write_text('<testsuite tests="14" failures="0" errors="0"/>')
            if label in {'linux-x86_64', 'windows-x64'}:
                for name in publisher.TEST_EXECUTABLES:
                    (folder / (name + '-packaged.xml')).write_text('<testsuite tests="3" failures="0"/>')
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
        return publisher.verify_artifacts(self.artifacts, self.output, SHA)

    def test_complete_five_packages_and_combined_checksum(self):
        assets = self.verify()
        self.assertEqual(len(assets), 6)
        self.assertEqual(len((self.output / 'SHA256SUMS.txt').read_text().splitlines()), 5)

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
        for value in ('<testsuite tests="14" skipped="1"/>',
                      '<testsuite tests="14" disabled="1"/>',
                      '<testsuite tests="14"><testcase><skipped/></testcase></testsuite>',
                      '<testsuite tests="14"><testcase status="notrun"/></testsuite>'):
            proof.write_text(value)
            with self.subTest(value=value), self.assertRaises(publisher.GuardError):
                self.verify()

    def test_checksum_parser_rejects_duplicates_paths_and_wrong_set(self):
        digest = 'a' * 64
        for value in (digest + '  ../file.zip', digest + '  file.zip\n' + digest + '  file.zip',
                      digest + '  other.zip', 'not a checksum'):
            with self.subTest(value=value), self.assertRaises(publisher.GuardError):
                publisher.parse_checksums(value, {'file.zip'})


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.assets = {}
        for name in ('package.zip', 'SHA256SUMS.txt'):
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
        first = self.assets['package.zip']
        self.api.upload(7, first)
        self.api.writes.clear()
        self.publish()
        self.assertEqual([write[1] for write in self.api.writes if write[0] == 'UPLOAD'], ['SHA256SUMS.txt'])

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
            api.upload(7, self.assets['package.zip'])
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


if __name__ == '__main__':
    unittest.main()
