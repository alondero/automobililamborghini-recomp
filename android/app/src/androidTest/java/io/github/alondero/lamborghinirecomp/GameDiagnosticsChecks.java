package io.github.alondero.lamborghinirecomp;

import android.content.Context;
import android.content.ContextWrapper;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;
import java.util.zip.ZipInputStream;

/** Exercise the release export writer against isolated files, including oversized logs. */
final class GameDiagnosticsChecks {
    static void run(Context target, File sandbox) throws Exception {
        File files = new File(sandbox, "diagnostics");
        require(files.mkdir(), "create diagnostics fixture");
        Context context = new ContextWrapper(target) {
            @Override public File getFilesDir() { return files; }
        };
        Map<String, byte[]> empty = export(context);
        require(empty.containsKey("report.txt"), "export works before first game launch");
        require(text(empty, "report.txt").contains("native-errors.log: missing"), "missing logs are explicit");

        Files.write(new File(files, "native.log").toPath(), "device-name".getBytes(StandardCharsets.UTF_8));
        byte[] oversized = new byte[GameDiagnostics.MAX_FILE_BYTES + 37];
        java.util.Arrays.fill(oversized, (byte)'x');
        oversized[oversized.length - 1] = '!';
        Files.write(new File(files, "native-errors.log").toPath(), oversized);
        File logs = new File(files, "LamborghiniRecomp/logs");
        require(logs.mkdirs(), "create log fixtures");
        for (int i = 0; i < 12; ++i) {
            Files.write(new File(logs, String.format(java.util.Locale.ROOT, "lamborghini-%02d.log", i)).toPath(),
                ("log-" + i).getBytes(StandardCharsets.UTF_8));
        }
        File crashes = new File(files, "LamborghiniRecomp/crashes");
        require(crashes.mkdir(), "create crash fixtures");
        Files.write(new File(crashes, "crash-test.txt").toPath(), "stack".getBytes(StandardCharsets.UTF_8));
        Files.write(new File(files, "LamborghiniRecomp/graphics.json").toPath(), "{}".getBytes(StandardCharsets.UTF_8));
        Files.write(new File(files, "game.z64").toPath(), "ROM-MUST-NOT-EXPORT".getBytes(StandardCharsets.UTF_8));
        Files.write(new File(logs, "save.mpk").toPath(), "SAVE-MUST-NOT-EXPORT".getBytes(StandardCharsets.UTF_8));
        // Even a diagnostic-looking symlink must not expose another private file.
        Files.createSymbolicLink(new File(logs, "lamborghini-99.log").toPath(), new File(files, "game.z64").toPath());
        Map<String, byte[]> zip = export(context);
        require(text(zip, "native.log").equals("device-name"), "native stdout preserved");
        require(zip.get("native-errors.log").length == GameDiagnostics.MAX_FILE_BYTES, "stderr size bounded");
        require(zip.get("native-errors.log")[GameDiagnostics.MAX_FILE_BYTES - 1] == '!', "latest stderr retained");
        require(text(zip, "report.txt").contains("truncated"), "truncation recorded");
        require(zip.containsKey("logs/lamborghini-11.log") && !zip.containsKey("logs/lamborghini-01.log"), "newest ten logs only");
        require(!zip.containsKey("logs/lamborghini-99.log"), "symlink excluded");
        require(text(zip, "crashes/crash-test.txt").equals("stack"), "crash report exported");
        require(text(zip, "graphics.json").equals("{}"), "graphics settings exported");
        require(zip.keySet().stream().noneMatch(n -> n.endsWith(".z64") || n.endsWith(".mpk")), "game data excluded");
        require(text(zip, "report.txt").contains("Android:"), "device metadata exported");
        boolean failed = false;
        try {
            GameDiagnostics.write(context, new java.io.OutputStream() {
                @Override public void write(int value) throws java.io.IOException {
                    throw new java.io.IOException("destination full");
                }
            });
        } catch (java.io.IOException expected) { failed = true; }
        require(failed, "destination failure must fail export");
    }

    private static Map<String, byte[]> export(Context context) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        GameDiagnostics.write(context, bytes);
        Map<String, byte[]> result = new HashMap<>();
        try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(bytes.toByteArray()))) {
            java.util.zip.ZipEntry entry;
            byte[] buffer = new byte[8192];
            while ((entry = zip.getNextEntry()) != null) {
                ByteArrayOutputStream content = new ByteArrayOutputStream();
                int count;
                while ((count = zip.read(buffer)) != -1) content.write(buffer, 0, count);
                require(result.put(entry.getName(), content.toByteArray()) == null, "unique ZIP entries");
            }
        }
        return result;
    }

    private static String text(Map<String, byte[]> zip, String name) {
        require(zip.containsKey(name), "missing ZIP entry: " + name);
        return new String(zip.get(name), StandardCharsets.UTF_8);
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
