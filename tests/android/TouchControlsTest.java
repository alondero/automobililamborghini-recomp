package io.github.alondero.lamborghinirecomp;

import java.util.ArrayList;
import java.util.List;

/** Plain-JVM checks for TouchControls; run by tests/test_android_touch_controls.py. */
public final class TouchControlsTest {
    static final int LEFT = 1, RIGHT = 2, UP = 3, DOWN = 4, KA = 10, KB = 11, KS = 12;
    static final List<String> log = new ArrayList<>();
    static TouchControls.Button a, b, start;

    static TouchControls make() {
        log.clear();
        a = new TouchControls.Button("A", KA, true);
        b = new TouchControls.Button("B", KB, true);
        start = new TouchControls.Button("S", KS, false);
        a.setCircle(700, 400, 40);   // 660..740
        b.setCircle(600, 440, 40);   // 560..640
        start.left = 350; start.right = 450; start.top = 440; start.bottom = 480;
        TouchControls c = new TouchControls((k, d) -> log.add((d ? "+" : "-") + k),
            new int[] {LEFT, RIGHT, UP, DOWN}, a, b, start);
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

        System.out.println("OK");
    }
}
