package io.github.alondero.lamborghinirecomp;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.ScrollView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class LauncherActivity extends Activity {
    private static final int IMPORT_ROM = 1;
    private static final int IMPORT_DRIVER = 2;
    private static final String ROM = "Automobili Lamborghini (USA).z64";
    private static final String SHA256 = "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c";
    private static final ExecutorService IMPORTS = Executors.newSingleThreadExecutor();
    private TextView status;
    private Button play;
    private Button select;
    private Button driver;
    private Button systemDriver;
    private boolean resumed;

    @Override protected void onResume() { super.onResume(); resumed = true; }
    @Override protected void onPause() { resumed = false; super.onPause(); }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int padding = (int)(24 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        status = new TextView(this);
        status.setTextSize(20);
        status.setText("Automobili Lamborghini: Recompiled\n\nImport your own USA cartridge dump to play. Touch controls and Bluetooth/USB gamepads are supported.");
        layout.addView(status);
        select = new Button(this);
        select.setText("Import ROM");
        select.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, IMPORT_ROM);
        });
        layout.addView(select);
        play = new Button(this);
        play.setText("Play");
        play.setEnabled(new File(getFilesDir(), ROM).isFile());
        play.setOnClickListener(v -> prepare(null));
        layout.addView(play);
        TextView driverInfo = new TextView(this);
        driverInfo.setText("GPU driver: " + DriverImport.description(this) + "\nOlder Adreno devices may need a Mesa Turnip driver ZIP for Vulkan support.");
        layout.addView(driverInfo);
        driver = new Button(this);
        driver.setText("Import GPU driver ZIP");
        driver.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            startActivityForResult(intent, IMPORT_DRIVER);
        });
        layout.addView(driver);
        systemDriver = new Button(this);
        systemDriver.setText("Use system GPU driver");
        systemDriver.setOnClickListener(v -> {
            try { DriverImport.useSystem(this); driverInfo.setText("System GPU driver selected"); }
            catch (Exception error) { status.setText(error.getMessage()); }
        });
        layout.addView(systemDriver);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(layout);
        setContentView(scroll);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == IMPORT_ROM && result == RESULT_OK && data != null && data.getData() != null)
            prepare(data.getData());
        else if (request == IMPORT_DRIVER && result == RESULT_OK && data != null && data.getData() != null) {
            setBusy(true);
            status.setText("Importing GPU driver…");
            Uri uri = data.getData();
            IMPORTS.execute(() -> {
                String message;
                try { message = "GPU driver ready: " + DriverImport.install(this, uri); }
                catch (Exception error) { message = "Cannot import driver: " + error.getMessage(); }
                final String resultMessage = message;
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    status.setText(resultMessage);
                    setBusy(false);
                });
            });
        }
    }

    private void setBusy(boolean busy) {
        select.setEnabled(!busy);
        play.setEnabled(!busy && new File(getFilesDir(), ROM).isFile());
        driver.setEnabled(!busy);
        systemDriver.setEnabled(!busy);
    }

    private void prepare(Uri uri) {
        setBusy(true);
        status.setText(uri == null ? "Preparing game…" : "Validating ROM…");
        IMPORTS.execute(() -> {
            try {
                if (uri != null) importRom(uri);
                copyAsset("assets");
                copyAsset("lamborghini.syms.toml");
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    setBusy(false);
                    status.setText("ROM ready. Saves are kept in this app’s storage.");
                    if (resumed) startActivity(new Intent(this, GameActivity.class));
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    status.setText("Could not start: " + error.getMessage());
                    setBusy(false);
                });
            }
        });
    }

    private void importRom(Uri uri) throws Exception {
        File temporary = File.createTempFile("rom-import-", ".tmp", getFilesDir());
        try {
            // Bound the copy before allocating or hashing: the supported ROM is 4 MiB.
            try (InputStream input = getContentResolver().openInputStream(uri);
                 FileOutputStream output = new FileOutputStream(temporary)) {
                if (input == null) throw new Exception("Cannot open selected file");
                byte[] buffer = new byte[16384];
                int total = 0, count;
                while ((count = input.read(buffer)) != -1) {
                    total += count;
                    if (total > 4194304) throw new Exception("Expected the 4 MiB USA ROM");
                    output.write(buffer, 0, count);
                }
                if (total != 4194304) throw new Exception("Expected the 4 MiB USA ROM");
            }
            byte[] bytes = Files.readAllBytes(temporary.toPath());
            // Accept the usual z64, v64 and n64 byte orders, then validate all bytes.
            if ((bytes[0] & 255) == 0x37) {
                for (int i = 0; i < bytes.length; i += 2) {
                    byte value = bytes[i]; bytes[i] = bytes[i + 1]; bytes[i + 1] = value;
                }
            } else if ((bytes[0] & 255) == 0x40) {
                for (int i = 0; i < bytes.length; i += 4) {
                    byte a = bytes[i], b = bytes[i + 1];
                    bytes[i] = bytes[i + 3]; bytes[i + 1] = bytes[i + 2];
                    bytes[i + 2] = b; bytes[i + 3] = a;
                }
            }
            StringBuilder hash = new StringBuilder();
            for (byte value : MessageDigest.getInstance("SHA-256").digest(bytes))
                hash.append(String.format(java.util.Locale.ROOT, "%02x", value & 255));
            if (!SHA256.equals(hash.toString())) throw new Exception("This is not the supported USA ROM");
            Files.write(temporary.toPath(), bytes);
            Files.move(temporary.toPath(), new File(getFilesDir(), ROM).toPath(),
                       StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally {
            temporary.delete();
        }
    }

    private void copyAsset(String path) throws Exception {
        String[] children = getAssets().list(path);
        File destination = new File(getFilesDir(), path);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) throw new Exception("Cannot create assets directory");
            for (String child : children) copyAsset(path + "/" + child);
        } else {
            try (InputStream input = getAssets().open(path)) {
                Files.copy(input, destination.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
        }
    }
}
