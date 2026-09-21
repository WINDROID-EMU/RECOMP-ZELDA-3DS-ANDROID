package org.triaevum.android;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import org.triaevum.android.controls.WindroidVirtualControllerView;

/**
 * Full-screen settings dialog styled after WINDROID-EMU/Zenda.
 *
 * Opens as an in-process Dialog (not a separate Activity) so the Vulkan
 * swapchain surface is never destroyed during configuration.
 *
 * NOTE: Does NOT touch EmulationMenuSettings (Kotlin object) at construction
 * time — that class uses OverlayHost.appContext in its static initialiser and
 * will throw if the overlay host is not yet bound.  All overlay preferences
 * are accessed directly via SharedPreferences with the same key names.
 */
public final class TriAevumConfigDialog extends Dialog {

    private static final String TAG = "TriAevumCfg";
    // PreferenceManager.getDefaultSharedPreferences() uses "<packageName>_preferences"
    private static final String PREFS_NAME = "org.triaevum.android_preferences";
    private final TriAevumConfigManager mConfig;
    private final WindroidVirtualControllerView mOverlay;
    private SharedPreferences mPrefs;

    // Tabs
    private Button mTabGeral, mTabGraficos, mTabCamera, mTabControles;
    private Button mCurrentTab;

    // Panels
    private View mPanelGeral, mPanelGraficos, mPanelCamera, mPanelControles;

    // Geral
    private Spinner mSpLanguage;
    private Spinner mSpSurfaceRes;

    // Gráficos
    private Spinner mSpRenderScale;
    private Spinner mSpAAMode;
    private Spinner mSpFramerate;
    private CheckBox mCbVSync;
    private CheckBox mCbCustomTextures;

    // Câmera / HUD
    private CheckBox mCbFreeCamera;
    private SeekBar  mSbCamSpeed;
    private TextView mTvCamSpeedLabel;
    private CheckBox mCbCamInvertX;
    private CheckBox mCbCamInvertY;
    private Spinner  mSpHudLayout;
    private SeekBar  mSbHudScale;
    private TextView mTvHudScaleLabel;
    private SeekBar  mSbHudMarginX;
    private TextView mTvHudMarginXLabel;
    private SeekBar  mSbHudMarginY;
    private TextView mTvHudMarginYLabel;
    private CheckBox mCbMinimap;
    private CheckBox mCbDpadIcons;
    private CheckBox mCbItemsHint;

    // Controles
    private CheckBox mCbShowOverlay;
    private SeekBar  mSbOpacity;
    private TextView mTvOpacityLabel;
    private CheckBox mCbHaptic;
    private CheckBox mCbSwapScreens;
    private CheckBox mCbTouchScreen;
    private CheckBox mCbJoystickRelCenter;
    private CheckBox mCbDpadSlide;

    public TriAevumConfigDialog(Activity owner, WindroidVirtualControllerView overlay) {
        super(owner, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
        mConfig  = new TriAevumConfigManager(owner);
        mOverlay = overlay;
        // PreferenceManager.getDefaultSharedPreferences() → "<pkg>_preferences"
        mPrefs = owner.getApplicationContext()
            .getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        Log.d(TAG, "Dialog created. Prefs file: " + PREFS_NAME);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Stay fullscreen / immersive inside the dialog
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);

        setContentView(R.layout.dialog_config);

        // Ensure dialog fills the entire screen
        Window w = getWindow();
        if (w != null) {
            w.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        }

        bindViews();
        loadFromConfig();
        setupTabs();
        setupButtons();
    }

    // -------------------------------------------------------------------------
    // View binding
    // -------------------------------------------------------------------------

