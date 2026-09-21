package org.triaevum.android.controls;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Build;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.core.content.res.ResourcesCompat;
import androidx.preference.PreferenceManager;

import java.util.ArrayList;

/**
 * Ergonomic virtual gamepad overlay directly adapted from Windroid-emu
 * (https://github.com/WINDROID-EMU/Windroid-emu).
 *
 * Implements:
 * - Exact Windroid-emu D-Pad vector glyphs, position (640, 480) and 8-direction detection.
 * - Exact Windroid-emu triggers: LT (top) & LB (bumper) on the left, RT (top) & RB (bumper) on the right.
 * - Exact Windroid-emu analog sticks (Left CirclePad @ 280, 840, Right C-Stick @ 1750, 480).
 * - Full passthrough for native 3DS touchscreen taps and drags outside virtual buttons.
 */
public class WindroidVirtualControllerView extends View {
    public static final int SHAPE_CIRCLE = 0;
    public static final int SHAPE_RECTANGLE = 1;
    public static final int SHAPE_DPAD = 2;

    public static final int UP = 1;
    public static final int RIGHT_UP = 2;
    public static final int RIGHT = 3;
    public static final int RIGHT_DOWN = 4;
    public static final int DOWN = 5;
    public static final int LEFT_DOWN = 6;
    public static final int LEFT = 7;
    public static final int LEFT_UP = 8;

    private static final int A_BUTTON = 1;
    private static final int B_BUTTON = 2;
    private static final int X_BUTTON = 3;
    private static final int Y_BUTTON = 4;
    private static final int START_BUTTON = 5;
    private static final int SELECT_BUTTON = 6;
    private static final int LB_BUTTON = 7;  // 3DS L
    private static final int LT_BUTTON = 8;  // 3DS ZL
    private static final int RB_BUTTON = 9;  // 3DS R
    private static final int RT_BUTTON = 10; // 3DS ZR
    private static final int LEFT_ANALOG = 11;  // 3DS CirclePad
    private static final int RIGHT_ANALOG = 12; // 3DS C-Stick
    private static final int DPAD_BUTTON = 13;

    private static final float BASE_WIDTH = 2400F;
    private static final float BASE_HEIGHT = 1080F;

    private Paint paint;
    private Paint fillPaint;
    private Paint textPaint;

    private final Path dpadUp = new Path();
    private final Path dpadDown = new Path();
    private final Path dpadLeft = new Path();
    private final Path dpadRight = new Path();
    private final Path startButton = new Path();
    private final Path selectButton = new Path();

    private final ArrayList<VirtualControllerButton> buttonList = new ArrayList<>();
    private VirtualXInputDPad dpad;
    private VirtualXInputAnalog leftAnalog;
    private VirtualXInputAnalog rightAnalog;

    private Native3dsInputTarget inputTarget;
    private SharedPreferences preferences;
    public boolean isEditing = false;
    private int touchscreenPointerId = -1;

    private float scaleX = 1.0F;
    private float scaleY = 1.0F;
    private float scaleFactor = 1.0F;

    // ------- Settings button (top-center) -------
    /** Callback fired when the user taps the settings gear button. */
    public interface OnSettingsClickListener {
        void onSettingsClick();
    }
    private OnSettingsClickListener mSettingsListener;
    // Position computed in adjustButtons(); touch detection uses mSettingsTouchRadius
    private float mSettingsBtnX = 0F;
    private float mSettingsBtnY = 0F;
    private float mSettingsBtnRadius = 0F;  // display radius
    private float mSettingsTouchRadius = 0F; // slightly larger for comfortable tap
    private boolean mSettingsBtnPressed = false;
    private int mSettingsPointerId = -1;
    private final Paint mSettingsGearPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mSettingsGearFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mSettingsTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mSettingsRect = new RectF();

    // ------- Overlay opacity -------
    /** 20-100 percent; stored in SharedPreferences. */
    private static final String PREF_OPACITY = "VC_OVERLAY_OPACITY";
    private int mOverlayOpacityPercent = 100;

    public WindroidVirtualControllerView(Context context) {
        super(context);
        init();
    }

    public WindroidVirtualControllerView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public WindroidVirtualControllerView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    public void bindInputTarget(Native3dsInputTarget target) {
        this.inputTarget = target;
    }

    /** Register a listener to be notified when the user taps the settings gear icon. */
    public void setOnSettingsClickListener(OnSettingsClickListener listener) {
        mSettingsListener = listener;
    }

    private static final String PREF_SHOW_OVERLAY = "EmulationMenuSettings_ShowOverlay";
    private static final String PREF_HAPTIC = "EmulationMenuSettings_HapticFeedback";
    private static final String PREF_TOUCH_ENABLED = "EmulationMenuSettings_TouchEnabled";
    private boolean mShowControls = true;
    private boolean mHapticFeedbackEnabled = true;
    private boolean mTouchEnabled = false;

    public boolean isTouchEnabled() {
        return mTouchEnabled;
    }

    public void setTouchEnabled(boolean enabled) {
        mTouchEnabled = enabled;
        if (!enabled && touchscreenPointerId != -1) {
            touchscreenPointerId = -1;
            if (inputTarget != null) {
                inputTarget.touchPixels(0F, 0F, false);
            }
        }
        if (preferences != null) {
            preferences.edit().putBoolean(PREF_TOUCH_ENABLED, enabled).apply();
        }
    }

