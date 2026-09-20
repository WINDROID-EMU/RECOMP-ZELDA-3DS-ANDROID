package org.triaevum.android;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.view.animation.AlphaAnimation;
import android.view.animation.Animation;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
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
 * Downloads or unpacks The Legend of Zelda: Ocarina of Time 3D assets automatically,
 * unpacks the native data structures, and starts the game seamlessly.
 */
public class TriAevumDownloadActivity extends Activity {

    private static final String TAG = "TriAevumDownloader";
    private static final int REQUEST_CODE_PICK_ROM = 1001;
    private static final int REQUEST_CODE_STORAGE_PERMISSION = 1002;

    public static final String GAME_DOWNLOAD_URL = "https://4br.me/ocarina3dsrom";

    private TextView mTvStatus;
    private TextView mTvPercent;
    private TextView mTvDetails;
    private TextView mTvTouchToStart;
    private ProgressBar mPbDownload;
    private Button mBtnAction;
    private View mLayoutProgressDetails;
    private View mRootLayout;

    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private final ExecutorService mExecutor = Executors.newSingleThreadExecutor();
    private volatile boolean mIsDownloading = false;
    private volatile boolean mReadyToStart = false;

    public static boolean isGameInstalled(Context context) {
        File root = context.getExternalFilesDir(null);
        if (root == null) return false;
        File romfs = new File(root, "romfs.bin");
        File code = new File(root, "code.bin");
        File exheader = new File(root, "exheader.bin");
        File launch = new File(root, "TriAevum.android.launch.json");
        return romfs.isFile() && romfs.length() > 10_000_000L && code.isFile() && exheader.isFile() && launch.isFile();
    }

