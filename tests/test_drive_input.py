"""Window-free tests of the Windows message driver; no game or ROM required."""
import contextlib
import ctypes
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location(
    'drive_input', Path(__file__).resolve().parents[1] / 'tools' / 'drive_input.py')
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


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
