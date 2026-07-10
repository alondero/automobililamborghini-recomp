"""Build PR-quality before/after composites for the xBRZ HD pack.

Drives the running game window from attract through to a 1P arcade race with the
LAMBO_TEXTURE_PACK env var toggled, captures identical-frame baselines and pack
shots, and writes composited A/B PNGs to docs/images/. Each pair is captured on
the same in-game moment so the diff is the pack, not the simulation.
"""

import os, subprocess, sys, time
from pathlib import Path
from PIL import Image, ImageDraw

s = Path(r"C:\Users\alond\AppData\Local\Temp\claude\F--src-automobililamborghini-recomp--claude-worktrees-pr66-slim-numbered-scabbard\f41ca88e-9879-4321-9068-7ef958896e23\scratchpad")
pack = s / "pack_xbrz"
shots = s / "shots_pr"
shots.mkdir(parents=True, exist_ok=True)
docs_imgs = Path(r"F:\src\automobililamborghini-recomp\.claude\worktrees\pr66-slim-numbered-scabbard\docs\images")
docs_imgs.mkdir(parents=True, exist_ok=True)

t = Path(r"F:\src\automobililamborghini-recomp\.claude\worktrees\pr66-slim-numbered-scabbard\tools\drive_input.py")
exe = r"F:\src\automobililamborghini-recomp\build\lamborghini_modern.exe"
workdir = r"F:\src\automobililamborghini-recomp\build"


def drive(*args):
    subprocess.run([sys.executable, str(t), *args], check=True)


def kill():
    subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                    "Stop-Process -Name lamborghini_modern -Force -Confirm:$false"],
                   shell=False, check=False)
    time.sleep(2)


def launch(pack_dir):
    kill()
    env = os.environ.copy()
    env["LAMBO_TEXTURE_PACK"] = pack_dir or ""
    subprocess.Popen([exe], cwd=workdir, env=env,
                     creationflags=subprocess.DETACHED_PROCESS)
    time.sleep(14)


def press(key, ms):
    drive("press", key, str(ms))


def to_title():
    press("enter", 80); time.sleep(2.5)
    press("enter", 80); time.sleep(2.5)


def to_race_from_title():
    # from START menu: 1P, ARCADE, BASIC, car, name DONE, pak OK, then race loads
    for _ in range(7):
        press("x", 80); time.sleep(3.0)
    time.sleep(14)


def accelerate(seconds):
    press("x", int(seconds * 1000))


def shot(name):
    drive("shot", str(shots / name))


print("baseline: no pack")
launch("")
to_title()
shot("title.png")
to_race_from_title()
shot("race_static.png")
accelerate(5)
shot("race_moving.png")
kill()

print("pack: xbrz")
launch(str(pack))
to_title()
shot("title_pack.png")
to_race_from_title()
shot("race_static_pack.png")
accelerate(5)
shot("race_moving_pack.png")
kill()


def pair(label, base, pack_shot, out, scale=1.0):
    a = Image.open(shots / base).convert("RGB")
    b = Image.open(shots / pack_shot).convert("RGB")
    if scale != 1.0:
        w, h = a.size
        a = a.resize((int(w * scale), int(h * scale)), Image.LANCZOS)
        b = b.resize((int(w * scale), int(h * scale)), Image.LANCZOS)
    W, H = a.size
    gap = 6
    header = 32
    out_img = Image.new("RGB", (W * 2 + gap, H + header), (15, 15, 15))
    d = ImageDraw.Draw(out_img)
    d.text((10, 8), f"baseline ({label})", fill=(220, 220, 220))
    d.text((W + gap + 10, 8), f"xBRZ pack ({label})", fill=(220, 220, 220))
    out_img.paste(a, (0, header))
    out_img.paste(b, (W + gap, header))
    out_img.save(docs_imgs / out)
    print(f"wrote {out} {(W*2+gap)}x{H+header}")


pair("title (PRESS START)", "title.png",       "title_pack.png",       "xbrz-pack-title-before-after.png")
pair("race HUD (LAP 0/4)",  "race_static.png", "race_static_pack.png", "xbrz-pack-race-hud-before-after.png")
pair("race HUD (in motion)", "race_moving.png", "race_moving_pack.png", "xbrz-pack-race-moving-before-after.png", scale=0.75)