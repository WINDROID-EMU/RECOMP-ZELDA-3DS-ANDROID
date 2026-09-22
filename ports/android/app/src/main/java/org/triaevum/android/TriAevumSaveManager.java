package org.triaevum.android;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.documentfile.provider.DocumentFile;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Gerenciador de exportação e importação de saves do TriAevum.
 * Gerencia a pasta `savedata/` do jogo, empacotando em arquivos .zip
 * ou descompactando com proteção de integridade e rollback automático em caso de erro.
 */
public final class TriAevumSaveManager {
    private static final String TAG = "TriAevumSaveManager";
    private static final String SAVE_DIR_NAME = "savedata";
    private static final int BUFFER_SIZE = 32 * 1024;

    private static final ExecutorService sExecutor = Executors.newSingleThreadExecutor();
    private static Handler sMainHandler;

    private static synchronized Handler getMainHandler() {
        if (sMainHandler == null) {
            sMainHandler = new Handler(Looper.getMainLooper());
        }
        return sMainHandler;
    }

    public interface SaveCallback<T> {
        void onSuccess(T result);
        void onError(Exception error);
    }

    private TriAevumSaveManager() {}

    /**
     * Retorna a pasta de savedata do jogo.
     */
    public static File getSaveDataDir(Context context) {
        File root = context.getExternalFilesDir(null);
        File saveDir = new File(root, SAVE_DIR_NAME);
        if (!saveDir.exists()) {
            saveDir.mkdirs();
        }
        return saveDir;
    }

    /**
     * Verifica se existem arquivos salvos dentro da pasta savedata.
     */
    public static boolean hasSaveFiles(Context context) {
        File dir = getSaveDataDir(context);
        if (!dir.exists() || !dir.isDirectory()) {
            return false;
        }
        return countFilesInDir(dir) > 0;
    }

    /**
     * Retorna um resumo legível dos arquivos de save (ex: "3 arquivos (1.2 MB)").
     */
    public static String getSaveSummary(Context context) {
        File dir = getSaveDataDir(context);
        if (!dir.exists() || !dir.isDirectory()) {
            return "Nenhum save encontrado";
        }
        int count = countFilesInDir(dir);
        if (count == 0) {
            return "Nenhum save gravado ainda";
        }
        long bytes = getDirSizeBytes(dir);
        return String.format(Locale.getDefault(), "%d arquivo(s) de save (%s)", count, formatBytes(bytes));
    }

    private static int countFilesInDir(File dir) {
        int count = 0;
        File[] files = dir.listFiles();
        if (files == null) return 0;
        for (File f : files) {
            if (f.isFile() && f.length() > 0) {
                count++;
            } else if (f.isDirectory()) {
                count += countFilesInDir(f);
            }
        }
        return count;
    }

    private static long getDirSizeBytes(File dir) {
        long total = 0;
        File[] files = dir.listFiles();
        if (files == null) return 0;
        for (File f : files) {
            if (f.isFile()) {
                total += f.length();
            } else if (f.isDirectory()) {
                total += getDirSizeBytes(f);
            }
        }
        return total;
    }

    private static String formatBytes(long bytes) {
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return String.format(Locale.getDefault(), "%.1f KB", bytes / 1024.0);
        return String.format(Locale.getDefault(), "%.2f MB", bytes / (1024.0 * 1024.0));
    }

    /**
     * Exporta todo o conteúdo da pasta savedata para a URI selecionada via Storage Access Framework (SAF).
     */
    public static void exportSaveToUri(Context context, Uri targetUri, SaveCallback<String> callback) {
        sExecutor.execute(() -> {
            File saveDir = getSaveDataDir(context);
            if (!saveDir.exists() || countFilesInDir(saveDir) == 0) {
                getMainHandler().post(() -> callback.onError(new IOException("Nenhum arquivo de save disponível para exportação.")));
                return;
            }

            try (OutputStream os = context.getContentResolver().openOutputStream(targetUri);
                 BufferedOutputStream bos = new BufferedOutputStream(os, BUFFER_SIZE)) {

                int exportedCount = zipDirectory(saveDir, bos);
                bos.flush();

                Log.i(TAG, "Exportados com sucesso " + exportedCount + " arquivos de save para " + targetUri);
                getMainHandler().post(() -> callback.onSuccess("Save exportado com sucesso (" + exportedCount + " arquivos)!"));
            } catch (Exception e) {
                Log.e(TAG, "Falha ao exportar save para " + targetUri, e);
                getMainHandler().post(() -> callback.onError(e));
            }
        });
    }

