"""Run GameActivity's touch-release and touch-visibility callbacks against a minimal Android boundary."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import ROOT

JAVA = ROOT / "android/app/src/main/java/io/github/alondero/lamborghinirecomp"


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK not installed")
class ActivityLifecycleTests(unittest.TestCase):
    def test_surface_loss_releases_controls_without_activity_pause(self):
        stubs = {
            "android/os/Bundle.java": "package android.os; public class Bundle {}",
            "android/os/Looper.java": "package android.os; public class Looper { public static Looper getMainLooper() { return null; } }",
            "android/os/Handler.java": "package android.os; public class Handler { public Handler(Looper l) {} }",
            "android/system/Os.java": "package android.system; public class Os { public static void setenv(String a,String b,boolean c) {} }",
            "android/content/SharedPreferences.java": """package android.content;
public interface SharedPreferences {
 String getString(String key, String fallback);
 Editor edit();
 interface Editor { Editor putString(String key, String value); void apply(); }
}""",
            "android/hardware/input/InputManager.java": """package android.hardware.input;
import android.os.Handler;
public class InputManager {
 public static InputManager instance = new InputManager();
 public static InputDeviceListener listener;
 public interface InputDeviceListener {
  void onInputDeviceAdded(int deviceId);
  void onInputDeviceRemoved(int deviceId);
  void onInputDeviceChanged(int deviceId);
 }
 public void registerInputDeviceListener(InputDeviceListener l, Handler h) { listener = l; }
 public void unregisterInputDeviceListener(InputDeviceListener l) { if (listener == l) listener = null; }
}""",
            "android/view/InputDevice.java": """package android.view;
public class InputDevice {
 public static int[] ids = new int[0];
 public static int[] getDeviceIds() { return ids; }
}""",
            "android/view/SurfaceHolder.java": """package android.view;
public interface SurfaceHolder {
 interface Callback { void surfaceCreated(SurfaceHolder h); void surfaceChanged(SurfaceHolder h,int f,int w,int t); void surfaceDestroyed(SurfaceHolder h); }
 void addCallback(Callback c);
}""",
            "android/widget/RelativeLayout.java": """package android.widget;
public class RelativeLayout {
 public void addView(Object o, LayoutParams p) {}
 public static class LayoutParams { public static int MATCH_PARENT=-1; public LayoutParams(int a,int b) {} }
}""",
            "org/libsdl/app/SDLControllerManager.java": """package org.libsdl.app;
public class SDLControllerManager { public static boolean isDeviceSDLJoystick(int deviceId) { return deviceId >= 0; } }""",
            "org/libsdl/app/SDLActivity.java": """package org.libsdl.app;
import android.view.SurfaceHolder;
public class SDLActivity {
 protected android.widget.RelativeLayout mLayout = new android.widget.RelativeLayout();
 protected Surface mSurface = new Surface();
 public static int down,up,pauses;
 public static final int MODE_PRIVATE = 0;
 public static final String INPUT_SERVICE = "input";
 public static android.content.SharedPreferences prefs;
 public static class Surface { public SurfaceHolder getHolder() { return c -> {}; } }
 public static class Info { public String nativeLibraryDir="native"; }
 public Info getApplicationInfo() { return new Info(); }
 public java.io.File getCacheDir() { return new java.io.File("cache"); }
 public android.content.SharedPreferences getSharedPreferences(String name, int mode) { return prefs; }
 public Object getSystemService(String name) { return android.hardware.input.InputManager.instance; }
 protected String[] getLibraries() { return null; }
 public void loadLibraries() {}
 protected void onCreate(android.os.Bundle state) {}
 protected void onResume() {}
 protected void onPause() { ++pauses; }
 protected void onDestroy() {}
 public static void onNativeKeyDown(int key) { ++down; }
 public static void onNativeKeyUp(int key) { ++up; }
}""",
            "io/github/alondero/lamborghinirecomp/TouchOverlay.java": """package io.github.alondero.lamborghinirecomp;
import java.util.HashSet;
// Mirrors the real overlay's contract: hiding releases the keys it holds, for example through
// TouchControls. The real state machine is covered by tests/android/TouchControlsTest.java.
class TouchOverlay {
 interface Sink { void key(int key,boolean down); }
 static TouchOverlay instance;
 static boolean controls = true, menu = true;
 final Sink sink;
 private final HashSet<Integer> held = new HashSet<>();
 TouchOverlay(Object context,Sink sink) { this.sink=sink; instance=this; }
 void reset() { release(); }
 void setShown(boolean showControls,boolean showMenu) {
  controls = showControls; menu = showMenu;
  if (!showControls) release();
 }
 void press(int key) { held.add(key); sink.key(key,true); }
 private void release() { for (int key : held) sink.key(key,false); held.clear(); }
}""",
            "io/github/alondero/lamborghinirecomp/DriverImport.java": "package io.github.alondero.lamborghinirecomp; class DriverImport { static void configure(Object o) {} static void releaseGameLock() {} }",
            "io/github/alondero/lamborghinirecomp/ActivityTest.java": """package io.github.alondero.lamborghinirecomp;
