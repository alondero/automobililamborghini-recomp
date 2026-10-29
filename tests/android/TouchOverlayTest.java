package io.github.alondero.lamborghinirecomp;

import android.view.KeyEvent;
import android.view.MotionEvent;
import java.util.ArrayList;
import java.util.List;

/** Plain-JVM checks for TouchOverlay gesture dispatch; run by tests/test_android_touch_overlay.py. */
public final class TouchOverlayTest {
    // The stub Android boundary reports density 1, so a 1280x720 view puts the stick at
    // (104, 616) with radius 68 and A at (1206, 608) with radius 44.
    static final float EMPTY_X = 640, EMPTY_Y = 300, STICK_Y = 616, A_X = 1206, A_Y = 608;
    static final float MENU_X = 1230, MENU_Y = 27;

    static final List<String> log = new ArrayList<>();

    static TouchOverlay make() {
        log.clear();
        TouchOverlay overlay = new TouchOverlay(new android.content.Context(),
            (key, down) -> log.add((down ? "+" : "-") + key));
        overlay.onSizeChanged(1280, 720, 0, 0);
        return overlay;
    }

    static MotionEvent event(int action, int index, int[] ids, float[] xs, float[] ys) {
        return new MotionEvent(action, index, ids, xs, ys);
    }

    static boolean single(TouchOverlay overlay, int action, float x, float y) {
        return overlay.onTouchEvent(event(action, 0, new int[] {0}, new float[] {x}, new float[] {y}));
    }

    static void check(boolean ok, String what) {
        if (!ok) { System.out.println("FAIL: " + what + " log=" + log); System.exit(1); }
    }

    static void expect(String what, String... entries) { check(log.equals(List.of(entries)), what); }

    public static void main(String[] args) {
        // A shown overlay must keep the whole gesture, including a finger that starts on empty
        // space. Declining ACTION_DOWN would remove the view from the gesture, so Android would
        // never deliver the second finger or the slide onto the stick.
        TouchOverlay o = make();
        check(single(o, MotionEvent.ACTION_DOWN, EMPTY_X, EMPTY_Y),
              "a stray first finger stays with the shown overlay");
        check(o.onTouchEvent(event(MotionEvent.ACTION_POINTER_DOWN, 1,
                                  new int[] {0, 1}, new float[] {EMPTY_X, A_X}, new float[] {EMPTY_Y, A_Y})),
              "a second finger reaches A after a stray first finger");
        expect("second finger presses A", "+" + KeyEvent.KEYCODE_X);
        check(o.onTouchEvent(event(MotionEvent.ACTION_MOVE, 0,
                                  new int[] {0, 1}, new float[] {40, A_X}, new float[] {STICK_Y, A_Y})),
              "a slide onto the stick is delivered");
        expect("the stray finger steers once it reaches the stick",
               "+" + KeyEvent.KEYCODE_X, "+" + KeyEvent.KEYCODE_DPAD_LEFT);
        check(o.onTouchEvent(event(MotionEvent.ACTION_POINTER_UP, 1,
                                  new int[] {0, 1}, new float[] {40, A_X}, new float[] {STICK_Y, A_Y})),
              "lifting the second finger is delivered");
        check(single(o, MotionEvent.ACTION_UP, 40, STICK_Y), "lifting the first finger is delivered");
        expect("both fingers released", "+" + KeyEvent.KEYCODE_X, "+" + KeyEvent.KEYCODE_DPAD_LEFT,
               "-" + KeyEvent.KEYCODE_X, "-" + KeyEvent.KEYCODE_DPAD_LEFT);

        // A hidden pad declines the gesture so it reaches SDL's view, but Menu still claims its
        // own touch: it is the only pointer route into Settings.
        o = make();
        o.setShown(false, true);
        check(!single(o, MotionEvent.ACTION_DOWN, EMPTY_X, EMPTY_Y),
              "the hidden pad declines a touch on empty space");
        check(!single(o, MotionEvent.ACTION_DOWN, A_X, A_Y), "a hidden control declines its own spot");
        expect("a hidden pad sends nothing", new String[0]);
        check(single(o, MotionEvent.ACTION_DOWN, MENU_X, MENU_Y),
              "Menu still claims a touch while the pad is hidden");
        expect("Menu is the only control while the pad is hidden", "+" + KeyEvent.KEYCODE_F1);

        // Hiding the pad mid-gesture releases what the overlay held, through the real route.
        o = make();
        single(o, MotionEvent.ACTION_DOWN, A_X, A_Y);
        o.setShown(false, true);
        expect("hiding the pad releases the held button", "+" + KeyEvent.KEYCODE_X, "-" + KeyEvent.KEYCODE_X);
        check(!single(o, MotionEvent.ACTION_DOWN, A_X, A_Y), "the released control now declines");

        // A cancelled gesture releases everything, so no key survives a system takeover.
        o = make();
        single(o, MotionEvent.ACTION_DOWN, A_X, A_Y);
        single(o, MotionEvent.ACTION_MOVE, 40, STICK_Y);
        check(o.onTouchEvent(event(MotionEvent.ACTION_CANCEL, 0,
                                  new int[] {0}, new float[] {40}, new float[] {STICK_Y})),
              "cancel is consumed");
        expect("cancel releases the button and the stick",
               "+" + KeyEvent.KEYCODE_X, "-" + KeyEvent.KEYCODE_X,
               "+" + KeyEvent.KEYCODE_DPAD_LEFT, "-" + KeyEvent.KEYCODE_DPAD_LEFT);

        System.out.println("OK");
    }
}
