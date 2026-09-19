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
                mWindroidOverlay.setShowControls(showOverlay);
                mWindroidOverlay.setHapticFeedbackEnabled(haptic);
                AndroidNativeInputTarget.nativeSwapScreens(swapScreens);
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
    protected void onDestroy() {
        super.onDestroy();
        // Clean process termination to prevent dirty static globals from persisting
        // across consecutive app launches on Android Bionic.
        Process.killProcess(Process.myPid());
    }
}