    static int zipDirectory(File dir, OutputStream os) throws IOException {
        ZipOutputStream zos = new ZipOutputStream(os);
        int count = zipDirectoryContents(dir, "", zos);
        zos.finish();
        zos.flush();
        return count;
    }

    private static int zipDirectoryContents(File dir, String baseZipPath, ZipOutputStream zos) throws IOException {
        int count = 0;
        File[] files = dir.listFiles();
        if (files == null) return 0;

        byte[] buffer = new byte[BUFFER_SIZE];
        for (File file : files) {
            String entryName = baseZipPath.isEmpty() ? file.getName() : baseZipPath + "/" + file.getName();
            if (file.isDirectory()) {
                count += zipDirectoryContents(file, entryName, zos);
            } else if (file.isFile()) {
                ZipEntry entry = new ZipEntry(entryName);
                entry.setTime(file.lastModified());
                zos.putNextEntry(entry);
                try (FileInputStream fis = new FileInputStream(file);
                     BufferedInputStream bis = new BufferedInputStream(fis, BUFFER_SIZE)) {
                    int read;
                    while ((read = bis.read(buffer)) != -1) {
                        zos.write(buffer, 0, read);
                    }
                }
                zos.closeEntry();
                count++;
            }
        }
        return count;
    }

    /**
     * Importa um save a partir da URI selecionada. Suporta arquivos .zip e arquivos diretos (.bin, .oot3dsav).
     * Cria um backup do diretório atual antes de aplicar as mudanças para garantir rollback caso falhe.
     */
    public static void importSaveFromUri(Context context, Uri sourceUri, SaveCallback<Integer> callback) {
        sExecutor.execute(() -> {
            File saveDir = getSaveDataDir(context);
            File backupDir = new File(saveDir.getParentFile(), "savedata_import_backup_" + System.currentTimeMillis());

            try {
                // Criar backup prévio se houver arquivos atuais
                if (saveDir.exists() && countFilesInDir(saveDir) > 0) {
                    copyDirectory(saveDir, backupDir);
                }

                int importedCount = 0;
                boolean isZip = isZipFile(context, sourceUri);

                if (isZip) {
                    importedCount = extractZipToDir(context, sourceUri, saveDir);
                } else {
                    // Arquivo individual
                    String filename = resolveFileName(context, sourceUri);
                    if (filename == null || filename.trim().isEmpty()) {
                        filename = "save.bin";
                    }
                    File dest = new File(saveDir, filename);
                    try (InputStream is = context.getContentResolver().openInputStream(sourceUri);
                         FileOutputStream fos = new FileOutputStream(dest)) {
                        byte[] buffer = new byte[BUFFER_SIZE];
                        int len;
                        while ((len = is.read(buffer)) != -1) {
                            fos.write(buffer, 0, len);
                        }
                    }
                    importedCount = 1;
                }

                if (importedCount <= 0) {
                    throw new IOException("Nenhum arquivo válido encontrado no arquivo selecionado.");
                }

                // Importação bem-sucedida, limpar backup
                deleteDirectory(backupDir);
                final int finalCount = importedCount;
                Log.i(TAG, "Importados com sucesso " + finalCount + " arquivos de save a partir de " + sourceUri);
                getMainHandler().post(() -> callback.onSuccess(finalCount));

            } catch (Exception e) {
                Log.e(TAG, "Erro durante a importação do save a partir de " + sourceUri + ". Restaurando backup...", e);
                // Rollback em caso de erro
                if (backupDir.exists()) {
                    try {
                        deleteDirectory(saveDir);
                        copyDirectory(backupDir, saveDir);
                        deleteDirectory(backupDir);
                        Log.i(TAG, "Backup restaurado com sucesso após falha de importação.");
                    } catch (Exception rollbackError) {
                        Log.e(TAG, "Erro grave ao tentar restaurar backup", rollbackError);
                    }
                }
                getMainHandler().post(() -> callback.onError(e));
            }
        });
    }

