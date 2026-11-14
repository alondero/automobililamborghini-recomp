package io.github.alondero.lamborghinirecomp;

import android.content.Context;
import android.content.ContextWrapper;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.StandardCopyOption;
import java.io.IOException;
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
        // Native startup sets XDG_CONFIG_HOME to filesDir; lambo_paths.h appends
        // LamborghiniRecomp. Match the reader, not the recovery writer's helper.
        File directory = new File(files, "LamborghiniRecomp");
        File config = new File(directory, "graphics.json");
        try {
            Files.write(directory.toPath(), new byte[]{42});
            boolean directoryRejected = false;
            try { GraphicsRecovery.apply(context); }
            catch (Exception expected) { directoryRejected = true; }
            require(directoryRejected && directory.isFile(), "report blocked config directory without replacing it");
            Files.delete(directory.toPath());
            GraphicsRecovery.apply(context);
            require(directory.isDirectory(), "create native config directory on first use");
            require(!new File(files, "graphics.json").exists(), "no orphan config in files root");
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
            File backup = new File(directory, "graphics-before-recovery.json");
            require(Arrays.equals(original, Files.readAllBytes(backup.toPath())), "exact backup");

            File orphan = new File(directory, "graphics-recovery-interrupted.tmp");
            File unrelated = new File(directory, "graphics-recovery-not-temporary.json");
            Files.write(orphan.toPath(), new byte[]{1});
            Files.write(unrelated.toPath(), new byte[]{2});

            try (DriverImport.DriverLock lock = new DriverImport.DriverLock(context)) {
                byte[] before = Files.readAllBytes(config.toPath());
                boolean rejected = false;
                try { GraphicsRecovery.apply(context); }
                catch (Exception expected) { rejected = true; }
                require(rejected, "running game blocks recovery");
                require(Arrays.equals(before, Files.readAllBytes(config.toPath())), "locked file unchanged");
                require(Arrays.equals(original, Files.readAllBytes(backup.toPath())), "locked backup unchanged");
                GraphicsRecovery.cleanupTemporaryFiles(context);
                require(orphan.isFile(), "cleanup respects active game/recovery lock");
            }

            GraphicsRecovery.cleanupTemporaryFiles(context);
            require(!orphan.exists() && unrelated.isFile(), "cleanup only orphan recovery temporaries");
            require(Arrays.equals(original, Files.readAllBytes(backup.toPath())), "cleanup preserves backup");

            int[] moves = {0};
            GraphicsRecovery.apply(context, (source, destination, options) -> {
                moves[0]++;
                if (moves[0] == 1) {
                    require(Arrays.asList(options).contains(StandardCopyOption.ATOMIC_MOVE), "try atomic move first");
                    throw new AtomicMoveNotSupportedException(source.toString(), destination.toString(), "test mount");
                }
                require(options.length == 1 && options[0] == StandardCopyOption.REPLACE_EXISTING, "fallback replaces without atomic flag");
                Files.move(source, destination, options);
            });
            require(moves[0] == 2 && read(config).getString("res_option").equals("Original2x"), "fallback publishes native config");

            byte[] beforeMoveFailure = Files.readAllBytes(config.toPath());
            moves[0] = 0;
            boolean moveRejected = false;
            try {
                GraphicsRecovery.apply(context, (source, destination, options) -> {
                    moves[0]++;
                    throw new IOException("test write failure");
                });
            } catch (IOException expected) { moveRejected = true; }
            require(moveRejected && moves[0] == 1, "other move failures do not trigger fallback");
            require(Arrays.equals(beforeMoveFailure, Files.readAllBytes(config.toPath())), "failed publication preserves config");
            require(directory.listFiles((parent, name) -> name.startsWith("graphics-recovery-") && name.endsWith(".tmp")).length == 0,
                "failed publication removes temporary file");

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
            File[] configs = directory.listFiles();
            if (configs != null) for (File file : configs) file.delete();
            directory.delete();
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
