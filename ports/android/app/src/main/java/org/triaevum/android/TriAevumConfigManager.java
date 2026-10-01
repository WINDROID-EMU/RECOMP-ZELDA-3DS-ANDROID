package org.triaevum.android;

import android.content.Context;
import android.util.Log;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * Reads and writes all TriAevum config files stored in external app storage.
 *
 * Files managed:
 *   game_language.json      — game language selection
 *   topscreen_ui.json       — HUD, camera and screen layout
 *   oot3d_native_game.json  — render scale, AA, framerate, vsync, texture packs
 *   TriAevum.android.host.json — surface size limit
 */
public final class TriAevumConfigManager {

    private static final String TAG = "TriAevumConfig";

    // ------- game_language.json -------
    public static final String[] LANGUAGE_LABELS  = { "English", "Francais", "Espanol" };
    public static final String[] LANGUAGE_CODES   = { "en",      "fr",       "es"      };

    // ------- oot3d_native_game.json ---
    public static final String[] RENDER_SCALE_LABELS = { "0.5x", "1.0x (Padrão)", "1.5x", "2.0x", "3.0x", "4.0x" };
    public static final float[]  RENDER_SCALE_VALUES = {  0.5f,   1.0f,             1.5f,   2.0f,   3.0f,   4.0f  };

    public static final String[] AA_MODE_LABELS   = { "Desligado", "FXAA", "TAA", "MSAA 2x", "MSAA 4x" };
    public static final String[] AA_MODE_VALUES   = { "Off",       "FXAA", "TAA", "MSAA2x",  "MSAA4x"  };

    public static final String[] FRAMERATE_LABELS = { "30 FPS (Original)", "60 FPS (Nativo)" };
    public static final String[] FRAMERATE_VALUES = { "Original30",         "Interpolated2x" };

    // ------- oot3d_native_game.json (Visual Mods) ---
    public static final String[] GRASS_QUALITY_LABELS = { "Desligada", "Baixa", "Média", "Alta" };
    public static final String[] GRASS_QUALITY_VALUES = { "Off",       "Low",   "Medium", "High" };

    // ------- TriAevum.android.host.json ---
    public static final String[] SURFACE_RES_LABELS = { "720p (Padrão)", "1080p (Nativo Moto G100)", "Sem Limite" };
    public static final int[]    SURFACE_RES_VALUES = { 720,              1080,                        0           };

    // ------- topscreen_ui.json ---
    public static final String[] HUD_LAYOUT_LABELS  = { "Normal (Padrão)", "Restoration (MM3D)" };
    public static final String[] HUD_LAYOUT_VALUES  = { "normal",          "restoration"        };

    // -------------------------------------------------------------------------

    private final Context mContext;
    private final File mExternalDir;

    public TriAevumConfigManager(Context context) {
        mContext = context;
        mExternalDir = context.getExternalFilesDir(null);
    }

    public static native void nativeReloadGraphicsSettings();

    /**
     * Notifies the native engine to re-read and apply graphics settings live at runtime.
     */
    public void applyLiveSettings() {
        try {
            nativeReloadGraphicsSettings();
            Log.i(TAG, "Live graphics settings reload dispatched successfully");
        } catch (Throwable t) {
            Log.w(TAG, "nativeReloadGraphicsSettings unavailable: " + t.getMessage());
        }
    }

    // ---- helpers ----

