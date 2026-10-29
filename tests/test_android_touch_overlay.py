"""Exercise TouchOverlay's real gesture dispatch against a minimal Android boundary.

The overlay decides whether a gesture belongs to it or to the game surface
underneath, and Android ties that decision to ACTION_DOWN: a View that declines
the first event is dropped from the gesture, so a finger landing on empty space
would hide the second finger and the slide onto the stick from the scene. That
rule lives in TouchOverlay.onTouchEvent, which the TouchControls model test
cannot reach, so the View is compiled against small stand-ins for the Android
classes it uses. Skipped when no JDK is installed.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import ROOT

MAIN = ROOT / "android/app/src/main/java/io/github/alondero/lamborghinirecomp"
TEST = ROOT / "tests/android/TouchOverlayTest.java"

STUBS = {
    "android/content/Context.java": """package android.content;
public class Context {
 public android.content.res.Resources getResources() { return new android.content.res.Resources(); }
}""",
    "android/content/res/Resources.java": """package android.content.res;
public class Resources {
 public android.util.DisplayMetrics getDisplayMetrics() { return new android.util.DisplayMetrics(); }
}""",
    "android/util/DisplayMetrics.java": """package android.util;
public class DisplayMetrics { public float density = 1f; }""",
    "android/graphics/Canvas.java": """package android.graphics;
public class Canvas {
 public void drawCircle(float x,float y,float r,Paint p) {}
 public void drawRoundRect(RectF r,float rx,float ry,Paint p) {}
 public void drawLine(float x0,float y0,float x1,float y1,Paint p) {}
 public void drawText(String s,float x,float y,Paint p) {}
}""",
    "android/graphics/Paint.java": """package android.graphics;
public class Paint {
 public static final int ANTI_ALIAS_FLAG = 1;
 public enum Align { LEFT, CENTER, RIGHT }
 public enum Style { FILL, STROKE }
 public enum Cap { BUTT, ROUND }
 public Paint(int flags) {}
 public void setTextAlign(Align a) {}
 public void setTypeface(Typeface t) {}
 public void setStyle(Style s) {}
 public void setStrokeWidth(float w) {}
 public void setStrokeCap(Cap c) {}
 public void setColor(int c) {}
 public void setTextSize(float s) {}
}""",
    "android/graphics/RectF.java": """package android.graphics;
public class RectF {
 public float left, top, right, bottom;
 public void set(float l,float t,float r,float b) { left=l; top=t; right=r; bottom=b; }
}""",
    "android/graphics/Typeface.java": """package android.graphics;
public class Typeface { public static final Typeface DEFAULT_BOLD = new Typeface(); }""",
    "android/view/DisplayCutout.java": """package android.view;
public class DisplayCutout {
 public int getSafeInsetLeft() { return 0; }
 public int getSafeInsetTop() { return 0; }
 public int getSafeInsetRight() { return 0; }
 public int getSafeInsetBottom() { return 0; }
}""",
    "android/view/KeyEvent.java": """package android.view;
public class KeyEvent {
 public static final int KEYCODE_DPAD_LEFT = 21, KEYCODE_DPAD_RIGHT = 22, KEYCODE_DPAD_UP = 19, KEYCODE_DPAD_DOWN = 20;
 public static final int KEYCODE_X = 52, KEYCODE_C = 31, KEYCODE_I = 37, KEYCODE_J = 38, KEYCODE_K = 39, KEYCODE_L = 40;
 public static final int KEYCODE_ENTER = 66, KEYCODE_F1 = 131;
}""",
    "android/view/MotionEvent.java": """package android.view;
public class MotionEvent {
 public static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3;
 public static final int ACTION_POINTER_DOWN = 5, ACTION_POINTER_UP = 6;
 private final int action, index;
 private final int[] ids;
 private final float[] xs, ys;
 public MotionEvent(int action,int index,int[] ids,float[] xs,float[] ys) {
  this.action=action; this.index=index; this.ids=ids; this.xs=xs; this.ys=ys;
 }
 public int getActionMasked() { return action; }
 public int getActionIndex() { return index; }
 public int getPointerCount() { return ids.length; }
 public int getPointerId(int i) { return ids[i]; }
 public float getX(int i) { return xs[i]; }
 public float getY(int i) { return ys[i]; }
}""",
    "android/view/View.java": """package android.view;
public class View {
 public View(android.content.Context c) {}
 public WindowInsets onApplyWindowInsets(WindowInsets i) { return i; }
 protected void onSizeChanged(int w,int h,int ow,int oh) {}
 protected void onDraw(android.graphics.Canvas c) {}
 public boolean onTouchEvent(MotionEvent e) { return false; }
 public void invalidate() {}
 public int getWidth() { return 0; }
 public int getHeight() { return 0; }
}""",
    "android/view/WindowInsets.java": """package android.view;
public class WindowInsets {
 public int getSystemWindowInsetLeft() { return 0; }
 public int getSystemWindowInsetTop() { return 0; }
 public int getSystemWindowInsetRight() { return 0; }
 public int getSystemWindowInsetBottom() { return 0; }
 public DisplayCutout getDisplayCutout() { return null; }
}""",
}


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK not installed")
class TouchOverlayDispatchTests(unittest.TestCase):
    def test_shown_pad_keeps_the_gesture_and_hidden_pad_declines_it(self):
        with tempfile.TemporaryDirectory(prefix="lambo-overlay-") as temporary:
            tree = Path(temporary)
            files = []
            for name, source in STUBS.items():
                path = tree / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding="utf-8")
                files.append(str(path))
            files += [str(MAIN / "TouchControls.java"), str(MAIN / "TouchOverlay.java"), str(TEST)]
            subprocess.run(["javac", "-d", str(tree), *files], check=True, capture_output=True)
            result = subprocess.run(
                ["java", "-cp", str(tree), "io.github.alondero.lamborghinirecomp.TouchOverlayTest"],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
