package io.github.alondero.lamborghinirecomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

/**
 * On-screen controls drawn and hit-tested in one view so several fingers can work at once.
 *
 * The game sees keyboard keys (the default keyboard profile), so the stick is digital:
 * it holds the arrow keys while pushed past a dead zone. Layout, landscape:
 * stick bottom-left, A/B bottom-right, small C pad above them, Start bottom-centre,
 * and Menu top-right in an outline style because it is not a gamepad button.
 */
final class TouchOverlay extends View {
    /** Receives key presses for the game. Called on the UI thread. */
    interface KeySink { void key(int keyCode, boolean down); }

    // The menu toggle (F1), not Escape: the native loop toggles Settings on F1
    // before the frontend sees it, while Escape also reaches the modal and closes it.
    static final int MENU_KEY = KeyEvent.KEYCODE_F1;

    private static final float DEAD_ZONE = 0.30f; // fraction of stick radius
    private static final int NONE = -1;

    private static final class Control {
        final String label; final int key; final boolean round;
        final RectF bounds = new RectF();
        boolean down;
        Control(String label, int key, boolean round) { this.label = label; this.key = key; this.round = round; }
    }

    private final KeySink sink;
    private final float dp;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);

    private final Control a = new Control("A", KeyEvent.KEYCODE_X, true);
    private final Control b = new Control("B", KeyEvent.KEYCODE_C, true);
    private final Control cUp = new Control("▲", KeyEvent.KEYCODE_I, true);
    private final Control cDown = new Control("▼", KeyEvent.KEYCODE_K, true);
    private final Control cLeft = new Control("◀", KeyEvent.KEYCODE_J, true);
    private final Control cRight = new Control("▶", KeyEvent.KEYCODE_L, true);
    private final Control start = new Control("START", KeyEvent.KEYCODE_ENTER, false);
    private final Control menu = new Control("MENU", MENU_KEY, false);
    private final Control[] buttons = {a, b, cUp, cDown, cLeft, cRight, start, menu};
    private final int[] owner = new int[buttons.length]; // pointer id holding each button

    private final int[] stickKeys = {
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN};
    private final boolean[] stickDown = new boolean[4];
    private float stickX, stickY, stickR, knobX, knobY; // knob offset in px from centre
    private int stickPointer = NONE;

    TouchOverlay(Context context, KeySink sink) {
        super(context);
        this.sink = sink;
        dp = context.getResources().getDisplayMetrics().density;
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.DEFAULT_BOLD);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2 * dp);
        java.util.Arrays.fill(owner, NONE);
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        stickR = 68 * dp;
        stickX = 36 * dp + stickR;
        stickY = h - 36 * dp - stickR;
        circle(a, w - 74 * dp, h - 112 * dp, 44 * dp);
        circle(b, w - 166 * dp, h - 70 * dp, 40 * dp);
        float cx = w - 120 * dp, cy = h - 238 * dp, step = 32 * dp, r = 15 * dp;
        circle(cUp, cx, cy - step, r);
        circle(cDown, cx, cy + step, r);
        circle(cLeft, cx - step, cy, r);
        circle(cRight, cx + step, cy, r);
        start.bounds.set(w / 2f - 52 * dp, h - 56 * dp, w / 2f + 52 * dp, h - 18 * dp);
        menu.bounds.set(w - 84 * dp, 12 * dp, w - 12 * dp, 42 * dp);
    }

    private static void circle(Control c, float cx, float cy, float r) {
        c.bounds.set(cx - r, cy - r, cx + r, cy + r);
    }

    @Override protected void onDraw(Canvas canvas) {
        // Stick: ring plus a knob that follows the thumb.
        fill.setColor(0x33FFFFFF);
        canvas.drawCircle(stickX, stickY, stickR, fill);
        stroke.setColor(0x80FFFFFF);
        canvas.drawCircle(stickX, stickY, stickR, stroke);
        fill.setColor(stickPointer == NONE ? 0x66FFFFFF : 0xAAFFFFFF);
        canvas.drawCircle(stickX + knobX, stickY + knobY, stickR * 0.42f, fill);

        drawButton(canvas, a, 0xFF3FA34D, 22, 1f);
        drawButton(canvas, b, 0xFFD9453B, 22, 1f);
        for (Control c : new Control[] {cUp, cDown, cLeft, cRight}) drawButton(canvas, c, 0xFFE8B83A, 11, 0.55f);
        drawButton(canvas, start, 0xFF8A8F98, 12, 0.8f);

        // Menu is a port control, not an N64 button: outline only, no fill.
        stroke.setColor(menu.down ? 0xFFFFFFFF : 0x99FFFFFF);
        float corner = menu.bounds.height() / 2;
        canvas.drawRoundRect(menu.bounds, corner, corner, stroke);
        text.setColor(menu.down ? 0xFFFFFFFF : 0xCCFFFFFF);
        text.setTextSize(11 * dp);
        canvas.drawText("☰ MENU", menu.bounds.centerX(), menu.bounds.centerY() + 4 * dp, text);
    }

    private void drawButton(Canvas canvas, Control c, int color, float textSp, float alpha) {
        int a8 = (int) (255 * alpha * (c.down ? 0.85f : 0.45f));
        fill.setColor((a8 << 24) | (color & 0xFFFFFF));
        if (c.round) canvas.drawCircle(c.bounds.centerX(), c.bounds.centerY(), c.bounds.width() / 2, fill);
        else canvas.drawRoundRect(c.bounds, c.bounds.height() / 2, c.bounds.height() / 2, fill);
        text.setColor(c.down ? 0xFFFFFFFF : 0xDDFFFFFF);
        text.setTextSize(textSp * dp);
        canvas.drawText(c.label, c.bounds.centerX(), c.bounds.centerY() + textSp * dp * 0.35f, text);
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                return press(e.getPointerId(index), e.getX(index), e.getY(index));
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++)
                    if (e.getPointerId(i) == stickPointer) moveStick(e.getX(i), e.getY(i));
                return stickPointer != NONE || anyHeld();
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                release(e.getPointerId(index));
                return true;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                return true;
            default:
                return false;
        }
    }

    private boolean press(int pointer, float x, float y) {
        if (stickPointer == NONE && Math.hypot(x - stickX, y - stickY) <= stickR * 1.3f) {
            stickPointer = pointer;
            moveStick(x, y);
            return true;
        }
        for (int i = 0; i < buttons.length; i++) {
            Control c = buttons[i];
            // Small buttons get a little slop so a thumb does not miss them.
            float slop = c.bounds.width() < 40 * dp ? 6 * dp : 0;
            if (owner[i] == NONE && x >= c.bounds.left - slop && x <= c.bounds.right + slop
                    && y >= c.bounds.top - slop && y <= c.bounds.bottom + slop) {
                owner[i] = pointer;
                setButton(c, true);
                return true;
            }
        }
        return false;
    }

    private void moveStick(float x, float y) {
        float dx = x - stickX, dy = y - stickY;
        float len = (float) Math.hypot(dx, dy);
        if (len > stickR) { dx *= stickR / len; dy *= stickR / len; }
        knobX = dx; knobY = dy;
        float limit = stickR * DEAD_ZONE;
        setStick(0, dx < -limit);
        setStick(1, dx > limit);
        setStick(2, dy < -limit);
        setStick(3, dy > limit);
        invalidate();
    }

    private void setStick(int dir, boolean down) {
        if (stickDown[dir] == down) return;
        stickDown[dir] = down;
        sink.key(stickKeys[dir], down);
    }

    private void setButton(Control c, boolean down) {
        if (c.down == down) return;
        c.down = down;
        sink.key(c.key, down);
        invalidate();
    }

    private void release(int pointer) {
        if (pointer == stickPointer) {
            stickPointer = NONE;
            knobX = knobY = 0;
            for (int d = 0; d < 4; d++) setStick(d, false);
            invalidate();
        }
        for (int i = 0; i < buttons.length; i++) {
            if (owner[i] == pointer) { owner[i] = NONE; setButton(buttons[i], false); }
        }
    }

    private void releaseAll() {
        stickPointer = NONE;
        knobX = knobY = 0;
        for (int d = 0; d < 4; d++) setStick(d, false);
        for (int i = 0; i < buttons.length; i++) { owner[i] = NONE; setButton(buttons[i], false); }
        invalidate();
    }

    private boolean anyHeld() {
        for (int o : owner) if (o != NONE) return true;
        return false;
    }

    /** Releases everything, for example when the activity pauses. */
    void reset() { releaseAll(); }
}
