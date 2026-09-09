package io.github.alondero.lamborghinirecomp;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.ContextWrapper;
import android.net.Uri;
import android.os.Bundle;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Device regression checks use isolated cache storage, never the user's ROM or driver. */
public final class DriverImportInstrumentation extends Instrumentation {
    private int checks;
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }

    @Override public void onStart() {
        Bundle result = new Bundle();
        File sandbox = null;
        try {
            sandbox = Files.createTempDirectory(getTargetContext().getCacheDir().toPath(), "driver-tests-").toFile();
            final File files = new File(sandbox, "files");
            require(files.mkdir(), "create isolated files directory");
            Context context = new ContextWrapper(getTargetContext()) {
                @Override public File getFilesDir() { return files; }
            };
            File valid = archive(sandbox, "valid", "libtest.so", metadata(), elf());
            DriverImport.install(context, Uri.fromFile(valid));
            require(driverCount(files) == 1, "initial import");
            DriverImport.install(context, Uri.fromFile(valid));
            require(driverCount(files) == 1, "replacement removes old driver");
            String selected = new String(Files.readAllBytes(new File(files, "gpu-driver.json").toPath()), StandardCharsets.UTF_8);
            reject(context, archive(sandbox, "traversal", "../libtest.so", metadata(), elf()));
            byte[] wrongMachine = elf(); wrongMachine[18] = 62;
            reject(context, archive(sandbox, "wrong-abi", "libtest.so", metadata(), wrongMachine));
            reject(context, archive(sandbox, "large-meta", "libtest.so", new byte[65537], elf()));
            require(selected.equals(new String(Files.readAllBytes(new File(files, "gpu-driver.json").toPath()), StandardCharsets.UTF_8)), "failed imports preserve selection");
            require(driverCount(files) == 1, "failed imports leave no directories");
            DriverImport.useSystem(context);
            require(driverCount(files) == 0 && !new File(files, "gpu-driver.json").exists(), "system selection removes imported drivers");
            DriverImport.install(context, Uri.fromFile(valid));
            // configure holds the same OS lock used by the separate game process.
            DriverImport.configure(context);
            reject(context, valid);
            require(driverCount(files) == 1, "running game protects driver files");
            DriverImport.releaseGameLock();
            Files.write(new File(files, "gpu-driver.json").toPath(),
                "{\"name\":\"missing\",\"directory\":\"gpu-driver-missing\",\"libraryName\":\"libtest.so\"}"
                    .getBytes(StandardCharsets.UTF_8));
            boolean failedConfiguration = false;
            try { DriverImport.configure(context); }
            catch (Exception expected) { failedConfiguration = true; }
            require(failedConfiguration, "missing selected driver is rejected");
            DriverImport.install(context, Uri.fromFile(valid));
            require(driverCount(files) == 1, "failed startup releases driver lock");
            DriverImport.configure(context);
            result.putString("stream", "\n" + checks + " Android driver import checks passed.\n");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable error) {
            result.putString("stream", "\nFAILED: " + android.util.Log.getStackTraceString(error));
            finish(Activity.RESULT_CANCELED, result);
        } finally {
            if (sandbox != null) remove(sandbox);
        }
    }

    private void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
        checks++;
    }

    private void reject(Context context, File archive) throws Exception {
        boolean rejected = false;
        try { DriverImport.install(context, Uri.fromFile(archive)); }
        catch (Exception expected) { rejected = true; }
        require(rejected, "reject " + archive.getName());
    }

    private static int driverCount(File files) {
        File[] directories = files.listFiles(file -> file.isDirectory() && file.getName().startsWith("gpu-driver-"));
        return directories == null ? 0 : directories.length;
    }

    private static byte[] metadata() {
        return "{\"name\":\"Test\",\"libraryName\":\"libtest.so\",\"minApi\":26}".getBytes(StandardCharsets.UTF_8);
    }

    private static byte[] elf() {
        byte[] bytes = new byte[20];
        bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
        bytes[4] = 2; bytes[5] = 1; bytes[18] = (byte)183;
        return bytes;
    }

    private static File archive(File parent, String name, String library, byte[] metadata, byte[] elf) throws Exception {
        File file = new File(parent, name + ".zip");
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(file))) {
            zip.putNextEntry(new ZipEntry("meta.json")); zip.write(metadata); zip.closeEntry();
            zip.putNextEntry(new ZipEntry(library)); zip.write(elf); zip.closeEntry();
        }
        return file;
    }

    private static void remove(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) remove(child);
        file.delete();
    }
}