    private void bindViews() {
        mTabGeral    = findViewById(R.id.tab_geral);
        mTabGraficos = findViewById(R.id.tab_graficos);
        mTabCamera   = findViewById(R.id.tab_camera);
        mTabControles= findViewById(R.id.tab_controles);

        mPanelGeral    = findViewById(R.id.panel_geral);
        mPanelGraficos = findViewById(R.id.panel_graficos);
        mPanelCamera   = findViewById(R.id.panel_camera);
        mPanelControles= findViewById(R.id.panel_controles);

        // Geral
        mSpLanguage   = findViewById(R.id.sp_language);
        mSpSurfaceRes = findViewById(R.id.sp_surface_resolution);

        // Gráficos
        mSpRenderScale    = findViewById(R.id.sp_render_scale);
        mSpAAMode         = findViewById(R.id.sp_aa_mode);
        mSpFramerate      = findViewById(R.id.sp_framerate);
        mCbVSync          = findViewById(R.id.cb_vsync);
        mCbCustomTextures = findViewById(R.id.cb_custom_textures);

        // Câmera / HUD
        mCbFreeCamera      = findViewById(R.id.cb_free_camera);
        mSbCamSpeed        = findViewById(R.id.sb_cam_speed);
        mTvCamSpeedLabel   = findViewById(R.id.tv_cam_speed_label);
        mCbCamInvertX      = findViewById(R.id.cb_cam_invert_x);
        mCbCamInvertY      = findViewById(R.id.cb_cam_invert_y);
        mSpHudLayout       = findViewById(R.id.sp_hud_layout);
        mSbHudScale        = findViewById(R.id.sb_hud_scale);
        mTvHudScaleLabel   = findViewById(R.id.tv_hud_scale_label);
        mSbHudMarginX      = findViewById(R.id.sb_hud_margin_x);
        mTvHudMarginXLabel = findViewById(R.id.tv_hud_margin_x_label);
        mSbHudMarginY      = findViewById(R.id.sb_hud_margin_y);
        mTvHudMarginYLabel = findViewById(R.id.tv_hud_margin_y_label);
        mCbMinimap         = findViewById(R.id.cb_minimap);
        mCbDpadIcons       = findViewById(R.id.cb_dpad_icons);
        mCbItemsHint       = findViewById(R.id.cb_items_hint);

        // Controles
        mCbShowOverlay       = findViewById(R.id.cb_show_overlay);
        mSbOpacity           = findViewById(R.id.sb_overlay_opacity);
        mTvOpacityLabel      = findViewById(R.id.tv_opacity_label);
        mCbHaptic            = findViewById(R.id.cb_haptic);
        mCbSwapScreens       = findViewById(R.id.cb_swap_screens);
        mCbTouchScreen       = findViewById(R.id.cb_touch_screen);
        mCbJoystickRelCenter = findViewById(R.id.cb_joystick_rel_center);
        mCbDpadSlide         = findViewById(R.id.cb_dpad_slide);
    }

    // -------------------------------------------------------------------------
    // Load current config into UI
    // -------------------------------------------------------------------------

