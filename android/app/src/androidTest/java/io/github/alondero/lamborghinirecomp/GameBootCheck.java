package io.github.alondero.lamborghinirecomp;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import java.util.List;

/** Boots GameActivity and asserts the :game process survives native startup. */
final class GameBootCheck {
    private static final long WAIT_MS = 10000;
    private static final long POLL_MS = 500;

    private GameBootCheck() {}

    static void run(Context target, long holdMs) throws Exception {
        Intent intent = new Intent(target, GameActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        target.startActivity(intent);
        final long deadline = android.os.SystemClock.uptimeMillis() + Math.max(WAIT_MS, holdMs);
        while (android.os.SystemClock.uptimeMillis() < deadline) {
            if (gameProcessRunning(target)) break;
            Thread.sleep(POLL_MS);
        }
        // The native startup quits silently on failure; require the process
        // to still be alive after the full window, not just briefly present.
        Thread.sleep(2000);
        if (!gameProcessRunning(target)) {
            throw new AssertionError("game process died during startup");
        }
    }

    private static boolean gameProcessRunning(Context target) {
        ActivityManager manager =
            (ActivityManager) target.getSystemService(android.content.Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
        if (processes == null) return false;
        String prefix = target.getPackageName() + ":game";
        for (ActivityManager.RunningAppProcessInfo info : processes) {
            if (prefix.equals(info.processName)) return true;
        }
        return false;
    }
}
