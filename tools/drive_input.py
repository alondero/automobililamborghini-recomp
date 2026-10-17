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
Generic framework profile: space=A, lshift=B, q=Z, e=L, r=R, enter=Start;
w/a/s/d=stick up/left/down/right, i/j/k/l=D-pad, arrows=C buttons.
The port's player-one keyboard profile differs: x=A, c=B, z=Z and arrows
also steer. In a fresh v0.9.0 keyboard-only run, x/c, enter and w/a/s/d
worked; space did not confirm. Check the assigned player's profile.
Disconnect controllers before starting a keyboard-only test: a connected
controller is assigned to player one instead of the keyboard at startup.
Key names are physical keys, not game actions. x/c/z and arrow keys retain
their literal meaning for custom/legacy profiles. No player config is read or changed.
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

Manual v0.9.0 validation reached Championship with enter, x and s; a/d moved
right/left, w moved up, and c went back. Full 1600x900 captures matched the
client area at 144 DPI (150%). These observations apply to that keyboard-only
profile, not arbitrary mappings or every menu.
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


def press(h, keys, ms):
    # Validate the entire chord before posting anything (no partially held keys).
    if ms < 0 or not keys or any(k not in VK for k in keys):
        raise ValueError('use known physical key names and a non-negative duration')
    for k in keys:
        ext = (1 << 24) if k in EXTENDED else 0
        user32.PostMessageW(h, 0x0100, VK[k], 1 | (SC[k] << 16) | ext)
    time.sleep(ms / 1000.0)
    for k in keys:
        ext = (1 << 24) if k in EXTENDED else 0
        user32.PostMessageW(h, 0x0101, VK[k],
                            1 | (SC[k] << 16) | ext | (1 << 30) | (1 << 31))
    time.sleep(0.15)


def click(h, x, y):
    lp = (y << 16) | (x & 0xFFFF)
    # SDL2 tracks the cursor from WM_MOUSEMOVE; a bare WM_LBUTTONDOWN can arrive
    # with a stale position and hit-test against the wrong element.
    user32.PostMessageW(h, 0x0200, 0, lp)  # WM_MOUSEMOVE
    time.sleep(0.08)
    user32.PostMessageW(h, 0x0201, 1, lp)  # WM_LBUTTONDOWN, MK_LBUTTON
    time.sleep(0.08)
    user32.PostMessageW(h, 0x0202, 0, lp)  # WM_LBUTTONUP
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
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='cmd', required=True)
    commands.add_parser('find')
    commands.add_parser('shot').add_argument('path')
    click_parser = commands.add_parser('click')
    click_parser.add_argument('x', type=int)
    click_parser.add_argument('y', type=int)
    press_parser = commands.add_parser('press')
    press_parser.add_argument('keys', help='comma-separated physical keys: ' + ', '.join(VK))
    press_parser.add_argument('ms', type=int)
    args = parser.parse_args(argv)
    if args.cmd == 'press':
        keys = args.keys.split(',')
        if args.ms < 0 or any(k not in VK for k in keys):
            parser.error('use known physical key names and a non-negative duration')
    initialize_windows()
    h, title = find_window()
    if args.cmd == 'find':
        print(h, title)
        return 0 if h else 1
    if not h:
        print("window not found")
        return 1
    if args.cmd == 'shot':
        shot(h, args.path)
    elif args.cmd == 'click':
        click(h, args.x, args.y)
    elif args.cmd == 'press':
        press(h, keys, args.ms)
    return 0


if __name__ == '__main__':
    sys.exit(main())
