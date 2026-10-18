"""Post physical keys to an existing Windows game window and capture its client area.
This is windowed automation, not the headless scenario runner.

SendInput does NOT reach the SDL window even when focused on this box; PostMessage
WM_KEYDOWN/WM_KEYUP straight to the HWND works (SDL2 translates them by scancode)
and needs no focus at all. PrintWindow(PW_RENDERFULLCONTENT) captures occluded.

Usage (window must already be running):
  python tools/drive_input.py find                  -> HWND + title, exit 1 if absent
  python tools/drive_input.py shot out.png          -> screenshot client area
  python tools/drive_input.py press <key>[,key] <ms>-> hold key(s) for ms
  python tools/drive_input.py click <x> <y>     -> click at client-area pixels

`press` sends literal physical keys, not game actions, and never reads or
writes the player's Controls profile. `press --help` prints the seeded
player-one keyboard profile, which lives in the keys[] table in
src/ui/lambo_frontend.cpp. tests/test_drive_input.py parses that table and fails
when the two disagree, so the documented mapping cannot drift silently.

Space, Left Shift and F are sendable physical keys that the seeded profile does
not bind. A player who remapped their Controls is not covered by that table;
check their profile. Disconnect controllers before a keyboard-only test and
restart: startup assigns player one to a connected controller when one is
available, otherwise to the keyboard. This helper posts keyboard messages, so it
can only drive a keyboard-bound player one at all.

Use about 600 ms for stick left/right and 150 ms for up/down as a starting
point (reported menu timings, not a guarantee for every screen).

Frontend keys, for driving the recompui overlay rather than the ROM menus:
  esc, f1   -> open the settings overlay (TOGGLE_MENU)
  f15       -> the mapped Back action. A real F15 keypress is translated by
               RmlSDL::ConvertKey to KI_F15, the same Rml::Input key the
               controller's B button is translated to by cont_button_to_key, so
               it exercises the identical menu-action path. F15 is not a game
               key: is_sdl_input_fake_mapped() drops it before the ROM.
  f16,f17   -> TAB_LEFT_MENU / TAB_RIGHT_MENU
  enter     -> ACCEPT_MENU; RmlUi activates the focused button on Return.

Manual v0.9.0 validation on a fresh configuration with no controller reached
Championship with enter, x and s; a/d moved left/right at 600 ms, w/s at 150 ms,
x and enter confirmed, and c went back. Space did not confirm. Full 1600x900
captures matched the client area at 144 DPI (150%). Left Shift and the C buttons
were not separately validated. The championship exit in #318 was not exercised.
These observations apply to that keyboard-only profile, not arbitrary mappings
or every menu.

An earlier revision of this file recorded a proven route to a 1P arcade race
from attract (enter, enter, x, x, x, x, x, x). It was not re-validated under
this profile and is not repeated here as fact. For repeatable headless races,
use tools/run_game_scenario.py.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import sys
import time

user32 = None
gdi32 = None


def initialize_windows():
    """Opt out of DPI virtualization before any window/coordinate access."""
    global user32, gdi32
    if sys.platform != 'win32':
        raise RuntimeError('window automation requires Windows')
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    gdi32 = ctypes.WinDLL('gdi32', use_last_error=True)
    dpi = user32.SetProcessDpiAwarenessContext
    dpi.argtypes = [ctypes.c_void_p]
    dpi.restype = wt.BOOL
    if not dpi(ctypes.c_void_p(-4)):  # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        error = ctypes.get_last_error()
        # A manifest or previous call may already have set the desired mode.
        # Verify the effective thread context, including any host override.
        current = user32.GetThreadDpiAwarenessContext
        current.argtypes = []
        current.restype = ctypes.c_void_p
        equal = user32.AreDpiAwarenessContextsEqual
        equal.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        equal.restype = wt.BOOL
        if not equal(current(), ctypes.c_void_p(-4)):
            raise ctypes.WinError(error)
    # ctypes otherwise defaults to 32-bit int returns, truncating GDI handles
    # on 64-bit Python. Keep window, click and capture handles pointer-sized.
    signatures = [
        (user32.GetDC, [wt.HWND], wt.HDC),
        (user32.ReleaseDC, [wt.HWND, wt.HDC], ctypes.c_int),
        (user32.GetClientRect, [wt.HWND, ctypes.POINTER(wt.RECT)], wt.BOOL),
        (user32.PrintWindow, [wt.HWND, wt.HDC, wt.UINT], wt.BOOL),
        (user32.PostMessageW, [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM], wt.BOOL),
        (user32.GetClassNameW, [wt.HWND, wt.LPWSTR, ctypes.c_int], ctypes.c_int),
        (user32.GetWindowTextW, [wt.HWND, wt.LPWSTR, ctypes.c_int], ctypes.c_int),
        (user32.IsWindowVisible, [wt.HWND], wt.BOOL),
        (gdi32.CreateCompatibleDC, [wt.HDC], wt.HDC),
        (gdi32.CreateCompatibleBitmap, [wt.HDC, ctypes.c_int, ctypes.c_int], wt.HBITMAP),
        (gdi32.SelectObject, [wt.HDC, wt.HANDLE], wt.HANDLE),
        (gdi32.GetDIBits, [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT,
                          ctypes.c_void_p, ctypes.c_void_p, wt.UINT], ctypes.c_int),
        (gdi32.DeleteObject, [wt.HANDLE], wt.BOOL),
        (gdi32.DeleteDC, [wt.HDC], wt.BOOL),
    ]
    for function, args, result in signatures:
        function.argtypes = args
        function.restype = result

# SDL2 derives keysym.sym from the lParam scancode, not the wParam VK, so SC is
# what has to be right. The F-key scancodes jump: F1-F10 are 0x3B-0x44, then
# F11=0x57 ... F17=0x5D, F18=0x5E. Do not extrapolate from F1.
VK = {'x': 0x58, 'c': 0x43, 'z': 0x5A, 'enter': 0x0D,
      'up': 0x26, 'down': 0x28, 'left': 0x25, 'right': 0x27,
      'esc': 0x1B, 'tab': 0x09, 'f1': 0x70, 'f11': 0x7A, 'f15': 0x7E, 'f16': 0x7F, 'f17': 0x80}
SC = {'x': 0x2D, 'c': 0x2E, 'z': 0x2C, 'enter': 0x1C,
      'up': 0x48, 'down': 0x50, 'left': 0x4B, 'right': 0x4D,
      'esc': 0x01, 'tab': 0x0F, 'f1': 0x3B, 'f11': 0x57, 'f15': 0x5B, 'f16': 0x5C, 'f17': 0x5D}
EXTENDED = {'up', 'down', 'left', 'right'}
for key, scan in {'w': 0x11, 'a': 0x1E, 's': 0x1F, 'd': 0x20,
                  'i': 0x17, 'j': 0x24, 'k': 0x25, 'l': 0x26,
                  'q': 0x10, 'e': 0x12, 'r': 0x13, 'f': 0x21}.items():
    VK[key], SC[key] = ord(key.upper()), scan
VK.update(space=0x20, lshift=0x10)  # WM_KEYDOWN uses VK_SHIFT; scan selects left.
SC.update(space=0x39, lshift=0x2A)

# Player one's seeded keyboard bindings, mirroring the keys[] table in
# src/ui/lambo_frontend.cpp. The SC values above are SDL_SCANCODE values (SDL
# uses USB HID usage IDs), so a scancode read out of that table lands directly
# in this dict. tests/test_drive_input.py parses the C++ and compares it against
# this mapping, and profile_epilog() renders the help from it, so neither the
# table nor the help text can drift without failing a test.
SEEDED_PROFILE = {
    'A': ['x'], 'B': ['c'], 'Z': ['z'], 'START': ['enter'],
    'L': ['q'], 'R': ['e'],
    'C_UP': ['i'], 'C_DOWN': ['k'], 'C_LEFT': ['j'], 'C_RIGHT': ['l'],
    'STICK_LEFT': ['left', 'a'], 'STICK_RIGHT': ['right', 'd'],
    'STICK_UP': ['up', 'w'], 'STICK_DOWN': ['down', 's'],
}


def profile_epilog():
    """Render SEEDED_PROFILE for --help so the help has no second copy."""
    width = max(len(action) for action in SEEDED_PROFILE)
    rows = '\n'.join(f'{action.ljust(width)}  {"+".join(keys)}'
                     for action, keys in SEEDED_PROFILE.items())
    return ('Seeded player-one keyboard profile, from src/ui/lambo_frontend.cpp:\n'
            f'{rows}\n'
            'A stick entry lists both bindings. Space, Left Shift and F are '
            'sendable keys this profile does not bind.')


def find_window():
    # Match both SDL2's window class and the game's exact title. Steam also uses
    # SDL_app, while editors/browsers can contain the repo name in their title; either
    # predicate alone can silently send input and screenshots to the wrong window.
    hwnds = []
    def cb(h, lparam):
        cls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(h, cls, 256)
        if cls.value == 'SDL_app' and user32.IsWindowVisible(h):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(h, buf, 256)
            if buf.value == 'Automobili Lamborghini':
                hwnds.append((h, buf.value))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)(cb), 0)
    return hwnds[0] if hwnds else (None, None)


def _post(hwnd, message, wparam, lparam):
    """Post one message, and fail loudly instead of silently doing nothing.

    An ignored PostMessageW return is the "no visible result and no error"
    symptom reported in #319, for example when the game runs elevated and UIPI
    drops the post. Callers report the winerror and exit non-zero.
    """
    if not user32.PostMessageW(hwnd, message, wparam, lparam):
        raise OSError(ctypes.get_last_error(),
                      f'PostMessageW(0x{message:04X}) to window {hwnd} failed')


def press(h, keys, ms):
    # Validate the entire chord before posting anything (no partially held keys).
    if ms < 0 or not keys or any(k not in VK for k in keys):
        raise ValueError('use known physical key names and a non-negative duration')
    for k in keys:
        ext = (1 << 24) if k in EXTENDED else 0
        _post(h, 0x0100, VK[k], 1 | (SC[k] << 16) | ext)
    time.sleep(ms / 1000.0)
    for k in keys:
        ext = (1 << 24) if k in EXTENDED else 0
        _post(h, 0x0101, VK[k],
              1 | (SC[k] << 16) | ext | (1 << 30) | (1 << 31))
    time.sleep(0.15)


def click(h, x, y):
    lp = (y << 16) | (x & 0xFFFF)
    # SDL2 tracks the cursor from WM_MOUSEMOVE; a bare WM_LBUTTONDOWN can arrive
    # with a stale position and hit-test against the wrong element.
    _post(h, 0x0200, 0, lp)  # WM_MOUSEMOVE
    time.sleep(0.08)
    _post(h, 0x0201, 1, lp)  # WM_LBUTTONDOWN, MK_LBUTTON
    time.sleep(0.08)
    _post(h, 0x0202, 0, lp)  # WM_LBUTTONUP
    time.sleep(0.15)


def shot(h, path):
    rect = wt.RECT()
    user32.GetClientRect(h, ctypes.byref(rect))
    w, hgt = rect.right, rect.bottom
    hdc = user32.GetDC(h)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, hgt)
    previous = gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(h, mdc, 3)  # PW_RENDERFULLCONTENT | PW_CLIENTONLY
    # GetDIBits requires the bitmap not to be selected into a DC.
    gdi32.SelectObject(mdc, previous)

    class BMIH(ctypes.Structure):
        _fields_ = [("sz", wt.DWORD), ("w", ctypes.c_long), ("h", ctypes.c_long),
                    ("planes", wt.WORD), ("bpp", wt.WORD), ("comp", wt.DWORD),
                    ("szImg", wt.DWORD), ("xppm", ctypes.c_long),
                    ("yppm", ctypes.c_long), ("clrUsed", wt.DWORD),
                    ("clrImp", wt.DWORD)]

    bi = BMIH(ctypes.sizeof(BMIH), w, -hgt, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * hgt * 4)
    gdi32.GetDIBits(mdc, bmp, 0, hgt, buf, ctypes.byref(bi), 0)
    from PIL import Image
    Image.frombuffer('RGBA', (w, hgt), buf, 'raw', 'BGRA', 0, 1).convert('RGB').save(path)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(h, hdc)
    print(f"saved {path} {w}x{hgt}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, epilog=profile_epilog(),
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='cmd', required=True)
    commands.add_parser('find')
    commands.add_parser('shot').add_argument('path')
    click_parser = commands.add_parser('click')
    click_parser.add_argument('x', type=int)
    click_parser.add_argument('y', type=int)
    press_parser = commands.add_parser(
        'press', epilog=profile_epilog(),
        formatter_class=argparse.RawDescriptionHelpFormatter)
    press_parser.add_argument('keys', help='comma-separated physical keys: ' + ', '.join(VK))
    press_parser.add_argument('ms', type=int)
    args = parser.parse_args(argv)
    if args.cmd == 'press':
        keys = args.keys.split(',')
        if args.ms < 0 or any(k not in VK for k in keys):
            parser.error('use known physical key names and a non-negative duration')
    # initialize_windows() is deliberately outside the handler below: a refused
    # DPI request is a setup failure, not a failed command.
    initialize_windows()
    h, title = find_window()
    if args.cmd == 'find':
        print(h, title)
        return 0 if h else 1
    if not h:
        print("window not found")
        return 1
    try:
        if args.cmd == 'shot':
            shot(h, args.path)
        elif args.cmd == 'click':
            click(h, args.x, args.y)
        elif args.cmd == 'press':
            press(h, keys, args.ms)
    except OSError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
