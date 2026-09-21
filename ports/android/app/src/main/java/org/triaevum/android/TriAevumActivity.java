package org.triaevum.android;

import android.app.Activity;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import android.view.Surface;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;

import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import org.json.JSONObject;
import org.triaevum.android.controls.WindroidVirtualControllerView;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * Pure Android Activity hosting TriAevum.
 * Replaces SDLActivity completely, managing lifecycle, native Vulkan surface,
 * virtual overlay controls, and native game thread directly.
 */
public final class TriAevumActivity extends Activity {
    private static final String TAG = "TriAevum";

    static {
        System.loadLibrary("triaevum_title_bootstrap");
        System.loadLibrary("TriAevum");
    }

    // JNI Native bindings
    public static native void nativeSetStoragePath(String path);
    public static native void nativeSurfaceCreated(Surface surface);
    public static native void nativeSurfaceChanged(Surface surface, int width, int height);
    public static native void nativeSurfaceDestroyed();
    public static native void nativeOnPause();
    public static native void nativeOnResume();
    public static native void nativeMain(String[] args);

    private FrameLayout mLayout;
    private TriAevumSurface mSurface;
    private WindroidVirtualControllerView mWindroidOverlay;
    private AndroidNativeInputTarget mInputTarget;
    private Thread mGameThread;
    private boolean mGameStarted = false;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // Suppress verbose Qualcomm Adreno / Gralloc probing errors in Logcat
        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");
            java.lang.reflect.Method set = sp.getMethod("set", String.class, String.class);
            set.invoke(null, "log.tag.qdgralloc", "WARN");
            set.invoke(null, "log.tag.GraphicBufferAllocator", "WARN");
            set.invoke(null, "log.tag.Gralloc4", "WARN");
            set.invoke(null, "log.tag.AHardwareBuffer", "WARN");
        } catch (Throwable ignored) {}

        // Ensure launch profile allows 60 FPS interpolation
        try {
            new TriAevumConfigManager(this).ensureLaunchProfileOptimized();
        } catch (Throwable ignored) {}

        // Enable edge-to-edge layout across the entire physical display including camera cutouts
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        hideSystemBars();

        File root = getExternalFilesDir(null);
        if (root != null) {
            nativeSetStoragePath(root.getAbsolutePath());
            exportHudLayoutFromXml(root);
        }

        mLayout = new FrameLayout(this);
        mLayout.setLayoutParams(new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Create native Vulkan surface view
        int maximumShortEdge = resolveMaximumShortEdge();
        mSurface = new TriAevumSurface(this, maximumShortEdge);
        mLayout.addView(mSurface, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Create and bind virtual controller overlay
        try {
            mInputTarget = new AndroidNativeInputTarget();
            mWindroidOverlay = new WindroidVirtualControllerView(this);
            mWindroidOverlay.bindInputTarget(mInputTarget);

            // Synchronize saved preferences for controls and screen swapping
            try {
                android.content.SharedPreferences prefs = getApplicationContext()
                    .getSharedPreferences("org.triaevum.android_preferences", Context.MODE_PRIVATE);
                boolean showOverlay = prefs.getBoolean("EmulationMenuSettings_ShowOverlay", true);
                boolean haptic = prefs.getBoolean("EmulationMenuSettings_HapticFeedback", true);
                boolean swapScreens = prefs.getBoolean("EmulationMenuSettings_SwapScreens", false);
                boolean touchEnabled = prefs.getBoolean("EmulationMenuSettings_TouchEnabled", false);
                mWindroidOverlay.setShowControls(showOverlay);
                mWindroidOverlay.setHapticFeedbackEnabled(haptic);
                mWindroidOverlay.setTouchEnabled(touchEnabled);
                AndroidNativeInputTarget.nativeSwapScreens(swapScreens);
                AndroidNativeInputTarget.nativeSetTouchEnabled(touchEnabled);
            } catch (Throwable t) {
                Log.w(TAG, "Failed to sync initial control settings", t);
            }

            // Open settings dialog when the gear icon is tapped
            mWindroidOverlay.setOnSettingsClickListener(() -> {
                if (!isFinishing() && !isDestroyed()) {
                    try {
                        new TriAevumConfigDialog(this, mWindroidOverlay).show();
                    } catch (Exception err) {
                        Log.e(TAG, "Failed to open settings dialog", err);
                    }
                }
            });

            mLayout.addView(mWindroidOverlay, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            Log.i(TAG, "Windroid virtual controller overlay initialized successfully");
        } catch (Exception error) {
            Log.e(TAG, "Failed to initialize Windroid virtual controller overlay", error);
        }

        setContentView(mLayout);
    }

    void onSurfaceReady() {
        ensureGameStarted();
    }

    private synchronized void ensureGameStarted() {
        if (mGameStarted) return;
        mGameStarted = true;

        File root = getExternalFilesDir(null);
        final String[] args = new String[] {
            "TriAevum",
            "--launch-profile", new File(root, "TriAevum.android.launch.json").getAbsolutePath(),
            "--title-plugin", new File(getApplicationInfo().nativeLibraryDir,
                "libtriaevum_title_aot.so").getAbsolutePath()
        };

        mGameThread = new Thread(() -> {
            Log.i(TAG, "Launching native game loop...");
            try {
                nativeMain(args);
            } catch (Throwable t) {
                Log.e(TAG, "Native game loop terminated with exception", t);
            }
            Log.i(TAG, "Native game loop exited");
        }, "TriAevumGameThread");
        mGameThread.start();
    }

    private int resolveMaximumShortEdge() {
        int maximumShortEdge = 720;
        File config = new File(getExternalFilesDir(null), "TriAevum.android.host.json");
        if (config.isFile()) {
            try {
                maximumShortEdge = new JSONObject(new String(Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8))
                    .getInt("maximum_surface_short_edge");
                if (maximumShortEdge < 0) throw new IllegalArgumentException("Negative surface limit");
            } catch (Exception error) {
                Log.w(TAG, "Invalid Android host config; using 720p surface limit", error);
                maximumShortEdge = 720;
            }
        }
        return maximumShortEdge;
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        nativeOnResume();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
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
        } catch (Exception error) {
            Log.w(TAG, "Failed to set immersive sticky fullscreen", error);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        nativeOnPause();
        if (mWindroidOverlay != null) {
            mWindroidOverlay.releaseAll();
        } else if (mInputTarget != null) {
            mInputTarget.releaseAll();
        }
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (mInputTarget != null) {
            int keyCode = event.getKeyCode();
            if (keyCode == android.view.KeyEvent.KEYCODE_BACK) {
                int source = event.getSource();
                if ((source & android.view.InputDevice.SOURCE_GAMEPAD) != android.view.InputDevice.SOURCE_GAMEPAD &&
                    (source & android.view.InputDevice.SOURCE_JOYSTICK) != android.view.InputDevice.SOURCE_JOYSTICK) {
                    return super.dispatchKeyEvent(event);
                }
            }
            int hidMask = getHidMaskForKeyCode(keyCode);
            if (hidMask != 0) {
                if (event.getAction() == android.view.KeyEvent.ACTION_DOWN) {
                    mInputTarget.button(hidMask, true);
                    return true;
                } else if (event.getAction() == android.view.KeyEvent.ACTION_UP) {
                    mInputTarget.button(hidMask, false);
                    return true;
                }
            }
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean onGenericMotionEvent(android.view.MotionEvent event) {
        if (mInputTarget != null && ((event.getSource() & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK ||
                                     (event.getSource() & android.view.InputDevice.SOURCE_GAMEPAD) == android.view.InputDevice.SOURCE_GAMEPAD)) {
            if (event.getAction() == android.view.MotionEvent.ACTION_MOVE) {
                float x = event.getAxisValue(android.view.MotionEvent.AXIS_X);
                float y = event.getAxisValue(android.view.MotionEvent.AXIS_Y);
                if (Math.abs(x) < 0.15f) x = 0.0f;
                if (Math.abs(y) < 0.15f) y = 0.0f;
                mInputTarget.circlePad(Math.max(-1.0f, Math.min(1.0f, x)), Math.max(-1.0f, Math.min(1.0f, -y)));

                float rx = event.getAxisValue(android.view.MotionEvent.AXIS_Z);
                float ry = event.getAxisValue(android.view.MotionEvent.AXIS_RZ);
                if (rx == 0.0f && ry == 0.0f) {
                    rx = event.getAxisValue(android.view.MotionEvent.AXIS_RX);
                    ry = event.getAxisValue(android.view.MotionEvent.AXIS_RY);
                }
                if (Math.abs(rx) < 0.15f) rx = 0.0f;
                if (Math.abs(ry) < 0.15f) ry = 0.0f;
                mInputTarget.cStick(Math.max(-1.0f, Math.min(1.0f, rx)), Math.max(-1.0f, Math.min(1.0f, -ry)));

                float hatX = event.getAxisValue(android.view.MotionEvent.AXIS_HAT_X);
                float hatY = event.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y);
                mInputTarget.button(1 << 5, hatX < -0.5f); // DPAD_LEFT
                mInputTarget.button(1 << 4, hatX > 0.5f);  // DPAD_RIGHT
                mInputTarget.button(1 << 6, hatY < -0.5f); // DPAD_UP
                mInputTarget.button(1 << 7, hatY > 0.5f);  // DPAD_DOWN

                float lTrigger = event.getAxisValue(android.view.MotionEvent.AXIS_LTRIGGER);
                if (lTrigger == 0.0f) lTrigger = event.getAxisValue(android.view.MotionEvent.AXIS_BRAKE);
                float rTrigger = event.getAxisValue(android.view.MotionEvent.AXIS_RTRIGGER);
                if (rTrigger == 0.0f) rTrigger = event.getAxisValue(android.view.MotionEvent.AXIS_GAS);
                if (lTrigger > 0.5f) mInputTarget.button(1 << 14, true);
                if (rTrigger > 0.5f) mInputTarget.button(1 << 15, true);

                return true;
            }
        }
        return super.onGenericMotionEvent(event);
    }

    private static int getHidMaskForKeyCode(int keyCode) {
        switch (keyCode) {
            case android.view.KeyEvent.KEYCODE_BUTTON_A:
                return 1 << 0;
            case android.view.KeyEvent.KEYCODE_BUTTON_B:
                return 1 << 1;
            case android.view.KeyEvent.KEYCODE_BUTTON_SELECT:
            case android.view.KeyEvent.KEYCODE_BACK:
                return 1 << 2;
            case android.view.KeyEvent.KEYCODE_BUTTON_START:
            case android.view.KeyEvent.KEYCODE_MENU:
                return 1 << 3;
            case android.view.KeyEvent.KEYCODE_DPAD_RIGHT:
                return 1 << 4;
            case android.view.KeyEvent.KEYCODE_DPAD_LEFT:
                return 1 << 5;
            case android.view.KeyEvent.KEYCODE_DPAD_UP:
                return 1 << 6;
            case android.view.KeyEvent.KEYCODE_DPAD_DOWN:
                return 1 << 7;
            case android.view.KeyEvent.KEYCODE_BUTTON_R1:
                return 1 << 8;
            case android.view.KeyEvent.KEYCODE_BUTTON_L1:
                return 1 << 9;
            case android.view.KeyEvent.KEYCODE_BUTTON_X:
                return 1 << 10;
            case android.view.KeyEvent.KEYCODE_BUTTON_Y:
                return 1 << 11;
            case android.view.KeyEvent.KEYCODE_BUTTON_L2:
                return 1 << 14;
            case android.view.KeyEvent.KEYCODE_BUTTON_R2:
                return 1 << 15;
            default:
                return 0;
        }
    }

    private void exportHudLayoutFromXml(File root) {
        try {
            android.view.View hudView = getLayoutInflater().inflate(R.layout.hud_gameplay_layout, null);
            if (hudView == null) return;

            android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
            int screenWidth = Math.max(dm.widthPixels, dm.heightPixels);
            int screenHeight = Math.min(dm.widthPixels, dm.heightPixels);

            hudView.setLayoutParams(new android.widget.RelativeLayout.LayoutParams(screenWidth, screenHeight));
            hudView.measure(
                android.view.View.MeasureSpec.makeMeasureSpec(screenWidth, android.view.View.MeasureSpec.EXACTLY),
                android.view.View.MeasureSpec.makeMeasureSpec(screenHeight, android.view.View.MeasureSpec.EXACTLY)
            );
            hudView.layout(0, 0, screenWidth, screenHeight);

            // Compute scaling to 400x240 OoT3D top-screen canvas
            float scale = (float) screenHeight / 240.0f;
            float offsetX = ((float) screenWidth - 400.0f * scale) / 2.0f;
            float offsetY = 0.0f;
            if (offsetX < 0) {
                scale = (float) screenWidth / 400.0f;
                offsetX = 0.0f;
                offsetY = ((float) screenHeight - 240.0f * scale) / 2.0f;
            }

            JSONObject hudJson = new JSONObject();
            hudJson.put("version", 1);
            hudJson.put("screen_width", screenWidth);
            hudJson.put("screen_height", screenHeight);

            exportViewToCanvas(hudView, R.id.hud_btn_a, "btn_a", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_btn_b, "btn_b", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_btn_x, "btn_x", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_btn_y, "btn_y", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_btn_zr, "btn_zr", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_btn_zl, "btn_zl", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_diamond_cluster, "diamond_cluster", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_top_left_status, "status", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_bottom_left_collectibles, "rupees", hudJson, scale, offsetX, offsetY);
            exportViewToCanvas(hudView, R.id.hud_minimap_container, "minimap", hudJson, scale, offsetX, offsetY);

            File targetFile = new File(root, "custom_hud_layout.json");
            try (java.io.FileOutputStream fos = new java.io.FileOutputStream(targetFile)) {
                fos.write(hudJson.toString(2).getBytes(StandardCharsets.UTF_8));
                fos.flush();
            }
            Log.i(TAG, "Exported custom HUD layout from XML to " + targetFile.getAbsolutePath() + ": " + hudJson.toString());
        } catch (Throwable t) {
            Log.e(TAG, "Failed to export HUD layout from XML", t);
        }
    }

    private void exportViewToCanvas(android.view.View root, int viewId, String key, JSONObject out,
                                    float scale, float offsetX, float offsetY) {
        android.view.View target = root.findViewById(viewId);
        if (target == null) return;
        float x = target.getLeft();
        float y = target.getTop();
        android.view.View parent = (android.view.View) target.getParent();
        while (parent != null && parent != root) {
            x += parent.getLeft();
            y += parent.getTop();
            parent = (parent.getParent() instanceof android.view.View) ? (android.view.View) parent.getParent() : null;
        }
        float w = target.getWidth() > 0 ? target.getWidth() : target.getMeasuredWidth();
        float h = target.getHeight() > 0 ? target.getHeight() : target.getMeasuredHeight();

        float rawCanvasX = (x - offsetX) / scale;
        float rawCanvasY = (y - offsetY) / scale;
        float canvasW = w / scale;
        float canvasH = h / scale;

        float canvasX = Math.max(0.0f, Math.min(400.0f - canvasW, rawCanvasX));
        float canvasY = Math.max(0.0f, Math.min(240.0f - canvasH, rawCanvasY));

        try {
            JSONObject obj = new JSONObject();
            obj.put("x", canvasX);
            obj.put("y", canvasY);
            obj.put("width", canvasW);
            obj.put("height", canvasH);
            out.put(key, obj);
        } catch (Exception ignored) {}
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // Clean process termination to prevent dirty static globals from persisting
        // across consecutive app launches on Android Bionic.
        Process.killProcess(Process.myPid());
    }
}
