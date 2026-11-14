package io.github.alondero.lamborghinirecomp;

import android.content.Context;
import android.content.ContextWrapper;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import org.json.JSONObject;

/** Isolated filesystem checks; also runnable with app_process without installing an APK. */
public final class GraphicsRecoveryChecks {
    public static void main(String[] args) throws Exception {
        run(new File(args[0]));
        System.out.println("Graphics recovery: all checks passed");
    }

    static void run(File sandbox) throws Exception {
        File files = Files.createTempDirectory(sandbox.toPath(), "graphics-tests-").toFile();
        Context context = new ContextWrapper(null) {
            @Override public File getFilesDir() { return files; }
        };
        File config = new File(files, "graphics.json");
        try {
            GraphicsRecovery.apply(context);
            JSONObject values = read(config);
            require(values.getString("res_option").equals("Original2x"), "safe resolution on first use");
            require(values.getString("rr_option").equals("Original"), "original frame rate");
            require(values.getString("msaa_option").equals("None"), "anti-aliasing off");
            require(values.getInt("ds_option") == 1, "supersampling off");
            require(!values.getBoolean("rt_shadows"), "shadows off");

            byte[] original = "{\"res_option\":\"Auto\",\"camera_height_scale\":1.5,\"future\":{\"enabled\":true}}"
                .getBytes(StandardCharsets.UTF_8);
            Files.write(config.toPath(), original);
            GraphicsRecovery.apply(context);
            values = read(config);
            require(values.getDouble("camera_height_scale") == 1.5, "keep camera settings");
            require(values.getJSONObject("future").getBoolean("enabled"), "keep unknown nested settings");
            File backup = new File(files, "graphics-before-recovery.json");
            require(Arrays.equals(original, Files.readAllBytes(backup.toPath())), "exact backup");

            try (DriverImport.DriverLock lock = new DriverImport.DriverLock(context)) {
                byte[] before = Files.readAllBytes(config.toPath());
                boolean rejected = false;
                try { GraphicsRecovery.apply(context); }
                catch (Exception expected) { rejected = true; }
                require(rejected, "running game blocks recovery");
                require(Arrays.equals(before, Files.readAllBytes(config.toPath())), "locked file unchanged");
                require(Arrays.equals(original, Files.readAllBytes(backup.toPath())), "locked backup unchanged");
            }

            Files.write(config.toPath(), "{broken".getBytes(StandardCharsets.UTF_8));
            GraphicsRecovery.apply(context);
            require(read(config).getString("res_option").equals("Original2x"), "recover malformed JSON");
            require(new String(Files.readAllBytes(backup.toPath()), StandardCharsets.UTF_8).equals("{broken"), "keep malformed original");

            // A failed backup must not destroy the configuration it was meant to protect.
            Files.delete(backup.toPath());
            require(backup.mkdir(), "block backup destination");
            byte[] before = Files.readAllBytes(config.toPath());
            boolean rejected = false;
            try { GraphicsRecovery.apply(context); }
            catch (Exception expected) { rejected = true; }
            require(rejected, "report write failure");
            require(Arrays.equals(before, Files.readAllBytes(config.toPath())), "failed write preserves config");
        } finally {
            for (File file : files.listFiles()) file.delete();
            files.delete();
        }
    }

    private static JSONObject read(File file) throws Exception {
        return new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
