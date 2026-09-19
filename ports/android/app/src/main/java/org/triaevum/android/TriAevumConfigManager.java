package org.triaevum.android;

import android.content.Context;
import android.util.Log;
import org.json.JSONException;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
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

    // ------- TriAevum.android.host.json ---
    public static final String[] SURFACE_RES_LABELS = { "720p (Padrão)", "1080p (Nativo Moto G100)", "Sem Limite" };
    public static final int[]    SURFACE_RES_VALUES = { 720,              1080,                        0           };

    // -------------------------------------------------------------------------

    private final File mExternalDir;

    public TriAevumConfigManager(Context context) {
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
        try (FileOutputStream fos = new FileOutputStream(f)) {
            fos.write(obj.toString(2).getBytes(StandardCharsets.UTF_8));
        } catch (IOException | JSONException e) {
            Log.e(TAG, "Cannot write " + filename, e);
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
        return readJson("oot3d_native_game.json").optJSONObject("Graphics") != null
            ? readJson("oot3d_native_game.json").optJSONObject("Graphics")
            : new JSONObject();
    }

    private void patchGraphics(String key, Object value) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = root.optJSONObject("Graphics");
            if (gfx == null) gfx = new JSONObject();
            gfx.put(key, value);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    private void patchGraphicsNested(String parentKey, String childKey, Object value) {
        JSONObject root = readJson("oot3d_native_game.json");
        try {
            JSONObject gfx = root.optJSONObject("Graphics");
            if (gfx == null) gfx = new JSONObject();
            JSONObject parent = gfx.optJSONObject(parentKey);
            if (parent == null) parent = new JSONObject();
            parent.put(childKey, value);
            gfx.put(parentKey, parent);
            root.put("Graphics", gfx);
        } catch (JSONException ignored) {}
        writeJson("oot3d_native_game.json", root);
    }

    public float getRenderScale() {
        return (float) getGraphics().optDouble("RenderScale", 1.0);
    }

    public void setRenderScale(float v) {
        patchGraphics("RenderScale", v);
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
            JSONObject gfx = root.optJSONObject("Graphics");
            if (gfx == null) gfx = new JSONObject();
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
        patchGraphicsNested("FrameRate", "Mode", mode);
    }

    public boolean isVSync() {
        JSONObject pres = getGraphics().optJSONObject("Presentation");
        return pres == null || pres.optBoolean("VSync", true);
    }

    public void setVSync(boolean v) {
        patchGraphicsNested("Presentation", "VSync", v);
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
            JSONObject gfx = root.optJSONObject("Graphics");
            if (gfx == null) gfx = new JSONObject();
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

    // =========================================================================
    // topscreen_ui.json
    // =========================================================================

    private JSONObject readTopscreenUi() {
        return readJson("topscreen_ui.json");
    }

    private void writeTopscreenUi(JSONObject obj) {
        writeJson("topscreen_ui.json", obj);
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
        setRenderScale(1.0f);
        setAAMode("Off");
        setFrameRateMode("Original30");
        setVSync(true);
        setCustomTexturesEnabled(false);
        // Topscreen
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
     * allowing user selection in the config dialog to seamlessly toggle between 30 and 60 FPS.
     */
    public void ensureLaunchProfileOptimized() {
        JSONObject profile = readJson("TriAevum.android.launch.json");
        try {
            org.json.JSONArray args = profile.optJSONArray("arguments");
            if (args != null) {
                boolean changed = false;
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
                    }
                }
                if (changed) {
                    writeJson("TriAevum.android.launch.json", profile);
                    Log.i(TAG, "TriAevum.android.launch.json successfully updated for 60 FPS support");
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to optimize launch profile", e);
        }
    }
}
