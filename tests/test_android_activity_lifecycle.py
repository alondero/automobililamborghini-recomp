"""Run GameActivity's touch-release callbacks against a minimal Android boundary."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import ROOT


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK not installed")
class ActivityLifecycleTests(unittest.TestCase):
    def test_surface_loss_releases_controls_without_activity_pause(self):
        stubs = {
            "android/os/Bundle.java": "package android.os; public class Bundle {}",
            "android/system/Os.java": "package android.system; public class Os { public static void setenv(String a,String b,boolean c) {} }",
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
            "org/libsdl/app/SDLActivity.java": """package org.libsdl.app;
import android.view.SurfaceHolder;
public class SDLActivity {
 protected android.widget.RelativeLayout mLayout = new android.widget.RelativeLayout();
 protected Surface mSurface = new Surface();
 public static int down,up,pauses;
 public static class Surface { public SurfaceHolder getHolder() { return c -> {}; } }
 public static class Info { public String nativeLibraryDir="native"; }
 public Info getApplicationInfo() { return new Info(); }
 public java.io.File getCacheDir() { return new java.io.File("cache"); }
 protected String[] getLibraries() { return null; }
 public void loadLibraries() {}
 protected void onCreate(android.os.Bundle state) {}
 protected void onPause() { ++pauses; }
 public static void onNativeKeyDown(int key) { ++down; }
 public static void onNativeKeyUp(int key) { ++up; }
}""",
            "io/github/alondero/lamborghinirecomp/TouchOverlay.java": """package io.github.alondero.lamborghinirecomp;
class TouchOverlay {
 interface Sink { void key(int key,boolean down); }
 static TouchOverlay instance;
 final Sink sink;
 TouchOverlay(Object context,Sink sink) { this.sink=sink; instance=this; }
 void reset() {} // The Activity's held-key fallback must also release every key.
}""",
            "io/github/alondero/lamborghinirecomp/DriverImport.java": "package io.github.alondero.lamborghinirecomp; class DriverImport { static void configure(Object o) {} static void releaseGameLock() {} }",
            "io/github/alondero/lamborghinirecomp/ActivityTest.java": """package io.github.alondero.lamborghinirecomp;
import org.libsdl.app.SDLActivity;
public class ActivityTest {
 static void check(boolean value) { if (!value) throw new AssertionError(); }
 public static void main(String[] args) {
  GameActivity activity=new GameActivity(); activity.onCreate(null);
  TouchOverlay.instance.sink.key(21,true); TouchOverlay.instance.sink.key(22,true);
  activity.surfaceDestroyed(null);
  check(SDLActivity.down==2 && SDLActivity.up==2 && SDLActivity.pauses==0);
  activity.surfaceCreated(null); activity.surfaceChanged(null,0,1280,720);
  activity.onPause(); activity.surfaceDestroyed(null);
  check(SDLActivity.up==2); // Duplicate loss cannot leave or release phantom keys.
  TouchOverlay.instance.sink.key(21,true);
  activity.surfaceDestroyed(null);
  check(SDLActivity.down==3 && SDLActivity.up==3);
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
            files.append(str(ROOT / "android/app/src/main/java/io/github/alondero/lamborghinirecomp/GameActivity.java"))
            subprocess.run(["javac", "-d", str(tree), *files], check=True, capture_output=True)
            result = subprocess.run(["java", "-cp", str(tree), "io.github.alondero.lamborghinirecomp.ActivityTest"],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
