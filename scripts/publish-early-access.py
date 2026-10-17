#!/usr/bin/env python3
"""Fail-closed publisher for reviewed PhotoDeck release PR #4 and v0.10.0-ea.2 only.

Uses only the job's ephemeral token and GitHub's official HTTPS API. There is no
force, delete, asset replacement, arbitrary-version, or credential-storage path.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import HTTPRedirectHandler, Request, build_opener
import xml.etree.ElementTree as ET

REPOSITORY = 'Meingless/PhotoDeck'
TAG = 'v0.10.0-ea.2'
VERSION = TAG[1:]
# Fixed release authorization. Update PR_NUMBER only to the assigned release PR;
# no workflow input can change the approved repository, ancestry, branch or tag.
BASE_SHA = '49ff11fb17b2ad72d1d035917e02f4739b37d8d5'
PR_NUMBER = 4
PR_HEAD_REF = 'release/0.10.0-ea.2'
NOTES_PATH = Path('docs/releases/0.10.0-ea.2.md')
PLATFORMS = {
    'linux-x86_64': (f'PhotoDeck-{VERSION}-linux-x86_64.tar.gz',
                     f'PhotoDeck-{VERSION}-x86_64.AppImage'),
    'windows-x64': (f'PhotoDeck-{VERSION}-windows-x64.zip',),
    'macos-intel': (f'PhotoDeck-{VERSION}-macos-intel.zip',),
    'macos-apple-silicon': (f'PhotoDeck-{VERSION}-macos-apple-silicon.zip',),
}
RELEASE_ASSET_NAMES = frozenset(
    name for names in PLATFORMS.values() for name in names) | {'RELEASE-PROOF.json', 'SHA256SUMS.txt'}
TEST_EXECUTABLES = frozenset({
    'photodeck-tests', 'photodeck-feature-audit', 'photodeck-tones-tests',
    'photodeck-selections-tests', 'photodeck-blending-tests',
    'photodeck-transforms-tests', 'photodeck-layer-ops-tests',
    'photodeck-effects-tests', 'photodeck-output-tests',
    'photodeck-brushes-tests', 'photodeck-presets-tests',
    'photodeck-save-safety-tests', 'photodeck-parser-safety-tests',
    'photodeck-ui-resilience-tests', 'photodeck-layer-drag-tests',
    'photodeck-selection-workflow-tests', 'photodeck-positioning-tests',
    'photodeck-history-navigation-tests', 'photodeck-recovery-tests',
    'photodeck-recovery-files-tests', 'photodeck-recovery-codec-tests',
    'photodeck-usability-tests',
})
TEST_SUITE_COUNT = 22
TEST_CLASSES = {
    'photodeck-tests': 'PhotoDeckTests',
    'photodeck-feature-audit': 'FeatureAudit',
    'photodeck-tones-tests': 'ToneTests',
    'photodeck-selections-tests': 'SelectionTests',
    'photodeck-blending-tests': 'BlendTests',
    'photodeck-transforms-tests': 'TransformTests',
    'photodeck-layer-ops-tests': 'LayerOpsTests',
    'photodeck-layer-drag-tests': 'LayerDragTests',
    'photodeck-effects-tests': 'EffectTests',
    'photodeck-output-tests': 'OutputTests',
    'photodeck-brushes-tests': 'BrushTests',
    'photodeck-presets-tests': 'PresetTests',
    'photodeck-save-safety-tests': 'SaveSafetyTests',
    'photodeck-parser-safety-tests': 'ParserSafetyTests',
    'photodeck-ui-resilience-tests': 'UiResilienceTests',
    'photodeck-selection-workflow-tests': 'SelectionWorkflowTests',
    'photodeck-positioning-tests': 'PositioningTests',
    'photodeck-history-navigation-tests': 'HistoryNavigationTests',
    'photodeck-recovery-tests': 'RecoveryTests',
    'photodeck-recovery-files-tests': 'RecoveryFilesTests',
    'photodeck-recovery-codec-tests': 'RecoveryCodecTests',
    'photodeck-usability-tests': 'UsabilityTests',
}
CTEST_NAMES = frozenset(
    'photodeck-functional' if name == 'photodeck-tests' else
    name.removesuffix('-tests') for name in TEST_EXECUTABLES)
# Explicit source-declared Windows exceptions. Qt 6.8.3 bootUniqueId() is
# implemented only on Linux and Apple targets (qtbase qsysinfo.cpp); the four
# boot-identity rows therefore skip on Windows. Other conditional skips remain
# blocked until exact platform CI evidence is reviewed.
ALLOWED_SKIPS = {
    ('windows-x64', 'photodeck-save-safety-tests'): {
        'suite': 'SaveSafetyTests', 'tests': 17, 'cases': {
            'symbolicAliasesAndRetargetingAreProtected': 'This test requires Unix symbolic links.',
        },
    },
    ('windows-x64', 'photodeck-recovery-files-tests'): {
        'suite': 'RecoveryFilesTests', 'tests': 70, 'cases': {
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
        },
    },
}
CI_JOBS = frozenset({f'{label} / Qt 6.8.3' for label in PLATFORMS} |
                    {'Linux / ASan + UBSan'})


class GuardError(RuntimeError):
    """A safety condition has not been satisfied; do not continue publishing."""


def require(condition, message):
    if not condition:
        raise GuardError(message)


def full_sha(value):
    require(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{40}', value),
            'An exact lowercase 40-character commit SHA is required')
    return value


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        raise GuardError('GitHub API redirect refused; authentication stays on the fixed API host')


class GitHub:
    """Small fixed-host API client. Authentication is never printed or persisted."""
    def __init__(self):
        self.token = os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
        require(bool(self.token), 'The ephemeral job token is required')
        self.prefix = f'/repos/{REPOSITORY}'
        self.opener = build_opener(NoRedirect)

    def api(self, path, method='GET', data=None, optional=False):
        require(path.startswith('/'), 'Only fixed GitHub API paths are allowed')
        encoded = None if data is None else json.dumps(data).encode('utf-8')
        request = Request('https://api.github.com' + self.prefix + path,
                          data=encoded, method=method, headers={
                              'Authorization': 'Bearer ' + self.token,
                              'Accept': 'application/vnd.github+json',
                              'Content-Type': 'application/json',
                              'X-GitHub-Api-Version': '2022-11-28',
                              'User-Agent': 'PhotoDeck-one-release-publisher',
                          })
        try:
            with self.opener.open(request, timeout=60) as response:
                return json.load(response)
        except HTTPError as error:
            if optional and error.code == 404:
                return None
            raise GuardError(f'GitHub API {method} {path}: HTTP {error.code}') from None

    def pages(self, path, key=None):
        values = []
        for page in range(1, 101):
            separator = '&' if '?' in path else '?'
            result = self.api(f'{path}{separator}per_page=100&page={page}')
            batch = result[key] if key else result
            require(isinstance(batch, list), 'Unexpected GitHub list response')
            values.extend(batch)
            if len(batch) < 100:
                return values
        raise GuardError('GitHub pagination exceeded the safety limit')

    def upload(self, release_id, asset):
        # No replacement option exists: a conflicting name produces HTTP 422.
        path = self.prefix + f'/releases/{release_id}/assets?' + urlencode({'name': asset.path.name})
        connection = http.client.HTTPSConnection('uploads.github.com', timeout=180)
        try:
            connection.putrequest('POST', path)
            for name, value in {
                'Authorization': 'Bearer ' + self.token,
                'Accept': 'application/vnd.github+json',
                'X-GitHub-Api-Version': '2022-11-28',
                'User-Agent': 'PhotoDeck-one-release-publisher',
                'Content-Type': 'application/octet-stream',
                'Content-Length': str(asset.size),
            }.items():
                connection.putheader(name, value)
            connection.endheaders()
            with asset.path.open('rb') as source:
                while chunk := source.read(1024 * 1024):
                    connection.send(chunk)
            response = connection.getresponse()
            body = response.read()
            require(response.status == 201,
                    f'Asset upload failed for {asset.path.name}: HTTP {response.status}; draft retained')
            return json.loads(body)
        finally:
            connection.close()


def git(*args):
    result = subprocess.run(['git', *args], text=True, capture_output=True)
    require(result.returncode == 0, 'Source-history verification failed: git ' + ' '.join(args))
    return result.stdout.strip()


def verify_source_version(root=Path('.')):
    header = (root / 'src/version.h').read_text(encoding='utf-8')
    match = re.search(r'\bVersion\s*=\s*"([^"]+)"', header)
    require(match is not None and match.group(1) == VERSION, 'Source version differs from authorized release')
    cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    require('project(PhotoDeck VERSION 0.10.0 LANGUAGES CXX)' in cmake,
            'CMake version differs from authorized release')


def validate_context(env, event, expected_sha):
    require(env.get('GITHUB_REPOSITORY') == REPOSITORY, 'Wrong repository')
    require(env.get('GITHUB_REF') == 'refs/heads/main', 'Only main may publish')
    require(env.get('GITHUB_SHA') == expected_sha, 'Workflow source is not the requested main SHA')
    require(env.get('REQUESTED_TAG', TAG) == TAG, 'Only the authorized v0.10.0-ea.2 release is allowed')
    name = env.get('GITHUB_EVENT_NAME')
    require(name in {'workflow_run', 'workflow_dispatch'}, 'Unsupported publication trigger')
    if name == 'workflow_run':
        run = event.get('workflow_run', {})
        require(run.get('event') == 'push' and run.get('head_branch') == 'main' and
                run.get('head_sha') == expected_sha and
                run.get('head_repository', {}).get('full_name') == REPOSITORY and
                run.get('status') == 'completed' and run.get('conclusion') == 'success' and
                run.get('name') == 'Build and test', 'Trigger is not successful own-repository main CI')
        return str(run.get('id', ''))
    inputs = event.get('inputs', {})
    require(inputs.get('version') == TAG and inputs.get('expected_sha') == expected_sha,
            'Manual version/SHA inputs differ from the authorized run')
    return None


def verify_guard(api, expected_sha, env=None, event=None, git_fn=git, root=Path('.')):
    """Rechecked before packaging and immediately before external writes."""
    expected_sha = full_sha(expected_sha)
    env = os.environ if env is None else env
    if event is None:
        event = json.loads(Path(env['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
    trigger_id = validate_context(env, event, expected_sha)
    main = api.api('/git/ref/heads/main')
    require(main.get('object', {}).get('type') == 'commit' and
            main['object']['sha'] == expected_sha, 'Current main moved from the reviewed merge')
    require(type(PR_NUMBER) is int and PR_NUMBER > 0, 'The fixed reviewed release PR is not configured')
    pull = api.api(f'/pulls/{PR_NUMBER}')
    require(pull.get('number') == PR_NUMBER and pull.get('merged') is True and
            pull.get('merge_commit_sha') == expected_sha and
            pull.get('base', {}).get('ref') == 'main' and
            pull.get('base', {}).get('repo', {}).get('full_name') == REPOSITORY and
            pull.get('head', {}).get('repo', {}).get('full_name') == REPOSITORY and
            pull.get('head', {}).get('ref') == PR_HEAD_REF,
            f'Only the exact merged own-repository release PR #{PR_NUMBER} may publish')
    head_sha = full_sha(pull.get('head', {}).get('sha'))
    require(git_fn('rev-parse', 'HEAD') == expected_sha, 'Checkout is not the reviewed merge')
    git_fn('merge-base', '--is-ancestor', BASE_SHA, head_sha)
    git_fn('merge-base', '--is-ancestor', BASE_SHA, expected_sha)
    git_fn('merge-base', '--is-ancestor', head_sha, expected_sha)
    require(git_fn('rev-parse', expected_sha + '^{tree}') ==
            git_fn('rev-parse', head_sha + '^{tree}'),
            'Merged source tree differs from the final reviewed PR tree')
    verify_source_version(root)
    pr_query = urlencode({'event': 'pull_request', 'head_sha': head_sha, 'status': 'success'})
    pr_runs = api.pages('/actions/workflows/validate.yml/runs?' + pr_query, key='workflow_runs')
    valid_pr_runs = [run for run in pr_runs if
                     run.get('name') == 'Build and test' and
                     run.get('path') == '.github/workflows/validate.yml' and
                     run.get('head_sha') == head_sha and run.get('event') == 'pull_request' and
                     run.get('status') == 'completed' and run.get('conclusion') == 'success' and
                     run.get('head_repository', {}).get('full_name') == REPOSITORY and
                     (not run.get('pull_requests') or
                      any(item.get('number') == PR_NUMBER and
                          item.get('head', {}).get('sha', head_sha) == head_sha
                          for item in run['pull_requests']))]
    require(bool(valid_pr_runs), f'No successful final PR #{PR_NUMBER} head validation run')
    pr_run = max(valid_pr_runs, key=lambda item: item['id'])
    verify_ci_jobs(api, pr_run['id'])
    print(f'Final PR #{PR_NUMBER} head CI verified: run {pr_run["id"]}')
    query = urlencode({'branch': 'main', 'event': 'push', 'head_sha': expected_sha, 'status': 'success'})
    runs = api.pages('/actions/workflows/validate.yml/runs?' + query, key='workflow_runs')
    required_id = env.get('EXPECTED_CI_RUN_ID') or trigger_id
    valid = [run for run in runs if
             run.get('name') == 'Build and test' and
             run.get('path') == '.github/workflows/validate.yml' and
             run.get('head_sha') == expected_sha and run.get('event') == 'push' and
             run.get('head_branch') == 'main' and run.get('status') == 'completed' and
             run.get('conclusion') == 'success' and
             run.get('head_repository', {}).get('full_name') == REPOSITORY and
             (not required_id or str(run.get('id')) == required_id)]
    require(bool(valid), 'No successful Build and test push/main run for this exact SHA')
    run = max(valid, key=lambda item: item['id'])
    verify_ci_jobs(api, run['id'])
    return str(run['id'])


def verify_ci_jobs(api, run_id):
    jobs = api.pages(f'/actions/runs/{run_id}/jobs?filter=latest', key='jobs')
    require(len(jobs) == len(CI_JOBS) and {job.get('name') for job in jobs} == CI_JOBS and
            all(job.get('status') == 'completed' and job.get('conclusion') == 'success' for job in jobs),
            'All four native validation targets and Linux sanitizers must succeed')


@dataclass(frozen=True)
class Asset:
    path: Path
    size: int
    sha256: str


def inspect_asset(path):
    require(path.is_file() and not path.is_symlink(), f'Missing or unsafe asset: {path.name}')
    digest = hashlib.sha256()
    size = 0
    with path.open('rb') as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
            size += len(chunk)
    require(size > 0, f'Empty asset: {path.name}')
    return Asset(path, size, digest.hexdigest())


def parse_checksums(text, expected_names):
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9._-]+)', line)
        require(match is not None, 'Malformed package checksum entry')
        digest, name = match.groups()
        require(name not in result, 'Duplicate package checksum entry')
        result[name] = digest
    require(set(result) == set(expected_names), 'Checksum set differs from the exact expected package set')
    return result


def junit_count(node, key, default=None):
    value = node.get(key, default)
    require(isinstance(value, str) and re.fullmatch(r'[0-9]+', value),
            f'Malformed JUnit {key} count')
    return int(value)


def verify_junit(path, expected_count=None, *, expected_names=None, platform=None, executable=None):
    require(path.is_file() and not path.is_symlink(), f'Missing test proof: {path.name}')
    tree = ET.parse(path)
    root = tree.getroot()
    suites = [root] if root.tag == 'testsuite' else root.findall('testsuite') if root.tag == 'testsuites' else []
    require(bool(suites) and len(list(tree.iter('testsuite'))) == len(suites),
            f'Missing or nested JUnit suite: {path.name}')
    require(not any(node.tag in {'failure', 'error'} for node in tree.iter()),
            f'Failing JUnit proof: {path.name}')
    require(all(node.get('status') in {None, 'run'} for node in tree.iter('testcase')),
            f'Non-run JUnit testcase: {path.name}')
    if executable is not None:
        expected_class = TEST_CLASSES.get(executable)
        require(expected_class is not None and platform in PLATFORMS and len(suites) == 1 and
                suites[0].get('name') == expected_class and
                all(case.get('classname') == expected_class for case in tree.iter('testcase')),
                f'QtTest suite identity differs from the authorized executable: {path.name}')
    count = 0
    skipped_count = 0
    skipped_cases = []
    case_names = []
    allowed = ALLOWED_SKIPS.get((platform, executable), {})
    for suite in suites:
        require(all(junit_count(suite, key, '0') == 0 for key in ('failures', 'errors', 'disabled')),
                f'Failing JUnit summary: {path.name}')
        cases = suite.findall('testcase')
        tests = junit_count(suite, 'tests')
        require(tests > 0 and tests == len(cases),
                f'JUnit testcase count differs from summary: {path.name}')
        identities = [(case.get('classname'), case.get('name')) for case in cases]
        require(all(name for _, name in identities) and len(identities) == len(set(identities)),
                f'Missing or duplicate JUnit testcase identity: {path.name}')
        case_names.extend(case.get('name') for case in cases)
        suite_skips = 0
        for case in cases:
            skips = case.findall('skipped')
            if not skips:
                continue
            # A skip is accepted only for this platform, executable, exact Qt
            # suite size/class, testcase identity, reason and serialized node.
            reason = allowed.get('cases', {}).get(case.get('name'))
            require(reason is not None and len(suites) == 1 and
                    suite.get('name') == allowed.get('suite') and tests == allowed.get('tests') and
                    case.get('classname') == allowed.get('suite') and len(skips) == 1 and
                    skips[0].attrib == {'message': reason} and
                    not list(skips[0]) and not (skips[0].text or '').strip(),
                    f'Unexpected JUnit skip: {path.name}')
            suite_skips += 1
            skipped_cases.append({'suite': suite.get('name'), 'testcase': case.get('name'),
                                  'reason': reason})
        require(junit_count(suite, 'skipped', '0') == suite_skips,
                f'JUnit skipped count differs from allowed testcase proof: {path.name}')
        count += tests
        skipped_count += suite_skips
    require(len(list(tree.iter('testcase'))) == count and
            len(list(tree.iter('skipped'))) == skipped_count,
            f'Orphan or nested JUnit testcase/skip: {path.name}')
    if root.tag == 'testsuites':
        for key, expected in (('tests', count), ('skipped', skipped_count),
                              ('failures', 0), ('errors', 0), ('disabled', 0)):
            require(root.get(key) is None or junit_count(root, key) == expected,
                    f'JUnit aggregate {key} count differs from suite proofs: {path.name}')
    require(count > 0 and (expected_count is None or count == expected_count),
            f'Unexpected JUnit test count: {path.name}')
    if expected_names is not None:
        require(len(case_names) == len(expected_names) and set(case_names) == set(expected_names),
                f'JUnit suite inventory differs from the authorized 22 suites: {path.name}')
    result = {'testcases': count, 'executed': count - skipped_count,
              'skipped': skipped_count, 'skipped_cases': skipped_cases}
    if expected_names is not None:
        result['suite_names'] = sorted(case_names)
    return result


def verify_artifacts(artifacts, output, expected_sha, *, ci_run_id, package_run_id):
    expected_dirs = {'PhotoDeck-' + label for label in PLATFORMS}
    require(artifacts.is_dir() and {p.name for p in artifacts.iterdir()} == expected_dirs,
            'All four and only the four expected platform artifacts are required')
    full_sha(expected_sha)
    require(re.fullmatch(r'[0-9]+', ci_run_id) and re.fullmatch(r'[0-9]+', package_run_id),
            'Exact CI and packaging run IDs are required')
    assets = {}
    proof = {'schema_version': 1,
             'qt_testcase_counting': 'Includes initTestCase, cleanupTestCase and individual data rows; executed excludes skipped records.',
             'repository': REPOSITORY, 'tag': TAG, 'version': VERSION,
             'source_commit': expected_sha, 'reviewed_pull_request': PR_NUMBER,
             'source_ci_run_id': ci_run_id, 'package_run_id': package_run_id,
             'test_executables': sorted(TEST_EXECUTABLES), 'platforms': {}}
    for label, names in PLATFORMS.items():
        folder = artifacts / ('PhotoDeck-' + label)
        require(folder.is_dir() and not folder.is_symlink(), 'Unsafe artifact directory')
        require(not any(item.is_symlink() for item in folder.rglob('*')), 'Symlinks in build artifacts are not allowed')
        provenance = json.loads((folder / 'release-source.json').read_text(encoding='utf-8-sig'))
        platform = 'Linux' if label == 'linux-x86_64' else 'Windows' if label == 'windows-x64' else 'macOS'
        require(provenance == {'version': VERSION, 'source_commit': expected_sha, 'platform': platform},
                f'Artifact source/version provenance mismatch: {label}')
        dist = folder / 'dist'
        require(dist.is_dir() and not dist.is_symlink(), 'Missing package directory')
        require({p.name for p in dist.iterdir()} == set(names) | {'SHA256SUMS.txt'},
                f'Unexpected or incomplete {label} package file set')
        checksums = parse_checksums((dist / 'SHA256SUMS.txt').read_text(encoding='utf-8-sig'), names)
        for name in names:
            asset = inspect_asset(dist / name)
            require(asset.sha256 == checksums[name], f'Package checksum mismatch: {name}')
            assets[name] = asset
        require((folder / 'package-version.txt').read_text(encoding='utf-8-sig').strip() ==
                'PhotoDeck ' + VERSION, f'Packaged executable version mismatch: {label}')
        inventory = (folder / 'packaged-test-list.txt').read_text(encoding='utf-8-sig').splitlines()
        require(len(inventory) == TEST_SUITE_COUNT and set(inventory) == TEST_EXECUTABLES and
                (folder / 'packaged-test-count.txt').read_text(encoding='utf-8-sig').strip() == str(TEST_SUITE_COUNT),
                f'Expected complete 22-suite inventory: {label}')
        native = verify_junit(folder / 'build-release/tests.xml', TEST_SUITE_COUNT,
                              expected_names=CTEST_NAMES)
        platform_proof = {'native_ctest': native, 'native_qt': {}, 'bundled_runtime': {}}
        proof['platforms'][label] = platform_proof
        native_proofs = {p.name for p in (folder / 'build-release').glob('*-native.xml')}
        require(native_proofs == {name + '-native.xml' for name in TEST_EXECUTABLES},
                f'Incomplete native QtTest proofs: {label}')
        for name in TEST_EXECUTABLES:
            platform_proof['native_qt'][name] = verify_junit(
                folder / 'build-release' / (name + '-native.xml'), platform=label, executable=name)
        # macOS uses native CTest/QtTest, deployed-app startup and codesign verification.
        # Linux/Windows additionally rerun every suite in the bundled Qt runtime.
        if label in {'linux-x86_64', 'windows-x64'}:
            proofs = {p.name for p in folder.glob('*-packaged.xml')}
            require(proofs == {name + '-packaged.xml' for name in TEST_EXECUTABLES},
                    f'Incomplete packaged-runtime test proofs: {label}')
            for name in TEST_EXECUTABLES:
                platform_proof['bundled_runtime'][name] = verify_junit(
                    folder / (name + '-packaged.xml'), platform=label, executable=name)
        startup = 'linux' if label == 'linux-x86_64' else 'windows' if label == 'windows-x64' else 'macos'
        require(inspect_asset(folder / (startup + '-startup.png')).size > 8,
                f'Missing deployed-app startup proof: {label}')
        if label == 'linux-x86_64':
            require(inspect_asset(folder / 'appimage-startup.png').size > 8,
                    'Missing AppImage deployed-app startup proof')
        platform_proof['deployed_app_startup'] = 'passed'
        if label == 'linux-x86_64':
            platform_proof['appimage_startup'] = 'passed'
        # Git's Windows checkout may use CRLF. Only that byte-level newline
        # conversion is allowed; BOMs, bare CRs and all content stay significant.
        require((folder / NOTES_PATH).read_bytes().replace(b'\r\n', b'\n') ==
                NOTES_PATH.read_bytes().replace(b'\r\n', b'\n'),
                f'Package notes differ from the tested source: {label}')
    output.mkdir(parents=True, exist_ok=True)
    proof['packages'] = {name: {'size': asset.size, 'sha256': asset.sha256}
                         for name, asset in sorted(assets.items())}
    proof_path = output / 'RELEASE-PROOF.json'
    proof_path.write_text(json.dumps(proof, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    assets[proof_path.name] = inspect_asset(proof_path)
    combined = output / 'SHA256SUMS.txt'
    combined.write_text(''.join(f'{assets[name].sha256}  {name}\n' for name in sorted(assets)), encoding='utf-8')
    assets[combined.name] = inspect_asset(combined)
    return assets


def verify_remote_assets(remote_assets, assets, complete):
    names = [item.get('name') for item in remote_assets]
    require(len(names) == len(set(names)), 'Duplicate remote asset names; refusing to modify release')
    require(set(names) <= set(assets), 'Unrelated release assets exist; refusing to modify release')
    if complete:
        require(set(names) == set(assets), 'Uploaded release asset set is incomplete')
    for remote in remote_assets:
        asset = assets[remote['name']]
        require(remote.get('state') == 'uploaded' and remote.get('size') == asset.size and
                remote.get('digest') == 'sha256:' + asset.sha256,
                f'Remote size/SHA256 mismatch: {asset.path.name}; never overwritten')
    return set(names)


def tag_commit(api):
    ref = api.api('/git/ref/tags/' + TAG, optional=True)
    if ref is None:
        return None
    obj = ref.get('object', {})
    for _ in range(5):
        full_sha(obj.get('sha'))
        if obj.get('type') == 'commit':
            return obj['sha']
        require(obj.get('type') == 'tag', 'Unexpected tag object type')
        obj = api.api('/git/tags/' + obj['sha']).get('object', {})
    raise GuardError('Tag nesting exceeds the safety limit')


def release_body(notes, expected_sha, ci_run_id, package_run_id, assets):
    require(re.fullmatch(r'[0-9]+', ci_run_id) and re.fullmatch(r'[0-9]+', package_run_id),
            'Exact CI and packaging run IDs are required')
    fingerprint = assets['SHA256SUMS.txt'].sha256
    return notes.rstrip() + (
        '\n\n## Verified publication\n\n'
        f'- Source: [{expected_sha}](https://github.com/{REPOSITORY}/commit/{expected_sha})\n'
        f'- Reviewed release PR: https://github.com/{REPOSITORY}/pull/{PR_NUMBER}\n'
        f'- Successful source CI (four native targets and Linux ASan/UBSan): https://github.com/{REPOSITORY}/actions/runs/{ci_run_id}\n'
        f'- Four-platform packaging: https://github.com/{REPOSITORY}/actions/runs/{package_run_id}\n'
        '- All four packaging targets passed the exact 22-suite native CTest inventory, supplied all 22 granular QtTest reports, and passed deployed-app startup checks.\n'
        '- Linux and Windows additionally reran all 22 suites in the bundled Qt runtime.\n'
        '- RELEASE-PROOF.json records observed testcase totals and every explicitly allowed platform-specific skip; SHA256SUMS.txt covers all five packages and that proof.\n'
        '- Windows packages are unsigned. macOS packages are ad-hoc signed and are not notarized.\n'
        f'\n<!-- photodeck-one-release:{expected_sha}:{fingerprint} -->\n')


def publish(api, expected_sha, assets, body, reguard):
    """No deletion or overwrite; failed uploads leave an unpublished draft."""
    require(set(assets) == RELEASE_ASSET_NAMES,
            'Only the complete five packages, exact release proof and checksums may publish')
    existing_tag = tag_commit(api)
    require(existing_tag in {None, expected_sha}, 'Existing tag points elsewhere; never moved')
    releases = [release for release in api.pages('/releases') if release.get('tag_name') == TAG]
    require(len(releases) <= 1, 'Ambiguous existing release identity')
    release = releases[0] if releases else None
    if release:
        require(release.get('target_commitish') == expected_sha and
                release.get('name') == 'PhotoDeck ' + VERSION and
                release.get('body') == body and release.get('prerelease') is True,
                'Existing release differs from this exact publication; never overwritten')
        remote = api.pages(f'/releases/{release["id"]}/assets')
        present = verify_remote_assets(remote, assets, complete=not release.get('draft'))
        if not release.get('draft'):
            require(existing_tag == expected_sha, 'Published release tag is missing or changed')
            print('Verified existing published prerelease: ' + release['html_url'])
            return release
    else:
        present = set()
    # Packaging can take many minutes. Revalidate current main and successful CI
    # immediately before the first write, including on a draft-resume run.
    reguard()
    if existing_tag is None:
        api.api('/git/refs', method='POST', data={'ref': 'refs/tags/' + TAG, 'sha': expected_sha})
        require(tag_commit(api) == expected_sha, 'New release tag was not pinned to tested main')
    if release is None:
        release = api.api('/releases', method='POST', data={
            'tag_name': TAG, 'target_commitish': expected_sha,
            'name': 'PhotoDeck ' + VERSION, 'body': body,
            'draft': True, 'prerelease': True, 'make_latest': 'false',
        })
        require(release.get('draft') is True and release.get('tag_name') == TAG,
                'API did not create the expected unpublished draft')
    for name in sorted(set(assets) - present):
        api.upload(release['id'], assets[name])
    # Read back every server-computed digest and size before exposing the release.
    verify_remote_assets(api.pages(f'/releases/{release["id"]}/assets'), assets, complete=True)
    reguard()
    require(tag_commit(api) == expected_sha, 'Release tag changed during upload')
    latest = api.api(f'/releases/{release["id"]}')
    require(latest.get('draft') is True and latest.get('body') == body and
            latest.get('tag_name') == TAG and latest.get('target_commitish') == expected_sha and
            latest.get('prerelease') is True, 'Draft changed during upload; refusing to publish')
    verify_remote_assets(api.pages(f'/releases/{release["id"]}/assets'), assets, complete=True)
    # The single external visibility change occurs only after complete verification.
    result = api.api(f'/releases/{release["id"]}', method='PATCH',
                     data={'draft': False, 'prerelease': True, 'make_latest': 'false'})
    require(result.get('draft') is False and result.get('prerelease') is True and
            result.get('tag_name') == TAG, 'Publication result was not the expected prerelease')
    verify_remote_assets(api.pages(f'/releases/{release["id"]}/assets'), assets, complete=True)
    require(tag_commit(api) == expected_sha, 'Published release tag verification failed')
    print('Published verified prerelease: ' + result['html_url'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['check', 'publish'])
    parser.add_argument('--artifacts', type=Path, default=Path('release-artifacts'))
    args = parser.parse_args()
    expected_sha = full_sha(os.environ.get('EXPECTED_SHA'))
    api = GitHub()
    ci_run_id = verify_guard(api, expected_sha)
    if args.command == 'check':
        output = os.environ.get('GITHUB_OUTPUT')
        if output:
            with Path(output).open('a', encoding='utf-8') as target:
                target.write(f'source_sha={expected_sha}\nci_run_id={ci_run_id}\n')
        print(f'Authorized reviewed PR #{PR_NUMBER} merge {expected_sha}; CI run {ci_run_id}')
        return
    package_run_id = os.environ.get('GITHUB_RUN_ID', '')
    assets = verify_artifacts(args.artifacts, Path('release-upload'), expected_sha,
                              ci_run_id=ci_run_id, package_run_id=package_run_id)
    body = release_body(NOTES_PATH.read_text(encoding='utf-8'), expected_sha, ci_run_id,
                        package_run_id, assets)
    publish(api, expected_sha, assets, body, lambda: verify_guard(api, expected_sha))


if __name__ == '__main__':
    try:
        main()
    except (GuardError, OSError, ValueError, ET.ParseError) as error:
        print('Publication stopped: ' + str(error), file=sys.stderr)
        sys.exit(1)
