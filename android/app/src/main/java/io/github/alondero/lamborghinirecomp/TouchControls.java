package io.github.alondero.lamborghinirecomp;

/**
 * Hit-testing and pointer ownership for the on-screen controls, with no Android
 * dependency so it can be tested on a plain JVM (tests/test_android_touch_controls.py).
 *
 * Each pointer (finger) owns at most one control: the stick or one button. A pointer that
 * slides off its button releases it, and may then claim whatever it slides onto, so a
 * thumb can roll between B and A. A control is held by one pointer at a time.
 *
 * A hidden control is not drawn by the overlay and never claims a pointer, so a touch
 * that lands on no shown control is left to the game surface underneath. Hiding a held
 * control releases it, so no key stays pressed across the transition.
 */
final class TouchControls {
    /** Receives key transitions for the game. */
    interface KeySink { void key(int keyCode, boolean down); }

    static final int NONE = -1;
    /** Stick dead zone as a fraction of the stick radius. */
    static final float DEAD_ZONE = 0.30f;
    /** sin(22.5 degrees): a direction key is held when its axis carries more than this share of the push, giving 8-way output. */
    private static final float AXIS_SHARE = 0.38f;
    /** A press may begin this far outside the stick radius (fraction of the radius). */
    private static final float STICK_GRAB = 1.3f;

    static final class Button {
        final String label;
        final int key;
        final boolean round;
        float left, top, right, bottom; // px, view coordinates
        float slop;                     // extra px accepted around the shape
        boolean down;
        boolean visible = true;         // a hidden button is not drawn and claims nothing
        int owner = NONE;

        Button(String label, int key, boolean round) { this.label = label; this.key = key; this.round = round; }

        void setCircle(float cx, float cy, float r) { left = cx - r; top = cy - r; right = cx + r; bottom = cy + r; }

        float centerX() { return (left + right) / 2; }
        float centerY() { return (top + bottom) / 2; }

        boolean contains(float x, float y) {
            if (round) return Math.hypot(x - centerX(), y - centerY()) <= (right - left) / 2 + slop;
            return x >= left - slop && x <= right + slop && y >= top - slop && y <= bottom + slop;
        }
    }

    final Button[] buttons;
    final KeySink sink;
    private final int[] stickKeys; // left, right, up, down
    private final boolean[] stickDown = new boolean[4];
    float stickX, stickY, stickR; // centre and radius, px
    float knobX, knobY;           // knob offset from the centre, px
    int stickPointer = NONE;
    private boolean stickVisible = true;

    TouchControls(KeySink sink, int[] stickKeys, Button... buttons) {
        this.sink = sink;
        this.stickKeys = stickKeys;
        this.buttons = buttons;
    }

    /** Claims the touch, or reports that no shown control covers it. */
    boolean down(int pointer, float x, float y) { return claim(pointer, x, y); }

    /** Shows or hides the stick; hiding it releases the direction keys it holds. */
    void setStickVisible(boolean visible) {
        if (stickVisible == visible) return;
        stickVisible = visible;
        if (visible) return;
        stickPointer = NONE;
        knobX = knobY = 0;
        for (int d = 0; d < 4; d++) setStick(d, false);
    }

    /** Shows or hides one button; hiding it releases the key it holds. */
    void setButtonVisible(Button b, boolean visible) {
        if (b.visible == visible) return;
        b.visible = visible;
        if (visible) return;
        b.owner = NONE;
        setButton(b, false);
    }

    void move(int pointer, float x, float y) {
        if (pointer == stickPointer) { moveStick(x, y); return; }
        for (Button b : buttons) {
            if (b.owner != pointer) continue;
            if (b.contains(x, y)) return;
            release(pointer); // slid off: let go, then try what is under the finger now
            break;
        }
        claim(pointer, x, y);
    }

    void up(int pointer) { release(pointer); }

    void releaseAll() {
        stickPointer = NONE;
        knobX = knobY = 0;
        for (int d = 0; d < 4; d++) setStick(d, false);
        for (Button b : buttons) { b.owner = NONE; setButton(b, false); }
    }

    private boolean claim(int pointer, float x, float y) {
        if (stickVisible && stickPointer == NONE && stickR > 0
            && Math.hypot(x - stickX, y - stickY) <= stickR * STICK_GRAB) {
            stickPointer = pointer;
            moveStick(x, y);
            return true;
        }
        for (Button b : buttons) {
            if (b.visible && b.owner == NONE && b.contains(x, y)) {
                b.owner = pointer;
                setButton(b, true);
                return true;
            }
        }
        return false;
    }

    private void release(int pointer) {
        if (pointer == stickPointer) {
            stickPointer = NONE;
            knobX = knobY = 0;
            for (int d = 0; d < 4; d++) setStick(d, false);
        }
        for (Button b : buttons) {
            if (b.owner == pointer) { b.owner = NONE; setButton(b, false); }
        }
    }

    private void moveStick(float x, float y) {
        float dx = x - stickX, dy = y - stickY;
        float len = (float) Math.hypot(dx, dy);
        if (len > stickR) { dx *= stickR / len; dy *= stickR / len; len = stickR; }
        knobX = dx; knobY = dy;
        boolean live = len > stickR * DEAD_ZONE; // radial, so diagonals need no more travel than straights
        setStick(0, live && dx / len < -AXIS_SHARE);
        setStick(1, live && dx / len > AXIS_SHARE);
        setStick(2, live && dy / len < -AXIS_SHARE);
        setStick(3, live && dy / len > AXIS_SHARE);
    }

    private void setStick(int dir, boolean down) {
        if (stickDown[dir] == down) return;
        stickDown[dir] = down;
        sink.key(stickKeys[dir], down);
    }

    private void setButton(Button b, boolean down) {
        if (b.down == down) return;
        b.down = down;
        sink.key(b.key, down);
    }
}
