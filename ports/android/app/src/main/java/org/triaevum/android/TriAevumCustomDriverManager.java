package org.triaevum.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.util.Log;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Manages custom Vulkan GPU drivers (such as Mesa Turnip) on Android.
 * Drivers are extracted into internal app storage (/data/user/0/.../files/custom_drivers/)
 * to comply with Android SELinux execution permissions.
 */
public final class TriAevumCustomDriverManager {

    private static final String TAG = "CustomDriverManager";
    private static final String PREF_SELECTED_DRIVER = "custom_driver_selected_id";

    public static final class CustomDriverInfo {
        public final String id;
        public final String name;
        public final String description;
        public final String author;
        public final String driverVersion;
        public final String vendor;
        public final String libraryName;
        public final File driverDir;

        public CustomDriverInfo(String id, String name, String description, String author,
                                String driverVersion, String vendor, String libraryName, File driverDir) {
            this.id = id;
            this.name = name;
            this.description = description;
            this.author = author;
            this.driverVersion = driverVersion;
            this.vendor = vendor;
            this.libraryName = libraryName;
            this.driverDir = driverDir;
        }

        public File getLibraryFile() {
            return new File(driverDir, libraryName);
        }

        public boolean isValid() {
            File lib = getLibraryFile();
            return lib.isFile() && lib.canRead();
        }

        @Override
        public String toString() {
            return name + (driverVersion != null && !driverVersion.isEmpty() ? " (" + driverVersion + ")" : "");
        }
    }

    private final Context mContext;
    private final File mDriversRootDir;
    private final SharedPreferences mPrefs;

    public TriAevumCustomDriverManager(Context context) {
        mContext = context.getApplicationContext();
        mDriversRootDir = new File(mContext.getFilesDir(), "custom_drivers");
        if (!mDriversRootDir.exists()) {
            mDriversRootDir.mkdirs();
        }
        mPrefs = mContext.getSharedPreferences("org.triaevum.android_preferences", Context.MODE_PRIVATE);
    }

    public File getDriversRootDir() {
        return mDriversRootDir;
    }

    /**
     * Lists all custom Turnip drivers installed in internal storage.
     */
    public List<CustomDriverInfo> getInstalledDrivers() {
        List<CustomDriverInfo> list = new ArrayList<>();
        File[] subdirs = mDriversRootDir.listFiles(File::isDirectory);
        if (subdirs == null) return list;

        for (File dir : subdirs) {
            CustomDriverInfo info = loadDriverFromDir(dir);
            if (info != null && info.isValid()) {
                list.add(info);
            }
        }
        return list;
    }

    private CustomDriverInfo loadDriverFromDir(File dir) {
        String id = dir.getName();
        String name = id;
        String desc = "Custom GPU Driver";
        String author = "Mesa / Community";
        String version = "";
        String vendor = "Mesa";
        String libraryName = "vulkan.adreno.so";

        File metaFile = new File(dir, "meta.json");
        if (metaFile.isFile()) {
            try {
                String jsonStr = new String(Files.readAllBytes(metaFile.toPath()), StandardCharsets.UTF_8);
                JSONObject json = new JSONObject(jsonStr);
                if (json.has("name")) name = json.optString("name", name);
                if (json.has("description")) desc = json.optString("description", desc);
                if (json.has("author")) author = json.optString("author", author);
                if (json.has("driverVersion")) version = json.optString("driverVersion", version);
                if (json.has("vendor")) vendor = json.optString("vendor", vendor);
                if (json.has("libraryName")) libraryName = json.optString("libraryName", libraryName);
            } catch (Exception e) {
                Log.w(TAG, "Failed to parse meta.json in " + dir.getName(), e);
            }
        }

        // If libraryName does not exist, look for any .so in dir
        File candidateLib = new File(dir, libraryName);
        if (!candidateLib.isFile()) {
            File[] soFiles = dir.listFiles((d, f) -> f.endsWith(".so"));
            if (soFiles != null && soFiles.length > 0) {
                libraryName = soFiles[0].getName();
            }
        }

        return new CustomDriverInfo(id, name, desc, author, version, vendor, libraryName, dir);
    }