    private void loadFromConfig() {
        Log.d(TAG, "--- loadFromConfig START ---");
        // Language
        setupSpinner(mSpLanguage, TriAevumConfigManager.LANGUAGE_LABELS, null);
        String langCode = mConfig.getLanguageCode();
        Log.d(TAG, "  language: " + langCode);
        for (int i = 0; i < TriAevumConfigManager.LANGUAGE_CODES.length; i++) {
            if (TriAevumConfigManager.LANGUAGE_CODES[i].equals(langCode)) {
                mSpLanguage.setSelection(i);
                break;
            }
        }

        // Surface resolution
        setupSpinner(mSpSurfaceRes, TriAevumConfigManager.SURFACE_RES_LABELS, null);
        int surfEdge = mConfig.getSurfaceMaxShortEdge();
        Log.d(TAG, "  surface_max_short_edge: " + surfEdge);
        for (int i = 0; i < TriAevumConfigManager.SURFACE_RES_VALUES.length; i++) {
            if (TriAevumConfigManager.SURFACE_RES_VALUES[i] == surfEdge) {
                mSpSurfaceRes.setSelection(i);
                break;
            }
        }

        // Render scale
        setupSpinner(mSpRenderScale, TriAevumConfigManager.RENDER_SCALE_LABELS, null);
        float rs = mConfig.getRenderScale();
        for (int i = 0; i < TriAevumConfigManager.RENDER_SCALE_VALUES.length; i++) {
            if (Math.abs(TriAevumConfigManager.RENDER_SCALE_VALUES[i] - rs) < 0.01f) {
                mSpRenderScale.setSelection(i);
                break;
            }
        }

        // AA
        setupSpinner(mSpAAMode, TriAevumConfigManager.AA_MODE_LABELS, null);
        String aaMode = mConfig.getAAMode();
        for (int i = 0; i < TriAevumConfigManager.AA_MODE_VALUES.length; i++) {
            if (TriAevumConfigManager.AA_MODE_VALUES[i].equals(aaMode)) {
                mSpAAMode.setSelection(i);
                break;
            }
        }

        // Framerate
        setupSpinner(mSpFramerate, TriAevumConfigManager.FRAMERATE_LABELS, null);
        String frMode = mConfig.getFrameRateMode();
        for (int i = 0; i < TriAevumConfigManager.FRAMERATE_VALUES.length; i++) {
            if (TriAevumConfigManager.FRAMERATE_VALUES[i].equals(frMode)) {
                mSpFramerate.setSelection(i);
                break;
            }
        }

        mCbVSync.setChecked(mConfig.isVSync());
        mCbCustomTextures.setChecked(mConfig.isCustomTexturesEnabled());
        Log.d(TAG, "  renderScale=" + mConfig.getRenderScale()
            + " AA=" + mConfig.getAAMode()
            + " FR=" + mConfig.getFrameRateMode()
            + " vsync=" + mConfig.isVSync()
            + " customTex=" + mConfig.isCustomTexturesEnabled());

        // Camera / HUD
        mCbFreeCamera.setChecked(mConfig.isFreeCameraEnabled());
        int speedLevel = mConfig.getFreeCameraSpeedLevel();
        mSbCamSpeed.setProgress(speedLevel - 1); // 0-4 → levels 1-5
        mTvCamSpeedLabel.setText("Velocidade: " + speedLevel);
        mCbCamInvertX.setChecked(mConfig.isFreeCameraInvertX());
        mCbCamInvertY.setChecked(mConfig.isFreeCameraInvertY());

        // HUD Layout
        setupSpinner(mSpHudLayout, TriAevumConfigManager.HUD_LAYOUT_LABELS, null);
        String hudLayout = mConfig.getHudLayout();
        for (int i = 0; i < TriAevumConfigManager.HUD_LAYOUT_VALUES.length; i++) {
            if (TriAevumConfigManager.HUD_LAYOUT_VALUES[i].equalsIgnoreCase(hudLayout)) {
                mSpHudLayout.setSelection(i);
                break;
            }
        }

        float hudScale = mConfig.getHudScale();
        int hudPct = Math.round(hudScale * 100);
        // map 0.5-1.5 → seekbar 0-10
        int hudProg = Math.round((hudScale - 0.5f) / 0.1f);
        mSbHudScale.setProgress(Math.max(0, Math.min(10, hudProg)));
        mTvHudScaleLabel.setText("Escala HUD: " + hudPct + "%");

        // Margins (-3..16 -> seekbar 0..19)
        int marginX = mConfig.getHudMarginX();
        int marginY = mConfig.getHudMarginY();
        mSbHudMarginX.setProgress(Math.max(0, Math.min(19, marginX + 3)));
        mTvHudMarginXLabel.setText("Margem X (Horizontal): " + marginX);
        mSbHudMarginY.setProgress(Math.max(0, Math.min(19, marginY + 3)));
        mTvHudMarginYLabel.setText("Margem Y (Vertical): " + marginY);

        mCbMinimap.setChecked(mConfig.isMinimapVisible());
        mCbDpadIcons.setChecked(mConfig.isRenderDpadIcons());
        mCbItemsHint.setChecked(mConfig.isRenderItemsHint());
        Log.d(TAG, "  freeCamera=" + mConfig.isFreeCameraEnabled()
            + " speed=" + mConfig.getFreeCameraSpeedLevel()
            + " invertX=" + mConfig.isFreeCameraInvertX()
            + " invertY=" + mConfig.isFreeCameraInvertY()
            + " hudLayout=" + mConfig.getHudLayout()
            + " hudMarginX=" + marginX
            + " hudMarginY=" + marginY
            + " hudScale=" + mConfig.getHudScale()
            + " minimap=" + mConfig.isMinimapVisible()
            + " dpadIcons=" + mConfig.isRenderDpadIcons()
            + " itemsHint=" + mConfig.isRenderItemsHint());

        // Controls — read directly from SharedPreferences (same keys as EmulationMenuSettings)
        boolean showOverlay = (mOverlay != null) ? mOverlay.isShowControls() : mPrefs.getBoolean("EmulationMenuSettings_ShowOverlay", true);
        boolean haptic      = (mOverlay != null) ? mOverlay.isHapticFeedbackEnabled() : mPrefs.getBoolean("EmulationMenuSettings_HapticFeedback", true);
        boolean swapScr     = mPrefs.getBoolean("EmulationMenuSettings_SwapScreens", false);
        boolean touchScreen = (mOverlay != null) ? mOverlay.isTouchEnabled() : mPrefs.getBoolean("EmulationMenuSettings_TouchEnabled", false);
        boolean joyRel      = mPrefs.getBoolean("EmulationMenuSettings_JoystickRelCenter", true);
        boolean dpadSlide   = mPrefs.getBoolean("EmulationMenuSettings_DpadSlideEnable", true);
        Log.d(TAG, "  [prefs=" + PREFS_NAME + "]"
            + " showOverlay=" + showOverlay
            + " haptic=" + haptic
            + " swapScreens=" + swapScr
            + " touchScreen=" + touchScreen
            + " joystickRelCenter=" + joyRel
            + " dpadSlide=" + dpadSlide);
        mCbShowOverlay.setChecked(showOverlay);
        mCbHaptic.setChecked(haptic);
        mCbSwapScreens.setChecked(swapScr);
        mCbTouchScreen.setChecked(touchScreen);
        mCbJoystickRelCenter.setChecked(joyRel);
        mCbDpadSlide.setChecked(dpadSlide);

        // Opacity from overlay
        int overlayOpacity = 100;
        if (mOverlay != null) overlayOpacity = mOverlay.getOverlayOpacityPercent();
        mSbOpacity.setProgress(overlayOpacity);
        mTvOpacityLabel.setText("Opacidade dos Controles: " + overlayOpacity + "%");

        // SeekBar live listeners
        mSbCamSpeed.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                mTvCamSpeedLabel.setText("Velocidade: " + (progress + 1));
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });

        mSbHudScale.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                float scale = 0.5f + progress * 0.1f;
                mTvHudScaleLabel.setText("Escala HUD: " + Math.round(scale * 100) + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });

        mSbHudMarginX.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                int val = progress - 3;
                mTvHudMarginXLabel.setText("Margem X (Horizontal): " + val);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });

        mSbHudMarginY.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                int val = progress - 3;
                mTvHudMarginYLabel.setText("Margem Y (Vertical): " + val);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });

        mSbOpacity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                int pct = Math.max(20, progress); // minimum 20%
                mTvOpacityLabel.setText("Opacidade dos Controles: " + pct + "%");
                if (fromUser && mOverlay != null) {
                    mOverlay.setOverlayOpacityPercent(pct);
                }
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });
    }

    // -------------------------------------------------------------------------
    // Tab navigation
    // -------------------------------------------------------------------------

    private void setupTabs() {
        mCurrentTab = mTabGeral;

        mTabGeral.setOnClickListener(v -> selectTab(mTabGeral, mPanelGeral));
        mTabGraficos.setOnClickListener(v -> selectTab(mTabGraficos, mPanelGraficos));
        mTabCamera.setOnClickListener(v -> selectTab(mTabCamera, mPanelCamera));
        mTabControles.setOnClickListener(v -> selectTab(mTabControles, mPanelControles));
    }

    private void selectTab(Button tab, View panel) {
        // Deselect previous
        if (mCurrentTab != null) {
            mCurrentTab.setTextColor(Color.parseColor("#8B9BB4"));
            mCurrentTab.setBackgroundResource(R.drawable.btn_dark);
        }
        // Select new
        tab.setTextColor(Color.parseColor("#FFD700"));
        tab.setBackgroundResource(R.drawable.tab_selected_bg);
        mCurrentTab = tab;

        // Hide all panels then show selected
        mPanelGeral.setVisibility(View.GONE);
        mPanelGraficos.setVisibility(View.GONE);
        mPanelCamera.setVisibility(View.GONE);
        mPanelControles.setVisibility(View.GONE);
        panel.setVisibility(View.VISIBLE);
    }

    // -------------------------------------------------------------------------
    // Header buttons
    // -------------------------------------------------------------------------

    private void setupButtons() {
        Button btnSave    = findViewById(R.id.btn_config_save);
        Button btnClose   = findViewById(R.id.btn_config_close);
        Button btnRestore = findViewById(R.id.btn_config_restore);

        btnSave.setOnClickListener(v -> {
            saveToConfig();
            mConfig.applyLiveSettings();
            Toast.makeText(getContext(), "Configurações salvas e aplicadas!", Toast.LENGTH_SHORT).show();
            dismiss();
        });

        btnClose.setOnClickListener(v -> dismiss());

        btnRestore.setOnClickListener(v -> {
            mConfig.restoreDefaults();
            mConfig.applyLiveSettings();
            mPrefs.edit()
                .putBoolean("EmulationMenuSettings_ShowOverlay", true)
                .putBoolean("EmulationMenuSettings_HapticFeedback", true)
                .putBoolean("EmulationMenuSettings_SwapScreens", false)
                .putBoolean("EmulationMenuSettings_TouchEnabled", false)
                .putBoolean("EmulationMenuSettings_JoystickRelCenter", true)
                .putBoolean("EmulationMenuSettings_DpadSlideEnable", true)
                .apply();
            if (mOverlay != null) {
                mOverlay.setShowControls(true);
                mOverlay.setHapticFeedbackEnabled(true);
                mOverlay.setTouchEnabled(false);
                mOverlay.setOverlayOpacityPercent(100);
            }
            try {
                AndroidNativeInputTarget.nativeSwapScreens(false);
                AndroidNativeInputTarget.nativeSetTouchEnabled(false);
            } catch (Throwable t) {
                Log.w(TAG, "Failed to reset native control settings", t);
            }
            loadFromConfig();
            Toast.makeText(getContext(), "Padrões restaurados e aplicados.", Toast.LENGTH_SHORT).show();
        });
    }

    // -------------------------------------------------------------------------
    // Save
    // -------------------------------------------------------------------------

    private void saveToConfig() {
        Log.d(TAG, "--- saveToConfig START ---");
        // Geral
        int langIdx = mSpLanguage.getSelectedItemPosition();
        if (langIdx >= 0 && langIdx < TriAevumConfigManager.LANGUAGE_CODES.length) {
            String lang = TriAevumConfigManager.LANGUAGE_CODES[langIdx];
            Log.d(TAG, "  SET language -> " + lang);
            mConfig.setLanguageCode(lang);
        }

        int resIdx = mSpSurfaceRes.getSelectedItemPosition();
        if (resIdx >= 0 && resIdx < TriAevumConfigManager.SURFACE_RES_VALUES.length) {
            int edge = TriAevumConfigManager.SURFACE_RES_VALUES[resIdx];
            Log.d(TAG, "  SET surfaceMaxShortEdge -> " + edge);
            mConfig.setSurfaceMaxShortEdge(edge);
        }

        // Gráficos
        int rsIdx = mSpRenderScale.getSelectedItemPosition();
        if (rsIdx >= 0 && rsIdx < TriAevumConfigManager.RENDER_SCALE_VALUES.length) {
            float rs = TriAevumConfigManager.RENDER_SCALE_VALUES[rsIdx];
            Log.d(TAG, "  SET renderScale -> " + rs);
            mConfig.setRenderScale(rs);
        }

        int aaIdx = mSpAAMode.getSelectedItemPosition();
        if (aaIdx >= 0 && aaIdx < TriAevumConfigManager.AA_MODE_VALUES.length) {
            String aa = TriAevumConfigManager.AA_MODE_VALUES[aaIdx];
            Log.d(TAG, "  SET AA -> " + aa);
            mConfig.setAAMode(aa);
        }

        int frIdx = mSpFramerate.getSelectedItemPosition();
        if (frIdx >= 0 && frIdx < TriAevumConfigManager.FRAMERATE_VALUES.length) {
            String fr = TriAevumConfigManager.FRAMERATE_VALUES[frIdx];
            Log.d(TAG, "  SET frameRate -> " + fr);
            mConfig.setFrameRateMode(fr);
        }

        Log.d(TAG, "  SET vsync -> " + mCbVSync.isChecked());
        mConfig.setVSync(mCbVSync.isChecked());
        Log.d(TAG, "  SET customTextures -> " + mCbCustomTextures.isChecked());
        mConfig.setCustomTexturesEnabled(mCbCustomTextures.isChecked());

        // Câmera / HUD
        Log.d(TAG, "  SET freeCamera -> " + mCbFreeCamera.isChecked());
        mConfig.setFreeCameraEnabled(mCbFreeCamera.isChecked());
        Log.d(TAG, "  SET camSpeedLevel -> " + (mSbCamSpeed.getProgress() + 1));
        mConfig.setFreeCameraSpeedLevel(mSbCamSpeed.getProgress() + 1);
        mConfig.setFreeCameraInvertX(mCbCamInvertX.isChecked());
        mConfig.setFreeCameraInvertY(mCbCamInvertY.isChecked());
        int hudLayoutIdx = mSpHudLayout.getSelectedItemPosition();
        if (hudLayoutIdx >= 0 && hudLayoutIdx < TriAevumConfigManager.HUD_LAYOUT_VALUES.length) {
            String hl = TriAevumConfigManager.HUD_LAYOUT_VALUES[hudLayoutIdx];
            Log.d(TAG, "  SET hudLayout -> " + hl);
            mConfig.setHudLayout(hl);
        }

        float hudScale = 0.5f + mSbHudScale.getProgress() * 0.1f;
        Log.d(TAG, "  SET hudScale -> " + hudScale);
        mConfig.setHudScale(hudScale);

        int mx = mSbHudMarginX.getProgress() - 3;
        int my = mSbHudMarginY.getProgress() - 3;
        Log.d(TAG, "  SET hudMarginX -> " + mx + ", hudMarginY -> " + my);
        mConfig.setHudMarginX(mx);
        mConfig.setHudMarginY(my);

        mConfig.setMinimapVisible(mCbMinimap.isChecked());
        mConfig.setRenderDpadIcons(mCbDpadIcons.isChecked());
        mConfig.setRenderItemsHint(mCbItemsHint.isChecked());

        // Controles — write directly to SharedPreferences (same keys as EmulationMenuSettings)
        boolean showOverlay = mCbShowOverlay.isChecked();
        boolean haptic = mCbHaptic.isChecked();
        boolean swapScr = mCbSwapScreens.isChecked();
        boolean touchScreen = mCbTouchScreen.isChecked();
        boolean joyRel = mCbJoystickRelCenter.isChecked();
        boolean dpadSlide = mCbDpadSlide.isChecked();

        Log.d(TAG, "  saving controls: showOverlay=" + showOverlay
            + " haptic=" + haptic
            + " swapScreens=" + swapScr
            + " touchScreen=" + touchScreen
            + " joystickRelCenter=" + joyRel
            + " dpadSlide=" + dpadSlide);
        mPrefs.edit()
            .putBoolean("EmulationMenuSettings_ShowOverlay", showOverlay)
            .putBoolean("EmulationMenuSettings_HapticFeedback", haptic)
            .putBoolean("EmulationMenuSettings_SwapScreens", swapScr)
            .putBoolean("EmulationMenuSettings_TouchEnabled", touchScreen)
            .putBoolean("EmulationMenuSettings_JoystickRelCenter", joyRel)
            .putBoolean("EmulationMenuSettings_DpadSlideEnable", dpadSlide)
            .apply();

        if (mOverlay != null) {
            mOverlay.setShowControls(showOverlay);
            mOverlay.setHapticFeedbackEnabled(haptic);
            mOverlay.setTouchEnabled(touchScreen);
            mOverlay.setOverlayOpacityPercent(mSbOpacity.getProgress());
        }
        try {
            AndroidNativeInputTarget.nativeSwapScreens(swapScr);
            AndroidNativeInputTarget.nativeSetTouchEnabled(touchScreen);
        } catch (Throwable t) {
            Log.w(TAG, "Failed to apply native control settings", t);
        }
        Log.d(TAG, "--- saveToConfig END ---");
    }

    // -------------------------------------------------------------------------
    // Spinner helper
    // -------------------------------------------------------------------------

    private void setupSpinner(Spinner spinner, String[] items, Integer selectedIndex) {
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(
            getContext(), android.R.layout.simple_spinner_item, items) {
            @Override
            public View getView(int pos, View convertView, ViewGroup parent) {
                TextView tv = (TextView) super.getView(pos, convertView, parent);
                tv.setTextColor(Color.parseColor("#C8D8E8"));
                tv.setTextSize(12f);
                return tv;
            }
            @Override
            public View getDropDownView(int pos, View convertView, ViewGroup parent) {
                TextView tv = (TextView) super.getDropDownView(pos, convertView, parent);
                tv.setTextColor(Color.parseColor("#FFD700"));
                tv.setBackgroundColor(Color.parseColor("#1A212B"));
                tv.setPadding(24, 16, 24, 16);
                tv.setTextSize(12f);
                return tv;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        if (selectedIndex != null) spinner.setSelection(selectedIndex);
    }
}
