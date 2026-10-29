package io.github.alondero.lamborghinirecomp;

import java.util.ArrayList;
import java.util.List;

/** Plain-JVM checks for TouchControls; run by tests/test_android_touch_controls.py. */
public final class TouchControlsTest {
    static final int LEFT = 1, RIGHT = 2, UP = 3, DOWN = 4, KA = 10, KB = 11, KS = 12, KM = 13;
    static final List<String> log = new ArrayList<>();
    static TouchControls.Button a, b, start, menu;

    static TouchControls make() {
        log.clear();
        a = new TouchControls.Button("A", KA, true);
        b = new TouchControls.Button("B", KB, true);
        start = new TouchControls.Button("S", KS, false);
        menu = new TouchControls.Button("M", KM, false);
        a.setCircle(700, 400, 40);   // 660..740
        b.setCircle(600, 440, 40);   // 560..640
        start.left = 350; start.right = 450; start.top = 440; start.bottom = 480;
        menu.left = 0; menu.top = 0; menu.right = 40; menu.bottom = 40;
        TouchControls c = new TouchControls((k, d) -> log.add((d ? "+" : "-") + k),
            new int[] {LEFT, RIGHT, UP, DOWN}, a, b, start, menu);
        c.stickX = 100; c.stickY = 400; c.stickR = 60;
        return c;
    }

    static void check(boolean ok, String what) {
        if (!ok) { System.out.println("FAIL: " + what + " log=" + log); System.exit(1); }
    }

    static void expect(String what, String... entries) { check(log.equals(List.of(entries)), what); }

