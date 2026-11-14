package io.github.alondero.lamborghinirecomp;

import android.content.Context;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import org.json.JSONObject;
import org.json.JSONException;

/** Pre-renderer recovery; shares the driver's cross-process game lock. */
final class GraphicsRecovery {
    static void apply(Context context) throws Exception {
        try (DriverImport.DriverLock lock = new DriverImport.DriverLock(context)) {
            applyToDirectory(context.getFilesDir());
        }
    }

    // The caller holds the game lock through backup and publication.
    private static void applyToDirectory(File directory) throws Exception {
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
            Files.move(temporary.toPath(), config.toPath(),
                StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally {
            temporary.delete();
        }
    }
}
