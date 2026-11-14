package io.github.alondero.lamborghinirecomp;

import android.content.Context;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.CopyOption;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import org.json.JSONObject;
import org.json.JSONException;

/** Pre-renderer recovery; shares the driver's cross-process game lock. */
final class GraphicsRecovery {
    static void apply(Context context) throws Exception {
        apply(context, Files::move);
    }

    // The move boundary lets tests exercise mounts without atomic-rename support.
    interface MoveOperation {
        void move(Path source, Path destination, CopyOption... options) throws IOException;
    }

    static void apply(Context context, MoveOperation move) throws Exception {
        try (DriverImport.DriverLock lock = new DriverImport.DriverLock(context)) {
            File directory = configDirectory(context);
            Files.createDirectories(directory.toPath());
            removeTemporaryFiles(directory);
            applyToDirectory(directory, move);
        }
    }

    private static File configDirectory(Context context) {
        // main.cpp sets XDG_CONFIG_HOME to filesDir; lambo_paths.h appends this name.
        return new File(context.getFilesDir(), "LamborghiniRecomp");
    }

    static void cleanupTemporaryFiles(Context context) {
        // Never delete another recovery's in-flight file, including across processes.
        try (DriverImport.DriverLock lock = new DriverImport.DriverLock(context)) {
            removeTemporaryFiles(configDirectory(context));
        } catch (Exception error) {
            android.util.Log.w("Lamborghini", "Graphics recovery cleanup deferred", error);
        }
    }

    private static void removeTemporaryFiles(File directory) throws IOException {
        File[] files = directory.listFiles();
        if (files == null) return;
        for (File file : files) {
            String name = file.getName();
            if (file.isFile() && name.startsWith("graphics-recovery-") && name.endsWith(".tmp"))
                Files.deleteIfExists(file.toPath());
        }
    }

    // The caller holds the game lock through backup and publication.
    private static void applyToDirectory(File directory, MoveOperation move) throws Exception {
        File config = new File(directory, "graphics.json");
        JSONObject values = new JSONObject();
        if (config.exists()) {
            byte[] previous = Files.readAllBytes(config.toPath());
            // Preserve the exact original, including malformed JSON, before replacing it.
            Files.write(new File(directory, "graphics-before-recovery.json").toPath(), previous);
            try { values = new JSONObject(new String(previous, StandardCharsets.UTF_8)); }
            catch (JSONException invalid) { /* Recover defaults; original is in the backup. */ }
        }
        values.put("res_option", "Original2x");
        values.put("msaa_option", "None");
        values.put("rr_option", "Original");
        values.put("hpfb_option", "Off");
        values.put("ds_option", 1);
        values.put("developer_mode", false);
        values.put("rt_shadows", false);
        File temporary = File.createTempFile("graphics-recovery-", ".tmp", directory);
        try {
            Files.write(temporary.toPath(), values.toString(4).getBytes(StandardCharsets.UTF_8));
            try {
                move.move(temporary.toPath(), config.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException unsupported) {
                move.move(temporary.toPath(), config.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
        } finally {
            temporary.delete();
        }
    }
}
