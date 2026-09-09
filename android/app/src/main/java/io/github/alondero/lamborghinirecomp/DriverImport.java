package io.github.alondero.lamborghinirecomp;

import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.system.Os;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.StandardOpenOption;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.HashSet;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import org.json.JSONObject;

// AdrenoTools-compatible ZIP import. Driver code stays private to this app.
final class DriverImport {
    // A process-wide file lock prevents replacing/removing libraries while the
    // separate game process might still load them. Process death releases it.
    private static DriverLock gameLock;

    private static final class DriverLock implements AutoCloseable {
        private final FileChannel channel;
        private final FileLock lock;

        DriverLock(Context context) throws Exception {
            channel = FileChannel.open(new File(context.getFilesDir(), "gpu-driver.lock").toPath(),
                StandardOpenOption.CREATE, StandardOpenOption.WRITE);
            try {
                lock = channel.tryLock();
                if (lock == null) throw new Exception("Close the running game before changing GPU drivers");
            } catch (Exception error) {
                channel.close();
                throw error;
            }
        }

        @Override public void close() throws Exception {
            try { lock.release(); } finally { channel.close(); }
        }
    }

    private static void pruneDrivers(Context context, File selected) {
        File[] directories = context.getFilesDir().listFiles();
        if (directories == null) return;
        for (File directory : directories) {
            if (!directory.isDirectory() || !directory.getName().matches("gpu-driver-[0-9]+") || directory.equals(selected)) continue;
            File[] files = directory.listFiles();
            if (files != null) for (File file : files) file.delete();
            if (!directory.delete()) android.util.Log.w("Lamborghini", "Could not remove obsolete driver " + directory.getName());
        }
    }

    private static File selection(Context context) { return new File(context.getFilesDir(), "gpu-driver.json"); }

    static String description(Context context) {
        try { return new JSONObject(new String(Files.readAllBytes(selection(context).toPath()), StandardCharsets.UTF_8)).getString("name"); }
        catch (Exception ignored) { return "System GPU driver"; }
    }

    static void useSystem(Context context) throws Exception {
        try (DriverLock lock = new DriverLock(context)) {
            Files.deleteIfExists(selection(context).toPath());
            pruneDrivers(context, null);
        }
    }

    static void configure(Context context) throws Exception {
        if (gameLock != null) return;
        DriverLock lock = new DriverLock(context);
        try {
            File selected = selection(context);
            if (selected.isFile()) {
                JSONObject config = new JSONObject(new String(Files.readAllBytes(selected.toPath()), StandardCharsets.UTF_8));
                File directory = new File(context.getFilesDir(), config.getString("directory"));
                String library = config.getString("libraryName");
                if (!new File(directory, library).isFile()) throw new Exception("Imported GPU driver is missing; select System GPU driver or import again");
                Os.setenv("LAMBO_ANDROID_DRIVER_DIR", directory.getAbsolutePath() + "/", true);
                Os.setenv("LAMBO_ANDROID_DRIVER_NAME", library, true);
            }
            gameLock = lock;
        } catch (Exception error) {
            lock.close();
            throw error;
        }
    }

    static void releaseGameLock() {
        DriverLock lock = gameLock;
        gameLock = null;
        if (lock != null) {
            try { lock.close(); }
            catch (Exception error) { android.util.Log.w("Lamborghini", "Could not release GPU driver lock", error); }
        }
    }

    static String install(Context context, Uri uri) throws Exception {
        try (DriverLock lock = new DriverLock(context)) {
            return installLocked(context, uri);
        }
    }

    private static String installLocked(Context context, Uri uri) throws Exception {
        if (Build.VERSION.SDK_INT < 28) throw new Exception("Custom GPU drivers require Android 9 or newer");
        File directory = Files.createTempDirectory(context.getFilesDir().toPath(), "gpu-driver-").toFile();
        boolean installed = false;
        try {
            long total = 0;
            HashSet<String> names = new HashSet<>();
            try (InputStream input = context.getContentResolver().openInputStream(uri)) {
                if (input == null) throw new Exception("Cannot open driver archive");
                try (ZipInputStream zip = new ZipInputStream(input)) {
                    ZipEntry entry;
                    byte[] buffer = new byte[16384];
                    while ((entry = zip.getNextEntry()) != null) {
                        String name = entry.getName();
                        // No directories, links or paths are interpreted from the ZIP.
                        if (!name.matches("[A-Za-z0-9_.-]+") || name.equals(".") || name.equals("..") || entry.isDirectory())
                            throw new Exception("Driver archive must contain files at its root");
                        if (!names.add(name) || names.size() > 32) throw new Exception("Invalid driver archive entries");
                        boolean extract = name.endsWith(".so") || name.equals("meta.json");
                        long entrySize = 0;
                        try (FileOutputStream output = extract ? new FileOutputStream(new File(directory, name)) : null) {
                            int count;
                            while ((count = zip.read(buffer)) != -1) {
                                total += count;
                                entrySize += count;
                                if (name.equals("meta.json") && entrySize > 65536) throw new Exception("Driver metadata exceeds 64 KiB");
                                if (total > 128L * 1024 * 1024) throw new Exception("Driver archive exceeds 128 MiB");
                                if (output != null) output.write(buffer, 0, count);
                            }
                        }
                    }
                }
            }
            JSONObject meta = new JSONObject(new String(Files.readAllBytes(new File(directory, "meta.json").toPath()), StandardCharsets.UTF_8));
            String library = meta.getString("libraryName");
            if (!library.matches("[A-Za-z0-9_.-]+\\.so") || !new File(directory, library).isFile())
                throw new Exception("Driver library is missing");
            if (meta.getInt("minApi") > Build.VERSION.SDK_INT) throw new Exception("This driver requires a newer Android version");
            // The port only ships ARM64. Reject an ELF for a different machine.
            byte[] elf = new byte[20];
            try (InputStream input = Files.newInputStream(new File(directory, library).toPath())) {
                if (input.read(elf) != elf.length || elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F' || elf[4] != 2 || elf[5] != 1 || (elf[18] & 255) != 183 || elf[19] != 0)
                    throw new Exception("Driver must contain an ARM64 ELF library");
            }
            JSONObject config = new JSONObject().put("name", meta.getString("name"))
                .put("directory", directory.getName()).put("libraryName", library);
            File temporary = File.createTempFile("gpu-selection-", ".tmp", context.getFilesDir());
            try {
                Files.write(temporary.toPath(), config.toString().getBytes(StandardCharsets.UTF_8));
                Files.move(temporary.toPath(), selection(context).toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } finally { temporary.delete(); }
            installed = true;
            pruneDrivers(context, directory);
            return meta.getString("name");
        } finally {
            if (!installed) {
                File[] files = directory.listFiles();
                if (files != null) for (File file : files) file.delete();
                directory.delete();
            }
        }
    }
}