    public static void main(String[] args) {
        TouchControls c = make();

        // A finger that lands on empty space must not block later fingers (the overlay
        // takes the whole gesture; the model just ignores the unclaimed pointer).
        c.down(0, 300, 100);
        c.down(1, 700, 400);
        expect("second finger presses A after a stray first finger", "+" + KA);
        c.up(1);
        c.up(0);
        log.clear();

        // Sliding from B to A releases B and presses A.
        c.down(0, 600, 440);
        c.move(0, 650, 420);   // still inside B's reach? 50px from B centre -> outside; A is 52px away
        c.move(0, 690, 400);
        check(log.contains("+" + KB) && log.contains("-" + KB) && log.contains("+" + KA), "B to A roll");
        check(!b.down && a.down, "B released, A held after roll");
        c.up(0);
        check(!a.down, "A released on lift");

        // Sliding off a button onto nothing releases it.
        c = make();
        c.down(0, 700, 400);
        c.move(0, 400, 100);
        expect("slide off A", "+" + KA, "-" + KA);
        // ...and a finger dragged in from empty space presses a button.
        c.move(0, 700, 400);
        check(a.down, "slide in presses A");

        // Two fingers: stick + A together, lifting one leaves the other held.
        c = make();
        c.down(0, 100, 400);
        c.down(1, 700, 400);
        c.move(0, 10, 400);    // hard left
        check(a.down && log.contains("+" + LEFT), "stick left while A held");
        c.up(1);
        check(!a.down && c.stickPointer == 0, "A lifted, stick still owned");
        c.up(0);
        check(c.knobX == 0 && !log.get(log.size() - 1).startsWith("+"), "stick recentres");

        // Dead zone is radial: 30% of 60px = 18px.
        c = make();
        c.down(0, 100, 400);
        c.move(0, 100 + 15, 400);
        expect("inside dead zone sends nothing", new String[0]);
        c.move(0, 100 + 21, 400);
        expect("just past dead zone presses right", "+" + RIGHT);
        // Diagonal at the same radius presses both axes (21px at 45 degrees).
        c = make();
        c.down(0, 100, 400);
        c.move(0, 100 + 15, 400 + 15);
        check(log.contains("+" + RIGHT) && log.contains("+" + DOWN), "diagonal at radius 21px");
        // Diagonal inside the dead zone radius stays silent (15px per axis is 21px; use 12 -> 17px).
        c = make();
        c.down(0, 100, 400);
        c.move(0, 100 + 12, 400 + 12);
        expect("diagonal inside radial dead zone", new String[0]);

        // Cancel releases everything.
        c = make();
        c.down(0, 100, 400); c.move(0, 10, 400);
        c.down(1, 700, 400);
        c.releaseAll();
        check(!a.down && c.stickPointer == TouchControls.NONE, "cancel releases all");

        // A button already held by another finger is not stolen.
        c = make();
        c.down(0, 700, 400);
        c.down(1, 700, 400);
        c.up(1);
        check(a.down, "second finger on a held button does not release it");

        // Hidden controls claim nothing, so the overlay can decline the gesture and let the
        // touch reach the game surface underneath.
        c = make();
        c.setStickVisible(false);
        for (TouchControls.Button button : c.buttons) c.setButtonVisible(button, false);
        check(!c.down(0, 100, 400), "hidden stick claims no touch");
        check(!c.down(1, 700, 400), "hidden button claims no touch");
        expect("hidden controls send no keys", new String[0]);
        check(!c.down(2, 20, 20), "a hidden button stays unreachable");

        // Hiding a held control releases it: no key stays pressed across the transition.
        c = make();
        check(c.down(0, 700, 400), "shown button claims its touch");
        c.setButtonVisible(a, false);
        expect("hiding a held button releases it", "+" + KA, "-" + KA);
        check(!a.down && a.owner == TouchControls.NONE, "hidden button forgets its pointer");
        c = make();
        c.down(0, 100, 400);
        c.move(0, 10, 400);
        c.setStickVisible(false);
        expect("hiding a held stick releases the arrow keys", "+" + LEFT, "-" + LEFT);
        check(c.stickPointer == TouchControls.NONE && c.knobX == 0 && c.knobY == 0,
              "hidden stick recentres and drops its pointer");

        // Menu can stay shown while the rest of the pad is hidden, so Settings stays reachable.
        c = make();
        boolean shown = TouchOverlayPolicy.controlsShown(TouchOverlayPolicy.AUTO, true);
        check(!shown, "Auto hides the controls while a controller is connected");
        c.setStickVisible(shown);
        for (TouchControls.Button button : c.buttons)
            c.setButtonVisible(button, shown || button == menu);
        check(!c.down(0, 100, 400), "hidden stick ignores a touch");
        check(!c.down(1, 700, 400), "hidden A ignores a touch");
        check(c.down(2, 20, 20), "Menu claims its touch while the rest is hidden");
        expect("Menu is the only control that works while the pad is hidden", "+" + KM);

        // The stored preference: Auto by default, with the launcher cycling through the three
        // choices, and Never hiding even the Menu button because the player asked for that.
        check(TouchOverlayPolicy.controlsShown(null, true) == false, "missing preference is Auto");
        check(TouchOverlayPolicy.controlsShown("junk", true) == false, "unknown preference is Auto");
        check(TouchOverlayPolicy.controlsShown(TouchOverlayPolicy.AUTO, false), "Auto shows without a controller");
        check(TouchOverlayPolicy.controlsShown(TouchOverlayPolicy.ALWAYS, true), "Always ignores a controller");
        check(!TouchOverlayPolicy.controlsShown(TouchOverlayPolicy.NEVER, false), "Never hides without a controller");
        check(TouchOverlayPolicy.menuShown(TouchOverlayPolicy.AUTO), "Auto keeps Menu reachable");
        check(TouchOverlayPolicy.menuShown(TouchOverlayPolicy.ALWAYS), "Always keeps Menu");
        check(!TouchOverlayPolicy.menuShown(TouchOverlayPolicy.NEVER), "Never hides Menu too");
        check(TouchOverlayPolicy.next(null).equals(TouchOverlayPolicy.ALWAYS), "Auto cycles to Always");
        check(TouchOverlayPolicy.next(TouchOverlayPolicy.ALWAYS).equals(TouchOverlayPolicy.NEVER), "Always cycles to Never");
        check(TouchOverlayPolicy.next(TouchOverlayPolicy.NEVER).equals(TouchOverlayPolicy.AUTO), "Never cycles back to Auto");

        System.out.println("OK");
    }
}
