"""Standard-library tests for the CTest-to-packaged-runtime inventory contract."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'packaged_test_list', Path(__file__).resolve().parents[1] / 'scripts/packaged-test-list.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackagedInventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def executable(self, name):
        path = self.root / name
        path.touch()
        return str(path)

    def inventory(self, *commands):
        return {'tests': [{'name': f'suite-{i}', 'command': command}
                          for i, command in enumerate(commands)]}

    def test_all_suites_are_kept_in_order(self):
        inventory = self.inventory([self.executable('photodeck-tests')],
                                   [self.executable('photodeck-ui-resilience-tests')])
        self.assertEqual(module.test_executables(inventory, 2),
                         ['photodeck-tests', 'photodeck-ui-resilience-tests'])

    def test_windows_exe_suffix_is_removed(self):
        inventory = self.inventory([self.executable('photodeck-save-safety-tests.exe')])
        self.assertEqual(module.test_executables(inventory, 1), ['photodeck-save-safety-tests'])

    def test_native_output_arguments_keep_the_same_inventory(self):
        binary = self.executable('photodeck-tests')
        command = [binary, '-o', str(self.root/'photodeck-tests-native.xml')+',junitxml',
                   '-o', str(self.root/'photodeck-tests-native.txt')+',txt']
        self.assertEqual(module.test_executables(self.inventory(command), 1), ['photodeck-tests'])
        release = self.root/'Release'
        release.mkdir()
        executable = release/'photodeck-tests.exe'
        executable.touch()
        command[0] = str(executable)
        self.assertEqual(module.test_executables(self.inventory(command), 1), ['photodeck-tests'])

    def test_arbitrary_output_arguments_are_rejected(self):
        command = [self.executable('photodeck-tests'), '-o',
                   str(self.root/'photodeck-tests-native.xml')+',junitxml',
                   '-o', str(self.root/'photodeck-tests-native.txt')+',txt']
        for index, value in [(1, '--other'), (2, str(self.root/'other.xml')+',junitxml'),
                             (2, str(self.root/'photodeck-tests-native.xml')+',xml'),
                             (3, '--other'), (4, str(self.root/'other.txt')+',txt'),
                             (4, str(self.root/'photodeck-tests-native.txt')+',xml')]:
            altered = command.copy()
            altered[index] = value
            with self.subTest(index=index, value=value), self.assertRaisesRegex(ValueError, 'Unsupported'):
                module.test_executables(self.inventory(altered), 1)

    def test_count_drift_is_an_error(self):
        with self.assertRaisesRegex(ValueError, 'Expected 14 CTest suites, found 0'):
            module.test_executables({'tests': []}, 14)

    def test_missing_binary_is_an_error(self):
        with self.assertRaisesRegex(ValueError, 'Missing or unexpected'):
            module.test_executables(self.inventory([str(self.root / 'photodeck-tests')]), 1)

    def test_unexpected_name_is_an_error(self):
        with self.assertRaisesRegex(ValueError, 'Missing or unexpected'):
            module.test_executables(self.inventory([self.executable('other-tests')]), 1)

    def test_arguments_or_missing_command_are_an_error(self):
        for command in ([], [self.executable('photodeck-tests'), '--extra']):
            with self.subTest(command=command), self.assertRaisesRegex(ValueError, 'Unsupported'):
                module.test_executables(self.inventory(command), 1)

    def test_duplicate_binary_is_an_error(self):
        binary = self.executable('photodeck-tests')
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            module.test_executables(self.inventory([binary], [binary]), 2)


if __name__ == '__main__':
    unittest.main()
