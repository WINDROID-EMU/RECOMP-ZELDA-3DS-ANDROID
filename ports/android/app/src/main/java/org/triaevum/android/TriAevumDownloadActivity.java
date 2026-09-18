package org.triaevum.android;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * First-run launcher activity that verifies game assets and downloads/extracts
 * them seamlessly before transitioning to TriAevumActivity.
 */
public class TriAevumDownloadActivity extends Activity {

    private static final String TAG = "TriAevumDownloader";

    /**
     * Set this to the Google Drive or direct download link provided by the user.
     * Leave empty until the link is configured.
     */
    public static final String GAME_DOWNLOAD_URL = "";

    private TextView mTvStatus;
    private TextView mTvPercent;
    private TextView mTvDetails;
    private ProgressBar mPbDownload;
    private Button mBtnAction;

    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private final ExecutorService mExecutor = Executors.newSingleThreadExecutor();
    private volatile boolean mIsDownloading = false;

    /**
     * Checks if the required game data files already exist in app storage.
     */
    public static boolean isGameInstalled(Context context) {
        File root = context.getExternalFilesDir(null);
        if (root == null) return false;
        File romfs = new File(root, "romfs.bin");
        File launch = new File(root, "TriAevum.android.launch.json");
        return romfs.isFile() && romfs.length() > 10_000_000L && launch.isFile();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Fast path: if the game is already installed, launch immediately
        if (isGameInstalled(this)) {
            launchGame();
            return;
        }

        // Keep screen on during download
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        hideSystemBars();

        setContentView(R.layout.activity_downloader);

        mTvStatus    = findViewById(R.id.tv_download_status);
        mTvPercent   = findViewById(R.id.tv_download_percent);
        mTvDetails   = findViewById(R.id.tv_download_details);
        mPbDownload  = findViewById(R.id.pb_download);
        mBtnAction   = findViewById(R.id.btn_download_action);

        mBtnAction.setOnClickListener(v -> startDownload());

        if (GAME_DOWNLOAD_URL == null || GAME_DOWNLOAD_URL.trim().isEmpty()) {
            mTvStatus.setText("Aguardando link de download do jogo...");
            mTvPercent.setText("0%");
            mTvDetails.setText("O link do Google Drive será adicionado em breve.");
            mBtnAction.setVisibility(View.GONE);
        } else {
            startDownload();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
    }

    private void hideSystemBars() {
        try {
            WindowInsetsControllerCompat controller =
                WindowCompat.getInsetsController(getWindow(), getWindow().getDecorView());
            if (controller != null) {
                controller.hide(WindowInsetsCompat.Type.systemBars());
                controller.setSystemBarsBehavior(
                    WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } catch (Exception ignored) {}
    }

    private void launchGame() {
        Intent intent = new Intent(this, TriAevumActivity.class);
        startActivity(intent);
        finish();
    }

    private void startDownload() {
        if (mIsDownloading) return;
        mIsDownloading = true;
        mBtnAction.setVisibility(View.GONE);
        mTvStatus.setText("Conectando ao servidor...");
        mPbDownload.setIndeterminate(true);

        mExecutor.execute(() -> {
            try {
                File targetDir = getExternalFilesDir(null);
                if (targetDir == null) throw new IllegalStateException("Armazenamento indisponível");
                if (!targetDir.exists()) targetDir.mkdirs();

                File tempDownloadFile = new File(targetDir, "game_download_temp.bin");
                downloadWithGoogleDriveSupport(GAME_DOWNLOAD_URL, tempDownloadFile);

                // Check if downloaded file is a ZIP archive
                mMainHandler.post(() -> {
                    mTvStatus.setText("Download concluído! Extraindo arquivos do jogo...");
                    mPbDownload.setIndeterminate(true);
                    mTvDetails.setText("Processando descompactação...");
                });

                if (isZipFile(tempDownloadFile)) {
                    extractZip(tempDownloadFile, targetDir);
                } else {
                    // Raw romfs or container
                    File romfsDest = new File(targetDir, "romfs.bin");
                    if (!tempDownloadFile.renameTo(romfsDest)) {
                        copyFile(tempDownloadFile, romfsDest);
                        tempDownloadFile.delete();
                    }
                }

                // Verify launch profile template if needed
                ensureDefaultConfigs(targetDir);

                mMainHandler.post(() -> {
                    mTvStatus.setText("Concluído com sucesso! Iniciando...");
                    mPbDownload.setIndeterminate(false);
                    mPbDownload.setProgress(100);
                    mTvPercent.setText("100%");
                    mMainHandler.postDelayed(this::launchGame, 1000);
                });

            } catch (Exception e) {
                Log.e(TAG, "Download error", e);
                mIsDownloading = false;
                mMainHandler.post(() -> {
                    mTvStatus.setText("Falha no download: " + e.getMessage());
                    mPbDownload.setIndeterminate(false);
                    mTvDetails.setText("Verifique sua conexão e tente novamente.");
                    mBtnAction.setVisibility(View.VISIBLE);
                    mBtnAction.setText("Tentar Novamente");
                });
            }
        });
    }

    /**
     * Downloads from URL, handling Google Drive's large file virus scan interstitial confirm token.
     */
    private void downloadWithGoogleDriveSupport(String rawUrl, File destination) throws Exception {
        String urlString = resolveGoogleDriveDirectUrl(rawUrl);
        String cookies = "";

        HttpURLConnection conn = openConnection(urlString, cookies);
        int responseCode = conn.getResponseCode();

        // Handle redirect loops or confirmation prompt
        if (responseCode == HttpURLConnection.HTTP_MOVED_TEMP ||
            responseCode == HttpURLConnection.HTTP_MOVED_PERM ||
            responseCode == 307 || responseCode == 308) {
            String newUrl = conn.getHeaderField("Location");
            cookies = extractCookies(conn, cookies);
            conn.disconnect();
            conn = openConnection(newUrl, cookies);
            responseCode = conn.getResponseCode();
        }

        String contentType = conn.getContentType();
        // If Google Drive returns HTML warning page for large files (>100MB)
        if (contentType != null && contentType.contains("text/html")) {
            cookies = extractCookies(conn, cookies);
            String html = readStreamToString(conn.getInputStream());
            conn.disconnect();

            String confirmUrl = parseGoogleDriveConfirmUrl(html, urlString);
            if (confirmUrl == null) {
                throw new IllegalStateException("Não foi possível confirmar o download do Google Drive");
            }
            conn = openConnection(confirmUrl, cookies);
            responseCode = conn.getResponseCode();
        }

        if (responseCode != HttpURLConnection.HTTP_OK && responseCode != 206) {
            throw new IllegalStateException("Servidor retornou HTTP " + responseCode);
        }

        long contentLength = conn.getContentLengthLong();
        final long totalBytes = contentLength > 0 ? contentLength : -1;

        mMainHandler.post(() -> {
            mTvStatus.setText("Baixando dados do jogo...");
            mPbDownload.setIndeterminate(totalBytes <= 0);
            if (totalBytes > 0) mPbDownload.setMax(100);
        });

        long downloadedBytes = 0;
        long startTime = SystemClock.elapsedRealtime();
        long lastUiUpdateTime = 0;

        try (InputStream in = new BufferedInputStream(conn.getInputStream(), 65536);
             OutputStream out = new FileOutputStream(destination)) {
            byte[] buffer = new byte[65536];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
                downloadedBytes += read;

                long now = SystemClock.elapsedRealtime();
                if (now - lastUiUpdateTime > 150) {
                    lastUiUpdateTime = now;
                    final long currentRead = downloadedBytes;
                    final double elapsedSec = Math.max(0.01, (now - startTime) / 1000.0);
                    final double speedMBs = (currentRead / (1024.0 * 1024.0)) / elapsedSec;

                    mMainHandler.post(() -> {
                        if (totalBytes > 0) {
                            int pct = (int) (currentRead * 100 / totalBytes);
                            mPbDownload.setProgress(pct);
                            mTvPercent.setText(pct + "%");
                            double currentMB = currentRead / (1024.0 * 1024.0);
                            double totalMB = totalBytes / (1024.0 * 1024.0);
                            mTvDetails.setText(String.format(Locale.US, "%.1f MB / %.1f MB (%.2f MB/s)", currentMB, totalMB, speedMBs));
                        } else {
                            double currentMB = currentRead / (1024.0 * 1024.0);
                            mTvDetails.setText(String.format(Locale.US, "%.1f MB baixados (%.2f MB/s)", currentMB, speedMBs));
                        }
                    });
                }
            }
            out.flush();
        } finally {
            conn.disconnect();
        }
    }

    private static String resolveGoogleDriveDirectUrl(String url) {
        if (url == null) return "";
        // Match file ID from formats:
        // https://drive.google.com/file/d/FILE_ID/view
        // https://drive.google.com/open?id=FILE_ID
        // https://drive.google.com/uc?id=FILE_ID
        Pattern p = Pattern.compile("/d/([a-zA-Z0-9_-]+)|id=([a-zA-Z0-9_-]+)");
        Matcher m = p.matcher(url);
        if (m.find()) {
            String fileId = m.group(1) != null ? m.group(1) : m.group(2);
            return "https://drive.google.com/uc?export=download&id=" + fileId;
        }
        return url;
    }

    private static String parseGoogleDriveConfirmUrl(String html, String baseFallbackUrl) {
        // Look for confirm token in href, e.g. href="/uc?export=download&amp;confirm=t&amp;id=..."
        Pattern p = Pattern.compile("href=\"([^\"]*confirm=[^\"]*)\"|name=\"confirm\"\\s+value=\"([^\"]+)\"");
        Matcher m = p.matcher(html);
        if (m.find()) {
            if (m.group(1) != null) {
                String link = m.group(1).replace("&amp;", "&");
                if (link.startsWith("/")) link = "https://drive.google.com" + link;
                return link;
            } else if (m.group(2) != null) {
                String token = m.group(2);
                return baseFallbackUrl + "&confirm=" + token;
            }
        }
        // Fallback common confirm parameter
        return baseFallbackUrl + "&confirm=t";
    }

    private static String extractCookies(HttpURLConnection conn, String existingCookies) {
        StringBuilder sb = new StringBuilder(existingCookies);
        Map<String, List<String>> headers = conn.getHeaderFields();
        List<String> setCookies = headers.get("Set-Cookie");
        if (setCookies != null) {
            for (String cookie : setCookies) {
                String cookieVal = cookie.split(";")[0];
                if (sb.length() > 0) sb.append("; ");
                sb.append(cookieVal);
            }
        }
        return sb.toString();
    }

    private static HttpURLConnection openConnection(String urlStr, String cookies) throws Exception {
        URL url = new URL(urlStr);
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();
        conn.setConnectTimeout(25000);
        conn.setReadTimeout(30000);
        conn.setInstanceFollowRedirects(true);
        conn.setRequestProperty("User-Agent", "Mozilla/5.0 (Android; Mobile; rv:115.0) Gecko/115.0 Firefox/115.0");
        if (cookies != null && !cookies.isEmpty()) {
            conn.setRequestProperty("Cookie", cookies);
        }
        return conn;
    }

    private static String readStreamToString(InputStream is) throws Exception {
        StringBuilder sb = new StringBuilder();
        byte[] buf = new byte[4096];
        int r;
        while ((r = is.read(buf)) != -1) {
            sb.append(new String(buf, 0, r, StandardCharsets.UTF_8));
        }
        return sb.toString();
    }

    private static boolean isZipFile(File file) {
        try (InputStream is = new FileInputStream(file)) {
            byte[] header = new byte[4];
            int read = is.read(header);
            return read == 4 && header[0] == 0x50 && header[1] == 0x4B && header[2] == 0x03 && header[3] == 0x04;
        } catch (Exception e) {
            return false;
        }
    }

    private static void extractZip(File zipFile, File destDir) throws Exception {
        try (ZipInputStream zis = new ZipInputStream(new BufferedInputStream(new FileInputStream(zipFile)))) {
            ZipEntry entry;
            byte[] buffer = new byte[65536];
            while ((entry = zis.getNextEntry()) != null) {
                File target = new File(destDir, entry.getName());
                if (entry.isDirectory()) {
                    target.mkdirs();
                } else {
                    target.getParentFile().mkdirs();
                    try (OutputStream os = new FileOutputStream(target)) {
                        int len;
                        while ((len = zis.read(buffer)) > 0) {
                            os.write(buffer, 0, len);
                        }
                    }
                }
                zis.closeEntry();
            }
        }
        zipFile.delete();
    }

    private static void copyFile(File src, File dst) throws Exception {
        try (InputStream in = new FileInputStream(src); OutputStream out = new FileOutputStream(dst)) {
            byte[] buf = new byte[65536];
            int len;
            while ((len = in.read(buf)) > 0) out.write(buf, 0, len);
        }
    }

    private static void ensureDefaultConfigs(File root) {
        File launch = new File(root, "TriAevum.android.launch.json");
        if (!launch.isFile()) {
            String defaultLaunch = "{\n" +
                "  \"format\": \"oot3d_native_game_launch_profile_v1\",\n" +
                "  \"arguments\": [\n" +
                "    \"--a32-process-manifest\",\n" +
                "    \"${profile_dir}/process-manifest.json\",\n" +
                "    \"--resource-root\",\n" +
                "    \"${profile_dir}/resources\",\n" +
                "    \"--renderer\",\n" +
                "    \"nri\",\n" +
                "    \"--ui-profile\",\n" +
                "    \"topscreen\",\n" +
                "    \"--topscreen-config\",\n" +
                "    \"${profile_dir}/topscreen_ui.json\",\n" +
                "    \"--save-data\",\n" +
                "    \"${profile_dir}/savedata\",\n" +
                "    \"--gameplay-timing\",\n" +
                "    \"native30_no_interpolation\",\n" +
                "    \"--presentation-rate\",\n" +
                "    \"30\",\n" +
                "    \"--width\",\n" +
                "    \"1280\",\n" +
                "    \"--height\",\n" +
                "    \"720\"\n" +
                "  ]\n" +
                "}";
            try (FileOutputStream fos = new FileOutputStream(launch)) {
                fos.write(defaultLaunch.getBytes(StandardCharsets.UTF_8));
            } catch (Exception ignored) {}
        }
    }
}
