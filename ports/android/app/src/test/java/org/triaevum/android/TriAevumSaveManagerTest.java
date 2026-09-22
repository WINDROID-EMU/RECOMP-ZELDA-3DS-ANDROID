package org.triaevum.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class TriAevumSaveManagerTest {

    @Rule
    public TemporaryFolder mFolder = new TemporaryFolder();

    @Test
    public void zipAndUnzipPreservesSaveFiles() throws IOException {
        File srcDir = mFolder.newFolder("savedata_src");
        File destDir = mFolder.newFolder("savedata_dest");

        // Criar arquivos de save simulados
        File save0 = new File(srcDir, "save00.bin");
        byte[] data0 = "TRIAEVUM_SAVE_DATA_0".getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream fos = new FileOutputStream(save0)) {
            fos.write(data0);
        }

        File quickSav = new File(srcDir, "quick.oot3dsav");
        byte[] dataQuick = "QUICK_SAVESTATE_CONTENT".getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream fos = new FileOutputStream(quickSav)) {
            fos.write(dataQuick);
        }

        File subDir = new File(srcDir, "subfolder");
        subDir.mkdirs();
        File nestedSave = new File(subDir, "extra.sav");
        byte[] dataNested = "NESTED_SAV_12345".getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream fos = new FileOutputStream(nestedSave)) {
            fos.write(dataNested);
        }

        // Compactar para buffer em memória
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        int zippedCount = TriAevumSaveManager.zipDirectory(srcDir, baos);
        assertEquals(3, zippedCount);

        // Descompactar do buffer em memória para destDir
        ByteArrayInputStream bais = new ByteArrayInputStream(baos.toByteArray());
        int extractedCount = TriAevumSaveManager.extractZipStreamToDir(bais, destDir);
        assertEquals(3, extractedCount);

        // Validar integridade dos arquivos
        File extracted0 = new File(destDir, "save00.bin");
        assertTrue(extracted0.isFile());
        assertArrayEquals(data0, Files.readAllBytes(extracted0.toPath()));

        File extractedQuick = new File(destDir, "quick.oot3dsav");
        assertTrue(extractedQuick.isFile());
        assertArrayEquals(dataQuick, Files.readAllBytes(extractedQuick.toPath()));

        File extractedNested = new File(destDir, "subfolder/extra.sav");
        assertTrue(extractedNested.isFile());
        assertArrayEquals(dataNested, Files.readAllBytes(extractedNested.toPath()));
    }

    @Test
    public void zipSlipDetectionPreventsDirectoryTraversal() throws IOException {
        File destDir = mFolder.newFolder("dest_security");

        // Criar arquivo zip malicioso em memória com path traversal
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        try (ZipOutputStream zos = new ZipOutputStream(baos)) {
            ZipEntry malicious = new ZipEntry("../evil.bin");
            zos.putNextEntry(malicious);
            zos.write("MALICIOUS_DATA".getBytes(StandardCharsets.UTF_8));
            zos.closeEntry();
        }

        ByteArrayInputStream bais = new ByteArrayInputStream(baos.toByteArray());
        try {
            TriAevumSaveManager.extractZipStreamToDir(bais, destDir);
            fail("Esperava que ZipSlip lançasse SecurityException");
        } catch (SecurityException e) {
            assertTrue(e.getMessage().contains("ZipSlip"));
        }
    }
}
