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
Keys: x(=A) c(=B) z(=Z) enter(=Start) up down left right (stick)

Proven route to a 1P arcade race from attract (wait ~2.5s between presses, longer
after the pak message): enter, enter, x (ONE PLAYER), x (ARCADE), x (BASIC SERIES),
x (car SELECT), x (name DONE), x (pak message OK) -> race loads in ~10 s.
"""
import ctypes, ctypes.wintypes as wt, sys, time

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

VK = {'x': 0x58, 'c': 0x43, 'z': 0x5A, 'enter': 0x0D,
      'up': 0x26, 'down': 0x28, 'left': 0x25, 'right': 0x27}
SC = {'x': 0x2D, 'c': 0x2E, 'z': 0x2C, 'enter': 0x1C,
      'up': 0x48, 'down': 0x50, 'left': 0x4B, 'right': 0x4D}
EXTENDED = {'up', 'down', 'left', 'right'}


def find_window():
    # Match SDL2's window class ('SDL_app'), NOT a title substring: an editor or
    # browser with the repo ("...lamborghini-recomp...") open in a tab would
    # otherwise match first and steal the screenshot/key events.
    hwnds = []
    def cb(h, l):
        cls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(h, cls, 256)
        if cls.value == 'SDL_app' and user32.IsWindowVisible(h):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(h, buf, 256)
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
    elif cmd == 'press':
        press(h, sys.argv[2].split(','), int(sys.argv[3]))
