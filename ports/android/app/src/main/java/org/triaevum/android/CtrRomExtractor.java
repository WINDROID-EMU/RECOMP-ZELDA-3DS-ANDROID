package org.triaevum.android;

import android.content.Context;
import android.util.Log;

import java.io.DataInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;

/**
 * Extracts 3DS / CCI ROM contents (romfs.bin, code.bin, exheader.bin)
 * directly on-device and applies regional input adapters (e.g. USA to EUR)
 * to ensure 100% bit-perfect compatibility with the recompiled engine.
 */
public final class CtrRomExtractor {

    private static final String TAG = "CtrRomExtractor";
    private static final int MEDIA_UNIT = 0x200; // 512 bytes
    private static final long INVALID = 0xFFFFFFFFL;

    public interface ProgressCallback {
        void onProgress(String stage, int percent);
    }

    public static boolean extractRom(Context context, File romFile, File outputDir, ProgressCallback callback) throws Exception {
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

            // Adapt USA code.bin to canonical EUR code.bin if matching USA revision
            String codeSha = sha256Hex(finalCode);
            if ("ef210566e1d9d16879a746dfb063fcbad232f0171d860de906531ecc526cc020".equalsIgnoreCase(codeSha)) {
                if (callback != null) callback.onProgress("Adaptando executável regional...", 25);
                finalCode = adaptUsaCodeToEur(context, finalCode);
            }
            writeFile(new File(outputDir, "code.bin"), finalCode);

            // 5. RomFS (romfs.bin) - Extract Level-3 service view directly
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

            // Skip the 0x1000 IVFC superblock to get the pure Level-3 RomFS service view
            long level3Offset = romfsOffset + 0x1000L;
            long level3Size = romfsSize - 0x1000L;

            if (callback != null) callback.onProgress("Extraindo RomFS dos dados do jogo...", 30);

            // Stream copy RomFS with chunked progress
            File romfsDest = new File(outputDir, "romfs.bin");
            try (FileOutputStream fos = new FileOutputStream(romfsDest)) {
                raf.seek(level3Offset);
                byte[] buffer = new byte[1024 * 1024]; // 1MB buffer
                long remaining = level3Size;
                long totalCopied = 0;

                while (remaining > 0) {
                    int toRead = (int) Math.min(buffer.length, remaining);
                    raf.readFully(buffer, 0, toRead);
                    fos.write(buffer, 0, toRead);
                    remaining -= toRead;
                    totalCopied += toRead;

                    if (callback != null) {
                        int progress = (int) (30 + (totalCopied * 65 / level3Size));
                        callback.onProgress(String.format("Extraindo RomFS (%.0f MB / %.0f MB)...",
                                totalCopied / (1024.0 * 1024.0), level3Size / (1024.0 * 1024.0)), progress);
                    }
                }
                fos.flush();
            }

            // Normalize USA regional directories and QM tables if needed
            if (callback != null) callback.onProgress("Verificando adaptação de recursos regionais...", 96);
            normalizeRomFsIfNeeded(romfsDest);

            Log.i(TAG, "Extração e adaptação da ROM 3DS concluída com sucesso!");
            return true;
        }
    }

    static byte[] adaptUsaCodeToEur(Context context, byte[] usaCode) throws IOException {
        try (InputStream is = context.getAssets().open("adapters/oot3d_usa_code_copies.bin")) {
            DataInputStream dis = new DataInputStream(is);
            int count = Integer.reverseBytes(dis.readInt()); // uint32 LE
            byte[] out = new byte[4567040];
            int outPos = 0;
            for (int i = 0; i < count; i++) {
                int origin = Integer.reverseBytes(dis.readInt());
                int length = Integer.reverseBytes(dis.readInt());
                System.arraycopy(usaCode, origin, out, outPos, length);
                outPos += length;
            }
            Log.i(TAG, "Adapted USA code.bin to canonical EUR code.bin successfully (" + count + " copy operations)");
            return out;
        }
    }

    public static void normalizeRomFsIfNeeded(File romfsFile) throws IOException {
        try (RandomAccessFile raf = new RandomAccessFile(romfsFile, "rw")) {
            byte[] header = new byte[40];
            raf.seek(0);
            raf.readFully(header);

            long h1 = readU32(header, 4);  // dir hash offset
            long h2 = readU32(header, 8);  // dir hash size
            long h3 = readU32(header, 12); // dir table offset
            long h4 = readU32(header, 16); // dir table size
            long h5 = readU32(header, 20); // file hash offset
            long h6 = readU32(header, 24); // file hash size
            long h7 = readU32(header, 28); // file table offset
            long h8 = readU32(header, 32); // file table size
            long h9 = readU32(header, 36); // data offset

            byte[] dirs = new byte[(int) h4];
            raf.seek(h3);
            raf.readFully(dirs);

            byte[] files = new byte[(int) h8];
            raf.seek(h7);
            raf.readFully(files);

            Map<Integer, DirRecord> dirRecords = new TreeMap<>();
            Map<Integer, FileRecord> fileRecords = new TreeMap<>();

            class StackItem {
                final int offset;
                final String prefix;
                final long expectedParent;
                StackItem(int o, String p, long ep) { offset = o; prefix = p; expectedParent = ep; }
            }

            ArrayDeque<StackItem> pending = new ArrayDeque<>();
            pending.push(new StackItem(0, "", 0));

            while (!pending.isEmpty()) {
                StackItem item = pending.pop();
                int offset = item.offset;
                long parent = readU32(dirs, offset);
                long sibling = readU32(dirs, offset + 4);
                long childDir = readU32(dirs, offset + 8);
                long childFile = readU32(dirs, offset + 12);
                long nameLen = readU32(dirs, offset + 20);

                byte[] nameBytes = Arrays.copyOfRange(dirs, offset + 24, offset + 24 + (int) nameLen);
                String name = new String(nameBytes, StandardCharsets.UTF_16LE);
                String path = item.prefix.isEmpty() ? name : (item.prefix + "/" + name);
                dirRecords.put(offset, new DirRecord(parent, nameBytes, path));

                if (sibling != INVALID) {
                    pending.push(new StackItem((int) sibling, item.prefix, item.expectedParent));
                }
                if (childDir != INVALID) {
                    pending.push(new StackItem((int) childDir, path, offset));
                }

                while (childFile != INVALID) {
                    int fo = (int) childFile;
                    long fParent = readU32(files, fo);
                    long fSibling = readU32(files, fo + 4);
                    long fPayload = readU32(files, fo + 8) | (readU32(files, fo + 12) << 32);
                    long fLen = readU32(files, fo + 16) | (readU32(files, fo + 20) << 32);
                    long fNameLen = readU32(files, fo + 28);
                    byte[] fNameBytes = Arrays.copyOfRange(files, fo + 32, fo + 32 + (int) fNameLen);
                    String fName = new String(fNameBytes, StandardCharsets.UTF_16LE);
                    String fPath = path.isEmpty() ? fName : (path + "/" + fName);
                    fileRecords.put(fo, new FileRecord(fParent, fNameBytes, fPath, fPayload, fLen));
                    childFile = fSibling;
                }
            }

            // Check if USA ROM (contains message/us)
            boolean hasMessageUs = false;
            for (DirRecord dr : dirRecords.values()) {
                if ("message/us".equals(dr.path)) { hasMessageUs = true; break; }
            }
            if (!hasMessageUs) {
                return; // Already EUR or normalized
            }

            FileRecord qmRecord = null;
            for (FileRecord fr : fileRecords.values()) {
                if ("message/us/us.qm".equals(fr.path)) { qmRecord = fr; break; }
            }
            if (qmRecord == null) throw new IOException("missing USA message container us.qm");

            long qmOffset = h9 + qmRecord.payload;
            raf.seek(qmOffset);
            byte[] qmHead = new byte[16];
            raf.readFully(qmHead);
            long qmCount = readU32(qmHead, 8);
            int qmTableSize = 16 + (int) qmCount * 96;
            byte[] qmTable = new byte[qmTableSize];
            raf.seek(qmOffset);
            raf.readFully(qmTable);

            // Adapt QM table (US slots 1, 5, 7 -> EU slots 2, 4, 6)
            int[][] slotPairs = new int[][] { {2, 1}, {4, 5}, {6, 7} };
            for (int i = 0; i < qmCount; i++) {
                int entry = 16 + i * 96;
                for (int[] pair : slotPairs) {
                    int destSlot = pair[0];
                    int srcSlot = pair[1];
                    int destStart = entry + 16 + destSlot * 8;
                    int srcStart = entry + 16 + srcSlot * 8;
                    System.arraycopy(qmTable, srcStart, qmTable, destStart, 8);
                }
            }

            long[] dirHashes = new long[(int) (h2 / 4)];
            Arrays.fill(dirHashes, INVALID);
            for (Map.Entry<Integer, DirRecord> entry : dirRecords.entrySet()) {
                int offset = entry.getKey();
                DirRecord dr = entry.getValue();
                byte[] encName = dr.nameBytes;
                if ("message/us".equals(dr.path) || "misc/us".equals(dr.path)) {
                    encName = "eu".getBytes(StandardCharsets.UTF_16LE);
                    System.arraycopy(encName, 0, dirs, offset + 24, 4);
                }
                int bucket = (int) (nameHash(dr.parent, encName) % dirHashes.length);
                writeU32(dirs, offset + 16, dirHashes[bucket]);
                dirHashes[bucket] = offset;
            }

            long[] fileHashes = new long[(int) (h6 / 4)];
            Arrays.fill(fileHashes, INVALID);
            for (Map.Entry<Integer, FileRecord> entry : fileRecords.entrySet()) {
                int offset = entry.getKey();
                FileRecord fr = entry.getValue();
                byte[] encName = fr.nameBytes;
                if ("message/us/us.qm".equals(fr.path)) {
                    encName = "eu.qm".getBytes(StandardCharsets.UTF_16LE);
                    System.arraycopy(encName, 0, files, offset + 32, encName.length);
                }
                int bucket = (int) (nameHash(fr.parent, encName) % fileHashes.length);
                writeU32(files, offset + 24, fileHashes[bucket]);
                fileHashes[bucket] = offset;
            }

            byte[] dirHashBytes = new byte[(int) h2];
            for (int i = 0; i < dirHashes.length; i++) writeU32(dirHashBytes, i * 4, dirHashes[i]);

            byte[] fileHashBytes = new byte[(int) h6];
            for (int i = 0; i < fileHashes.length; i++) writeU32(fileHashBytes, i * 4, fileHashes[i]);

            // Patch modified tables in-place
            raf.seek(h1);
            raf.write(dirHashBytes);
            raf.seek(h3);
            raf.write(dirs);
            raf.seek(h5);
            raf.write(fileHashBytes);
            raf.seek(h7);
            raf.write(files);
            raf.seek(qmOffset);
            raf.write(qmTable);

            Log.i(TAG, "Normalized USA RomFS to Level-3 EUR service view successfully");
        }
    }

    private static long nameHash(long parent, byte[] encoded) {
        long value = (parent ^ 123456789L) & 0xFFFFFFFFL;
        for (int i = 0; i < encoded.length; i += 2) {
            int unit = (encoded[i] & 0xFF) | ((encoded[i + 1] & 0xFF) << 8);
            value = (((value >>> 5) | (value << 27)) ^ unit) & 0xFFFFFFFFL;
        }
        return value;
    }

    private static long readU32(byte[] data, int offset) {
        return ((long) (data[offset] & 0xFF))
                | (((long) (data[offset + 1] & 0xFF)) << 8)
                | (((long) (data[offset + 2] & 0xFF)) << 16)
                | (((long) (data[offset + 3] & 0xFF)) << 24);
    }

    private static void writeU32(byte[] data, int offset, long val) {
        data[offset] = (byte) (val & 0xFF);
        data[offset + 1] = (byte) ((val >> 8) & 0xFF);
        data[offset + 2] = (byte) ((val >> 16) & 0xFF);
        data[offset + 3] = (byte) ((val >> 24) & 0xFF);
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

    static String sha256Hex(byte[] data) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] digest = md.digest(data);
            StringBuilder sb = new StringBuilder(digest.length * 2);
            for (byte b : digest) {
                sb.append(String.format("%02x", b));
            }
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
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

    private static class DirRecord {
        final long parent;
        final byte[] nameBytes;
        final String path;
        DirRecord(long p, byte[] n, String pt) { parent = p; nameBytes = n; path = pt; }
    }

    private static class FileRecord {
        final long parent;
        final byte[] nameBytes;
        final String path;
        final long payload;
        final long len;
        FileRecord(long p, byte[] n, String pt, long pl, long l) {
            parent = p; nameBytes = n; path = pt; payload = pl; len = l;
        }
    }
}
