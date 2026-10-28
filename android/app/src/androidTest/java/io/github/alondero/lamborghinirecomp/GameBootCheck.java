package io.github.alondero.lamborghinirecomp;

import android.app.ActivityManager;
import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import java.util.List;

/** Presses the launcher's Play button, then watches the newly started game PID. */
final class GameBootCheck {
    private GameBootCheck() {}

    static LauncherActivity launcher(Instrumentation instrumentation) {
        Intent intent = new Intent(Intent.ACTION_MAIN)
            .addCategory(Intent.CATEGORY_LAUNCHER)
            .setClass(instrumentation.getTargetContext(), LauncherActivity.class)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        return (LauncherActivity) instrumentation.startActivitySync(intent);
    }

    static void run(Instrumentation instrumentation, long holdMs) throws Exception {
        Context target = instrumentation.getTargetContext();
        if (gamePid(target) != 0)
            throw new AssertionError("game already running; stop it before gameboot");
        LauncherActivity launcher = launcher(instrumentation);
        instrumentation.waitForIdleSync();
        Button[] play = new Button[1];
        boolean[] enabled = new boolean[1];
        instrumentation.runOnMainSync(() -> {
            play[0] = findPlay(launcher.getWindow().getDecorView());
            enabled[0] = play[0] != null && play[0].isEnabled();
        });
        if (!enabled[0])
            throw new AssertionError("import the supported USA ROM through the launcher first");
        instrumentation.runOnMainSync(() -> play[0].performClick());
        GameProcessCheck.await(new GameProcessCheck.Monitor() {
            public long now() { return SystemClock.uptimeMillis(); }
            public void sleep(long milliseconds) throws InterruptedException { Thread.sleep(milliseconds); }
            public int gamePid() { return GameBootCheck.gamePid(target); }
        }, holdMs);
    }

    private static Button findPlay(View view) {
        if (view instanceof Button && "Play".contentEquals(((Button) view).getText()))
            return (Button) view;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                Button button = findPlay(group.getChildAt(i));
                if (button != null) return button;
            }
        }
        return null;
    }

    private static int gamePid(Context target) {
        ActivityManager manager = (ActivityManager) target.getSystemService(Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
        if (processes != null) {
            for (ActivityManager.RunningAppProcessInfo info : processes) {
                if ((target.getPackageName() + ":game").equals(info.processName)) return info.pid;
            }
        }
        return 0;
    }
}
