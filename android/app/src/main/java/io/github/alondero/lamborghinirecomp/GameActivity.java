package io.github.alondero.lamborghinirecomp;

import android.os.Bundle;
import android.system.Os;
import android.view.SurfaceHolder;
import android.widget.RelativeLayout;
import java.util.HashSet;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity implements SurfaceHolder.Callback {
    private final HashSet<Integer> held = new HashSet<>();
    private TouchOverlay overlay;

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
        overlay = new TouchOverlay(this, (key, down) -> {
            if (down) { held.add(key); onNativeKeyDown(key); }
            else { held.remove(key); onNativeKeyUp(key); }
        });
        mLayout.addView(overlay, new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT));
        mSurface.getHolder().addCallback(this);
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

    @Override protected void onPause() {
        releaseTouchControls();
        super.onPause();
    }
}
