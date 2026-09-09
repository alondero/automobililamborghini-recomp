package io.github.alondero.lamborghinirecomp;

import android.os.Bundle;
import android.system.Os;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.RelativeLayout;
import java.util.HashSet;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    private final HashSet<Integer> held = new HashSet<>();

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
        button("◀", KeyEvent.KEYCODE_DPAD_LEFT, 16, 90, false);
        button("▶", KeyEvent.KEYCODE_DPAD_RIGHT, 152, 90, false);
        button("▲", KeyEvent.KEYCODE_DPAD_UP, 84, 158, false);
        button("▼", KeyEvent.KEYCODE_DPAD_DOWN, 84, 22, false);
        button("A", KeyEvent.KEYCODE_X, 16, 90, true);
        button("B", KeyEvent.KEYCODE_C, 94, 22, true);
        button("Z", KeyEvent.KEYCODE_Z, 172, 22, true);
        button("Start", KeyEvent.KEYCODE_ENTER, 16, 180, true);
        button("Menu", KeyEvent.KEYCODE_ESCAPE, 16, 260, true);
    }

    private void button(String label, int key, int x, int y, boolean right) {
        float density = getResources().getDisplayMetrics().density;
        Button button = new Button(this);
        button.setText(label);
        button.setAlpha(0.60f);
        button.setFocusable(false);
        RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams((int)(78*density), (int)(64*density));
        params.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        params.addRule(right ? RelativeLayout.ALIGN_PARENT_RIGHT : RelativeLayout.ALIGN_PARENT_LEFT);
        params.bottomMargin = (int)(y*density);
        if (right) params.rightMargin = (int)(x*density); else params.leftMargin = (int)(x*density);
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    held.add(key); onNativeKeyDown(key); return true;
                case MotionEvent.ACTION_UP:
                    held.remove(key); onNativeKeyUp(key); view.performClick(); return true;
                case MotionEvent.ACTION_CANCEL:
                    held.remove(key); onNativeKeyUp(key); return true;
                default: return true;
            }
        });
        mLayout.addView(button, params);
    }

    @Override protected void onPause() {
        for (int key : held) onNativeKeyUp(key);
        held.clear();
        super.onPause();
    }
}