    private static boolean isZipFile(Context context, Uri uri) {
        try (InputStream is = context.getContentResolver().openInputStream(uri)) {
            if (is == null) return false;
            byte[] header = new byte[4];
            int read = is.read(header);
            if (read >= 4) {
                // Assinatura ZIP: 0x50 0x4B 0x03 0x04 ('PK\x03\x04')
                return header[0] == 0x50 && header[1] == 0x4B && header[2] == 0x03 && header[3] == 0x04;
            }
        } catch (Exception ignored) {}
        return false;
    }

    private static int extractZipToDir(Context context, Uri zipUri, File targetDir) throws IOException {
        try (InputStream is = context.getContentResolver().openInputStream(zipUri)) {
            if (is == null) throw new IOException("Não foi possível abrir o arquivo ZIP para leitura.");
            return extractZipStreamToDir(is, targetDir);
        }
    }

    static int extractZipStreamToDir(InputStream is, File targetDir) throws IOException {
        int count = 0;
        if (!targetDir.exists()) {
            targetDir.mkdirs();
        }

        BufferedInputStream bis = (is instanceof BufferedInputStream) ? (BufferedInputStream) is : new BufferedInputStream(is, BUFFER_SIZE);
        ZipInputStream zis = new ZipInputStream(bis);

        ZipEntry entry;
        byte[] buffer = new byte[BUFFER_SIZE];
        String canonicalTarget = targetDir.getCanonicalPath();

        while ((entry = zis.getNextEntry()) != null) {
            String entryName = entry.getName();

            // Caso o zip contenha uma pasta raiz "savedata/", ajustamos o caminho
            if (entryName.startsWith("savedata/") || entryName.startsWith("savedata\\")) {
                entryName = entryName.substring("savedata/".length());
            }

            if (entryName.isEmpty()) {
                zis.closeEntry();
                continue;
            }

            File outputFile = new File(targetDir, entryName);

            // Proteção contra ZipSlip / Directory Traversal Vulnerability
            String canonicalOutput = outputFile.getCanonicalPath();
            if (!canonicalOutput.startsWith(canonicalTarget + File.separator) && !canonicalOutput.equals(canonicalTarget)) {
                throw new SecurityException("Entrada ZIP inválida detectada (ZipSlip): " + entry.getName());
            }

            if (entry.isDirectory()) {
                outputFile.mkdirs();
            } else {
                File parent = outputFile.getParentFile();
                if (parent != null && !parent.exists()) {
                    parent.mkdirs();
                }
                try (FileOutputStream fos = new FileOutputStream(outputFile);
                     BufferedOutputStream bos = new BufferedOutputStream(fos, BUFFER_SIZE)) {
                    int read;
                    while ((read = zis.read(buffer)) != -1) {
                        bos.write(buffer, 0, read);
                    }
                    bos.flush();
                }
                count++;
            }
            zis.closeEntry();
        }
        return count;
    }

    private static String resolveFileName(Context context, Uri uri) {
        try {
            DocumentFile doc = DocumentFile.fromSingleUri(context, uri);
            if (doc != null && doc.getName() != null) {
                return doc.getName();
            }
        } catch (Exception ignored) {}
        return uri.getLastPathSegment();
    }

    private static void copyDirectory(File src, File dest) throws IOException {
        if (src.isDirectory()) {
            if (!dest.exists()) {
                dest.mkdirs();
            }
            File[] files = src.listFiles();
            if (files != null) {
                for (File file : files) {
                    copyDirectory(file, new File(dest, file.getName()));
                }
            }
        } else if (src.isFile()) {
            try (FileInputStream fis = new FileInputStream(src);
                 FileOutputStream fos = new FileOutputStream(dest)) {
                byte[] buffer = new byte[BUFFER_SIZE];
                int len;
                while ((len = fis.read(buffer)) != -1) {
                    fos.write(buffer, 0, len);
                }
            }
        }
    }

    private static void deleteDirectory(File dir) {
        if (dir == null || !dir.exists()) return;
        File[] files = dir.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.isDirectory()) {
                    deleteDirectory(file);
                } else {
                    file.delete();
                }
            }
        }
        dir.delete();
    }
}
