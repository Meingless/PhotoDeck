#!/usr/bin/env python3
"""List every built CTest executable for an equivalent packaged-runtime audit."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def test_executables(inventory, expected_count):
    tests = inventory.get('tests', [])
    if len(tests) != expected_count:
        raise ValueError(f'Expected {expected_count} CTest suites, found {len(tests)}')
    executables = []
    for test in tests:
        command = test.get('command', [])
        if len(command) not in {1, 5}:
            raise ValueError(f'Unsupported CTest command for {test.get("name", "unknown")}')
        executable = Path(command[0])
        name = executable.name.removesuffix('.exe')
        if not re.fullmatch(r'photodeck-[a-z-]+', name) or not executable.is_file():
            raise ValueError(f'Missing or unexpected test executable: {executable}')
        if len(command) == 5:
            xml = Path(command[2].removesuffix(',junitxml'))
            text = Path(command[4].removesuffix(',txt'))
            allowed_parents = {executable.parent, executable.parent.parent}
            if (command[1] != '-o' or command[3] != '-o' or
                    not command[2].endswith(',junitxml') or not command[4].endswith(',txt') or
                    xml.name != name + '-native.xml' or text.name != name + '-native.txt' or
                    xml.parent != text.parent or xml.parent not in allowed_parents):
                raise ValueError(f'Unsupported CTest command for {test.get("name", "unknown")}')
        if name in executables:
            raise ValueError(f'Duplicate test executable: {name}')
        executables.append(name)
    return executables


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--config', default='Release')
    parser.add_argument('--expected-count', required=True, type=int)
    args = parser.parse_args()
    inventory = json.loads(subprocess.check_output([
        'ctest', '--test-dir', args.build_dir, '-C', args.config, '--show-only=json-v1'
    ], text=True))
    print('\n'.join(test_executables(inventory, args.expected_count)))


if __name__ == '__main__':
    main()