    /**
     * Installs a Turnip driver package from a user-selected ZIP URI.
     */
    public CustomDriverInfo installDriverFromZip(Uri zipUri, String originalFileName) throws Exception {
        String baseName = "driver_" + System.currentTimeMillis();
        if (originalFileName != null && originalFileName.toLowerCase().endsWith(".zip")) {
            baseName = originalFileName.substring(0, originalFileName.length() - 4)
                    .replaceAll("[^a-zA-Z0-9._-]", "_");
        }

        File targetDir = new File(mDriversRootDir, baseName);
        if (targetDir.exists()) {
            deleteRecursive(targetDir);
        }
        targetDir.mkdirs();

        try (InputStream rawIn = mContext.getContentResolver().openInputStream(zipUri);
             BufferedInputStream bis = new BufferedInputStream(rawIn);
             ZipInputStream zis = new ZipInputStream(bis)) {

            ZipEntry entry;
            byte[] buffer = new byte[8192];
            while ((entry = zis.getNextEntry()) != null) {
                String entryName = entry.getName();
                // Avoid Zip Slip directory traversal vulnerability
                File destFile = new File(targetDir, entryName);
                String destCanonical = destFile.getCanonicalPath();
                if (!destCanonical.startsWith(targetDir.getCanonicalPath())) {
                    throw new SecurityException("Zip Slip exploit attempt in " + entryName);
                }

                if (entry.isDirectory()) {
                    destFile.mkdirs();
                } else {
                    File parent = destFile.getParentFile();
                    if (parent != null && !parent.exists()) {
                        parent.mkdirs();
                    }
                    try (FileOutputStream fos = new FileOutputStream(destFile)) {
                        int len;
                        while ((len = zis.read(buffer)) > 0) {
                            fos.write(buffer, 0, len);
                        }
                    }
                    if (destFile.getName().endsWith(".so")) {
                        destFile.setReadable(true, false);
                        destFile.setExecutable(true, false);
                    }
                }
                zis.closeEntry();
            }
        }

        // If files were extracted inside a single root directory inside zip, flatten if needed
        flattenSubdirectoryIfNeeded(targetDir);

        // Ensure all .so have execution permissions
        makeExecutableRecursive(targetDir);

        CustomDriverInfo installed = loadDriverFromDir(targetDir);
        if (installed == null || !installed.isValid()) {
            deleteRecursive(targetDir);
            throw new IllegalArgumentException("O arquivo ZIP não contém um driver Vulkan (.so) válido.");
        }

        // Auto-select newly installed driver
        setSelectedDriverId(installed.id);
        Log.i(TAG, "Custom driver successfully installed: " + installed.name + " (" + installed.libraryName + ")");
        return installed;
    }

    private void flattenSubdirectoryIfNeeded(File dir) {
        File[] files = dir.listFiles();
        if (files != null && files.length == 1 && files[0].isDirectory()) {
            File sub = files[0];
            File[] subFiles = sub.listFiles();
            if (subFiles != null) {
                for (File sf : subFiles) {
                    sf.renameTo(new File(dir, sf.getName()));
                }
            }
            sub.delete();
        }
    }

    private void makeExecutableRecursive(File dir) {
        File[] files = dir.listFiles();
        if (files == null) return;
        for (File f : files) {
            if (f.isDirectory()) {
                makeExecutableRecursive(f);
            } else if (f.getName().endsWith(".so")) {
                f.setReadable(true, false);
                f.setExecutable(true, false);
            }
        }
    }

    public boolean deleteDriver(String driverId) {
        if (driverId == null || driverId.isEmpty()) return false;
        File dir = new File(mDriversRootDir, driverId);
        if (dir.exists()) {
            deleteRecursive(dir);
            if (driverId.equals(getSelectedDriverId())) {
                setSelectedDriverId("");
            }
            return true;
        }
        return false;
    }

    private void deleteRecursive(File fileOrDir) {
        if (fileOrDir.isDirectory()) {
            File[] children = fileOrDir.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        fileOrDir.delete();
    }

    public String getSelectedDriverId() {
        return mPrefs.getString(PREF_SELECTED_DRIVER, "");
    }

    public void setSelectedDriverId(String driverId) {
        mPrefs.edit().putString(PREF_SELECTED_DRIVER, driverId != null ? driverId : "").apply();
    }

    public CustomDriverInfo getSelectedDriver() {
        String id = getSelectedDriverId();
        if (id.isEmpty()) return null;
        File dir = new File(mDriversRootDir, id);
        if (!dir.isDirectory()) return null;
        CustomDriverInfo info = loadDriverFromDir(dir);
        return (info != null && info.isValid()) ? info : null;
    }
}
