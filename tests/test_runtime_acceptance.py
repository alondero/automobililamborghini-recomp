"""Acceptance orchestration must use fresh championship input and fail closed."""
import contextlib
import importlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
acceptance = importlib.import_module('run_runtime_acceptance')


class RuntimeAcceptanceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / 'Automobili Lamborghini (USA).z64').write_bytes(b'test fixture')
        self.exe = self.root / 'game'
        self.exe.write_bytes(b'test executable')
        for target, value in [('ROOT', self.root),
                              ('runner.create_artifacts', None),
                              ('subprocess.check_output', None),
                              ('subprocess.run', None),
                              ('check_race_field.run', None),
                              ('check_field_championship.run', None),
                              ('runner.main', None)]:
            mock = patch.object(acceptance, target, value) if '.' not in target else patch(
                'run_runtime_acceptance.' + target)
            result = mock.start()
            self.addCleanup(mock.stop)
            if '.' in target:
                setattr(self, target.split('.')[0] + '_' + target.split('.')[1], result)
        self.runner_create_artifacts.return_value = self.root
        self.subprocess_check_output.return_value = 'test-commit\n'
        self.check_race_field_run.side_effect = lambda args: self.root / f'field-{args.cars}'
        self.check_field_championship_run.return_value = self.root / 'championship'
        self.runner_main.return_value = 0

    def run_suite(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            acceptance.run(self.exe, self.root / 'build', **kwargs)

    def report(self):
        return json.loads((self.root / 'result.json').read_text())

    def test_headless_suite_checks_fresh_mode3_snapshot_and_high_ranks(self):
        self.run_suite(headless=True)
        fields = [call.args[0] for call in self.check_race_field_run.call_args_list]
        self.assertEqual([(args.cars, args.mode) for args in fields], [(2, 2), (6, 2), (16, 3)])
        results = [call.args[0] for call in self.check_field_championship_run.call_args_list]
        self.assertEqual([args.player_place for args in results], [10, 16])
        self.assertTrue(all(args.state == self.root / 'field-16/race.lstate' for args in results))
        self.runner_main.assert_not_called()
        self.assertEqual(self.report()['not_run'], ['launcher-quit', 'repeated-reset'])
        self.assertEqual(self.report()['outcome'], 'passed')

    def test_missing_native_fixture_leaves_failed_report(self):
        self.subprocess_run.side_effect = subprocess.CalledProcessError(8, ['ctest'])
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_suite(headless=True)
        self.check_race_field_run.assert_not_called()
        self.assertEqual(self.report()['outcome'], 'failed')

    def test_failed_ui_scenario_leaves_failed_report(self):
        self.runner_main.side_effect = [0, 1]
        with self.assertRaisesRegex(RuntimeError, 'repeated-reset failed'):
            self.run_suite()
        self.assertEqual(self.report()['outcome'], 'failed')
