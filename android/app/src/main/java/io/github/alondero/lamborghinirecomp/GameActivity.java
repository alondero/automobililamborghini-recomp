package io.github.alondero.lamborghinirecomp;

import android.content.SharedPreferences;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.system.Os;
import android.view.InputDevice;
import android.view.SurfaceHolder;
import android.widget.RelativeLayout;
import java.util.HashSet;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLControllerManager;

public final class GameActivity extends SDLActivity implements SurfaceHolder.Callback {
    private final HashSet<Integer> held = new HashSet<>();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TouchOverlay overlay;
    private SharedPreferences preferences;

    // Android reports controller connects and drops while the game runs, so the overlay
    // follows them without polling and without a native -> Java bridge. A device counts
    // when SDL's own Android joystick handler accepts it, which is the set the port's
    // controller profile can use.
    private final InputManager.InputDeviceListener devices = new InputManager.InputDeviceListener() {
        @Override public void onInputDeviceAdded(int deviceId) { refreshTouchControls(); }
        @Override public void onInputDeviceRemoved(int deviceId) { refreshTouchControls(); }
        @Override public void onInputDeviceChanged(int deviceId) { refreshTouchControls(); }
    };

    @Override protected String[] getLibraries() { return new String[]{"c++_shared", "SDL2", "main"}; }

    @Override public void loadLibraries() {
        try {
            String nativeDir = getApplicationInfo().nativeLibraryDir;
            Os.setenv("LAMBO_ANDROID_NATIVE_LIB_DIR", nativeDir + "/", true);
            Os.setenv("LAMBO_ANDROID_CACHE_DIR", getCacheDir().getAbsolutePath() + "/", true);
            Os.setenv("SDL_VULKAN_LIBRARY", nativeDir + "/liblambo_vulkan.so", true);
            DriverImport.configure(this);
            super.loadLibraries();
        } catch (Throwable error) {
            DriverImport.releaseGameLock();
            throw new IllegalStateException("Cannot load GPU driver: " + error.getMessage(), error);
        }
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (mLayout == null) return;
        preferences = getSharedPreferences(TouchOverlayPolicy.STORE, MODE_PRIVATE);
        overlay = new TouchOverlay(this, (key, down) -> {
            if (down) { held.add(key); onNativeKeyDown(key); }
            else { held.remove(key); onNativeKeyUp(key); }
        });
        mLayout.addView(overlay, new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT));
        mSurface.getHolder().addCallback(this);
        InputManager manager = (InputManager) getSystemService(INPUT_SERVICE);
        // Without the service the stored preference still decides; only hotplug is lost.
        if (manager != null) manager.registerInputDeviceListener(devices, handler);
        refreshTouchControls();
    }

    // One rule, owned here: the overlay and the native input state cannot disagree about
    // whether the on-screen pad is in use.
    private void refreshTouchControls() {
        if (overlay == null) return;
        String stored = preferences == null ? TouchOverlayPolicy.AUTO
            : preferences.getString(TouchOverlayPolicy.KEY, TouchOverlayPolicy.AUTO);
        boolean controller = controllerConnected();
        overlay.setShown(TouchOverlayPolicy.controlsShown(stored, controller),
                        TouchOverlayPolicy.menuShown(stored));
    }

    private static boolean controllerConnected() {
        for (int deviceId : InputDevice.getDeviceIds())
            if (SDLControllerManager.isDeviceSDLJoystick(deviceId)) return true;
        return false;
    }

    private void releaseTouchControls() {
        if (overlay != null) overlay.reset();
        for (int key : held) onNativeKeyUp(key);
        held.clear();
    }

    // SurfaceView replacement can native-pause SDL without pausing this Activity.
    // Keep touch ownership here; SDL publishes generations and owns the native window.
    @Override public void surfaceDestroyed(SurfaceHolder holder) { releaseTouchControls(); }
    @Override public void surfaceCreated(SurfaceHolder holder) { }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { }

    @Override protected void onResume() {
        super.onResume();
        // A controller can connect or drop while the app is in the background.
        refreshTouchControls();
    }

    @Override protected void onPause() {
        releaseTouchControls();
        super.onPause();
    }

    @Override protected void onDestroy() {
        InputManager manager = (InputManager) getSystemService(INPUT_SERVICE);
        if (manager != null) manager.unregisterInputDeviceListener(devices);
        super.onDestroy();
    }
}
