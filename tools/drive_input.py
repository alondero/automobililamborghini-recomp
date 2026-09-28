"""Drive the running lamborghini_modern window without a human: post key events and
capture screenshots. Lets a headless session navigate the menus into a REAL race
(attract/demo draws no HUD, so HUD work needs this).

SendInput does NOT reach the SDL window even when focused on this box; PostMessage
WM_KEYDOWN/WM_KEYUP straight to the HWND works (SDL2 translates them by scancode)
and needs no focus at all. PrintWindow(PW_RENDERFULLCONTENT) captures occluded.

Usage (window must already be running):
  python tools/drive_input.py find                  -> HWND + title, exit 1 if absent
  python tools/drive_input.py shot out.png          -> screenshot client area
  python tools/drive_input.py press <key>[,key] <ms>-> hold key(s) for ms
  python tools/drive_input.py click <x> <y>     -> click at client-area pixels
Keys: x(=A) c(=B) z(=Z) enter(=Start) up down left right (stick)

Frontend keys, for driving the recompui overlay rather than the ROM menus:
  esc, f1   -> open the settings overlay (TOGGLE_MENU)
  f15       -> the mapped Back action. A real F15 keypress is translated by
               RmlSDL::ConvertKey to KI_F15, the same Rml::Input key the
               controller's B button is translated to by cont_button_to_key, so
               it exercises the identical menu-action path. F15 is not a game
               key: is_sdl_input_fake_mapped() drops it before the ROM.
  f16,f17   -> TAB_LEFT_MENU / TAB_RIGHT_MENU
  enter     -> ACCEPT_MENU; RmlUi activates the focused button on Return.

Proven route to a 1P arcade race from attract (wait ~2.5s between presses, longer
after the pak message): enter, enter, x (ONE PLAYER), x (ARCADE), x (BASIC SERIES),
x (car SELECT), x (name DONE), x (pak message OK) -> race loads in ~10 s.
"""
import ctypes, ctypes.wintypes as wt, sys, time

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

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


def find_window():
    # Match both SDL2's window class and the game's exact title. Steam also uses
    # SDL_app, while editors/browsers can contain the repo name in their title; either
    # predicate alone can silently send input and screenshots to the wrong window.
    hwnds = []
    def cb(h, l):
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
    gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(h, mdc, 3)  # PW_RENDERFULLCONTENT | PW_CLIENTONLY

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


if __name__ == '__main__':
    cmd = sys.argv[1]
    h, title = find_window()
    if cmd == 'find':
        print(h, title)
        sys.exit(0 if h else 1)
    if not h:
        print("window not found")
        sys.exit(1)
    if cmd == 'shot':
        shot(h, sys.argv[2])
    elif cmd == 'click':
        click(h, int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == 'press':
        press(h, sys.argv[2].split(','), int(sys.argv[3]))