    /** Returns the current overlay opacity (20-100). */
    public int getOverlayOpacityPercent() {
        return mOverlayOpacityPercent;
    }

    /** Sets overlay opacity (clamped to 20-100) and persists to SharedPreferences. */
    public void setOverlayOpacityPercent(int percent) {
        mOverlayOpacityPercent = Math.max(20, Math.min(100, percent));
        if (preferences != null) {
            preferences.edit().putInt(PREF_OPACITY, mOverlayOpacityPercent).apply();
        }
        invalidate();
    }

    public boolean isShowControls() {
        return mShowControls;
    }

    public void setShowControls(boolean show) {
        mShowControls = show;
        if (!show) {
            releaseAllControls();
        }
        if (preferences != null) {
            preferences.edit().putBoolean(PREF_SHOW_OVERLAY, show).apply();
        }
        invalidate();
    }

    public boolean isHapticFeedbackEnabled() {
        return mHapticFeedbackEnabled;
    }

    public void setHapticFeedbackEnabled(boolean enabled) {
        mHapticFeedbackEnabled = enabled;
        if (preferences != null) {
            preferences.edit().putBoolean(PREF_HAPTIC, enabled).apply();
        }
    }

    public void releaseAllControls() {
        for (VirtualControllerButton btn : buttonList) {
            if (btn.isPressed) {
                btn.fingerId = -1;
                btn.isPressed = false;
            }
        }
        resetAnalog(true);
        resetAnalog(false);
        if (dpad != null) {
            dpad.fingerId = -1;
            dpad.fingerX = 0F;
            dpad.fingerY = 0F;
            dpad.isPressed = false;
            dpad.dpadStatus = 0;
        }
        if (inputTarget != null) {
            inputTarget.releaseAll();
        }
    }

