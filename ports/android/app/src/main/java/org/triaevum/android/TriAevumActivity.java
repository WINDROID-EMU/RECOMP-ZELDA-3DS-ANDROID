package org.triaevum.android;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import org.json.JSONObject;
import org.triaevum.android.controls.WindroidVirtualControllerView;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import android.view.ViewGroup;
import android.view.WindowManager;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

/** SDL owns Surface/lifecycle. Game, renderer and title loading retain their native owners. */
public final class TriAevumActivity extends SDLActivity {
    private WindroidVirtualControllerView mWindroidOverlay;
    private AndroidNativeInputTarget mInputTarget;

    @Override protected SDLSurface createSDLSurface(Context context) {
        int maximumShortEdge = 720;
        File config = new File(getExternalFilesDir(null), "TriAevum.android.host.json");
        if (config.isFile()) {
            try {
                maximumShortEdge = new JSONObject(new String(Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8))
                    .getInt("maximum_surface_short_edge");
                if (maximumShortEdge < 0) throw new IllegalArgumentException("Negative surface limit");
            } catch (Exception error) {
                Log.w("TriAevum", "Invalid Android host config; using 720p surface limit", error);
                maximumShortEdge = 720;
            }
        }
        return new TriAevumSurface(context, maximumShortEdge);
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // Enable edge-to-edge layout across the entire physical display including camera cutouts
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        hideSystemBars();

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
                Log.w("TriAevum", "Failed to sync initial control settings", t);
            }

            // Open the Zenda-style settings dialog when the gear icon is tapped
            mWindroidOverlay.setOnSettingsClickListener(() -> {
                if (!isFinishing() && !isDestroyed()) {
                    try {
                        new TriAevumConfigDialog(this, mWindroidOverlay).show();
                    } catch (Exception err) {
                        Log.e("TriAevum", "Failed to open settings dialog", err);
                    }
                }
            });

            ViewGroup.LayoutParams lp = new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
            mLayout.addView(mWindroidOverlay, lp);
            Log.i("TriAevum", "Windroid virtual controller overlay initialized successfully");
        } catch (Exception error) {
            Log.e("TriAevum", "Failed to initialize Windroid virtual controller overlay", error);
        }

    }

    @Override protected void onResume() {
        super.onResume();
        hideSystemBars();
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
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
            Log.w("TriAevum", "Failed to set immersive sticky fullscreen", error);
        }
    }

    @Override protected void onPause() {
        super.onPause();
        if (mWindroidOverlay != null) {
            mWindroidOverlay.releaseAll();
        } else if (mInputTarget != null) {
            mInputTarget.releaseAll();
        }
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        // Clean process termination to prevent dirty static globals from persisting
        // across consecutive app launches on Android Bionic.
        Process.killProcess(Process.myPid());
    }

    @Override protected String[] getLibraries() {
        return new String[] { "SDL2", "triaevum_title_bootstrap", "TriAevum" };
    }

    @Override protected String[] getArguments() {
        File root = getExternalFilesDir(null);
        return new String[] {
            "--launch-profile", new File(root, "TriAevum.android.launch.json").getAbsolutePath(),
            "--title-plugin", new File(getApplicationInfo().nativeLibraryDir,
                "libtriaevum_title_aot.so").getAbsolutePath()
        };
    }
}
