package io.github.alondero.lamborghinirecomp;

/**
 * Stored Touch controls preference and the rule that shows or hides the on-screen pad,
 * with no Android dependency so it can be tested on a plain JVM
 * (tests/test_android_touch_controls.py).
 *
 * Auto, the default, hides the pad while Android reports a device that SDL's own
 * Android joystick handler accepts. Auto also keeps the Menu button, because that
 * button is the only pointer route into the port's Settings overlay. Always and
 * Never are the player's explicit choices: Never hides everything, and the launcher
 * is then the way back.
 */
final class TouchOverlayPolicy {
    /** SharedPreferences file and key for the launcher's choice. */
    static final String STORE = "touch-controls";
    static final String KEY = "mode";
    static final String AUTO = "auto", ALWAYS = "always", NEVER = "never";

    private TouchOverlayPolicy() {}

    /** Missing or unknown stored values fall back to Auto, so a fresh install is fixed. */
    static String normalize(String stored) {
        return ALWAYS.equals(stored) || NEVER.equals(stored) ? stored : AUTO;
    }

    /** Auto -> Always -> Never -> Auto, for the launcher's toggle. */
    static String next(String stored) {
        switch (normalize(stored)) {
            case ALWAYS: return NEVER;
            case NEVER: return AUTO;
            default: return ALWAYS;
        }
    }

    static String label(String stored) {
        switch (normalize(stored)) {
            case ALWAYS: return "Touch controls: Always";
            case NEVER: return "Touch controls: Never";
            default: return "Touch controls: Auto (hidden while a gamepad is connected)";
        }
    }

    /** True when the stick and buttons are drawn and claim touches. */
    static boolean controlsShown(String stored, boolean controllerConnected) {
        switch (normalize(stored)) {
            case ALWAYS: return true;
            case NEVER: return false;
            default: return !controllerConnected;
        }
    }

    /** True when the Menu button stays drawn and tappable; the route into Settings while the pad is hidden. */
    static boolean menuShown(String stored) {
        return !NEVER.equals(normalize(stored));
    }
}
