package org.triaevum.android;

import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;

/**
 * Extracts 3DS / CCI ROM contents (romfs.bin, code.bin, exheader.bin)
 * directly on-device without needing external host tools.
 */
public final class CtrRomExtractor {

    private static final String TAG = "CtrRomExtractor";
    private static final int MEDIA_UNIT = 0x200; // 512 bytes

    public interface ProgressCallback {
        void onProgress(String stage, int percent);
    }

    public static boolean extractRom(File romFile, File outputDir, ProgressCallback callback) throws Exception {
        if (!romFile.isFile() || romFile.length() < 0x200) {
            throw new IOException("Arquivo de ROM inválido ou vazio");
        }

        try (RandomAccessFile raf = new RandomAccessFile(romFile, "r")) {
            long fileSize = raf.length();

            // 1. Read container header (NCSD or NCCH)
            byte[] header = new byte[0x200];
            raf.seek(0);
            raf.readFully(header);

            String magic = new String(header, 0x100, 4, StandardCharsets.ISO_8859_1);
            long partitionBase = 0;
            long partitionSize = fileSize;

            if ("NCSD".equals(magic)) {
                // Partition 0 base and size in media units
                long pBaseUnits = readU32LE(header, 0x120);
                long pSizeUnits = readU32LE(header, 0x124);
                partitionBase = pBaseUnits * MEDIA_UNIT;
                partitionSize = pSizeUnits * MEDIA_UNIT;
            } else if (!"NCCH".equals(magic)) {
                throw new IOException("O arquivo baixado não é uma ROM 3DS válida (NCSD/NCCH ausente)");
            }

            // 2. Parse partition 0 NCCH
            byte[] ncch = new byte[0x200];
            raf.seek(partitionBase);
            raf.readFully(ncch);

            String ncchMagic = new String(ncch, 0x100, 4, StandardCharsets.ISO_8859_1);
            if (!"NCCH".equals(ncchMagic)) {
                throw new IOException("Partição de jogo NCCH inválida");
            }

            if (callback != null) callback.onProgress("Extraindo ExHeader...", 5);

            // 3. ExHeader (at partitionBase + 0x200, size 0x800 = 2048 bytes)
            long exheaderOffset = partitionBase + 0x200L;
            byte[] exheader = new byte[0x800];
            raf.seek(exheaderOffset);
            raf.readFully(exheader);
            writeFile(new File(outputDir, "exheader.bin"), exheader);

            boolean compressedCode = (exheader[0x0D] & 1) != 0;

            if (callback != null) callback.onProgress("Extraindo código executável (.code)...", 10);

            // 4. ExeFS (code.bin)
            long exefsUnits = readU32LE(ncch, 0x1A0);
            long exefsOffset = partitionBase + exefsUnits * MEDIA_UNIT;
            byte[] exefsHeader = new byte[0x200];
            raf.seek(exefsOffset);
            raf.readFully(exefsHeader);

            long codeOffset = -1;
            int codeSize = 0;

            // Search 8 section entries (16 bytes each: 8 bytes ASCII name, 4 bytes offset, 4 bytes size)
            for (int i = 0; i < 8; i++) {
                int entry = i * 16;
                String secName = new String(exefsHeader, entry, 8, StandardCharsets.US_ASCII).trim();
                if (secName.startsWith(".code")) {
                    long relOffset = readU32LE(exefsHeader, entry + 8);
                    codeSize = (int) readU32LE(exefsHeader, entry + 12);
                    codeOffset = exefsOffset + 0x200L + relOffset;
                    break;
                }
            }

            if (codeOffset < 0 || codeSize <= 0) {
                throw new IOException("Seção .code não encontrada no ExeFS (a ROM pode estar criptografada)");
            }

            byte[] codeBytes = new byte[codeSize];
            raf.seek(codeOffset);
            raf.readFully(codeBytes);

            byte[] finalCode;
            if (compressedCode) {
                if (callback != null) callback.onProgress("Descompactando .code...", 20);
                finalCode = decompressExeFsCode(codeBytes);
            } else {
                finalCode = codeBytes;
            }
            writeFile(new File(outputDir, "code.bin"), finalCode);

            // 5. RomFS (romfs.bin)
            long romfsUnits = readU32LE(ncch, 0x1B0);
            long romfsSizeUnits = readU32LE(ncch, 0x1B4);
            long romfsOffset = partitionBase + romfsUnits * MEDIA_UNIT;
            long romfsSize = romfsSizeUnits * MEDIA_UNIT;

            if (romfsOffset <= 0 || romfsSize <= 0) {
                throw new IOException("Seção RomFS ausente no NCCH");
            }

            raf.seek(romfsOffset);
            byte[] ivfcCheck = new byte[4];
            raf.readFully(ivfcCheck);
            String ivfc = new String(ivfcCheck, StandardCharsets.ISO_8859_1);
            if (!"IVFC".equals(ivfc)) {
                throw new IOException("RomFS não é válida ou está criptografada (IVFC não encontrado)");
            }

            if (callback != null) callback.onProgress("Extraindo RomFS dos dados do jogo...", 30);

            // Stream copy RomFS with chunked progress
            File romfsDest = new File(outputDir, "romfs.bin");
            try (FileOutputStream fos = new FileOutputStream(romfsDest)) {
                raf.seek(romfsOffset);
                byte[] buffer = new byte[1024 * 1024]; // 1MB buffer
                long remaining = romfsSize;
                long totalCopied = 0;

                while (remaining > 0) {
                    int toRead = (int) Math.min(buffer.length, remaining);
                    raf.readFully(buffer, 0, toRead);
                    fos.write(buffer, 0, toRead);
                    remaining -= toRead;
                    totalCopied += toRead;

                    if (callback != null) {
                        int progress = (int) (30 + (totalCopied * 70 / romfsSize));
                        callback.onProgress(String.format("Extraindo RomFS (%.0f MB / %.0f MB)...",
                                totalCopied / (1024.0 * 1024.0), romfsSize / (1024.0 * 1024.0)), progress);
                    }
                }
                fos.flush();
            }

            Log.i(TAG, "Extração da ROM 3DS concluída com sucesso!");
            return true;
        }
    }

