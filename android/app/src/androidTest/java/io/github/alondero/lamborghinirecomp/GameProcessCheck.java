package io.github.alondero.lamborghinirecomp;

/** Monotonic startup and survival checks, shared with the host timing fixture. */
final class GameProcessCheck {
    interface Monitor {
        long now();
        void sleep(long milliseconds) throws Exception;
        int gamePid();
    }

    private GameProcessCheck() {}

    static void await(Monitor monitor, long holdMs) throws Exception {
        long waitingSince = monitor.now();
        int pid;
        while ((pid = monitor.gamePid()) == 0) {
            if (monitor.now() - waitingSince >= 10000)
                throw new AssertionError("game process failed to start within 10 seconds");
            monitor.sleep(500);
        }
        long started = monitor.now();
        long duration = Math.max(2000, holdMs);
        long elapsed;
        while ((elapsed = monitor.now() - started) < duration) {
            monitor.sleep(Math.min(500, duration - elapsed));
            if (monitor.gamePid() != pid)
                throw new AssertionError("game process died or restarted during soak window");
        }
    }
}
