package io.github.alondero.lamborghinirecomp;

import android.app.Instrumentation;
import android.content.res.AssetManager;
import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.Arrays;

/** Executes the real launcher extraction against the APK in empty cache storage. */
final class LauncherAssetChecks {
    private LauncherAssetChecks() {}

    static void run(Instrumentation instrumentation, File sandbox) throws Exception {
        LauncherActivity launcher = GameBootCheck.launcher(instrumentation);
        try {
            File files = new File(sandbox, "extracted");
            if (!files.mkdir()) throw new AssertionError("create extraction sandbox");
            launcher.ensureAssets(files);
            verify(launcher.getAssets(), files);
            File marker = new File(files, "assets/.assets-version");
            byte[] version = Files.readAllBytes(marker.toPath());
            String expected = Integer.toString(launcher.getPackageManager()
                .getPackageInfo(launcher.getPackageName(), 0).versionCode);
            if (!expected.equals(new String(version, StandardCharsets.UTF_8)))
                throw new AssertionError("asset marker must match installed version");
            long timestamp = 1000000000000L;
            if (!marker.setLastModified(timestamp)) throw new AssertionError("set marker timestamp");
            long cachedTimestamp = marker.lastModified();
            launcher.ensureAssets(files);
            if (marker.lastModified() != cachedTimestamp)
                throw new AssertionError("current complete extraction should be reused");
            Files.write(marker.toPath(), "outdated".getBytes(StandardCharsets.UTF_8));
            launcher.ensureAssets(files);
            verify(launcher.getAssets(), files);
            if (!Arrays.equals(version, Files.readAllBytes(marker.toPath())))
                throw new AssertionError("stale marker must be replaced");
            Files.delete(new File(files, "assets/LatoLatin-Regular.ttf").toPath());
            launcher.ensureAssets(files);
            verify(launcher.getAssets(), files);
            Files.delete(new File(files, "lamborghini.syms.toml").toPath());
            launcher.ensureAssets(files);
            verify(launcher.getAssets(), files);
        } finally {
            instrumentation.runOnMainSync(launcher::finish);
        }
    }

    private static void verify(AssetManager assets, File files) throws Exception {
        for (String path : new String[]{"assets/LatoLatin-Regular.ttf", "assets/LatoLatin-Bold.ttf",
                "assets/NotoEmoji-Regular.ttf", "assets/promptfont/promptfont.ttf", "assets/recomp.rcss"}) {
            if (!new File(files, path).isFile()) throw new AssertionError("missing extracted " + path);
        }
        compare(assets, "assets", files);
        compare(assets, "lamborghini.syms.toml", files);
        if (new File(files, "assets/assets").exists()) throw new AssertionError("extra extracted assets level");
    }

    private static void compare(AssetManager assets, String path, File files) throws Exception {
        String[] children = assets.list(path);
        if (children != null && children.length > 0) {
            for (String child : children) compare(assets, path + "/" + child, files);
        } else {
            byte[] bundled;
            try (InputStream input = assets.open(path)) { bundled = digest(input); }
            try (InputStream input = Files.newInputStream(new File(files, path).toPath())) {
                if (!Arrays.equals(bundled, digest(input)))
                    throw new AssertionError("extracted bytes differ: " + path);
            }
        }
    }

    private static byte[] digest(InputStream input) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] buffer = new byte[16384];
        int count;
        while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
        return digest.digest();
    }
}