import android.hardware.input.InputManager;
import android.view.InputDevice;
import org.libsdl.app.SDLActivity;
public class ActivityTest {
 static class Prefs implements android.content.SharedPreferences {
  final java.util.HashMap<String,String> values = new java.util.HashMap<>();
  public String getString(String key,String fallback) { return values.containsKey(key) ? values.get(key) : fallback; }
  public Editor edit() { return new Editor() {
   public Editor putString(String key,String value) { values.put(key,value); return this; }
   public void apply() {}
  }; }
 }
 static void check(boolean value, String what) { if (!value) throw new AssertionError(what); }
 static void noController() { InputDevice.ids = new int[] {-1}; }
 static void controller() { InputDevice.ids = new int[] {3}; }
 public static void main(String[] args) {
  SDLActivity.prefs = new Prefs();
  noController();
  GameActivity activity=new GameActivity(); activity.onCreate(null);
  check(SDLActivity.down==0 && SDLActivity.up==0, "create sends no keys");
  check(TouchOverlay.controls && TouchOverlay.menu, "the controls start visible without a controller");

  TouchOverlay.instance.press(21); TouchOverlay.instance.press(22);
  activity.surfaceDestroyed(null);
  check(SDLActivity.down==2 && SDLActivity.up==2 && SDLActivity.pauses==0,
        "surface loss releases the controls without pausing the activity");
  activity.surfaceCreated(null); activity.surfaceChanged(null,0,1280,720);
  activity.onPause(); activity.surfaceDestroyed(null);
  check(SDLActivity.up==2, "a duplicate loss cannot release phantom keys");
  TouchOverlay.instance.press(21);
  activity.surfaceDestroyed(null);
  check(SDLActivity.down==3 && SDLActivity.up==3, "a key after duplicate loss is released once");

  // A controller joining while the game runs hides the pad and releases what it held.
  TouchOverlay.instance.press(23);
  controller();
  InputManager.listener.onInputDeviceAdded(3);
  check(!TouchOverlay.controls, "a connected controller hides the touch controls");
  check(TouchOverlay.menu, "hiding keeps the Menu button, the route into Settings");
  check(SDLActivity.up==4, "hiding releases the held key");

  // Disconnecting brings them back without a restart.
  noController();
  InputManager.listener.onInputDeviceRemoved(3);
  check(TouchOverlay.controls && TouchOverlay.menu, "disconnect shows the controls again");

  // Surface loss while hidden keeps the rule.
  controller();
  InputManager.listener.onInputDeviceAdded(3);
  check(!TouchOverlay.controls, "the controller hides the controls again");
  activity.surfaceDestroyed(null);
  check(!TouchOverlay.controls && SDLActivity.up==4, "surface loss keeps the hidden state");

  // Resume re-reads presence, for a change the app missed while backgrounded.
  noController();
  activity.onPause(); activity.onResume();
  check(TouchOverlay.controls, "resume shows the controls again");

  // Never hides the whole overlay, including Menu; the launcher is then the way back.
  SDLActivity.prefs.edit().putString(TouchOverlayPolicy.KEY, TouchOverlayPolicy.NEVER).apply();
  new GameActivity().onCreate(null);
  check(!TouchOverlay.controls && !TouchOverlay.menu, "Never hides Menu as well");

  // Always ignores controller presence.
  SDLActivity.prefs.edit().putString(TouchOverlayPolicy.KEY, TouchOverlayPolicy.ALWAYS).apply();
  controller();
  new GameActivity().onCreate(null);
  check(TouchOverlay.controls && TouchOverlay.menu, "Always keeps the controls with a controller connected");
 }
}""",
        }
        with tempfile.TemporaryDirectory(prefix="lambo-activity-") as temporary:
            tree = Path(temporary)
            files = []
            for name, source in stubs.items():
                path = tree / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding="utf-8")
                files.append(str(path))
            files.append(str(JAVA / "GameActivity.java"))
            files.append(str(JAVA / "TouchOverlayPolicy.java"))
            subprocess.run(["javac", "-d", str(tree), *files], check=True, capture_output=True)
            result = subprocess.run(["java", "-cp", str(tree), "io.github.alondero.lamborghinirecomp.ActivityTest"],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