    private static byte[] decompressExeFsCode(byte[] compressed) throws IOException {
        if (compressed.length < 8) {
            throw new IOException("ExeFS .code comprimido muito pequeno");
        }
        int len = compressed.length;
        long bufferTopBottom = readU32LE(compressed, len - 8);
        long additionalSize = readU32LE(compressed, len - 4);
        int decompressedSize = (int) (len + additionalSize);

        int footerSize = (int) ((bufferTopBottom >> 24) & 0xFF);
        int encodedSize = (int) (bufferTopBottom & 0xFFFFFF);

        int index = len - footerSize;
        int stopIndex = len - encodedSize;
        int outputIndex = decompressedSize;
        byte[] output = new byte[decompressedSize];
        System.arraycopy(compressed, 0, output, 0, len);

        while (index > stopIndex) {
            index--;
            int control = compressed[index] & 0xFF;
            for (int i = 0; i < 8; i++) {
                if (index <= stopIndex || outputIndex == 0) break;
                if ((control & 0x80) != 0) {
                    if (index < 2) throw new IOException("Referência de compressão truncada");
                    index -= 2;
                    int b0 = compressed[index] & 0xFF;
                    int b1 = compressed[index + 1] & 0xFF;
                    int segment = b0 | (b1 << 8);
                    int segmentSize = ((segment >> 12) & 0xF) + 3;
                    int segmentOffset = (segment & 0xFFF) + 2;
                    for (int j = 0; j < segmentSize; j++) {
                        int source = outputIndex + segmentOffset;
                        if (source >= output.length) {
                            throw new IOException("Back-reference out of range: " + source);
                        }
                        outputIndex--;
                        output[outputIndex] = output[source];
                    }
                } else {
                    if (index <= stopIndex || outputIndex == 0) {
                        throw new IOException("Literal de compressão truncado");
                    }
                    index--;
                    outputIndex--;
                    output[outputIndex] = compressed[index];
                }
                control = (control << 1) & 0xFF;
            }
        }
        return output;
    }

    private static long readU32LE(byte[] data, int offset) {
        return ((long) (data[offset] & 0xFF))
                | (((long) (data[offset + 1] & 0xFF)) << 8)
                | (((long) (data[offset + 2] & 0xFF)) << 16)
                | (((long) (data[offset + 3] & 0xFF)) << 24);
    }

    private static void writeFile(File dest, byte[] bytes) throws IOException {
        try (FileOutputStream fos = new FileOutputStream(dest)) {
            fos.write(bytes);
            fos.flush();
        }
    }
}
