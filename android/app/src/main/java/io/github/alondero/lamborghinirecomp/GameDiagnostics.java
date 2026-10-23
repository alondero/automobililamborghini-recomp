package io.github.alondero.lamborghinirecomp;

import android.annotation.TargetApi;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.os.Build;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.time.Instant;
import java.util.Arrays;
import java.util.Comparator;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Release-safe diagnostics. Call on a worker: exit traces and document providers can block. */
final class GameDiagnostics {
    static final int MAX_FILE_BYTES = 512 * 1024;

    static void write(Context context, OutputStream destination) throws Exception {
        StringBuilder report = new StringBuilder("Lamborghini Android diagnostics\n");
        PackageInfo app = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
        report.append("Exported: ").append(Instant.now()).append('\n')
            .append("Installed app: ").append(app.versionName).append(" (").append(app.versionCode).append(")\n")
            .append("Android: ").append(Build.VERSION.RELEASE).append(" API ").append(Build.VERSION.SDK_INT).append('\n')
            .append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" / ").append(Build.DEVICE).append('\n')
            .append("Build: ").append(Build.FINGERPRINT).append('\n')
            .append("ABI: ").append(Arrays.toString(Build.SUPPORTED_ABIS)).append('\n')
            .append("Selected GPU driver now: ").append(DriverImport.description(context)).append('\n')
            .append("Historical exits/logs may predate this app version or driver selection.\n")
            .append("Log files contain their last ").append(MAX_FILE_BYTES).append(" bytes at most.\n");
        try (ZipOutputStream zip = new ZipOutputStream(destination)) {
            File files = context.getFilesDir();
            addFile(zip, new File(files, "native.log"), "native.log", report);
            addFile(zip, new File(files, "native-errors.log"), "native-errors.log", report);
            addFile(zip, new File(files, "LamborghiniRecomp/graphics.json"), "graphics.json", report);
            addRecent(zip, new File(files, "LamborghiniRecomp/logs"), "logs/", "lamborghini-", ".log", 10, report);
            addRecent(zip, new File(files, "LamborghiniRecomp/crashes"), "crashes/", "crash-", ".txt", 3, report);
            if (Build.VERSION.SDK_INT >= 30) ExitHistory.add(context, zip, report);
            else report.append("Process exit history unavailable before Android 11.\n");
            addBytes(zip, "report.txt", report.toString().getBytes(StandardCharsets.UTF_8));
        }
    }

    private static void addRecent(ZipOutputStream zip, File directory, String prefix,
                                  String start, String end, int limit, StringBuilder report) throws IOException {
        if (!Files.isDirectory(directory.toPath(), LinkOption.NOFOLLOW_LINKS)) return;
        File[] files = directory.listFiles(file -> file.getName().startsWith(start)
            && file.getName().endsWith(end) && Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS));
        if (files == null) { report.append(prefix).append(": unavailable\n"); return; }
        // Native log/report names begin with their timestamp; newest first.
        Arrays.sort(files, Comparator.comparing(File::getName).reversed());
        for (int i = 0; i < Math.min(limit, files.length); ++i)
            addFile(zip, files[i], prefix + files[i].getName(), report);
    }

    private static void addFile(ZipOutputStream zip, File file, String name, StringBuilder report) throws IOException {
        if (!Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            report.append(name).append(": missing or not a regular file\n");
            return;
        }
        byte[] content;
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long length = input.length();
            int count = (int)Math.min(length, MAX_FILE_BYTES);
            input.seek(length - count);
            content = new byte[count];
            input.readFully(content);
            report.append(name).append(": ").append(length).append(" bytes, modified ")
                .append(Instant.ofEpochMilli(file.lastModified()));
            if (length > count) report.append(" (truncated to last ").append(count).append(")");
            report.append('\n');
        } catch (IOException error) {
            report.append(name).append(": unavailable: ").append(error).append('\n');
            return;
        }
        // Destination failures must fail the export, rather than report success with a broken ZIP.
        addBytes(zip, name, content);
    }

    private static void addBytes(ZipOutputStream zip, String name, byte[] content) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(content);
        zip.closeEntry();
    }

    // Keep Android 11-only types out of the entry point used on older devices.
    // This app uses platform annotations only; the caller checks SDK_INT.
    @android.annotation.SuppressLint("UseRequiresApi")
    @TargetApi(30)
    private static final class ExitHistory {
        static void add(Context context, ZipOutputStream zip, StringBuilder report) throws IOException {
            java.util.List<ApplicationExitInfo> history;
            try {
                ActivityManager manager = context.getSystemService(ActivityManager.class);
                history = manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 10);
            } catch (RuntimeException error) {
                report.append("Process exit history unavailable: ").append(error).append('\n');
                return;
            }
            report.append("\nRecent game process exits (up to ten package records):\n");
            int gameExits = 0;
            int traces = 0;
            for (ApplicationExitInfo exit : history) {
                if (!(context.getPackageName() + ":game").equals(exit.getProcessName())) continue;
                report.append(exit).append('\n');
                ++gameExits;
                if (traces == 3) continue;
                byte[] trace = null;
                try (InputStream input = exit.getTraceInputStream()) {
                    if (input == null) { report.append("No retained trace.\n"); continue; }
                    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                    byte[] buffer = new byte[8192];
                    int read;
                    while (bytes.size() < MAX_FILE_BYTES && (read = input.read(buffer, 0,
                            Math.min(buffer.length, MAX_FILE_BYTES - bytes.size()))) != -1)
                        bytes.write(buffer, 0, read);
                    trace = bytes.toByteArray();
                    if (input.read() != -1) report.append("Trace truncated to first ").append(MAX_FILE_BYTES).append(" bytes.\n");
                } catch (IOException | RuntimeException error) {
                    report.append("Trace unavailable: ").append(error).append('\n');
                }
                if (trace != null) {
                    // Android 12+ native crash traces use protobuf; ANR traces are text.
                    String extension = exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE ? ".pb" : ".txt";
                    addBytes(zip, "exit-traces/" + exit.getTimestamp() + "-" + exit.getPid() + extension, trace);
                    ++traces;
                }
            }
            if (gameExits == 0) report.append("No retained game process exits.\n");
        }
    }
}
