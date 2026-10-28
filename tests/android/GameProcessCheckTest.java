package io.github.alondero.lamborghinirecomp;

public final class GameProcessCheckTest {
    private static final class Clock implements GameProcessCheck.Monitor {
        long time;
        long appears = 500;
        long dies = Long.MAX_VALUE;
        boolean restart;
        public long now() { return time; }
        public void sleep(long milliseconds) { time += milliseconds; }
        public int gamePid() {
            if (time < appears) return 0;
            if (time >= dies) return restart ? 43 : 0;
            return 42;
        }
    }

    private static void fails(Clock clock, long hold, String message) throws Exception {
        try { GameProcessCheck.await(clock, hold); }
        catch (AssertionError expected) {
            if (!expected.getMessage().contains(message)) throw expected;
            return;
        }
        throw new AssertionError("expected failure: " + message);
    }

    public static void main(String[] args) throws Exception {
        Clock normal = new Clock();
        GameProcessCheck.await(normal, 45000);
        if (normal.time != 45500) throw new AssertionError("must hold 45 seconds after startup");
        Clock slow = new Clock();
        slow.appears = 9500;
        GameProcessCheck.await(slow, 45000);
        if (slow.time != 54500) throw new AssertionError("startup must not consume soak time");
        Clock minimum = new Clock();
        GameProcessCheck.await(minimum, 0);
        if (minimum.time != 2500) throw new AssertionError("minimum soak is 2 seconds");
        Clock missing = new Clock();
        missing.appears = Long.MAX_VALUE;
        fails(missing, 45000, "failed to start");
        if (missing.time != 10000) throw new AssertionError("startup timeout is independent of hold");
        Clock earlyDeath = new Clock();
        earlyDeath.dies = 1000;
        fails(earlyDeath, 45000, "died or restarted");
        Clock lateDeath = new Clock();
        lateDeath.dies = 30000;
        fails(lateDeath, 45000, "died or restarted");
        Clock restart = new Clock();
        restart.dies = 30000;
        restart.restart = true;
        fails(restart, 45000, "died or restarted");
        Clock boundaryDeath = new Clock();
        boundaryDeath.dies = 45500;
        fails(boundaryDeath, 45000, "died or restarted");
        System.out.println("OK: full soak, startup timeout, early/late death, restart, boundary death");
    }
}