    private boolean hasEmbeddedGameRom() {
        try {
            String[] files = getAssets().list("game");
            if (files != null) {
                for (String f : files) {
                    if ("romfs.bin".equals(f)) return true;
                }
            }
        } catch (Exception ignored) {}
        return false;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Fast path: if the game is already installed, launch immediately
        if (isGameInstalled(this)) {
            launchGame();
            return;
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        hideSystemBars();

        setContentView(R.layout.activity_downloader);

        mRootLayout            = findViewById(R.id.layout_downloader_root);
        mTvStatus              = findViewById(R.id.tv_download_status);
        mTvPercent             = findViewById(R.id.tv_download_percent);
        mTvDetails             = findViewById(R.id.tv_download_details);
        mTvTouchToStart        = findViewById(R.id.tv_touch_to_start);
        mPbDownload            = findViewById(R.id.pb_download);
        mBtnAction             = findViewById(R.id.btn_download_action);
        mLayoutProgressDetails = findViewById(R.id.layout_progress_details);

        mBtnAction.setOnClickListener(v -> {
            if (hasEmbeddedGameRom()) {
                startEmbeddedInstall();
            } else {
                requestStoragePermissionAndPickRom();
            }
        });

        mRootLayout.setOnClickListener(v -> {
            if (mReadyToStart) {
                launchGame();
            }
        });

        if (isGameDataInstalled()) {
            setupReadyToStart();
        } else if (hasEmbeddedGameRom()) {
            startEmbeddedInstall();
        } else {
            // Show ROM selection button
            mBtnAction.setVisibility(View.VISIBLE);
            mBtnAction.setText("Selecionar ROM 3DS");
            mTvStatus.setText("Selecione sua ROM 3DS do jogo para começar");
            mLayoutProgressDetails.setVisibility(View.GONE);
        }
    }

    private boolean isGameDataInstalled() {
        File targetDir = getExternalFilesDir(null);
        if (targetDir == null || !targetDir.exists()) return false;
        File codeBin = new File(targetDir, "code.bin");
        File romfsBin = new File(targetDir, "romfs.bin");
        File manifest = new File(targetDir, "process-manifest.json");
        return codeBin.exists() && codeBin.length() > 0 &&
               romfsBin.exists() && romfsBin.length() > 100_000_000 &&
               manifest.exists() && manifest.length() > 0;
    }

    private void setupReadyToStart() {
        File targetDir = getExternalFilesDir(null);
        if (targetDir != null) {
            unpackBundledAssets(targetDir);
            new File(targetDir, "resources").mkdirs();
            new File(targetDir, "savedata").mkdirs();
        }
        mReadyToStart = true;
        mIsDownloading = false;
        mTvStatus.setText("Jogo pronto! Iniciando...");
        mPbDownload.setProgress(100);
        mTvPercent.setText("100%");
        mLayoutProgressDetails.setVisibility(View.GONE);
        mBtnAction.setVisibility(View.GONE);
        mTvTouchToStart.setVisibility(View.VISIBLE);
        AlphaAnimation pulse = new AlphaAnimation(0.25f, 1.0f);
        pulse.setDuration(600);
        pulse.setRepeatMode(Animation.REVERSE);
        pulse.setRepeatCount(Animation.INFINITE);
        mTvTouchToStart.startAnimation(pulse);
        mMainHandler.postDelayed(this::launchGame, 400);
    }

    private void startEmbeddedInstall() {
        if (mIsDownloading) return;
        mIsDownloading = true;
        mBtnAction.setVisibility(View.GONE);
        mTvTouchToStart.setVisibility(View.GONE);
        mLayoutProgressDetails.setVisibility(View.VISIBLE);
        mTvStatus.setText("Instalando arquivos do jogo...");
        mPbDownload.setIndeterminate(false);
        mPbDownload.setProgress(0);
        mTvPercent.setText("0%");
        mTvDetails.setText("Extraindo dados embutidos no APK...");

        mExecutor.execute(() -> {
            File targetDir = getExternalFilesDir(null);
            if (targetDir == null) {
                showError("Armazenamento externo indisponível");
                return;
            }
            if (!targetDir.exists()) targetDir.mkdirs();

            try {
                String[] files = getAssets().list("game");
                if (files == null || files.length == 0) {
                    throw new IOException("Nenhum arquivo de jogo encontrado nos assets do APK");
                }

                long totalBytes = 0;
                for (String filename : files) {
                    try (AssetFileDescriptor afd = getAssets().openFd("game/" + filename)) {
                        totalBytes += afd.getLength();
                    } catch (Exception e) {
                        try (InputStream is = getAssets().open("game/" + filename)) {
                            totalBytes += is.available();
                        } catch (Exception ignored) {}
                    }
                }
                if (totalBytes <= 0) totalBytes = 481_000_000L;

                final long finalTotalBytes = totalBytes;
                long copiedBytes = 0;
                byte[] buffer = new byte[1024 * 1024]; // 1MB fast stream buffer

                for (String filename : files) {
                    File dest = new File(targetDir, filename);
                    try (InputStream in = getAssets().open("game/" + filename);
                         OutputStream out = new FileOutputStream(dest)) {
                        int read;
                        while ((read = in.read(buffer)) > 0) {
                            out.write(buffer, 0, read);
                            copiedBytes += read;
                            final long currentCopied = copiedBytes;
                            final int percent = (int) Math.min(100, (currentCopied * 100) / finalTotalBytes);
                            mMainHandler.post(() -> {
                                mPbDownload.setProgress(percent);
                                mTvPercent.setText(percent + "%");
                                double currentMB = currentCopied / (1024.0 * 1024.0);
                                double totalMB = finalTotalBytes / (1024.0 * 1024.0);
                                mTvDetails.setText(String.format(Locale.US, "Instalando %s (%.1f MB / %.1f MB)",
                                        filename, currentMB, totalMB));
                            });
                        }
                        out.flush();
                    }
                }

                new File(targetDir, "resources").mkdirs();
                new File(targetDir, "savedata").mkdirs();

                mMainHandler.post(() -> {
                    mReadyToStart = true;
                    mIsDownloading = false;
                    mTvStatus.setText("Jogo instalado com sucesso! Iniciando...");
                    mPbDownload.setProgress(100);
                    mTvPercent.setText("100%");
                    mLayoutProgressDetails.setVisibility(View.GONE);
                    mTvTouchToStart.setVisibility(View.VISIBLE);
                    AlphaAnimation pulse = new AlphaAnimation(0.25f, 1.0f);
                    pulse.setDuration(600);
                    pulse.setRepeatMode(Animation.REVERSE);
                    pulse.setRepeatCount(Animation.INFINITE);
                    mTvTouchToStart.startAnimation(pulse);

                    mMainHandler.postDelayed(this::launchGame, 400);
                });

            } catch (Exception e) {
                Log.e(TAG, "Embedded installation error", e);
                showError("Erro na instalação: " + e.getMessage());
            }
        });
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

    private synchronized void launchGame() {
        Intent intent = new Intent(this, TriAevumActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(intent);
        finish();
    }

    private void requestStoragePermissionAndPickRom() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ uses scoped storage, no need for MANAGE_EXTERNAL_STORAGE
            pickRomFile();
        } else {
            // Android 10 and below need storage permission
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED ||
                checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, REQUEST_CODE_STORAGE_PERMISSION);
            } else {
                pickRomFile();
            }
        }
    }

    private void pickRomFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        String[] mimeTypes = {"application/octet-stream", "application/x-3ds", "application/x-cci"};
        intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
        intent.putExtra(Intent.EXTRA_TITLE, "Selecione a ROM 3DS (.3ds ou .cci)");
        startActivityForResult(intent, REQUEST_CODE_PICK_ROM);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_CODE_STORAGE_PERMISSION) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                pickRomFile();
            } else {
                showError("Permissão de armazenamento necessária para selecionar a ROM");
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_CODE_PICK_ROM && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                // Persist permission to access the file
                try {
                    getContentResolver().takePersistableUriPermission(uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                    processSelectedRom(uri);
                } catch (Exception e) {
                    Log.e(TAG, "Failed to take persistable URI permission", e);
                    showError("Erro ao acessar arquivo selecionado");
                }
            }
        }
    }

    private void processSelectedRom(Uri uri) {
        if (mIsDownloading) return;
        mIsDownloading = true;
        mBtnAction.setVisibility(View.GONE);
        mTvTouchToStart.setVisibility(View.GONE);
        mLayoutProgressDetails.setVisibility(View.VISIBLE);
        mTvStatus.setText("Processando ROM selecionada...");
        mPbDownload.setIndeterminate(false);
        mPbDownload.setProgress(0);
        mTvPercent.setText("0%");
        mTvDetails.setText("Convertendo ROM para formato do app...");

        mExecutor.execute(() -> {
            File targetDir = getExternalFilesDir(null);
            if (targetDir == null) {
                showError("Armazenamento externo indisponível");
                return;
            }
            if (!targetDir.exists()) targetDir.mkdirs();

            try {
                // Copy ROM to temp file for processing
                File tempRomFile = new File(targetDir, "temp_rom.3ds");
                copyUriToFile(uri, tempRomFile);

                // Extract ROM using CtrRomExtractor
                CtrRomExtractor.extractRom(this, tempRomFile, targetDir, (stage, percent) -> {
                    mMainHandler.post(() -> {
                        mTvStatus.setText(stage);
                        mPbDownload.setProgress(percent);
                        mTvPercent.setText(percent + "%");
                        mTvDetails.setText("Extraindo e convertendo ROM...");
                    });
                });

                // Clean up temp file
                tempRomFile.delete();

                // Unpack bundled assets
                unpackBundledAssets(targetDir);

                // Create required directories
                new File(targetDir, "resources").mkdirs();
                new File(targetDir, "savedata").mkdirs();

                mMainHandler.post(() -> {
                    mReadyToStart = true;
                    mIsDownloading = false;
                    mTvStatus.setText("ROM convertida com sucesso! Iniciando...");
                    mPbDownload.setProgress(100);
                    mTvPercent.setText("100%");
                    mLayoutProgressDetails.setVisibility(View.GONE);
                    mTvTouchToStart.setVisibility(View.VISIBLE);
                    AlphaAnimation pulse = new AlphaAnimation(0.25f, 1.0f);
                    pulse.setDuration(600);
                    pulse.setRepeatMode(Animation.REVERSE);
                    pulse.setRepeatCount(Animation.INFINITE);
                    mTvTouchToStart.startAnimation(pulse);
                    mMainHandler.postDelayed(this::launchGame, 400);
                });

            } catch (Exception e) {
                Log.e(TAG, "ROM processing error", e);
                showError("Erro ao processar ROM: " + e.getMessage());
            }
        });
    }

    private void copyUriToFile(Uri uri, File destination) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(destination)) {
            byte[] buffer = new byte[1024 * 1024]; // 1MB buffer
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            out.flush();
        }
    }

    private void startDownload() {
        if (mIsDownloading) return;
        mIsDownloading = true;
        mBtnAction.setVisibility(View.GONE);
        mTvTouchToStart.setVisibility(View.GONE);
        mLayoutProgressDetails.setVisibility(View.VISIBLE);
        mTvStatus.setText("Conectando ao servidor...");
        mPbDownload.setIndeterminate(true);

        mExecutor.execute(() -> {
            File targetDir = getExternalFilesDir(null);
            if (targetDir == null) {
                showError("Armazenamento externo indisponível");
                return;
            }
            if (!targetDir.exists()) targetDir.mkdirs();

            if (isGameDataInstalled()) {
                mMainHandler.post(this::setupReadyToStart);
                return;
            }

            File tempDownloadFile = new File(targetDir, "zelda_oot3d_download.tmp");

            try {
                // 1. Download file
                downloadFile(GAME_DOWNLOAD_URL, tempDownloadFile);

                // 2. Unpack bundled assets (process-manifest.json, launch configuration, etc.)
                unpackBundledAssets(targetDir);

                // 3. Extract downloaded content (ZIP or direct 3DS / CCI ROM)
                mMainHandler.post(() -> {
                    mTvStatus.setText("Extraindo arquivos do jogo...");
                    mPbDownload.setIndeterminate(false);
                    mPbDownload.setProgress(0);
                    mTvPercent.setText("0%");
                    mTvDetails.setText("Processando ROM...");
                });

                if (isZipFile(tempDownloadFile)) {
                    extractZip(tempDownloadFile, targetDir);
                } else {
                    // Extract 3DS / CCI container
                    CtrRomExtractor.extractRom(TriAevumDownloadActivity.this, tempDownloadFile, targetDir, (stage, percent) -> {
                        mMainHandler.post(() -> {
                            mTvStatus.setText(stage);
                            mPbDownload.setProgress(percent);
                            mTvPercent.setText(percent + "%");
                            mTvDetails.setText("Extraindo partição NCCH...");
                        });
                    });
                }

                // Delete temporary download file
                tempDownloadFile.delete();

                // Make sure required directories exist
                new File(targetDir, "resources").mkdirs();
                new File(targetDir, "savedata").mkdirs();

                // 4. Success! Show "TOQUE NA TELA PARA INICIAR"
                mMainHandler.post(() -> {
                    mReadyToStart = true;
                    mIsDownloading = false;
                    mTvStatus.setText("Download e extração concluídos com sucesso!");
                    mPbDownload.setProgress(100);
                    mTvPercent.setText("100%");
                    mLayoutProgressDetails.setVisibility(View.GONE);

                    // Glowing pulse animation on "TOQUE NA TELA PARA INICIAR"
                    mTvTouchToStart.setVisibility(View.VISIBLE);
                    AlphaAnimation pulse = new AlphaAnimation(0.25f, 1.0f);
                    pulse.setDuration(600);
                    pulse.setRepeatMode(Animation.REVERSE);
                    pulse.setRepeatCount(Animation.INFINITE);
                    mTvTouchToStart.startAnimation(pulse);
                });

            } catch (Exception e) {
                Log.e(TAG, "Download/Extraction error", e);
                showError("Erro: " + e.getMessage());
            }
        });
    }

    private void showError(String msg) {
        mIsDownloading = false;
        mMainHandler.post(() -> {
            mTvStatus.setText(msg);
            mPbDownload.setIndeterminate(false);
            mTvDetails.setText("Toque em 'Tentar Novamente' para reiniciar.");
            mBtnAction.setVisibility(View.VISIBLE);
            mBtnAction.setText("Tentar Novamente");
        });
    }

    private void downloadFile(String initialUrl, File destination) throws Exception {
        String currentUrl = initialUrl;
        String cookies = "";
        HttpURLConnection conn = null;

        // Follow up to 8 redirects (including shorteners like 4br.me and Google Drive)
        for (int redirectCount = 0; redirectCount < 8; redirectCount++) {
            conn = openConnection(currentUrl, cookies);
            int code = conn.getResponseCode();

            cookies = extractCookies(conn, cookies);

            if (code == HttpURLConnection.HTTP_MOVED_TEMP ||
                code == HttpURLConnection.HTTP_MOVED_PERM ||
                code == 307 || code == 308 || code == 303) {
                String loc = conn.getHeaderField("Location");
                conn.disconnect();
                if (loc != null && !loc.isEmpty()) {
                    currentUrl = loc;
                    continue;
                }
            }

            // Check if Google Drive returned virus warning page
            String contentType = conn.getContentType();
            if (contentType != null && contentType.contains("text/html")) {
                String html = readStreamToString(conn.getInputStream());
                conn.disconnect();

                String confirmUrl = parseGoogleDriveConfirmUrl(html, currentUrl);
                if (confirmUrl != null && !confirmUrl.equals(currentUrl)) {
                    currentUrl = confirmUrl;
                    continue;
                }
            }

            break;
        }

        if (conn == null) throw new IllegalStateException("Falha ao abrir conexão");

        int finalCode = conn.getResponseCode();
        if (finalCode != HttpURLConnection.HTTP_OK && finalCode != 206) {
            throw new IllegalStateException("Servidor retornou HTTP " + finalCode);
        }

        long contentLength = conn.getContentLengthLong();
        final long totalBytes = contentLength > 0 ? contentLength : -1;

        mMainHandler.post(() -> {
            mTvStatus.setText("Baixando The Legend of Zelda: Ocarina of Time 3D...");
            mPbDownload.setIndeterminate(totalBytes <= 0);
            if (totalBytes > 0) mPbDownload.setMax(100);
        });

        long downloadedBytes = 0;
        long startTime = SystemClock.elapsedRealtime();
        long lastUiUpdateTime = 0;

        try (InputStream in = new BufferedInputStream(conn.getInputStream(), 131072);
             OutputStream out = new FileOutputStream(destination)) {
            byte[] buffer = new byte[131072]; // 128KB chunk
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
                downloadedBytes += read;

                long now = SystemClock.elapsedRealtime();
                if (now - lastUiUpdateTime > 120) {
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

    private static String parseGoogleDriveConfirmUrl(String html, String baseFallbackUrl) {
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
        return baseFallbackUrl + "&confirm=t";
    }

    private static String extractCookies(HttpURLConnection conn, String existingCookies) {
        StringBuilder sb = new StringBuilder(existingCookies != null ? existingCookies : "");
        Map<String, List<String>> headers = conn.getHeaderFields();
        if (headers != null) {
            List<String> setCookies = headers.get("Set-Cookie");
            if (setCookies != null) {
                for (String cookie : setCookies) {
                    String cookieVal = cookie.split(";")[0];
                    if (sb.length() > 0) sb.append("; ");
                    sb.append(cookieVal);
                }
            }
        }
        return sb.toString();
    }

    private static HttpURLConnection openConnection(String urlStr, String cookies) throws Exception {
        URL url = new URL(urlStr);
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();
        conn.setConnectTimeout(25000);
        conn.setReadTimeout(35000);
        conn.setInstanceFollowRedirects(false); // Handle redirects manually to retain cookies
        conn.setRequestProperty("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
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
    }

    private void unpackBundledAssets(File targetDir) {
        try {
            String[] files = getAssets().list("game");
            if (files != null) {
                for (String filename : files) {
                    File dest = new File(targetDir, filename);
                    if (!dest.exists()) {
                        try (InputStream in = getAssets().open("game/" + filename);
                             OutputStream out = new FileOutputStream(dest)) {
                            byte[] buf = new byte[8192];
                            int len;
                            while ((len = in.read(buf)) > 0) out.write(buf, 0, len);
                        }
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not unpack game assets", e);
        }
    }
}
