import contextlib
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

TOOLS = Path(__file__).resolve().parents[1] / 'tools/emu_instrumentation'
SPEC = importlib.util.spec_from_file_location('probe_jp_weather_ai', TOOLS / 'probe_jp_weather_ai.py')
probe = importlib.util.module_from_spec(SPEC)
with patch.object(sys, 'path', [str(TOOLS), *sys.path]):
    SPEC.loader.exec_module(probe)


class WeatherAIProbeTests(unittest.TestCase):
    def run_case(self, stops, weather=1, difficulty=0, active_difficulty=None):
        parameter = 0x8007DAF0 + weather * 16 + difficulty * 4
        countdown = 0x800989D0 + 0x84 + 0x30
        process = Mock()
        process.poll.return_value = None
        client = Mock()
        client.enable_no_ack_mode.return_value = True
        client.continue_until_halt.side_effect = stops
        client.parse_stop_reason.side_effect = lambda stop: stop
        states = iter([6, 8])
        timer_values = iter([25, 24])
        def memory(_client, address, size=2):
            if address == probe.STATE:
                return next(states)
            if address == countdown:
                return next(timer_values)
            return {probe.ACTIVE_WEATHER: weather, 0x800C4FFE: difficulty if active_difficulty is None else active_difficulty,
                    0x800C4FFA: 0, parameter: 0x435C0000, 0x8007DA78 + difficulty * 2: 50,
                    0x800C4E9E: 1, 0x8008C7D8: 0, 0x800BA270: 173, 0x800BA28A: 98}[address]
        with contextlib.ExitStack() as stack:
            for name, value in [('subprocess.Popen', Mock(return_value=process)),
                                ('WeatherClient', Mock(return_value=client)), ('time.sleep', Mock()),
                                ('pause', Mock(return_value='S05')), ('read', Mock(side_effect=memory)),
                                ('write_halfword', Mock()), ('wait_write', Mock(return_value='T05'))]:
                target, attribute = (probe, name) if '.' not in name else (getattr(probe, name.split('.')[0]), name.split('.')[1])
                stack.enter_context(patch.object(target, attribute, value))
            result = probe.measure(Path('ares.exe'), Path('jp.z64'), weather, difficulty, 9341, 1, 1, 3)
        client.disconnect.assert_called_once()
        process.terminate.assert_called_once()
        process.wait.assert_called_once_with(timeout=5)
        return result

    def test_selected_table_read_and_countdown_pair_required(self):
        result = self.run_case([{'kind': 'rwatch', 'addr': 0x8007DB00},
                                {'kind': 'watch', 'addr': 0x80098A84},
                                {'kind': 'watch', 'addr': 0x80098A84}])
        self.assertTrue(result['complete'])
        self.assertEqual([event['value'] for event in result['countdown_events']], [25, 24])
        self.assertEqual(result['speed_parameter'], 220)

    def test_speed_read_timeout_keeps_partial_evidence_incomplete(self):
        result = self.run_case([{'kind': 'rwatch', 'addr': 0x8007DB00}, TimeoutError('no timer write')])
        self.assertFalse(result['complete'])
        self.assertEqual(result['speed_parameter'], 220)
        self.assertIn('TimeoutError', result['error'])

    def test_wrong_watchpoint_cannot_succeed(self):
        result = self.run_case([{'kind': 'watch', 'addr': 0x1234}])
        self.assertFalse(result['complete'])
        self.assertIn('Unexpected AI stop', result['error'])

    def test_wrong_active_difficulty_cannot_succeed(self):
        result = self.run_case([], active_difficulty=2)
        self.assertFalse(result['complete'])
        self.assertIn('Loader did not apply', result['error'])

    def test_watchpoint_rejection_is_fatal(self):
        client = Mock()
        client.set_watchpoint.return_value = False
        with self.assertRaisesRegex(RuntimeError, 'rejected'):
            probe.watch(client, 0x8007DB00, 'read')


if __name__ == '__main__':
    unittest.main()
