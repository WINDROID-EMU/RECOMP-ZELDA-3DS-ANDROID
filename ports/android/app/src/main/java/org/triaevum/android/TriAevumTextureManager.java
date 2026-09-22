package org.triaevum.android;

import android.content.Context;
import android.net.Uri;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.util.Log;

import java.io.File;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Gerenciador de texturas personalizadas do TriAevum.
 * Converte Uris do Storage Access Framework para caminhos de sistema de arquivos,
 * escaneia e valida diretórios de texturas, e provê o caminho padrão do app.
 */
public final class TriAevumTextureManager {
    private static final String TAG = "TriAevumTextureManager";
    public static final String DEFAULT_TEXTURES_DIR_NAME = "textures";

    private static final ExecutorService sExecutor = Executors.newSingleThreadExecutor();
    private static Handler sMainHandler;

    private static synchronized Handler getMainHandler() {
        if (sMainHandler == null) {
            sMainHandler = new Handler(Looper.getMainLooper());
        }
        return sMainHandler;
    }

    public interface TextureCountCallback {
        void onCountReady(int count, String summary);
    }

    private TriAevumTextureManager() {}

    /**
     * Retorna o diretório de texturas padrão do aplicativo.
     * Ex: /sdcard/Android/data/org.triaevum.android/files/textures
     */
    public static File getDefaultTexturesDir(Context context) {
        File root = context.getExternalFilesDir(null);
        File texturesDir = new File(root, DEFAULT_TEXTURES_DIR_NAME);
        if (!texturesDir.exists()) {
            texturesDir.mkdirs();
        }
        return texturesDir;
    }

    /**
     * Converte uma TreeUri (ACTION_OPEN_DOCUMENT_TREE) para o caminho absoluto no sistema de arquivos.
     */
    public static String resolvePathFromTreeUri(Context context, Uri treeUri) {
        if (treeUri == null) {
            return null;
        }

        try {
            if (DocumentsContract.isTreeUri(treeUri)) {
                String docId = DocumentsContract.getTreeDocumentId(treeUri);
                if (docId != null) {
                    if (docId.startsWith("primary:")) {
                        String relPath = docId.substring("primary:".length());
                        File file = new File(Environment.getExternalStorageDirectory(), relPath);
                        return file.getAbsolutePath();
                    }

                    String[] parts = docId.split(":", 2);
                    if (parts.length > 0) {
                        File secondary = new File("/storage/" + parts[0] + (parts.length > 1 ? "/" + parts[1] : ""));
                        if (secondary.exists()) {
                            return secondary.getAbsolutePath();
                        }
                    }
                }
            }

            // Fallback via path
            String path = treeUri.getPath();
            if (path != null) {
                int idx = path.indexOf("primary:");
                if (idx != -1) {
                    String relPath = path.substring(idx + "primary:".length());
                    return new File(Environment.getExternalStorageDirectory(), relPath).getAbsolutePath();
                }
                if (path.startsWith("/tree/")) {
                    String raw = path.substring("/tree/".length());
                    int colon = raw.indexOf(':');
                    if (colon != -1) {
                        String prefix = raw.substring(0, colon);
                        String sub = raw.substring(colon + 1);
                        if ("primary".equalsIgnoreCase(prefix)) {
                            return new File(Environment.getExternalStorageDirectory(), sub).getAbsolutePath();
                        } else {
                            File sec = new File("/storage/" + prefix + "/" + sub);
                            if (sec.exists()) {
                                return sec.getAbsolutePath();
                            }
                        }
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Erro ao resolver caminho da URI: " + treeUri, e);
        }

        return null;
    }

    /**
     * Conta recursivamente arquivos de imagem (.png) dentro do diretório.
     */
    public static int countTexturesRecursively(File dir, int maxCount) {
        if (dir == null || !dir.exists() || !dir.isDirectory()) {
            return 0;
        }

        int count = 0;
        File[] files = dir.listFiles();
        if (files == null) {
            return 0;
        }

        for (File f : files) {
            if (f.isDirectory()) {
                count += countTexturesRecursively(f, maxCount - count);
            } else if (f.isFile()) {
                String name = f.getName().toLowerCase(Locale.US);
                if (name.endsWith(".png")) {
                    count++;
                }
            }
            if (count >= maxCount) {
                break;
            }
        }

        return count;
    }

    /**
     * Conta as texturas em segundo plano e retorna o resultado na thread principal.
     */
    public static void countTexturesAsync(String path, TextureCountCallback callback) {
        if (callback == null) return;

        if (path == null || path.trim().isEmpty()) {
            callback.onCountReady(0, "Nenhum diretório selecionado");
            return;
        }

        File dir = new File(path);
        if (!dir.exists() || !dir.isDirectory()) {
            callback.onCountReady(0, "Diretório inacessível ou não encontrado");
            return;
        }

        sExecutor.execute(() -> {
            int count = countTexturesRecursively(dir, 50000);
            String summary;
            if (count == 0) {
                summary = "Nenhuma textura (.png) encontrada nesta pasta";
            } else if (count >= 50000) {
                summary = "Mais de 50.000 texturas encontradas";
            } else {
                summary = String.format(Locale.getDefault(), "%,d texturas encontradas", count);
            }

            getMainHandler().post(() -> callback.onCountReady(count, summary));
        });
    }
}