    private void performHaptic() {
        if (!mHapticFeedbackEnabled) return;
        try {
            Vibrator v = (Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);
            if (v != null && v.hasVibrator()) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(VibrationEffect.createOneShot(35, VibrationEffect.DEFAULT_AMPLITUDE));
                } else {
                    v.vibrate(35);
                }
            }
        } catch (Exception ignored) {}
    }

    private void init() {
        setWillNotDraw(false);
        setFocusable(true);

        try {
            preferences = PreferenceManager.getDefaultSharedPreferences(getContext());
            mOverlayOpacityPercent = preferences.getInt(PREF_OPACITY, 100);
            mShowControls = preferences.getBoolean(PREF_SHOW_OVERLAY, true);
            mHapticFeedbackEnabled = preferences.getBoolean(PREF_HAPTIC, true);
            mTouchEnabled = preferences.getBoolean(PREF_TOUCH_ENABLED, false);
        } catch (Exception ignored) {}

        paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setStrokeWidth(16F);
        paint.setColor(Color.WHITE);
        paint.setStyle(Paint.Style.STROKE);

        fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        fillPaint.setColor(Color.WHITE);
        fillPaint.setStyle(Paint.Style.FILL);

        textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTextSize(64F);

        try {
            Typeface font = ResourcesCompat.getFont(getContext(), R.font.quicksand);
            if (font != null) {
                textPaint.setTypeface(font);
            }
        } catch (Exception ignored) {
            textPaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
        }

        // Settings gear paint
        mSettingsGearPaint.setStyle(Paint.Style.STROKE);
        mSettingsGearPaint.setColor(Color.parseColor("#FFD700"));
        mSettingsGearFillPaint.setStyle(Paint.Style.FILL);
        mSettingsGearFillPaint.setColor(Color.parseColor("#1A1500"));
        mSettingsTextPaint.setColor(Color.parseColor("#FFD700"));
        mSettingsTextPaint.setTextAlign(Paint.Align.CENTER);
        mSettingsTextPaint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));

        // Exact Windroid-emu base layout on 2400x1080 canvas
        addButton(A_BUTTON, 2065F, 910F, 180F, SHAPE_CIRCLE);
        addButton(B_BUTTON, 2205F, 735F, 180F, SHAPE_CIRCLE);
        addButton(X_BUTTON, 1925F, 735F, 180F, SHAPE_CIRCLE);
        addButton(Y_BUTTON, 2065F, 560F, 180F, SHAPE_CIRCLE);
        addButton(START_BUTTON, 1330F, 980F, 130F, SHAPE_CIRCLE);
        addButton(SELECT_BUTTON, 1120F, 980F, 130F, SHAPE_CIRCLE);

        // Triggers copied from Windroid-emu: LT above LB on left, RT above RB on right
        addButton(LB_BUTTON, 280F, 300F, 260F, SHAPE_RECTANGLE);
        addButton(LT_BUTTON, 280F, 140F, 260F, SHAPE_RECTANGLE);
        addButton(RB_BUTTON, 2065F, 300F, 260F, SHAPE_RECTANGLE);
        addButton(RT_BUTTON, 2065F, 140F, 260F, SHAPE_RECTANGLE);

        // Analogs and D-Pad copied from Windroid-emu
        leftAnalog = new VirtualXInputAnalog(LEFT_ANALOG, 280F, 840F, 275F);
        rightAnalog = new VirtualXInputAnalog(RIGHT_ANALOG, 1750F, 480F, 275F);
        dpad = new VirtualXInputDPad(DPAD_BUTTON, 640F, 480F, 200F);
    }

    private void addButton(int id, float x, float y, float radius, int shape) {
        buttonList.add(new VirtualControllerButton(id, x, y, radius, shape));
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (w <= 0 || h <= 0) return;
        adjustButtons(w, h);
    }

    /**
     * Scale controls proportionally from base 2400x1080 canvas to current display size.
     */
    private void adjustButtons(float width, float height) {
        scaleX = width / BASE_WIDTH;
        scaleY = height / BASE_HEIGHT;
        scaleFactor = Math.min(scaleX, scaleY);

        for (VirtualControllerButton btn : buttonList) {
            btn.x = btn.baseX * scaleX;
            btn.y = btn.baseY * scaleY;
            btn.radius = btn.baseRadius * scaleFactor;
        }

        leftAnalog.x = leftAnalog.baseX * scaleX;
        leftAnalog.y = leftAnalog.baseY * scaleY;
        leftAnalog.radius = leftAnalog.baseRadius * scaleFactor;

        rightAnalog.x = rightAnalog.baseX * scaleX;
        rightAnalog.y = rightAnalog.baseY * scaleY;
        rightAnalog.radius = rightAnalog.baseRadius * scaleFactor;

        dpad.x = dpad.baseX * scaleX;
        dpad.y = dpad.baseY * scaleY;
        dpad.radius = dpad.baseRadius * scaleFactor;

        // Settings gear button: center top, radius = ~24dp equivalent
        mSettingsBtnRadius = 44F * scaleFactor;
        mSettingsTouchRadius = mSettingsBtnRadius * 1.6F;
        mSettingsBtnX = width / 2F;
        mSettingsBtnY = mSettingsBtnRadius + 18F * scaleFactor;
        mSettingsGearPaint.setStrokeWidth(6F * scaleFactor);
        mSettingsTextPaint.setTextSize(mSettingsBtnRadius * 0.9F);

        // Allow saved user preferences only if layout was explicitly saved by user
        if (preferences != null && preferences.getBoolean("VC_CUSTOM_USER_SAVED", false)) {
            for (VirtualControllerButton i : buttonList) {
                if (preferences.contains("VC_BUTTON_" + i.id + "_X")) {
                    i.x = preferences.getFloat("VC_BUTTON_" + i.id + "_X", i.x);
                    i.y = preferences.getFloat("VC_BUTTON_" + i.id + "_Y", i.y);
                }
            }
            if (preferences.contains("VC_BUTTON_" + LEFT_ANALOG + "_X")) {
                leftAnalog.x = preferences.getFloat("VC_BUTTON_" + LEFT_ANALOG + "_X", leftAnalog.x);
                leftAnalog.y = preferences.getFloat("VC_BUTTON_" + LEFT_ANALOG + "_Y", leftAnalog.y);
            }
            if (preferences.contains("VC_BUTTON_" + RIGHT_ANALOG + "_X")) {
                rightAnalog.x = preferences.getFloat("VC_BUTTON_" + RIGHT_ANALOG + "_X", rightAnalog.x);
                rightAnalog.y = preferences.getFloat("VC_BUTTON_" + RIGHT_ANALOG + "_Y", rightAnalog.y);
            }
            if (preferences.contains("VC_BUTTON_DPAD_X")) {
                dpad.x = preferences.getFloat("VC_BUTTON_DPAD_X", dpad.x);
                dpad.y = preferences.getFloat("VC_BUTTON_DPAD_Y", dpad.y);
            }
        }
    }

    private String getButtonName(int id) {
        switch (id) {
            case A_BUTTON: return "A";
            case B_BUTTON: return "B";
            case X_BUTTON: return "X";
            case Y_BUTTON: return "Y";
            case RB_BUTTON: return "R";
            case LB_BUTTON: return "L";
            case RT_BUTTON: return "ZR";
            case LT_BUTTON: return "ZL";
            default: return "";
        }
    }

    private void drawDPad(Path path, boolean isPressed, Canvas canvas, int baseAlpha) {
        paint.setStyle(isPressed ? Paint.Style.FILL_AND_STROKE : Paint.Style.STROKE);
        paint.setColor(Color.WHITE);
        paint.setAlpha(isPressed ? 240 : baseAlpha);
        canvas.drawPath(path, paint);
    }

    @Override
    protected void onDraw(@NonNull Canvas canvas) {
        super.onDraw(canvas);

        // Scale alpha uniformly across ALL virtual controller elements
        float alphaFactor = mOverlayOpacityPercent / 100.0f;
        int baseAlpha = Math.max(10, Math.min(255, (int) (220 * alphaFactor)));
        int fillAlpha = Math.max(8, Math.min(255, (int) (130 * alphaFactor)));
        paint.setStrokeWidth(16F * scaleFactor);

        // ---- Settings gear button (always drawn, scales uniformly with min floor) ----
        drawSettingsButton(canvas, alphaFactor);

        if (!mShowControls) {
            return;
        }

        // 1. Draw buttons (ABXY, Triggers, Start, Select)
        for (VirtualControllerButton i : buttonList) {
            paint.setColor(Color.WHITE);
            paint.setStrokeWidth(16F * scaleFactor);

            if (i.isPressed) {
                paint.setStyle(Paint.Style.FILL_AND_STROKE);
                paint.setAlpha(240);
                textPaint.setColor(Color.BLACK);
                textPaint.setAlpha(255);
            } else {
                paint.setStyle(Paint.Style.STROKE);
                paint.setAlpha(baseAlpha);
                textPaint.setColor(Color.WHITE);
                textPaint.setAlpha(baseAlpha);
            }

            textPaint.setTextSize(i.radius * 0.42F);
            float textOffset = (textPaint.getFontMetrics().ascent + textPaint.getFontMetrics().descent) / 2F;

            switch (i.shape) {
                case SHAPE_CIRCLE:
                    canvas.drawCircle(i.x, i.y, i.radius / 2F, paint);
                    break;
                case SHAPE_RECTANGLE:
                    // Windroid-emu rounded rectangle trigger with 32dp corner radius
                    canvas.drawRoundRect(
                            i.x - i.radius / 2F,
                            i.y - i.radius / 4F,
                            i.x + i.radius / 2F,
                            i.y + i.radius / 4F,
                            32F * scaleFactor, 32F * scaleFactor, paint);
                    break;
            }

            switch (i.id) {
                case START_BUTTON: {
                    paint.setStrokeWidth(12F * scaleFactor);
                    startButton.reset();
                    float w3 = i.radius / 3.0F;
                    float h8 = i.radius / 8.0F;
                    startButton.moveTo(i.x - w3, i.y - h8);
                    startButton.lineTo(i.x + w3, i.y - h8);
                    startButton.moveTo(i.x - w3, i.y);
                    startButton.lineTo(i.x + w3, i.y);
                    startButton.moveTo(i.x - w3, i.y + h8);
                    startButton.lineTo(i.x + w3, i.y + h8);
                    paint.setStyle(Paint.Style.STROKE);
                    if (i.isPressed) {
                        paint.setColor(Color.BLACK);
                        paint.setAlpha(255);
                    } else {
                        paint.setColor(Color.WHITE);
                        paint.setAlpha(baseAlpha);
                    }
                    canvas.drawPath(startButton, paint);
                    break;
                }
                case SELECT_BUTTON: {
                    paint.setStrokeWidth(12F * scaleFactor);
                    selectButton.reset();
                    float s4 = i.radius / 4F;
                    selectButton.moveTo(i.x - s4 + 4F, i.y - s4 + 40F * scaleFactor);
                    selectButton.lineTo(i.x - s4 + 4F, i.y - s4);
                    selectButton.lineTo(i.x - s4 + 4F + 40F * scaleFactor, i.y - s4);
                    selectButton.lineTo(i.x - s4 + 4F + 40F * scaleFactor, i.y - s4 + 20F * scaleFactor);
                    selectButton.lineTo(i.x - s4 + 4F + 40F * scaleFactor, i.y - s4);
                    selectButton.lineTo(i.x - s4 + 4F, i.y - s4);
                    selectButton.close();

                    selectButton.moveTo(i.x - s4 + 20F * scaleFactor, i.y - s4 + 30F * scaleFactor);
                    selectButton.lineTo(i.x - s4 + 60F * scaleFactor, i.y - s4 + 30F * scaleFactor);
                    selectButton.lineTo(i.x - s4 + 60F * scaleFactor, i.y - s4 + 70F * scaleFactor);
                    selectButton.lineTo(i.x - s4 + 20F * scaleFactor, i.y - s4 + 70F * scaleFactor);
                    selectButton.close();

                    paint.setStyle(Paint.Style.STROKE);
                    if (i.isPressed) {
                        paint.setColor(Color.BLACK);
                        paint.setAlpha(255);
                    } else {
                        paint.setColor(Color.WHITE);
                        paint.setAlpha(baseAlpha);
                    }
                    canvas.drawPath(selectButton, paint);
                    break;
                }
                default:
                    canvas.drawText(getButtonName(i.id), i.x, i.y - textOffset - 2, textPaint);
                    break;
            }
        }

        // 2. Left Analog Stick (Circle Pad)
        float leftOuterRadius = leftAnalog.radius / 2F;
        float leftMaxTravel = leftOuterRadius * 0.65F;
        float leftDist = (float) Math.sqrt(leftAnalog.fingerX * leftAnalog.fingerX + leftAnalog.fingerY * leftAnalog.fingerY);
        float leftStickX = leftAnalog.x;
        float leftStickY = leftAnalog.y;
        if (leftDist > 0.001F) {
            float travel = Math.min(leftDist, leftMaxTravel);
            leftStickX = leftAnalog.x + (leftAnalog.fingerX / leftDist) * travel;
            leftStickY = leftAnalog.y + (leftAnalog.fingerY / leftDist) * travel;
        }

        paint.setColor(Color.WHITE);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(16F * scaleFactor);
        paint.setAlpha(baseAlpha);
        canvas.drawCircle(leftAnalog.x, leftAnalog.y, leftOuterRadius, paint);

        fillPaint.setColor(Color.WHITE);
        fillPaint.setAlpha(leftAnalog.isPressed ? 230 : fillAlpha);
        canvas.drawCircle(leftStickX, leftStickY, leftOuterRadius * 0.44F, fillPaint);
        paint.setAlpha(leftAnalog.isPressed ? 255 : baseAlpha);
        canvas.drawCircle(leftStickX, leftStickY, leftOuterRadius * 0.44F, paint);

        // 3. Right Analog Stick (C-Stick / Camera)
        float rightOuterRadius = rightAnalog.radius / 2F;
        float rightMaxTravel = rightOuterRadius * 0.65F;
        float rightDist = (float) Math.sqrt(rightAnalog.fingerX * rightAnalog.fingerX + rightAnalog.fingerY * rightAnalog.fingerY);
        float rightStickX = rightAnalog.x;
        float rightStickY = rightAnalog.y;
        if (rightDist > 0.001F) {
            float travel = Math.min(rightDist, rightMaxTravel);
            rightStickX = rightAnalog.x + (rightAnalog.fingerX / rightDist) * travel;
            rightStickY = rightAnalog.y + (rightAnalog.fingerY / rightDist) * travel;
        }

        paint.setColor(Color.WHITE);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(14F * scaleFactor);
        paint.setAlpha(baseAlpha);
        canvas.drawCircle(rightAnalog.x, rightAnalog.y, rightOuterRadius, paint);

        fillPaint.setColor(Color.WHITE);
        fillPaint.setAlpha(rightAnalog.isPressed ? 210 : fillAlpha);
        canvas.drawCircle(rightStickX, rightStickY, rightOuterRadius * 0.44F, fillPaint);
        paint.setAlpha(rightAnalog.isPressed ? 240 : baseAlpha);
        canvas.drawCircle(rightStickX, rightStickY, rightOuterRadius * 0.44F, paint);

        // 4. Exact Windroid-emu D-Pad vector glyphs
        float dOffset = 20F * scaleFactor;
        float dR4 = (dpad.radius / 4F);
        float dR2 = (dpad.radius / 2F);

        // D-Pad Left
        dpadLeft.reset();
        dpadLeft.moveTo(dpad.x - dOffset, dpad.y);
        dpadLeft.lineTo(dpad.x - dOffset - dR4, dpad.y - dR4);
        dpadLeft.lineTo(dpad.x - dOffset - dR4 - dR2, dpad.y - dR4);
        dpadLeft.lineTo(dpad.x - dOffset - dR4 - dR2, dpad.y - dR4 + dR2);
        dpadLeft.lineTo(dpad.x - dOffset - dR4, dpad.y - dR4 + dR2);
        dpadLeft.lineTo(dpad.x - dOffset, dpad.y);
        dpadLeft.close();

        // D-Pad Up
        dpadUp.reset();
        dpadUp.moveTo(dpad.x, dpad.y - dOffset);
        dpadUp.lineTo(dpad.x - dR4, dpad.y - dOffset - dR4);
        dpadUp.lineTo(dpad.x - dR4, dpad.y - dOffset - dR4 - dR2);
        dpadUp.lineTo(dpad.x - dR4 + dR2, dpad.y - dOffset - dR4 - dR2);
        dpadUp.lineTo(dpad.x - dR4 + dR2, dpad.y - dOffset - dR4);
        dpadUp.lineTo(dpad.x, dpad.y - dOffset);
        dpadUp.close();

        // D-Pad Right
        dpadRight.reset();
        dpadRight.moveTo(dpad.x + dOffset, dpad.y);
        dpadRight.lineTo(dpad.x + dOffset + dR4, dpad.y - dR4);
        dpadRight.lineTo(dpad.x + dOffset + dR4 + dR2, dpad.y - dR4);
        dpadRight.lineTo(dpad.x + dOffset + dR4 + dR2, dpad.y - dR4 + dR2);
        dpadRight.lineTo(dpad.x + dOffset + dR4, dpad.y - dR4 + dR2);
        dpadRight.lineTo(dpad.x + dOffset, dpad.y);
        dpadRight.close();

        // D-Pad Down
        dpadDown.reset();
        dpadDown.moveTo(dpad.x, dpad.y + dOffset);
        dpadDown.lineTo(dpad.x - dR4, dpad.y + dOffset + dR4);
        dpadDown.lineTo(dpad.x - dR4, dpad.y + dOffset + dR4 + dR2);
        dpadDown.lineTo(dpad.x - dR4 + dR2, dpad.y + dOffset + dR4 + dR2);
        dpadDown.lineTo(dpad.x - dR4 + dR2, dpad.y + dOffset + dR4);
        dpadDown.lineTo(dpad.x, dpad.y + dOffset);
        dpadDown.close();

        paint.setStrokeWidth(16F * scaleFactor);
        drawDPad(dpadUp, dpad.dpadStatus == UP || dpad.dpadStatus == RIGHT_UP || dpad.dpadStatus == LEFT_UP, canvas, baseAlpha);
        drawDPad(dpadDown, dpad.dpadStatus == DOWN || dpad.dpadStatus == RIGHT_DOWN || dpad.dpadStatus == LEFT_DOWN, canvas, baseAlpha);
        drawDPad(dpadLeft, dpad.dpadStatus == LEFT || dpad.dpadStatus == LEFT_DOWN || dpad.dpadStatus == LEFT_UP, canvas, baseAlpha);
        drawDPad(dpadRight, dpad.dpadStatus == RIGHT || dpad.dpadStatus == RIGHT_DOWN || dpad.dpadStatus == RIGHT_UP, canvas, baseAlpha);
    }

    private void drawSettingsButton(Canvas canvas, float alphaFactor) {
        if (mSettingsBtnX <= 0F) return;

        // Keep settings button visible with at least 35% opacity so user can always find it
        float gearAlphaRatio = Math.max(0.35F, alphaFactor);

        // Background circle (semi-transparent black)
        mSettingsGearFillPaint.setAlpha((int) ((mSettingsBtnPressed ? 220 : 140) * gearAlphaRatio));
        canvas.drawCircle(mSettingsBtnX, mSettingsBtnY, mSettingsBtnRadius, mSettingsGearFillPaint);

        // Outer circle ring
        mSettingsGearPaint.setColor(mSettingsBtnPressed
            ? Color.parseColor("#FFFFFF")
            : Color.parseColor("#FFD700"));
        mSettingsGearPaint.setAlpha((int) ((mSettingsBtnPressed ? 240 : 180) * gearAlphaRatio));
        canvas.drawCircle(mSettingsBtnX, mSettingsBtnY, mSettingsBtnRadius, mSettingsGearPaint);

        // Gear symbol (⚙)
        mSettingsTextPaint.setAlpha((int) ((mSettingsBtnPressed ? 255 : 200) * gearAlphaRatio));
        mSettingsTextPaint.setColor(mSettingsBtnPressed
            ? Color.parseColor("#FFFFFF")
            : Color.parseColor("#FFD700"));
        float textY = mSettingsBtnY - (mSettingsTextPaint.descent() + mSettingsTextPaint.ascent()) / 2F;
        canvas.drawText("\u2699", mSettingsBtnX, textY, mSettingsTextPaint);
    }

    /**
     * Hit testing faithful to Windroid-emu's detectClick logic.
     */
    private boolean detectClick(float px, float py, float x, float y, float radius, int shape) {
        switch (shape) {
            case SHAPE_RECTANGLE:
                // Width = radius, Height = radius / 2, with comfortable thumb margin
                float rw = (radius / 2F) * 1.1F;
                float rh = (radius / 4F) * 1.15F;
                return (px >= x - rw && px <= x + rw) && (py >= y - rh && py <= y + rh);
            case SHAPE_DPAD:
                float dMargin = radius * 1.1F;
                return (px >= x - dMargin && px <= x + dMargin) && (py >= y - dMargin && py <= y + dMargin);
            case SHAPE_CIRCLE:
            default:
                float hitRadius = (radius / 2F) * 1.2F;
                float dx = px - x;
                float dy = py - y;
                return (dx * dx + dy * dy) <= (hitRadius * hitRadius);
        }
    }

    /**
     * Windroid-emu ControllerUtils.getAxisStatus 8-way directional calculation.
     */
    public static int getAxisStatus(float axisX, float axisY, float deadZone) {
        boolean axisXNeutral = (axisX < deadZone && axisX > -deadZone);
        boolean axisYNeutral = (axisY < deadZone && axisY > -deadZone);

        if (axisX > deadZone && axisY < -deadZone) return RIGHT_UP;
        if (axisX > deadZone && axisYNeutral) return RIGHT;
        if (axisX > deadZone && axisY > deadZone) return RIGHT_DOWN;
        if (axisY > deadZone && axisXNeutral) return DOWN;
        if (axisY < -deadZone && axisXNeutral) return UP;
        if (axisX < -deadZone && axisY > deadZone) return LEFT_DOWN;
        if (axisX < -deadZone && axisYNeutral) return LEFT;
        if (axisX < -deadZone && axisY < -deadZone) return LEFT_UP;
        return 0;
    }

    private int get3dsHidMaskForButton(int id) {
        switch (id) {
            case A_BUTTON: return 1 << 0;
            case B_BUTTON: return 1 << 1;
            case SELECT_BUTTON: return 1 << 2;
            case START_BUTTON: return 1 << 3;
            case RB_BUTTON: return 1 << 8;  // R
            case LB_BUTTON: return 1 << 9;  // L
            case X_BUTTON: return 1 << 10;
            case Y_BUTTON: return 1 << 11;
            case LT_BUTTON: return 1 << 14; // ZL
            case RT_BUTTON: return 1 << 15; // ZR
            default: return 0;
        }
    }

    private void handleButton(VirtualControllerButton button, boolean isPressed) {
        if (isPressed && !button.isPressed) {
            performHaptic();
        }
        button.isPressed = isPressed;
        if (inputTarget != null) {
            int mask = get3dsHidMaskForButton(button.id);
            if (mask != 0) {
                inputTarget.button(mask, isPressed);
            }
        }
    }

    private void updateDpadHid(int oldStatus, int newStatus) {
        if (inputTarget == null || oldStatus == newStatus) return;
        if (oldStatus == 0 && newStatus != 0) {
            performHaptic();
        }

        int oldUp = (oldStatus == UP || oldStatus == RIGHT_UP || oldStatus == LEFT_UP) ? 1 : 0;
        int newUp = (newStatus == UP || newStatus == RIGHT_UP || newStatus == LEFT_UP) ? 1 : 0;
        if (oldUp != newUp) inputTarget.button(1 << 6, newUp == 1);

        int oldDown = (oldStatus == DOWN || oldStatus == RIGHT_DOWN || oldStatus == LEFT_DOWN) ? 1 : 0;
        int newDown = (newStatus == DOWN || newStatus == RIGHT_DOWN || newStatus == LEFT_DOWN) ? 1 : 0;
        if (oldDown != newDown) inputTarget.button(1 << 7, newDown == 1);

        int oldLeft = (oldStatus == LEFT || oldStatus == LEFT_DOWN || oldStatus == LEFT_UP) ? 1 : 0;
        int newLeft = (newStatus == LEFT || newStatus == LEFT_DOWN || newStatus == LEFT_UP) ? 1 : 0;
        if (oldLeft != newLeft) inputTarget.button(1 << 5, newLeft == 1);

        int oldRight = (oldStatus == RIGHT || oldStatus == RIGHT_DOWN || oldStatus == RIGHT_UP) ? 1 : 0;
        int newRight = (newStatus == RIGHT || newStatus == RIGHT_DOWN || newStatus == RIGHT_UP) ? 1 : 0;
        if (oldRight != newRight) inputTarget.button(1 << 4, newRight == 1);
    }

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);

        switch (action) {
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_DOWN: {
                float px = event.getX(actionIndex);
                float py = event.getY(actionIndex);
                boolean hit = false;

                // 0. Check Settings gear button (top-center)
                if (!hit && mSettingsTouchRadius > 0F) {
                    float sdx = px - mSettingsBtnX;
                    float sdy = py - mSettingsBtnY;
                    if (sdx * sdx + sdy * sdy <= mSettingsTouchRadius * mSettingsTouchRadius) {
                        mSettingsPointerId = pointerId;
                        mSettingsBtnPressed = true;
                        performHaptic();
                        invalidate();
                        hit = true;
                    }
                }

                if (!hit && mShowControls) {
                    // 1. Check ABXY, Triggers (LT, LB, RT, RB), Start and Select
                    for (VirtualControllerButton btn : buttonList) {
                        if (detectClick(px, py, btn.x, btn.y, btn.radius, btn.shape)) {
                            btn.fingerId = pointerId;
                            handleButton(btn, true);
                            hit = true;
                            break;
                        }
                    }

                    // 2. Check Circle Pad (Left Analog)
                    if (!hit && detectClick(px, py, leftAnalog.x, leftAnalog.y, leftAnalog.radius, SHAPE_CIRCLE)) {
                        leftAnalog.fingerId = pointerId;
                        leftAnalog.isPressed = true;
                        updateAnalogPosition(px - leftAnalog.x, py - leftAnalog.y, true);
                        hit = true;
                    }

                    // 3. Check C-Stick (Right Analog)
                    if (!hit && detectClick(px, py, rightAnalog.x, rightAnalog.y, rightAnalog.radius, SHAPE_CIRCLE)) {
                        rightAnalog.fingerId = pointerId;
                        rightAnalog.isPressed = true;
                        updateAnalogPosition(px - rightAnalog.x, py - rightAnalog.y, false);
                        hit = true;
                    }

                    // 4. Check D-Pad
                    if (!hit && detectClick(px, py, dpad.x, dpad.y, dpad.radius, SHAPE_DPAD)) {
                        float posX = px - dpad.x;
                        float posY = py - dpad.y;
                        dpad.fingerId = pointerId;
                        dpad.fingerX = posX;
                        dpad.fingerY = posY;
                        dpad.isPressed = true;
                        int newStatus = getAxisStatus(posX / dpad.radius, posY / dpad.radius, 0.25F);
                        updateDpadHid(dpad.dpadStatus, newStatus);
                        dpad.dpadStatus = newStatus;
                        hit = true;
                    }
                }

                // 5. Native 3DS touchscreen passthrough for touches outside virtual controls (or when controls are hidden)
                if (!hit && mTouchEnabled) {
                    touchscreenPointerId = pointerId;
                    if (inputTarget != null) {
                        float normX = (getWidth() > 0) ? Math.max(0.0f, Math.min(1.0f, px / (float) getWidth())) : 0.0f;
                        float normY = (getHeight() > 0) ? Math.max(0.0f, Math.min(1.0f, py / (float) getHeight())) : 0.0f;
                        inputTarget.touchPixels(normX, normY, true);
                    }
                }

                invalidate();
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int pId = event.getPointerId(i);
                    float curX = event.getX(i);
                    float curY = event.getY(i);

                    if (mShowControls && leftAnalog.isPressed && leftAnalog.fingerId == pId) {
                        updateAnalogPosition(curX - leftAnalog.x, curY - leftAnalog.y, true);
                    } else if (mShowControls && rightAnalog.isPressed && rightAnalog.fingerId == pId) {
                        updateAnalogPosition(curX - rightAnalog.x, curY - rightAnalog.y, false);
                    } else if (mShowControls && dpad.isPressed && dpad.fingerId == pId) {
                        float posX = curX - dpad.x;
                        float posY = curY - dpad.y;
                        dpad.fingerX = posX;
                        dpad.fingerY = posY;
                        int newStatus = getAxisStatus(posX / dpad.radius, posY / dpad.radius, 0.25F);
                        updateDpadHid(dpad.dpadStatus, newStatus);
                        dpad.dpadStatus = newStatus;
                    } else if (touchscreenPointerId == pId && mTouchEnabled) {
                        if (inputTarget != null) {
                            float normX = (getWidth() > 0) ? Math.max(0.0f, Math.min(1.0f, curX / (float) getWidth())) : 0.0f;
                            float normY = (getHeight() > 0) ? Math.max(0.0f, Math.min(1.0f, curY / (float) getHeight())) : 0.0f;
                            inputTarget.moveTouchPixels(normX, normY);
                        }
                    }
                }
                invalidate();
                break;
            }
            case MotionEvent.ACTION_POINTER_UP: {
                for (VirtualControllerButton btn : buttonList) {
                    if (btn.fingerId == pointerId) {
                        btn.fingerId = -1;
                        handleButton(btn, false);
                    }
                }

                if (leftAnalog.fingerId == pointerId) {
                    resetAnalog(true);
                }

                if (rightAnalog.fingerId == pointerId) {
                    resetAnalog(false);
                }

                if (dpad.fingerId == pointerId) {
                    dpad.fingerId = -1;
                    dpad.fingerX = 0F;
                    dpad.fingerY = 0F;
                    dpad.isPressed = false;
                    updateDpadHid(dpad.dpadStatus, 0);
                    dpad.dpadStatus = 0;
                }

                if (touchscreenPointerId == pointerId) {
                    touchscreenPointerId = -1;
                    if (inputTarget != null) {
                        inputTarget.touchPixels(0F, 0F, false);
                    }
                }

                invalidate();
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL: {
                // Settings gear button release
                if (mSettingsBtnPressed && pointerId == mSettingsPointerId) {
                    mSettingsBtnPressed = false;
                    mSettingsPointerId = -1;
                    invalidate();
                    if (action != MotionEvent.ACTION_CANCEL && mSettingsListener != null) {
                        performHaptic();
                        mSettingsListener.onSettingsClick();
                    }
                    break;
                }

                for (VirtualControllerButton btn : buttonList) {
                    if (btn.isPressed) {
                        btn.fingerId = -1;
                        handleButton(btn, false);
                    }
                }

                resetAnalog(true);
                resetAnalog(false);

                dpad.fingerId = -1;
                dpad.fingerX = 0F;
                dpad.fingerY = 0F;
                dpad.isPressed = false;
                updateDpadHid(dpad.dpadStatus, 0);
                dpad.dpadStatus = 0;

                if (touchscreenPointerId != -1) {
                    touchscreenPointerId = -1;
                    if (inputTarget != null) {
                        inputTarget.touchPixels(0F, 0F, false);
                    }
                }

                invalidate();
                break;
            }

        }
        return true;
    }

    private void updateAnalogPosition(float dx, float dy, boolean isLeft) {
        VirtualXInputAnalog analog = isLeft ? leftAnalog : rightAnalog;
        float maxTravel = (analog.radius / 2F) * 0.65F;

        analog.fingerX = dx;
        analog.fingerY = dy;

        float normX = dx / maxTravel;
        float normY = -dy / maxTravel; // Invert Y for 3DS CTR HID (+Y is UP)

        float length = (float) Math.sqrt(normX * normX + normY * normY);
        if (length > 1.0F) {
            normX /= length;
            normY /= length;
        }

        // 5% deadzone for steady centering
        if (length < 0.05F) {
            normX = 0F;
            normY = 0F;
        }

        if (inputTarget != null) {
            if (isLeft) {
                inputTarget.circlePad(normX, normY);
            } else {
                inputTarget.cStick(normX, normY);
            }
        }
    }

    private void resetAnalog(boolean isLeft) {
        VirtualXInputAnalog analog = isLeft ? leftAnalog : rightAnalog;
        analog.fingerId = -1;
        analog.fingerX = 0F;
        analog.fingerY = 0F;
        analog.isPressed = false;
        if (inputTarget != null) {
            if (isLeft) {
                inputTarget.circlePad(0F, 0F);
            } else {
                inputTarget.cStick(0F, 0F);
            }
        }
    }

    public void releaseAll() {
        if (inputTarget != null) {
            inputTarget.releaseAll();
        }
        for (VirtualControllerButton btn : buttonList) {
            btn.isPressed = false;
            btn.fingerId = -1;
        }
        resetAnalog(true);
        resetAnalog(false);
        dpad.isPressed = false;
        dpad.fingerId = -1;
        dpad.dpadStatus = 0;
        touchscreenPointerId = -1;
        postInvalidate();
    }

    public static class VirtualControllerButton {
        public int id;
        public float baseX;
        public float baseY;
        public float baseRadius;
        public float x;
        public float y;
        public float radius;
        public int shape;
        public int fingerId = -1;
        public boolean isPressed = false;

        public VirtualControllerButton(int id, float x, float y, float radius, int shape) {
            this.id = id;
            this.baseX = x;
            this.baseY = y;
            this.baseRadius = radius;
            this.x = x;
            this.y = y;
            this.radius = radius;
            this.shape = shape;
        }
    }

    public static class VirtualXInputDPad {
        public int id;
        public float baseX;
        public float baseY;
        public float baseRadius;
        public float x;
        public float y;
        public float radius;
        public int fingerId = -1;
        public boolean isPressed = false;
        public float fingerX = 0F;
        public float fingerY = 0F;
        public int dpadStatus = 0;

        public VirtualXInputDPad(int id, float x, float y, float radius) {
            this.id = id;
            this.baseX = x;
            this.baseY = y;
            this.baseRadius = radius;
            this.x = x;
            this.y = y;
            this.radius = radius;
        }
    }

    public static class VirtualXInputAnalog {
        public int id;
        public float baseX;
        public float baseY;
        public float baseRadius;
        public float x;
        public float y;
        public float radius;
        public int fingerId = -1;
        public boolean isPressed = false;
        public float fingerX = 0F;
        public float fingerY = 0F;

        public VirtualXInputAnalog(int id, float x, float y, float radius) {
            this.id = id;
            this.baseX = x;
            this.baseY = y;
            this.baseRadius = radius;
            this.x = x;
            this.y = y;
            this.radius = radius;
        }
    }
}
