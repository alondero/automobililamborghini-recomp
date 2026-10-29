"""SDL leaves the Android Vulkan window's surface to Vulkan (issue #358).

No ROM or device needed, only the pinned SDL checkout that
`scripts/build_android.py` fetches. Pinned SDL 2 tries to create an EGL surface
on every Android surface change, makes the UI thread wait for an EGL context
backup on every surface loss, and backs up and restores a GL context on every
pause and resume, whatever the window type. For a Vulkan window the restore
fails and reports a spurious render device reset. Patch 0048 limits all of it
to OpenGL windows, and skips the backup wait for an external context, which
the event loop never backs up. The production code is compiled from the
patched source and run against a recording SDL/EGL boundary.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts import build_android as android
from tests.patched_source import ROOT, method, patched_file
from tools.apply_patches import PatchError, apply_series, series_for

SDL = android.WORK / "deps/SDL"
SOURCE = "src/core/android/SDL_android.c"
CALLBACKS = ("JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeSurfaceCreated)(",
             "Uint64 SDL_AndroidGetNativeWindowGeneration(",
             "void *SDL_AndroidAcquireNativeWindow(",
             "JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeSurfaceChanged)(",
             "JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(onNativeSurfaceDestroyed)(")
EVENTS = "src/video/android/SDL_androidevents.c"
CONTEXT_STEPS = ("static void android_egl_context_restore(", "static void android_egl_context_backup(")


def definition(source, signature):
    # SDL declares each JNI callback before defining it; skip the prototypes.
    start = source.index(signature)
    while source[source.index(")", start + len(signature)) + 1:].lstrip()[0] != "{":
        start = source.index(signature, start + 1)
    return method(source[start:], signature)


def sdl_is_pinned():
    if not (SDL / ".git").exists():
        return False
    head = subprocess.run(["git", "-C", str(SDL), "rev-parse", "HEAD"], capture_output=True, text=True)
    return head.stdout.strip() == android.DEPS["SDL"][1]


PINNED = unittest.skipUnless(
    sdl_is_pinned(), "pinned SDL not fetched; run: python -c \"from scripts.build_android import "
    "fetch_dependency; fetch_dependency('SDL')\"")


@PINNED
@unittest.skipUnless(shutil.which("gcc"), "gcc needed for SDL surface callback fixture")
class SurfaceCallbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-sdl-surface-")
        cls.addClassCleanup(cls.temporary.cleanup)
        tree = Path(cls.temporary.name)
        callbacks = patched_file(SDL, SOURCE, tree / "sdl")
        events = patched_file(SDL, EVENTS, tree / "sdl")
        fixture = (ROOT / "tests/fixtures/sdl_android_surface.c").read_text(encoding="utf-8")
        fixture = fixture.replace("// PRODUCTION_SURFACE_CALLBACKS",
                                  "\n".join(definition(callbacks, signature) for signature in CALLBACKS))
        fixture = fixture.replace("// PRODUCTION_CONTEXT_STEPS",
                                  "\n".join(definition(events, signature) for signature in CONTEXT_STEPS))
        c = tree / "fixture.c"
        c.write_text(fixture, encoding="utf-8")
        cls.exe = tree / "fixture.exe"
        result = subprocess.run(["gcc", "-std=c99", "-Wall", "-Werror", "-Wno-unused-function", str(c), "-o", str(cls.exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def scenario(self, name):
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_vulkan_window_neither_creates_egl_surface_nor_waits(self):
        self.scenario("vulkan")

    def test_opengl_window_keeps_egl_surface_and_backup_wait(self):
        self.scenario("opengl")

    def test_opengl_backup_wait_stays_bounded(self):
        self.scenario("opengl-timeout")

    def test_external_context_skips_backup_wait(self):
        self.scenario("external-opengl")

    def test_vulkan_window_pause_and_resume_make_no_gl_calls(self):
        self.scenario("vulkan-pause-resume")

    def test_opengl_window_keeps_context_backup_and_restore(self):
        self.scenario("opengl-pause-resume")

    def test_retained_snapshots_survive_same_pointer_replacement_and_loss(self):
        self.scenario("native-generation")


@PINNED
class SdlPatchPreparationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="lambo-sdl-prepare-")
        self.addCleanup(temporary.cleanup)
        self.tree = Path(temporary.name) / "SDL"
        # A shared clone leaves the developer's SDL checkout and its metadata untouched.
        self.git("clone", "-q", "--shared", "--no-checkout", str(SDL), str(self.tree))
        self.git("-C", str(self.tree), "checkout", "-q", "--detach", android.DEPS["SDL"][1])
        self.patches = series_for("android")["build-android/deps/SDL"]

    def git(self, *args):
        return subprocess.check_output(["git", *args], stderr=subprocess.STDOUT)

    def assert_series_applied(self):
        # Removing the patches newest first must succeed only from the full series.
        for patch in reversed(self.patches):
            self.git("-C", str(self.tree), "apply", "--ignore-whitespace", "--reverse", str(patch))
        self.assertEqual(self.git("-C", str(self.tree), "diff", "HEAD"), b"")

    def test_clean_checkout_then_repeat(self):
        apply_series(self.tree, self.patches)
        apply_series(self.tree, self.patches)
        self.assert_series_applied()

    def test_checkout_prepared_before_0048_is_completed(self):
        # Earlier builds applied only the USB receiver patch.
        usb_receiver = self.patches[0]
        self.assertTrue(usb_receiver.name.startswith("0015-"))
        self.git("-C", str(self.tree), "apply", "--ignore-whitespace", str(usb_receiver))
        notes = self.tree / "notes.txt"
        notes.write_bytes(b"keep my notes\n")
        apply_series(self.tree, self.patches)
        self.assertEqual(notes.read_bytes(), b"keep my notes\n")
        notes.unlink()
        self.assert_series_applied()

    def test_conflicting_local_edit_is_rejected(self):
        path = self.tree / SOURCE
        original = path.read_bytes()
        edited = original.replace(b"if (!data->backup_done) {", b"if (local_edit) {", 1)
        self.assertNotEqual(edited, original)
        path.write_bytes(edited)
        with self.assertRaisesRegex(PatchError, "does not match any prefix"):
            apply_series(self.tree, self.patches)
        self.assertEqual(path.read_bytes(), edited)


if __name__ == "__main__":
    unittest.main()
