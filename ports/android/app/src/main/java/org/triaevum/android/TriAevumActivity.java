package org.triaevum.android;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import android.view.Gravity;
import android.view.Surface;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

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
            File customJson = new File(root, "custom_hud_layout.json");
            // O XML é sempre a fonte de verdade: regenera o JSON padrão a cada
            // inicialização, exceto quando o usuário salvou um layout customizado.
            if (!customJson.exists() || !isUserCustomizedHudLayout(customJson)) {
                exportHudLayoutFromXml(root);
            }
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

    private boolean isUserCustomizedHudLayout(File jsonFile) {
        try (java.io.FileInputStream fis = new java.io.FileInputStream(jsonFile)) {
            byte[] data = new byte[(int) jsonFile.length()];
            //noinspection ResultOfMethodCallIgnored
            fis.read(data);
            org.json.JSONObject j = new org.json.JSONObject(new String(data, java.nio.charset.StandardCharsets.UTF_8));
            return j.optBoolean("user_customized", false);
        } catch (Throwable ignored) {
            return false;
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
            exportViewToCanvas(hudView, R.id.hud_dpad_item_cluster, "dpad_items", hudJson, scale, offsetX, offsetY);

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
        float x = target.getLeft() + target.getTranslationX();
        float y = target.getTop() + target.getTranslationY();
        android.view.View parent = (android.view.View) target.getParent();
        while (parent != null && parent != root) {
            x += parent.getLeft() + parent.getTranslationX();
            y += parent.getTop() + parent.getTranslationY();
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

    public void openHudLayoutEditor() {
        File root = getExternalFilesDir(null);
        if (root == null) return;
        HudLayoutEditorOverlay editor = new HudLayoutEditorOverlay(this, root, new HudLayoutEditorOverlay.Callback() {
            @Override
            public void onSaved(HudLayoutEditorOverlay overlay) {
                mLayout.removeView(overlay);
            }

            @Override
            public void onCancelled(HudLayoutEditorOverlay overlay) {
                mLayout.removeView(overlay);
            }
        });
        mLayout.addView(editor, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    public void restoreDefaultHudLayout() {
        File root = getExternalFilesDir(null);
        if (root != null) {
            exportHudLayoutFromXml(root);
            android.widget.Toast.makeText(this, "Layout do HUD restaurado para o padrão!", android.widget.Toast.LENGTH_SHORT).show();
        }
    }

    public void openVirtualControlsEditor() {
        if (mWindroidOverlay == null) return;
        mWindroidOverlay.startEditing();

        // Barra de ferramentas superior para o editor de controles virtuais
        final LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(Color.parseColor("#EE0F141C"));
        int padH = Math.round(12f * getResources().getDisplayMetrics().density);
        int padV = Math.round(6f * getResources().getDisplayMetrics().density);
        bar.setPadding(padH, padV, padH, padV);

        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.TOP;
        bar.setLayoutParams(lp);

        // Título e subtítulo
        LinearLayout titleBox = new LinearLayout(this);
        titleBox.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        titleBox.setLayoutParams(titleLp);

        TextView title = new TextView(this);
        title.setText("🎮 Editor de Controles Virtuais");
        title.setTextColor(Color.parseColor("#FFC107"));
        title.setTextSize(13);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        titleBox.addView(title);

        TextView sub = new TextView(this);
        sub.setText("Toque e arraste os botões para onde quiser na tela");
        sub.setTextColor(Color.parseColor("#8B9BB4"));
        sub.setTextSize(10);
        titleBox.addView(sub);
        bar.addView(titleBox);

        int btnH = Math.round(34f * getResources().getDisplayMetrics().density);
        int marginR = Math.round(6f * getResources().getDisplayMetrics().density);

        // Botão Restaurar Padrão
        Button btnReset = new Button(this);
        btnReset.setText("↺ Padrão");
        btnReset.setTextColor(Color.WHITE);
        btnReset.setTextSize(11);
        btnReset.setBackgroundResource(R.drawable.btn_dark);
        LinearLayout.LayoutParams btnResetLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, btnH);
        btnResetLp.rightMargin = marginR;
        btnReset.setLayoutParams(btnResetLp);
        btnReset.setPadding(padH, 0, padH, 0);
        btnReset.setOnClickListener(v -> {
            mWindroidOverlay.resetControlPositions();
            android.widget.Toast.makeText(this, "Posições dos controles restauradas para o padrão!", android.widget.Toast.LENGTH_SHORT).show();
        });
        bar.addView(btnReset);

        // Botão Cancelar
        Button btnCancel = new Button(this);
        btnCancel.setText("Cancelar");
        btnCancel.setTextColor(Color.WHITE);
        btnCancel.setTextSize(11);
        btnCancel.setBackgroundResource(R.drawable.btn_dark);
        LinearLayout.LayoutParams btnCancelLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, btnH);
        btnCancelLp.rightMargin = marginR;
        btnCancel.setLayoutParams(btnCancelLp);
        btnCancel.setPadding(padH, 0, padH, 0);
        btnCancel.setOnClickListener(v -> {
            mWindroidOverlay.cancelEditing();
            mLayout.removeView(bar);
        });
        bar.addView(btnCancel);

        // Botão Salvar
        Button btnSave = new Button(this);
        btnSave.setText("💾 Salvar");
        btnSave.setTextColor(Color.parseColor("#1A1500"));
        btnSave.setTextSize(11);
        btnSave.setTypeface(null, android.graphics.Typeface.BOLD);
        btnSave.setBackgroundResource(R.drawable.btn_gold);
        LinearLayout.LayoutParams btnSaveLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, btnH);
        btnSave.setLayoutParams(btnSaveLp);
        btnSave.setPadding(Math.round(14f * getResources().getDisplayMetrics().density), 0, Math.round(14f * getResources().getDisplayMetrics().density), 0);
        btnSave.setOnClickListener(v -> {
            mWindroidOverlay.saveControlPositions();
            mLayout.removeView(bar);
            android.widget.Toast.makeText(this, "Posições dos controles salvas com sucesso!", android.widget.Toast.LENGTH_SHORT).show();
        });
        bar.addView(btnSave);

        mLayout.addView(bar);
    }

    // -------------------------------------------------------------------------
    // Save Data Export & Import (Storage Access Framework)
    // -------------------------------------------------------------------------

    public static final int REQUEST_CODE_EXPORT_SAVE = 2001;
    public static final int REQUEST_CODE_IMPORT_SAVE = 2002;

    public interface SaveActionListener {
        void onSaveOperationCompleted();
    }

    private SaveActionListener mSaveActionListener;

    public void setSaveActionListener(SaveActionListener listener) {
        mSaveActionListener = listener;
    }

    public void startExportSaveFlow(SaveActionListener listener) {
        mSaveActionListener = listener;
        try {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/zip");
            String timestamp = new java.text.SimpleDateFormat("yyyyMMdd_HHmmss", java.util.Locale.US).format(new java.util.Date());
            intent.putExtra(Intent.EXTRA_TITLE, "TriAevum_Save_" + timestamp + ".zip");
            startActivityForResult(intent, REQUEST_CODE_EXPORT_SAVE);
        } catch (Exception e) {
            Log.e(TAG, "Falha ao iniciar seletor de exportação de save", e);
            android.widget.Toast.makeText(this, "Erro ao abrir seletor de arquivo: " + e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
        }
    }

    public void startImportSaveFlow(SaveActionListener listener) {
        mSaveActionListener = listener;
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/zip",
                "application/x-zip-compressed",
                "application/octet-stream",
                "*/*"
            });
            startActivityForResult(intent, REQUEST_CODE_IMPORT_SAVE);
        } catch (Exception e) {
            Log.e(TAG, "Falha ao iniciar seletor de importação de save", e);
            android.widget.Toast.makeText(this, "Erro ao abrir seletor de arquivo: " + e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }

        Uri uri = data.getData();
        if (requestCode == REQUEST_CODE_EXPORT_SAVE) {
            android.widget.Toast.makeText(this, "Exportando save...", android.widget.Toast.LENGTH_SHORT).show();
            TriAevumSaveManager.exportSaveToUri(this, uri, new TriAevumSaveManager.SaveCallback<String>() {
                @Override
                public void onSuccess(String result) {
                    android.widget.Toast.makeText(TriAevumActivity.this, "✅ " + result, android.widget.Toast.LENGTH_LONG).show();
                    if (mSaveActionListener != null) {
                        mSaveActionListener.onSaveOperationCompleted();
                    }
                }

                @Override
                public void onError(Exception error) {
                    android.widget.Toast.makeText(TriAevumActivity.this, "❌ Erro ao exportar save: " + error.getMessage(), android.widget.Toast.LENGTH_LONG).show();
                }
            });
        } else if (requestCode == REQUEST_CODE_IMPORT_SAVE) {
            android.widget.Toast.makeText(this, "Importando save...", android.widget.Toast.LENGTH_SHORT).show();
            TriAevumSaveManager.importSaveFromUri(this, uri, new TriAevumSaveManager.SaveCallback<Integer>() {
                @Override
                public void onSuccess(Integer count) {
                    android.widget.Toast.makeText(TriAevumActivity.this, "✅ Save importado com sucesso (" + count + " arquivos)!", android.widget.Toast.LENGTH_LONG).show();
                    if (mSaveActionListener != null) {
                        mSaveActionListener.onSaveOperationCompleted();
                    }
                }

                @Override
                public void onError(Exception error) {
                    android.widget.Toast.makeText(TriAevumActivity.this, "❌ Erro ao importar save: " + error.getMessage(), android.widget.Toast.LENGTH_LONG).show();
                }
            });
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