    private JSONObject readJson(String filename) {
        if (mExternalDir == null) return new JSONObject();
        File f = new File(mExternalDir, filename);
        if (!f.isFile()) return new JSONObject();
        try {
            String raw = new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8);
            return new JSONObject(raw);
        } catch (Exception e) {
            Log.w(TAG, "Cannot read " + filename, e);
            return new JSONObject();
        }
    }

    private void writeJson(String filename, JSONObject obj) {
        if (mExternalDir == null) return;
        File f = new File(mExternalDir, filename);
        File temp = new File(mExternalDir, filename + ".tmp");
        try {
            try (FileOutputStream fos = new FileOutputStream(temp)) {
                fos.write(obj.toString(2).getBytes(StandardCharsets.UTF_8));
                fos.flush();
                try {
                    fos.getFD().sync();
                } catch (Exception ignored) {}
            }
            if (!temp.renameTo(f)) {
                try (FileOutputStream fos = new FileOutputStream(f)) {
                    fos.write(obj.toString(2).getBytes(StandardCharsets.UTF_8));
                    fos.flush();
                    try {
                        fos.getFD().sync();
                    } catch (Exception ignored) {}
                }
            }
        } catch (IOException | JSONException e) {
            Log.e(TAG, "Cannot write " + filename, e);
        } finally {
            if (temp.exists()) {
                temp.delete();
            }
        }
    }

    // =========================================================================
    // game_language.json
    // =========================================================================

    public String getLanguageCode() {
        return readJson("game_language.json").optString("selected", "en");
    }

    public void setLanguageCode(String code) {
        JSONObject obj = readJson("game_language.json");
        try { obj.put("selected", code); } catch (JSONException ignored) {}
        writeJson("game_language.json", obj);
    }

    // =========================================================================
    // TriAevum.android.host.json
    // =========================================================================

    public int getSurfaceMaxShortEdge() {
        return readJson("TriAevum.android.host.json").optInt("maximum_surface_short_edge", 720);
    }

    public void setSurfaceMaxShortEdge(int value) {
        JSONObject obj = readJson("TriAevum.android.host.json");
        try { obj.put("maximum_surface_short_edge", value); } catch (JSONException ignored) {}
        writeJson("TriAevum.android.host.json", obj);
    }

    // =========================================================================
    // oot3d_native_game.json – Graphics
    // =========================================================================

    private JSONObject getGraphics() {
        JSONObject root = readJson("oot3d_native_game.json");
        JSONObject gfx = root.optJSONObject("Graphics");
        return gfx != null ? gfx : new JSONObject();
    }

    public static final int GRAPHICS_SCHEMA_VERSION = 11;

    private JSONObject getOrCreateGraphics(JSONObject root) {
        JSONObject gfx = root.optJSONObject("Graphics");
        if (gfx == null) {
            gfx = new JSONObject();
        }
        try {
            gfx.put("SchemaVersion", GRAPHICS_SCHEMA_VERSION);
        } catch (JSONException ignored) {}
        return gfx;
    }

    private void patchGraphics(String key, Object value) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            gfx.put("Preset", "Custom");
            gfx.put(key, value);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    private void patchGraphicsNested(String parentKey, String childKey, Object value) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            JSONObject parent = gfx.optJSONObject(parentKey);
            if (parent == null) parent = new JSONObject();
            parent.put(childKey, value);
            gfx.put(parentKey, parent);
            gfx.put("Preset", "Custom");
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public float getRenderScale() {
        return (float) getGraphics().optDouble("RenderScale", 1.0);
    }

    public void setRenderScale(float v) {
        patchGraphics("RenderScale", (double) v);
    }

    public String getAAMode() {
        JSONObject aa = getGraphics().optJSONObject("AA");
        if (aa == null) return "Off";
        String mode = aa.optString("Mode", "Off");
        int msaaSamples = aa.optInt("MsaaSamples", 1);
        if ("MSAA".equalsIgnoreCase(mode)) {
            return msaaSamples >= 4 ? "MSAA4x" : "MSAA2x";
        }
        return mode;
    }

    public void setAAMode(String modeValue) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            gfx.put("Preset", "Custom");
            JSONObject aa = gfx.optJSONObject("AA");
            if (aa == null) aa = new JSONObject();
            if ("MSAA2x".equals(modeValue)) {
                aa.put("Mode", "MSAA");
                aa.put("MsaaSamples", 2);
            } else if ("MSAA4x".equals(modeValue)) {
                aa.put("Mode", "MSAA");
                aa.put("MsaaSamples", 4);
            } else {
                aa.put("Mode", modeValue);
                aa.put("MsaaSamples", 1);
            }
            gfx.put("AA", aa);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public String getFrameRateMode() {
        JSONObject fr = getGraphics().optJSONObject("FrameRate");
        if (fr == null) return "Original30";
        String mode = fr.optString("Mode", "Original30");
        if ("Fixed60".equalsIgnoreCase(mode) || "Native60".equalsIgnoreCase(mode)) {
            return "Interpolated2x";
        }
        return mode;
    }

    public void setFrameRateMode(String mode) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            gfx.put("Preset", "Custom");
            JSONObject fr = gfx.optJSONObject("FrameRate");
            if (fr == null) fr = new JSONObject();
            fr.put("Mode", mode);
            gfx.put("FrameRate", fr);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public boolean isVSync() {
        JSONObject pres = getGraphics().optJSONObject("Presentation");
        return pres == null || pres.optBoolean("VSync", true);
    }

    public void setVSync(boolean v) {
        patchGraphicsNested("Presentation", "VSync", v);
    }

    public void saveGraphicsSettings(float renderScale, String aaMode, String frameRateMode, boolean vsync, boolean customTextures) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            gfx.put("Preset", "Custom");
            gfx.put("RenderScale", (double) renderScale);

            JSONObject aa = gfx.optJSONObject("AA");
            if (aa == null) aa = new JSONObject();
            if ("MSAA2x".equals(aaMode)) {
                aa.put("Mode", "MSAA");
                aa.put("MsaaSamples", 2);
            } else if ("MSAA4x".equals(aaMode)) {
                aa.put("Mode", "MSAA");
                aa.put("MsaaSamples", 4);
            } else {
                aa.put("Mode", aaMode);
                aa.put("MsaaSamples", 1);
            }
            gfx.put("AA", aa);

            JSONObject fr = gfx.optJSONObject("FrameRate");
            if (fr == null) fr = new JSONObject();
            fr.put("Mode", frameRateMode);
            gfx.put("FrameRate", fr);

            JSONObject pres = gfx.optJSONObject("Presentation");
            if (pres == null) pres = new JSONObject();
            pres.put("VSync", vsync);
            gfx.put("Presentation", pres);

            JSONObject tp = gfx.optJSONObject("TexturePacks");
            if (tp == null) tp = new JSONObject();
            JSONObject az = tp.optJSONObject("Azahar");
            if (az == null) az = new JSONObject();
            az.put("LoadCustomTextures", customTextures);
            tp.put("Azahar", az);
            gfx.put("TexturePacks", tp);

            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public boolean isCustomTexturesEnabled() {
        JSONObject tp = getGraphics().optJSONObject("TexturePacks");
        if (tp == null) return false;
        JSONObject az = tp.optJSONObject("Azahar");
        return az != null && az.optBoolean("LoadCustomTextures", false);
    }

    public void setCustomTexturesEnabled(boolean v) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            JSONObject tp = gfx.optJSONObject("TexturePacks");
            if (tp == null) tp = new JSONObject();
            JSONObject az = tp.optJSONObject("Azahar");
            if (az == null) az = new JSONObject();
            az.put("LoadCustomTextures", v);
            tp.put("Azahar", az);
            gfx.put("TexturePacks", tp);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public boolean isCustomTexturesPreloadEnabled() {
        JSONObject tp = getGraphics().optJSONObject("TexturePacks");
        if (tp == null) return true;
        JSONObject az = tp.optJSONObject("Azahar");
        return az == null || az.optBoolean("PreloadTextures", true);
    }

    public void setCustomTexturesPreloadEnabled(boolean v) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            JSONObject tp = gfx.optJSONObject("TexturePacks");
            if (tp == null) tp = new JSONObject();
            JSONObject az = tp.optJSONObject("Azahar");
            if (az == null) az = new JSONObject();
            az.put("PreloadTextures", v);
            tp.put("Azahar", az);
            gfx.put("TexturePacks", tp);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public String getCustomTexturesPath() {
        JSONObject tp = getGraphics().optJSONObject("TexturePacks");
        if (tp == null) return "";
        JSONObject az = tp.optJSONObject("Azahar");
        if (az == null) return "";
        return az.optString("LoadDirectory", "");
    }

    public void setCustomTexturesPath(String path) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            JSONObject tp = gfx.optJSONObject("TexturePacks");
            if (tp == null) tp = new JSONObject();
            JSONObject az = tp.optJSONObject("Azahar");
            if (az == null) az = new JSONObject();
            az.put("LoadDirectory", path != null ? path : "");
            if (path != null && !path.trim().isEmpty()) {
                az.put("LoadCustomTextures", true);
            }
            tp.put("Azahar", az);
            gfx.put("TexturePacks", tp);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    // =========================================================================
    // Visual Mods (Cel-Shading, Grama 3D, FOV Multiplier)
    // =========================================================================

    public boolean isToonEnabled() {
        JSONObject fx = getGraphics().optJSONObject("Effects");
        if (fx == null) return false;
        JSONObject toon = fx.optJSONObject("Toon");
        if (toon == null) return false;
        String mode = toon.optString("Mode", "Off");
        return !"Off".equalsIgnoreCase(mode);
    }

    public boolean isToonOutlineEnabled() {
        JSONObject fx = getGraphics().optJSONObject("Effects");
        if (fx == null) return false;
        JSONObject toon = fx.optJSONObject("Toon");
        return toon != null && toon.optBoolean("OutlineEnabled", false);
    }

    public void setToonSettings(boolean enabled, boolean outline) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            gfx.put("Preset", "Custom");
            JSONObject fx = gfx.optJSONObject("Effects");
            if (fx == null) fx = new JSONObject();
            JSONObject toon = fx.optJSONObject("Toon");
            if (toon == null) toon = new JSONObject();

            if (enabled) {
                toon.put("Mode", "PicaMaterial");
                toon.put("LightBands", 4);
                toon.put("BandSoftness", 0.228);
                toon.put("Saturation", 1.09);
                toon.put("RimStrength", 0.34);
                toon.put("RimWidth", 3.17);
                toon.put("OutlineEnabled", outline);
                toon.put("OutlineWidth", 1.0);
                toon.put("OutlineOpacity", 1.0);
            } else {
                toon.put("Mode", "Off");
                toon.put("OutlineEnabled", false);
            }
            fx.put("Toon", toon);
            gfx.put("Effects", fx);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public String getGrassQuality() {
        JSONObject grass = getGraphics().optJSONObject("Grass");
        if (grass == null) return "Off";
        return grass.optString("Quality", "Off");
    }

    public void setGrassQuality(String quality) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            gfx.put("Preset", "Custom");
            JSONObject grass = buildGrassObject(quality);
            gfx.put("Grass", grass);
            gfx.put("GrassSavedPreset", grass);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    private JSONObject buildGrassObject(String quality) throws JSONException {
        JSONObject grass = new JSONObject();
        grass.put("Quality", quality);
        if ("Off".equalsIgnoreCase(quality)) {
            return grass;
        }

        JSONObject appearance = new JSONObject();
        appearance.put("BladeCurvature", 0.64);
        appearance.put("BladeDroop", 0.35);
        appearance.put("BladeSegments", 5);
        appearance.put("BladeTwistDegrees", 141.0);
        appearance.put("HeightScale", 1.15);
        appearance.put("ReceiveFog", true);
        appearance.put("ReceiveLighting", true);
        JSONArray rootColor = new JSONArray();
        rootColor.put(0.0988); rootColor.put(0.19); rootColor.put(0.035);
        appearance.put("RootColor", rootColor);
        appearance.put("ShapeVariation", 0.38);
        appearance.put("TextureColorInfluence", 0.75);
        appearance.put("TextureRootBrightness", 1.01);
        appearance.put("TextureTipBrightness", 1.74);
        JSONArray tipColor = new JSONArray();
        tipColor.put(0.1906); tipColor.put(0.72); tipColor.put(0.12);
        appearance.put("TipColor", tipColor);
        appearance.put("ToonRimEnabled", false);
        appearance.put("ToonRimFadeEnd", 901.0);
        appearance.put("ToonRimFadeStart", 0.0);
        grass.put("Appearance", appearance);

        JSONObject generation = new JSONObject();
        generation.put("BladeHeightMax", 12.51);
        generation.put("BladeHeightMin", 7.68);
        generation.put("BladeWidthMax", 1.94);
        generation.put("BladeWidthMin", 1.80);
        generation.put("ClusterCoverage", 0.65);
        generation.put("ClusterScale", 300.0);
        generation.put("ClusterStrength", 0.0);
        generation.put("IndividualRandomness", 1.0);
        generation.put("InstancesPerSquareMeter", "High".equalsIgnoreCase(quality) ? 1024.0 : ("Medium".equalsIgnoreCase(quality) ? 512.0 : 256.0));
        generation.put("MinimumSpacing", 0.0);
        generation.put("Seed", 1);
        grass.put("Generation", generation);

        JSONObject interaction = new JSONObject();
        interaction.put("ColliderHeightMultiplier", 1.69);
        interaction.put("ColliderRadiusMultiplier", 1.19);
        interaction.put("CollisionPush", 1.66);
        interaction.put("Damping", 1.46);
        interaction.put("FieldRadius", 600.0);
        interaction.put("FieldResolution", 256);
        interaction.put("MaximumBend", 0.85);
        interaction.put("RecoverySeconds", 1.0);
        interaction.put("VelocityResponse", 0.87);
        interaction.put("VerticalMargin", 69.0);
        grass.put("LinkInteraction", interaction);

        JSONObject wind = new JSONObject();
        wind.put("DirectionDegrees", 0.0);
        wind.put("GustFrequency", 0.35);
        wind.put("GustStrength", 0.35);
        wind.put("Randomness", 0.44);
        wind.put("SpatialScale", 1.0);
        wind.put("Speed", 2.84);
        wind.put("Strength", 0.56);
        wind.put("Turbulence", 0.42);
        grass.put("Wind", wind);

        JSONObject budget = new JSONObject();
        JSONObject perf = new JSONObject();
        perf.put("FrustumCulling", true);
        perf.put("CullingClusterSize", 25.0);
        perf.put("DrawFadeFraction", 0.15);
        perf.put("DensityFadeFraction", 0.30);
        perf.put("TuftTransitionFraction", 0.34);
        perf.put("FarTuftsEnabled", false);
        perf.put("MidrangeClustersEnabled", true);
        perf.put("MidrangeAdaptiveEnabled", true);
        perf.put("MidrangeAdaptiveCapacity", 10000);
        perf.put("MidrangeClusterCellExtent", 88.0);
        perf.put("MidrangeFarBladeFraction", 1.0);
        perf.put("SegmentLodStartDistance", 501.0);
        perf.put("SegmentLodEndDistance", 1000.0);
        perf.put("SegmentLodSoftness", 0.75);
        perf.put("FarBladeSegments", 1);

        if ("Low".equalsIgnoreCase(quality)) {
            budget.put("MaxInstancesPerRoom", 50000);
            budget.put("DrawDistance", 5000.0);
            perf.put("LodStartFraction", 0.50);
            perf.put("LodEndFraction", 1.0);
            perf.put("FarDensity", 0.30);
        } else if ("Medium".equalsIgnoreCase(quality)) {
            budget.put("MaxInstancesPerRoom", 150000);
            budget.put("DrawDistance", 15000.0);
            perf.put("LodStartFraction", 0.75);
            perf.put("LodEndFraction", 1.0);
            perf.put("FarDensity", 0.45);
        } else {
            budget.put("MaxInstancesPerRoom", 500000);
            budget.put("DrawDistance", 50000.0);
            perf.put("LodStartFraction", 1.0);
            perf.put("LodEndFraction", 1.0);
            perf.put("FarDensity", 0.62);
        }
        grass.put("Budget", budget);
        grass.put("Performance", perf);

        grass.put("Sources", getDefaultGrassSources());
        return grass;
    }

    private JSONArray getDefaultGrassSources() throws JSONException {
        JSONArray sources = new JSONArray();

        String[][] rules = new String[][] {
            {"be15aff93dfdcd88", "256", "256", "0.0", "0.252", "false", "65.0", "-0.15", "8.0"},
            {"2321986eb9820c29", "128", "128", "0.759", "0.836", "true", "48.0", "0.5", "1.0"},
            {"4b8941fd174516b0", "256", "256", "0.0", "0.572", "false", "48.0", "0.5", "8.0"},
            {"0a29e93a3b0742b3", "256", "256", "0.484", "0.485", "true", "48.0", "0.5", "8.0"},
            {"bd769b9ce136d73a", "256", "128", "0.498", "0.499", "true", "48.0", "0.5", "8.0"},
            {"d13528cd4896c851", "128", "128", "0.503", "0.504", "true", "48.0", "0.5", "8.0"}
        };

        for (int i = 0; i < rules.length; i++) {
            JSONObject rule = new JSONObject();
            rule.put("RuleId", i + 1);
            rule.put("Channel", "Green");
            rule.put("InputBlack", Double.parseDouble(rules[i][3]));
            rule.put("InputWhite", Double.parseDouble(rules[i][4]));
            rule.put("Invert", Boolean.parseBoolean(rules[i][5]));
            rule.put("MaximumSlopeDegrees", Double.parseDouble(rules[i][6]));
            rule.put("NormalOffset", Double.parseDouble(rules[i][7]));
            rule.put("OutputBlack", 0.0);
            rule.put("OutputWhite", 1.0);
            rule.put("ResponseExponent", Double.parseDouble(rules[i][8]));
            rule.put("Wrap", "Material");

            JSONObject target = new JSONObject();
            target.put("AssetName", "");
            target.put("Rgba8Hash", rules[i][0]);
            target.put("Width", Integer.parseInt(rules[i][1]));
            target.put("Height", Integer.parseInt(rules[i][2]));
            target.put("MapperSlotMask", 7);
            rule.put("Target", target);

            sources.put(rule);
        }
        return sources;
    }

    public float getFovMultiplier() {
        JSONObject cam = getGraphics().optJSONObject("Camera");
        if (cam == null) return 1.0f;
        return (float) cam.optDouble("FovMultiplier", 1.0);
    }

    public void setFovMultiplier(float fov) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            gfx.put("Preset", "Custom");
            JSONObject cam = gfx.optJSONObject("Camera");
            if (cam == null) cam = new JSONObject();
            cam.put("FovMultiplier", (double) fov);
            gfx.put("Camera", cam);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public void saveVisualMods(boolean toonEnabled, boolean toonOutline, String grassQuality, float fovMultiplier) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = getOrCreateGraphics(root);
            gfx.put("Preset", "Custom");

            // Toon
            JSONObject fx = gfx.optJSONObject("Effects");
            if (fx == null) fx = new JSONObject();
            JSONObject toon = fx.optJSONObject("Toon");
            if (toon == null) toon = new JSONObject();
            if (toonEnabled) {
                toon.put("Mode", "PostProcessPreview");
                toon.put("LightBands", 4);
                toon.put("BandSoftness", 0.15);
                toon.put("Saturation", 1.09);
                toon.put("RimStrength", 0.34);
                toon.put("RimWidth", 3.17);
                toon.put("OutlineEnabled", toonOutline);
                toon.put("OutlineWidth", 1.0);
                toon.put("OutlineOpacity", 1.0);
            } else {
                toon.put("Mode", "Off");
                toon.put("OutlineEnabled", false);
            }
            fx.put("Toon", toon);
            gfx.put("Effects", fx);

            // Grass
            JSONObject grass = buildGrassObject(grassQuality);
            gfx.put("Grass", grass);
            gfx.put("GrassSavedPreset", grass);

            // Camera FOV
            JSONObject cam = gfx.optJSONObject("Camera");
            if (cam == null) cam = new JSONObject();
            cam.put("FovMultiplier", (double) fovMultiplier);
            gfx.put("Camera", cam);

            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    // =========================================================================
    // topscreen_ui.json
    // =========================================================================

    private JSONObject readTopscreenUi() {
        JSONObject obj = readJson("topscreen_ui.json");
        if (!obj.has("schema")) {
            try {
                obj.put("schema", "oot3d_topscreen_ui_v2");
                obj.put("hud_layout", "normal");
                obj.put("hud_scale", 0.8);
                obj.put("hud_margin_x", 4);
                obj.put("hud_margin_y", 1);
                obj.put("magic_bar_y", 0);
                obj.put("minimap_visible", true);
                obj.put("render_hud", true);
                obj.put("render_dpad_icons", true);
                obj.put("render_items_hint", true);
                obj.put("select_action", "save_screen");
                obj.put("exit_items_to_save_screen", true);
                obj.put("camera_zoom_percent", 100);
                obj.put("camera_fov_percent", 100);
                org.json.JSONArray dpadChild = new org.json.JSONArray();
                dpadChild.put("view").put("ocarina").put("item_zr").put("item_zl");
                obj.put("dpad_child", dpadChild);
                org.json.JSONArray dpadAdult = new org.json.JSONArray();
                dpadAdult.put("view").put("ocarina").put("iron_boots").put("hover_boots");
                obj.put("dpad_adult", dpadAdult);
                obj.put("free_camera_enabled", false);
                obj.put("free_camera_speed_level", 3);
                obj.put("free_camera_smoothing", "default");
                obj.put("free_camera_invert_x", false);
                obj.put("free_camera_invert_y", false);
                obj.put("c_stick_aim_speed_level", 3);
                obj.put("c_stick_aim_invert_x", false);
                obj.put("c_stick_aim_invert_y", false);
            } catch (JSONException ignored) {}
        }
        return obj;
    }

    private void writeTopscreenUi(JSONObject obj) {
        try { obj.put("schema", "oot3d_topscreen_ui_v2"); } catch (JSONException ignored) {}
        writeJson("topscreen_ui.json", obj);
    }

    public String getHudLayout() {
        return readTopscreenUi().optString("hud_layout", "normal");
    }

    public void setHudLayout(String layout) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("hud_layout", layout); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    public int getHudMarginX() {
        return readTopscreenUi().optInt("hud_margin_x", 4);
    }

    public void setHudMarginX(int margin) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("hud_margin_x", margin); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    public int getHudMarginY() {
        return readTopscreenUi().optInt("hud_margin_y", 1);
    }

    public void setHudMarginY(int margin) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("hud_margin_y", margin); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    public boolean isRenderItemsHint() {
        return readTopscreenUi().optBoolean("render_items_hint", true);
    }

    public void setRenderItemsHint(boolean v) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("render_items_hint", v); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    public boolean isFreeCameraEnabled() {
        return readTopscreenUi().optBoolean("free_camera_enabled", true);
    }

    public void setFreeCameraEnabled(boolean v) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("free_camera_enabled", v); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    /** Returns level 1-5 */
    public int getFreeCameraSpeedLevel() {
        return readTopscreenUi().optInt("free_camera_speed_level", 3);
    }

    public void setFreeCameraSpeedLevel(int level) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("free_camera_speed_level", level); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    public boolean isFreeCameraInvertX() {
        return readTopscreenUi().optBoolean("free_camera_invert_x", false);
    }

    public void setFreeCameraInvertX(boolean v) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("free_camera_invert_x", v); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    public boolean isFreeCameraInvertY() {
        return readTopscreenUi().optBoolean("free_camera_invert_y", false);
    }

    public void setFreeCameraInvertY(boolean v) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("free_camera_invert_y", v); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    /** Returns scale 0.5-1.5 stored as hud_scale float */
    public float getHudScale() {
        return (float) readTopscreenUi().optDouble("hud_scale", 0.8);
    }

    public void setHudScale(float v) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("hud_scale", v); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    public boolean isMinimapVisible() {
        return readTopscreenUi().optBoolean("minimap_visible", true);
    }

    public void setMinimapVisible(boolean v) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("minimap_visible", v); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    public boolean isRenderDpadIcons() {
        return readTopscreenUi().optBoolean("render_dpad_icons", true);
    }

    public void setRenderDpadIcons(boolean v) {
        JSONObject obj = readTopscreenUi();
        try { obj.put("render_dpad_icons", v); } catch (JSONException ignored) {}
        writeTopscreenUi(obj);
    }

    // =========================================================================
    // Defaults
    // =========================================================================

    public void restoreDefaults() {
        // Language
        setLanguageCode("en");
        // Surface
        setSurfaceMaxShortEdge(720);
        // Graphics
        saveGraphicsSettings(1.0f, "Off", "Original30", true, false);
        saveVisualMods(false, false, "Off", 1.0f);
        // Topscreen
        setHudLayout("normal");
        setHudMarginX(4);
        setHudMarginY(1);
        setRenderItemsHint(true);
        setFreeCameraEnabled(true);
        setFreeCameraSpeedLevel(3);
        setFreeCameraInvertX(false);
        setFreeCameraInvertY(false);
        setHudScale(0.8f);
        setMinimapVisible(true);
        setRenderDpadIcons(true);
    }

    /**
     * Ensures TriAevum.android.launch.json enables visual interpolation and 60 Hz presentation,
     * as well as --topscreen-config and --topscreen-texture-overrides for seamless single-screen UI layout customization.
     */
    public void ensureLaunchProfileOptimized() {
        File atlasFile = new File(mContext.getExternalFilesDir(null), "atlas_overrides.o3tu");
        if (!atlasFile.exists()) {
            try (InputStream in = mContext.getAssets().open("game/atlas_overrides.o3tu");
                 OutputStream out = new FileOutputStream(atlasFile)) {
                byte[] buf = new byte[8192];
                int len;
                while ((len = in.read(buf)) > 0) {
                    out.write(buf, 0, len);
                }
                Log.i(TAG, "Unpacked bundled atlas_overrides.o3tu to external files directory");
            } catch (Exception e) {
                Log.d(TAG, "No bundled atlas_overrides.o3tu asset or could not unpack: " + e.getMessage());
            }
        }

        JSONObject profile = readJson("TriAevum.android.launch.json");
        try {
            org.json.JSONArray args = profile.optJSONArray("arguments");
            if (args != null) {
                boolean changed = false;
                boolean hasTopScreenConfig = false;
                boolean hasTextureOverrides = false;
                for (int i = 0; i < args.length() - 1; i++) {
                    if ("--gameplay-timing".equals(args.getString(i))) {
                        if (!"native30_interpolated".equals(args.getString(i + 1))) {
                            args.put(i + 1, "native30_interpolated");
                            changed = true;
                        }
                    } else if ("--presentation-rate".equals(args.getString(i))) {
                        if (!"60".equals(args.getString(i + 1))) {
                            args.put(i + 1, "60");
                            changed = true;
                        }
                    } else if ("--topscreen-config".equals(args.getString(i))) {
                        hasTopScreenConfig = true;
                    } else if ("--topscreen-texture-overrides".equals(args.getString(i))) {
                        hasTextureOverrides = true;
                    }
                }
                if (!hasTopScreenConfig) {
                    args.put("--topscreen-config");
                    args.put("${profile_dir}/topscreen_ui.json");
                    changed = true;
                }
                if (!hasTextureOverrides && atlasFile.exists()) {
                    args.put("--topscreen-texture-overrides");
                    args.put("${profile_dir}/atlas_overrides.o3tu");
                    changed = true;
                }
                if (changed) {
                    writeJson("TriAevum.android.launch.json", profile);
                    Log.i(TAG, "TriAevum.android.launch.json successfully updated for 60 FPS and TopScreen UI support");
                }
                File topScreenFile = new File(mExternalDir, "topscreen_ui.json");
                if (!topScreenFile.exists()) {
                    writeTopscreenUi(readTopscreenUi());
                    Log.i(TAG, "Default topscreen_ui.json created successfully");
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to optimize launch profile", e);
        }
    }
}
