package io.github.alondero.lamborghinirecomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.DisplayCutout;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

/**
 * Draws the on-screen controls and feeds touches to {@link TouchControls}.
 *
 * The game sees keyboard keys (the default keyboard profile), so the stick is digital:
 * it holds the arrow keys while pushed past a dead zone. Layout, landscape:
 * stick bottom-left, A/B bottom-right, small C pad above them, Start bottom-centre,
 * and Menu top-right in an outline style because it is not a gamepad button.
 * Z is not shown: it is a second accelerator button in the game (docs/analog-throttle.md),
 * redundant with A on a touch screen.
 *
 * The overlay is full screen and takes every touch it is shown for, so a finger that lands
 * off a control does not stop later fingers from reaching the stick or buttons. While the
 * controls are hidden it takes only the touches that land on a control it still shows, so
 * the rest reach the game surface underneath. See {@link TouchOverlayPolicy}.
 */
final class TouchOverlay extends View {
    // The menu toggle (F1), not Escape: the native loop toggles Settings on F1
    // before the frontend sees it, while Escape also reaches the modal and closes it.
    static final int MENU_KEY = KeyEvent.KEYCODE_F1;

    private final float dp;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();

    private final TouchControls.Button a = new TouchControls.Button("A", KeyEvent.KEYCODE_X, true);
    private final TouchControls.Button b = new TouchControls.Button("B", KeyEvent.KEYCODE_C, true);
    private final TouchControls.Button cUp = new TouchControls.Button("▲", KeyEvent.KEYCODE_I, true);
    private final TouchControls.Button cDown = new TouchControls.Button("▼", KeyEvent.KEYCODE_K, true);
    private final TouchControls.Button cLeft = new TouchControls.Button("◀", KeyEvent.KEYCODE_J, true);
    private final TouchControls.Button cRight = new TouchControls.Button("▶", KeyEvent.KEYCODE_L, true);
    private final TouchControls.Button start = new TouchControls.Button("START", KeyEvent.KEYCODE_ENTER, false);
    private final TouchControls.Button menu = new TouchControls.Button("MENU", MENU_KEY, false);
    private final TouchControls controls;

    // Draw style per button, in the same order as the buttons passed to TouchControls.
    private static final int[] COLORS = {0xFF3FA34D, 0xFFD9453B, 0xFFE8B83A, 0xFFE8B83A, 0xFFE8B83A, 0xFFE8B83A, 0xFF8A8F98};
    private static final float[] TEXT_SP = {22, 22, 11, 11, 11, 11, 12};
    private static final float[] ALPHA = {1f, 1f, 0.55f, 0.55f, 0.55f, 0.55f, 0.8f};

    private int insetLeft, insetTop, insetRight, insetBottom; // px, system bars and cutouts
    private boolean controlsShown = true; // stick and game buttons
    private boolean menuShown = true;     // port Settings, the pointer route into the overlay

