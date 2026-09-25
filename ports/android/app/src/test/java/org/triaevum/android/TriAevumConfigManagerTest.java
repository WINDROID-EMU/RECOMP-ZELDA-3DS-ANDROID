package org.triaevum.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.ContextWrapper;

import org.json.JSONObject;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public final class TriAevumConfigManagerTest {

    @Rule
    public TemporaryFolder mFolder = new TemporaryFolder();

    @Test
    public void writesGraphicsSchemaVersionOnCustomTexturesPathSet() throws Exception {
        File filesDir = mFolder.newFolder("files");
        Context mockContext = new ContextWrapper(null) {
            @Override
            public File getExternalFilesDir(String type) {
                return filesDir;
            }
        };

        TriAevumConfigManager configManager = new TriAevumConfigManager(mockContext);
        configManager.setCustomTexturesPath("/storage/emulated/0/Textures/ZeldaPack");

        File configFile = new File(filesDir, "oot3d_native_game.json");
        assertTrue(configFile.exists());

        String raw = new String(Files.readAllBytes(configFile.toPath()), StandardCharsets.UTF_8);
        JSONObject root = new JSONObject(raw);
        JSONObject gfx = root.getJSONObject("Graphics");

        assertEquals(TriAevumConfigManager.GRAPHICS_SCHEMA_VERSION, gfx.getInt("SchemaVersion"));
        JSONObject azahar = gfx.getJSONObject("TexturePacks").getJSONObject("Azahar");
        assertEquals("/storage/emulated/0/Textures/ZeldaPack", azahar.getString("LoadDirectory"));
        assertTrue(azahar.getBoolean("LoadCustomTextures"));
        assertEquals("/storage/emulated/0/Textures/ZeldaPack", configManager.getCustomTexturesPath());
        assertTrue(configManager.isCustomTexturesEnabled());
    }

    @Test
    public void savesAndPersistsCustomGraphicsSettings() throws Exception {
        File filesDir = mFolder.newFolder("files_gfx");
        Context mockContext = new ContextWrapper(null) {
            @Override
            public File getExternalFilesDir(String type) {
                return filesDir;
            }
        };

        TriAevumConfigManager configManager = new TriAevumConfigManager(mockContext);
        configManager.saveGraphicsSettings(2.0f, "FXAA", "Interpolated2x", true, true);

        File configFile = new File(filesDir, "oot3d_native_game.json");
        assertTrue(configFile.exists());

        String raw = new String(Files.readAllBytes(configFile.toPath()), StandardCharsets.UTF_8);
        JSONObject root = new JSONObject(raw);
        JSONObject gfx = root.getJSONObject("Graphics");

        assertEquals("Custom", gfx.getString("Preset"));
        assertEquals(2.0, gfx.getDouble("RenderScale"), 0.001);
        assertEquals("FXAA", gfx.getJSONObject("AA").getString("Mode"));
        assertEquals("Interpolated2x", gfx.getJSONObject("FrameRate").getString("Mode"));
        assertTrue(gfx.getJSONObject("Presentation").getBoolean("VSync"));
        assertTrue(gfx.getJSONObject("TexturePacks").getJSONObject("Azahar").getBoolean("LoadCustomTextures"));

        assertEquals(2.0f, configManager.getRenderScale(), 0.001f);
        assertEquals("FXAA", configManager.getAAMode());
        assertEquals("Interpolated2x", configManager.getFrameRateMode());
        assertTrue(configManager.isVSync());
        assertTrue(configManager.isCustomTexturesEnabled());
    }
}
