"""Window-free tests of the Windows message driver; no game or ROM required."""
import contextlib
import ctypes
import importlib.util
import io
from pathlib import Path
import re
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'drive_input', Path(__file__).resolve().parents[1] / 'tools' / 'drive_input.py')
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)

# The seeded profile names the N64 input; the C++ table names the frontend's
# GameInput enum. Only the four stick axes differ.
ACTION_NAMES = {'X_AXIS_NEG': 'STICK_LEFT', 'X_AXIS_POS': 'STICK_RIGHT',
                'Y_AXIS_POS': 'STICK_UP', 'Y_AXIS_NEG': 'STICK_DOWN'}
# SDL's scancode token for Enter is RETURN; the driver names that key `enter`.
SDL_NAME_ALIASES = {'RETURN': 'ENTER'}


class DriverTests(unittest.TestCase):
    def test_default_profile_keys_emit_scancodes(self):
        # Scan set 1 expected by SDL's Windows message translator, not SDL enums.
        expected = {'space': (0x20, 0x39), 'lshift': (0x10, 0x2A),
                    'w': (0x57, 0x11), 'a': (0x41, 0x1E),
                    's': (0x53, 0x1F), 'd': (0x44, 0x20),
                    'i': (0x49, 0x17), 'j': (0x4A, 0x24),
                    'k': (0x4B, 0x25), 'l': (0x4C, 0x26),
                    'q': (0x51, 0x10), 'e': (0x45, 0x12),
                    'r': (0x52, 0x13), 'f': (0x46, 0x21)}
        for key, (vk, scan) in expected.items():
            with self.subTest(key=key), patch.object(driver, 'user32') as api, \
                    patch.object(driver.time, 'sleep') as sleep:
                driver.press(123, [key], 600)
                self.assertEqual(api.PostMessageW.call_args_list[0].args,
                                 (123, 0x100, vk, 1 | scan << 16))
                self.assertEqual(api.PostMessageW.call_args_list[1].args,
                                 (123, 0x101, vk, 1 | scan << 16 | 3 << 30))
                self.assertEqual(sleep.call_args_list[0].args, (0.6,))

    def test_legacy_keys_and_chord_keep_literal_meanings(self):
        with patch.object(driver, 'user32') as api, patch.object(driver.time, 'sleep'):
            driver.press(123, ['x', 'left', 'f15'], 150)
            messages = [c.args for c in api.PostMessageW.call_args_list]
            self.assertEqual(messages[0], (123, 0x100, 0x58, 1 | 0x2D << 16))
            self.assertEqual(messages[1], (123, 0x100, 0x25, 1 | 0x4B << 16 | 1 << 24))
            self.assertEqual(messages[2], (123, 0x100, 0x7E, 1 | 0x5B << 16))
            self.assertEqual([m[1] for m in messages], [0x100] * 3 + [0x101] * 3)
            self.assertEqual(messages[4][3], messages[1][3] | 3 << 30)

    def test_invalid_chord_posts_nothing(self):
        with patch.object(driver, 'user32') as api:
            for keys, ms in [(['w', 'unknown'], 150), (['w'], -1), ([], 0)]:
                with self.assertRaises(ValueError):
                    driver.press(123, keys, ms)
            api.PostMessageW.assert_not_called()

    def test_help_and_invalid_cli_never_touch_windows(self):
        with patch.object(driver, 'initialize_windows') as init, \
                contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            for args, code in [(['--help'], 0), ([], 2),
                               (['press', 'w,unknown', '150'], 2),
                               (['press', 'w', '-1'], 2)]:
                with self.assertRaises(SystemExit) as result:
                    driver.main(args)
                self.assertEqual(result.exception.code, code)
            init.assert_not_called()

    def test_cli_initializes_before_window_lookup(self):
        events = []
        with patch.object(driver, 'initialize_windows', side_effect=lambda: events.append('dpi')), \
                patch.object(driver, 'find_window', side_effect=lambda: (events.append('find') or (123, 'game'))), \
                patch.object(driver, 'press') as press:
            self.assertEqual(driver.main(['press', 'w,d', '600']), 0)
            self.assertEqual(events, ['dpi', 'find'])
            press.assert_called_once_with(123, ['w', 'd'], 600)

    def test_dpi_context_and_pointer_sized_handles(self):
        api, gdi = Mock(), Mock()
        with patch.object(driver.sys, 'platform', 'win32'), \
                patch.object(ctypes, 'WinDLL', create=True, side_effect=[api, gdi]):
            driver.initialize_windows()
        self.assertEqual(api.SetProcessDpiAwarenessContext.call_args.args[0].value,
                         ctypes.c_void_p(-4).value)
        self.assertIs(gdi.CreateCompatibleBitmap.restype, driver.wt.HBITMAP)
        self.assertEqual(api.PostMessageW.argtypes[-1], driver.wt.LPARAM)

    def test_failed_dpi_setup_stops_before_lookup(self):
        api = Mock()
        api.SetProcessDpiAwarenessContext.return_value = 0
        api.AreDpiAwarenessContextsEqual.return_value = 0
        with patch.object(driver.sys, 'platform', 'win32'), \
                patch.object(ctypes, 'WinDLL', create=True, return_value=api), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5), \
                patch.object(ctypes, 'WinError', create=True, return_value=OSError('DPI denied')), \
                patch.object(driver, 'find_window') as find:
            with self.assertRaises(OSError):
                driver.main(['find'])
            find.assert_not_called()

    def test_already_per_monitor_v2_is_accepted(self):
        api = Mock()
        api.SetProcessDpiAwarenessContext.return_value = 0
        api.GetThreadDpiAwarenessContext.return_value = 1234
        api.AreDpiAwarenessContextsEqual.return_value = 1
        with patch.object(driver.sys, 'platform', 'win32'), \
                patch.object(ctypes, 'WinDLL', create=True, return_value=api), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5):
            driver.initialize_windows()
        self.assertEqual(api.AreDpiAwarenessContextsEqual.call_args.args[0], 1234)
        self.assertEqual(api.AreDpiAwarenessContextsEqual.call_args.args[1].value,
                         ctypes.c_void_p(-4).value)

    def test_seeded_profile_matches_the_frontend_table(self):
        # #319 was caused by this driver's key map drifting away from the profile
        # the port actually installs. Read that table straight out of the C++ so
        # the two cannot disagree again.
        source = (ROOT / 'src' / 'ui' / 'lambo_frontend.cpp').read_text(encoding='utf-8')
        table = re.search(r'std::pair<GameInput, SDL_Scancode> keys\[\] = \{(.*?)\};',
                          source, re.S)
        self.assertIsNotNone(table, 'seeded keys[] table not found in lambo_frontend.cpp')
        parsed = {}
        for game_input, scancode in re.findall(r'\{GameInput::(\w+), SDL_SCANCODE_(\w+)\}',
                                              table.group(1)):
            parsed.setdefault(ACTION_NAMES.get(game_input, game_input), []).append(scancode)
        # Slot 0 above is the first binding; the stick adds a second one here.
        second = re.findall(
            r'set_input_binding\(keyboard, GameInput::(\w+), 1, '
            r'InputField::keyboard\(SDL_SCANCODE_(\w+)\)\)', source)
        self.assertEqual(len(second), 4, 'expected four second stick bindings')
        for game_input, scancode in second:
            parsed[ACTION_NAMES[game_input]].append(scancode)

        # SDL_SCANCODE_* values are USB HID usage IDs, so they are the SC values
        # in the driver. Round-trip through them instead of hard-coding numbers.
        by_name = {name.upper(): scan for name, scan in driver.SC.items()}
        self.assertEqual(len(by_name), len(driver.SC), 'duplicate scancode name')
        by_scan = {scan: name for name, scan in driver.SC.items()}
        self.assertEqual(len(by_scan), len(driver.SC), 'duplicate scancode value')
        resolved = {action: [by_scan[by_name[SDL_NAME_ALIASES.get(code, code)]]
                             for code in codes]
                    for action, codes in parsed.items()}
        self.assertEqual(resolved, driver.SEEDED_PROFILE)

    def test_every_seeded_key_is_sendable(self):
        for action, keys in driver.SEEDED_PROFILE.items():
            for key in keys:
                with self.subTest(action=action, key=key):
                    self.assertIn(key, driver.VK)
                    self.assertIn(key, driver.SC)

    def test_help_epilog_lists_every_seeded_action(self):
        epilog = driver.profile_epilog()
        self.assertIn('src/ui/lambo_frontend.cpp', epilog)
        for action in driver.SEEDED_PROFILE:
            self.assertIn(action, epilog)

    def test_failed_key_down_in_a_chord_still_releases_the_earlier_key(self):
        # A chord that dies part way used to leave the keys that did go down
        # held in SDL after this process exited.
        api = Mock()
        api.PostMessageW.side_effect = [1, 0, 1]  # x down, c rejected, x released
        with patch.object(driver, 'user32', api), patch.object(driver.time, 'sleep'), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5):
            with self.assertRaises(OSError):
                driver.press(123, ['x', 'c'], 150)
        self.assertEqual([c.args[1] for c in api.PostMessageW.call_args_list],
                         [0x0100, 0x0100, 0x0101])
        self.assertEqual(api.PostMessageW.call_args.args[2], driver.VK['x'])

    def test_failed_key_up_does_not_skip_the_remaining_releases(self):
        # Every release is still attempted, and a key left held is not success.
        api = Mock()
        # Both downs succeed; the x key-up is rejected and c must still be sent.
        api.PostMessageW.side_effect = [1, 1, 0, 1]
        with patch.object(driver, 'user32', api), patch.object(driver.time, 'sleep'), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5):
            with self.assertRaises(OSError) as result:
                driver.press(123, ['x', 'c'], 150)
        self.assertEqual([c.args[1] for c in api.PostMessageW.call_args_list],
                         [0x0100, 0x0100, 0x0101, 0x0101])
        self.assertEqual(api.PostMessageW.call_args.args[2], driver.VK['c'])
        self.assertIn('still held: x', str(result.exception))

    def test_original_failure_survives_a_release_that_also_fails(self):
        api = Mock()
        # x goes down, the c key-down is rejected, and the x release fails too.
        api.PostMessageW.side_effect = [1, 0, 0]
        with patch.object(driver, 'user32', api), patch.object(driver.time, 'sleep'), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5):
            with self.assertRaises(OSError) as result:
                driver.press(123, ['x', 'c'], 150)
        message = str(result.exception)
        self.assertIn('0x0100', message)
        self.assertIn('still held: x', message)

    def test_failed_click_release_is_reported(self):
        api = Mock()
        api.PostMessageW.side_effect = [1, 1, 0]  # move ok, down ok, up rejected
        with patch.object(driver, 'user32', api), patch.object(driver.time, 'sleep'), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5):
            with self.assertRaises(OSError) as result:
                driver.click(123, 10, 20)
        self.assertEqual([c.args[1] for c in api.PostMessageW.call_args_list],
                         [0x0200, 0x0201, 0x0202])
        self.assertIn('left button', str(result.exception))

    def test_cli_exits_nonzero_when_a_key_cannot_be_released(self):
        # The regression: a stuck key used to be swallowed and reported as success.
        api = Mock()
        api.PostMessageW.side_effect = [1, 1, 0, 1]
        with patch.object(driver, 'initialize_windows'), \
                patch.object(driver, 'find_window', return_value=(123, 'game')), \
                patch.object(driver, 'user32', api), patch.object(driver.time, 'sleep'), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5), \
                contextlib.redirect_stderr(io.StringIO()) as stderr:
            self.assertEqual(driver.main(['press', 'x,c', '150']), 1)
        self.assertIn('still held', stderr.getvalue())

    def test_failed_click_down_sends_no_button_up(self):
        # Nothing went down, so nothing is released; a stray up would hit-test
        # against an element the caller never pressed.
        api = Mock()
        api.PostMessageW.side_effect = [1, 0, 1]  # move ok, down rejected
        with patch.object(driver, 'user32', api), patch.object(driver.time, 'sleep'), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5):
            with self.assertRaises(OSError):
                driver.click(123, 10, 20)
        self.assertEqual([c.args[1] for c in api.PostMessageW.call_args_list],
                         [0x0200, 0x0201])

    def test_failure_after_a_click_down_still_releases_the_button(self):
        # The release must not depend on the rest of the sequence succeeding,
        # or an interruption leaves the left button held.
        api = Mock()
        with patch.object(driver, 'user32', api), \
                patch.object(driver.time, 'sleep', side_effect=[None, OSError('cut')]):
            with self.assertRaises(OSError):
                driver.click(123, 10, 20)
        self.assertEqual([c.args[1] for c in api.PostMessageW.call_args_list],
                         [0x0200, 0x0201, 0x0202])

    def test_failed_post_raises_instead_of_doing_nothing_silently(self):
        api = Mock()
        api.PostMessageW.return_value = 0
        with patch.object(driver, 'user32', api), patch.object(driver.time, 'sleep'), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5):
            with self.assertRaises(OSError) as result:
                driver.press(123, ['x'], 150)
            self.assertEqual(result.exception.errno, 5)

    def test_cli_reports_a_failed_post_with_a_nonzero_exit(self):
        api = Mock()
        api.PostMessageW.return_value = 0
        with patch.object(driver, 'initialize_windows'), \
                patch.object(driver, 'find_window', return_value=(123, 'game')), \
                patch.object(driver, 'user32', api), patch.object(driver.time, 'sleep'), \
                patch.object(ctypes, 'get_last_error', create=True, return_value=5), \
                contextlib.redirect_stderr(io.StringIO()) as stderr:
            self.assertEqual(driver.main(['press', 'x', '150']), 1)
        self.assertIn('PostMessageW', stderr.getvalue())

    def test_capture_uses_physical_client_size_and_deselects_bitmap(self):
        api, gdi, pil = Mock(), Mock(), Mock()
        def client_rect(hwnd, output):
            rect = output._obj
            rect.right, rect.bottom = 1280, 720
            return 1
        api.GetClientRect.side_effect = client_rect
        gdi.SelectObject.side_effect = [42, 43]
        def read_pixels(*args):
            self.assertEqual(gdi.SelectObject.call_count, 2)
            self.assertEqual(gdi.SelectObject.call_args.args[1], 42)
            return 720
        gdi.GetDIBits.side_effect = read_pixels
        with patch.object(driver, 'user32', api), patch.object(driver, 'gdi32', gdi), \
                patch.dict('sys.modules', {'PIL': pil}), \
                contextlib.redirect_stdout(io.StringIO()):
            driver.shot(123, 'capture.png')
        self.assertEqual(gdi.CreateCompatibleBitmap.call_args.args[1:], (1280, 720))
        self.assertEqual(pil.Image.frombuffer.call_args.args[1], (1280, 720))
        gdi.DeleteObject.assert_called_once()
        api.ReleaseDC.assert_called_once()


if __name__ == '__main__':
    unittest.main()