    TouchOverlay(Context context, TouchControls.KeySink sink) {
        super(context);
        dp = context.getResources().getDisplayMetrics().density;
        controls = new TouchControls(sink,
            new int[] {KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
                       KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN},
            a, b, cUp, cDown, cLeft, cRight, start, menu);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.DEFAULT_BOLD);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2 * dp);
        stroke.setStrokeCap(Paint.Cap.ROUND);
    }

    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        insetLeft = insets.getSystemWindowInsetLeft();
        insetTop = insets.getSystemWindowInsetTop();
        insetRight = insets.getSystemWindowInsetRight();
        insetBottom = insets.getSystemWindowInsetBottom();
        DisplayCutout cutout = insets.getDisplayCutout();
        if (cutout != null) {
            insetLeft = Math.max(insetLeft, cutout.getSafeInsetLeft());
            insetTop = Math.max(insetTop, cutout.getSafeInsetTop());
            insetRight = Math.max(insetRight, cutout.getSafeInsetRight());
            insetBottom = Math.max(insetBottom, cutout.getSafeInsetBottom());
        }
        layout(getWidth(), getHeight());
        return super.onApplyWindowInsets(insets);
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) { layout(w, h); }

    private void layout(int w, int h) {
        float left = insetLeft, top = insetTop, right = w - insetRight, bottom = h - insetBottom;
        controls.stickR = 68 * dp;
        controls.stickX = left + 36 * dp + controls.stickR;
        controls.stickY = bottom - 36 * dp - controls.stickR;
        a.setCircle(right - 74 * dp, bottom - 112 * dp, 44 * dp);
        b.setCircle(right - 166 * dp, bottom - 70 * dp, 40 * dp);
        float cx = right - 120 * dp, cy = bottom - 238 * dp, step = 32 * dp, r = 15 * dp;
        cUp.setCircle(cx, cy - step, r);
        cDown.setCircle(cx, cy + step, r);
        cLeft.setCircle(cx - step, cy, r);
        cRight.setCircle(cx + step, cy, r);
        for (TouchControls.Button c : new TouchControls.Button[] {cUp, cDown, cLeft, cRight}) c.slop = 6 * dp;
        float mid = (left + right) / 2;
        start.left = mid - 52 * dp; start.right = mid + 52 * dp;
        start.top = bottom - 56 * dp; start.bottom = bottom - 18 * dp;
        menu.left = right - 84 * dp; menu.right = right - 12 * dp;
        menu.top = top + 12 * dp; menu.bottom = top + 42 * dp;
        invalidate();
    }

    @Override protected void onDraw(Canvas canvas) {
        TouchControls c = controls;
        if (controlsShown) drawControls(canvas, c);
        if (menuShown) drawMenu(canvas);
    }

    private void drawControls(Canvas canvas, TouchControls c) {
        // Stick: ring plus a knob that follows the thumb.
        fill.setColor(0x33FFFFFF);
        canvas.drawCircle(c.stickX, c.stickY, c.stickR, fill);
        stroke.setColor(0x80FFFFFF);
        canvas.drawCircle(c.stickX, c.stickY, c.stickR, stroke);
        fill.setColor(c.stickPointer == TouchControls.NONE ? 0x66FFFFFF : 0xAAFFFFFF);
        canvas.drawCircle(c.stickX + c.knobX, c.stickY + c.knobY, c.stickR * 0.42f, fill);

        for (int i = 0; i < COLORS.length; i++) drawButton(canvas, c.buttons[i], COLORS[i], TEXT_SP[i], ALPHA[i]);
    }

    private void drawMenu(Canvas canvas) {
        // Menu is a port control, not an N64 button: outline only, no fill, hamburger icon.
        int line = menu.down ? 0xFFFFFFFF : 0x99FFFFFF;
        stroke.setColor(line);
        float corner = (menu.bottom - menu.top) / 2;
        rect.set(menu.left, menu.top, menu.right, menu.bottom);
        canvas.drawRoundRect(rect, corner, corner, stroke);
        float ix = menu.left + 16 * dp, iy = menu.centerY();
        for (int k = -1; k <= 1; k++)
            canvas.drawLine(ix - 5 * dp, iy + k * 4 * dp, ix + 5 * dp, iy + k * 4 * dp, stroke);
        text.setColor(menu.down ? 0xFFFFFFFF : 0xCCFFFFFF);
        text.setTextSize(11 * dp);
        canvas.drawText("MENU", menu.left + 46 * dp, iy + 4 * dp, text);
    }

    private void drawButton(Canvas canvas, TouchControls.Button c, int color, float textSp, float alpha) {
        int a8 = (int) (255 * alpha * (c.down ? 0.85f : 0.45f));
        fill.setColor((a8 << 24) | (color & 0xFFFFFF));
        if (c.round) {
            canvas.drawCircle(c.centerX(), c.centerY(), (c.right - c.left) / 2, fill);
        } else {
            rect.set(c.left, c.top, c.right, c.bottom);
            canvas.drawRoundRect(rect, (c.bottom - c.top) / 2, (c.bottom - c.top) / 2, fill);
        }
        text.setColor(c.down ? 0xFFFFFFFF : 0xDDFFFFFF);
        text.setTextSize(textSp * dp);
        canvas.drawText(c.label, c.centerX(), c.centerY() + textSp * dp * 0.35f, text);
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        int index = e.getActionIndex();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                boolean claimed = controls.down(e.getPointerId(index), e.getX(index), e.getY(index));
                // While the pad is shown the overlay keeps every gesture, even one that starts
                // on empty space: declining ACTION_DOWN removes this view from the gesture, so
                // Android would stop delivering the second finger and the slide onto the stick.
                // Only a hidden pad declines, and that refusal is what lets Android hand the
                // whole gesture to SDL's view underneath.
                if (!controlsShown && !claimed) return false;
                break;
            case MotionEvent.ACTION_POINTER_DOWN:
                controls.down(e.getPointerId(index), e.getX(index), e.getY(index));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++)
                    controls.move(e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                controls.up(e.getPointerId(index));
                break;
            case MotionEvent.ACTION_CANCEL:
                controls.releaseAll();
                break;
            default:
                return false;
        }
        invalidate();
        return true; // this gesture is ours: later fingers arrive here too
    }

    /** Releases everything, for example when the activity pauses. */
    void reset() { controls.releaseAll(); invalidate(); }

    /**
     * Shows or hides the controls. Hiding releases every key the overlay holds, so no
     * throttle, brake or stick direction stays pressed across the transition. Menu can
     * stay shown while the rest is hidden: it is the only pointer route into Settings.
     */
    void setShown(boolean showControls, boolean showMenu) {
        if (controlsShown == showControls && menuShown == showMenu) return;
        controlsShown = showControls;
        menuShown = showMenu;
        controls.setStickVisible(showControls);
        for (TouchControls.Button button : controls.buttons)
            if (button != menu) controls.setButtonVisible(button, showControls);
        controls.setButtonVisible(menu, showMenu);
        invalidate();
    }
}
