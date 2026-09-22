package org.triaevum.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

public final class TriAevumTextureManagerTest {

    @Rule
    public TemporaryFolder mFolder = new TemporaryFolder();

    @Test
    public void countsPngTexturesRecursively() throws IOException {
        File rootDir = mFolder.newFolder("textures_root");

        // Arquivo PNG na raiz
        new FileOutputStream(new File(rootDir, "tex1_64x64_ABCDEF_0.png")).close();

        // Subpasta de personagens com 2 PNGs e 1 JPG (ignorado)
        File actorsDir = new File(rootDir, "actors");
        actorsDir.mkdirs();
        new FileOutputStream(new File(actorsDir, "tex1_128x128_123456_0.png")).close();
        new FileOutputStream(new File(actorsDir, "tex1_128x128_7890AB_0.PNG")).close();
        new FileOutputStream(new File(actorsDir, "preview.jpg")).close();

        // Subpasta de cenário aninhada com 1 PNG e 1 TXT
        File sceneDir = new File(actorsDir, "subscene");
        sceneDir.mkdirs();
        new FileOutputStream(new File(sceneDir, "tex1_256x256_FEDCBA_0.png")).close();
        new FileOutputStream(new File(sceneDir, "notes.txt")).close();

        int totalPngs = TriAevumTextureManager.countTexturesRecursively(rootDir, 1000);
        assertEquals(4, totalPngs);
    }

    @Test
    public void resolvePathFromTreeUriHandlesNullGracefully() {
        assertNull(TriAevumTextureManager.resolvePathFromTreeUri(null, null));
    }
}
