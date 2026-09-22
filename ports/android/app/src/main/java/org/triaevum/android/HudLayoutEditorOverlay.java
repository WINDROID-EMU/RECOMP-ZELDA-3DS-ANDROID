package org.triaevum.android;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Overlay interativo que permite ao usuário tocar e arrastar qualquer elemento
 * do HUD (corações, botões A/B, cluster X/Y/ZR/ZL, rúpias, minimapa) diretamente na tela,
 * salvando as posições no arquivo custom_hud_layout.json.
 */
public final class HudLayoutEditorOverlay extends FrameLayout {

    private static final String TAG = "HudLayoutEditor";

    public interface Callback {
        void onSaved(HudLayoutEditorOverlay editor);
        void onCancelled(HudLayoutEditorOverlay editor);
    }

    private final Activity mActivity;
    private final File mStorageDir;
    private final Callback mCallback;

    private View mHudView;
    private int mScreenWidth;
    private int mScreenHeight;
    private float mScale;
    private float mOffsetX;
    private float mOffsetY;

    public HudLayoutEditorOverlay(Activity activity, File storageDir, Callback callback) {
        super(activity);
        this.mActivity = activity;
        this.mStorageDir = storageDir;
        this.mCallback = callback;
        init();
    }

    private void init() {
        // Fundo escuro semi-transparente para que o jogo continue visível por trás
        setBackgroundColor(Color.parseColor("#990B0E14"));
        setClickable(true);
        setFocusable(true);

        DisplayMetrics dm = getResources().getDisplayMetrics();
        mScreenWidth = Math.max(dm.widthPixels, dm.heightPixels);
        mScreenHeight = Math.min(dm.widthPixels, dm.heightPixels);

        // Calcula escala para o canvas virtual 400x240 do OoT3D
        mScale = (float) mScreenHeight / 240.0f;
        mOffsetX = ((float) mScreenWidth - 400.0f * mScale) / 2.0f;
        mOffsetY = 0.0f;
        if (mOffsetX < 0) {
            mScale = (float) mScreenWidth / 400.0f;
            mOffsetX = 0.0f;
            mOffsetY = ((float) mScreenHeight - 240.0f * mScale) / 2.0f;
        }

        // Infla o layout visual do HUD
        mHudView = LayoutInflater.from(getContext()).inflate(R.layout.hud_gameplay_layout, this, false);
        mHudView.setBackgroundColor(Color.TRANSPARENT);
        addView(mHudView, new LayoutParams(mScreenWidth, mScreenHeight));

        // Força medição e layout inicial
        mHudView.measure(
            MeasureSpec.makeMeasureSpec(mScreenWidth, MeasureSpec.EXACTLY),
            MeasureSpec.makeMeasureSpec(mScreenHeight, MeasureSpec.EXACTLY)
        );
        mHudView.layout(0, 0, mScreenWidth, mScreenHeight);

        // Conecta o manipulador de arrasto nos elementos editáveis
        setupDraggable(R.id.hud_top_left_status, "Vida & Magia");
        setupDraggable(R.id.hud_btn_a, "Botão A");
        setupDraggable(R.id.hud_btn_b, "Botão B");
        setupDraggable(R.id.hud_diamond_cluster, "Itens (X/Y/ZR/ZL)");
        setupDraggable(R.id.hud_bottom_left_collectibles, "Rúpias");
        setupDraggable(R.id.hud_minimap_container, "Minimapa");

        // Carrega posições customizadas salvas previamente, se existirem
        post(this::loadSavedPositions);

        // Barra de ferramentas superior
        addView(createToolbar());
    }

    private View createToolbar() {
        LinearLayout bar = new LinearLayout(getContext());
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(Color.parseColor("#EE0F141C"));
        int padH = dpToPx(12);
        int padV = dpToPx(6);
        bar.setPadding(padH, padV, padH, padV);

        LayoutParams lp = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.TOP;
        bar.setLayoutParams(lp);

        // Título e subtítulo
        LinearLayout titleBox = new LinearLayout(getContext());
        titleBox.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        titleBox.setLayoutParams(titleLp);

        TextView title = new TextView(getContext());
        title.setText("✏️ Editor de Layout do HUD");
        title.setTextColor(Color.parseColor("#FFC107"));
        title.setTextSize(13);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        titleBox.addView(title);

        TextView sub = new TextView(getContext());
        sub.setText("Toque e arraste os elementos para onde quiser na tela");
        sub.setTextColor(Color.parseColor("#8B9BB4"));
        sub.setTextSize(10);
        titleBox.addView(sub);

        bar.addView(titleBox);

        // Botão Restaurar Padrão
        Button btnReset = new Button(getContext());
        btnReset.setText("↺ Padrão");
        btnReset.setTextColor(Color.WHITE);
        btnReset.setTextSize(11);
        btnReset.setBackgroundResource(R.drawable.btn_dark);
        LinearLayout.LayoutParams btnResetLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(34));
        btnResetLp.rightMargin = dpToPx(6);
        btnReset.setLayoutParams(btnResetLp);
        btnReset.setPadding(dpToPx(10), 0, dpToPx(10), 0);
        btnReset.setOnClickListener(v -> resetToDefaults());
        bar.addView(btnReset);

        // Botão Cancelar
        Button btnCancel = new Button(getContext());
        btnCancel.setText("Cancelar");
        btnCancel.setTextColor(Color.WHITE);
        btnCancel.setTextSize(11);
        btnCancel.setBackgroundResource(R.drawable.btn_dark);
        LinearLayout.LayoutParams btnCancelLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(34));
        btnCancelLp.rightMargin = dpToPx(6);
        btnCancel.setLayoutParams(btnCancelLp);
        btnCancel.setPadding(dpToPx(10), 0, dpToPx(10), 0);
        btnCancel.setOnClickListener(v -> {
            if (mCallback != null) mCallback.onCancelled(this);
        });
        bar.addView(btnCancel);

        // Botão Salvar
        Button btnSave = new Button(getContext());
        btnSave.setText("💾 Salvar");
        btnSave.setTextColor(Color.parseColor("#1A1500"));
        btnSave.setTextSize(11);
        btnSave.setTypeface(null, android.graphics.Typeface.BOLD);
        btnSave.setBackgroundResource(R.drawable.btn_gold);
        LinearLayout.LayoutParams btnSaveLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(34));
        btnSave.setLayoutParams(btnSaveLp);
        btnSave.setPadding(dpToPx(14), 0, dpToPx(14), 0);
        btnSave.setOnClickListener(v -> saveHudLayout());
        bar.addView(btnSave);

        return bar;
    }

    private void setupDraggable(int viewId, String label) {
        View target = mHudView.findViewById(viewId);
        if (target == null) return;

        // Cria moldura visual destacada
        final GradientDrawable normalBorder = new GradientDrawable();
        normalBorder.setStroke(dpToPx(1), Color.parseColor("#66FFD700"), dpToPx(4), dpToPx(2));
        normalBorder.setColor(Color.parseColor("#1AFFFFFF"));
        normalBorder.setCornerRadius(dpToPx(6));

        final GradientDrawable activeBorder = new GradientDrawable();
        activeBorder.setStroke(dpToPx(2), Color.parseColor("#FFFFD700"));
        activeBorder.setColor(Color.parseColor("#44FFD700"));
        activeBorder.setCornerRadius(dpToPx(6));

        target.setBackground(normalBorder);

        target.setOnTouchListener(new OnTouchListener() {
            private float startX, startY;
            private float initialTx, initialTy;
            private boolean isDragging = false;

            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        startX = event.getRawX();
                        startY = event.getRawY();
                        initialTx = v.getTranslationX();
                        initialTy = v.getTranslationY();
                        isDragging = true;
                        v.setBackground(activeBorder);
                        v.bringToFront();
                        return true;

                    case MotionEvent.ACTION_MOVE:
                        if (isDragging) {
                            float dx = event.getRawX() - startX;
                            float dy = event.getRawY() - startY;

                            float newTx = initialTx + dx;
                            float newTy = initialTy + dy;

                            // Limita o movimento aos limites da tela
                            float absX = getAbsoluteViewLeft(v) - v.getTranslationX() + newTx;
                            float absY = getAbsoluteViewTop(v) - v.getTranslationY() + newTy;
                            float w = v.getWidth();
                            float h = v.getHeight();

                            if (absX < 0) newTx = -(getAbsoluteViewLeft(v) - v.getTranslationX());
                            if (absY < 0) newTy = -(getAbsoluteViewTop(v) - v.getTranslationY());
                            if (absX + w > mScreenWidth) newTx = mScreenWidth - w - (getAbsoluteViewLeft(v) - v.getTranslationX());
                            if (absY + h > mScreenHeight) newTy = mScreenHeight - h - (getAbsoluteViewTop(v) - v.getTranslationY());

                            v.setTranslationX(newTx);
                            v.setTranslationY(newTy);
                        }
                        return true;

                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        isDragging = false;
                        v.setBackground(normalBorder);
                        return true;
                }
                return false;
            }
        });
    }

    private float getAbsoluteViewLeft(View v) {
        float x = v.getLeft() + v.getTranslationX();
        View parent = (v.getParent() instanceof View) ? (View) v.getParent() : null;
        while (parent != null && parent != mHudView && parent != this) {
            x += parent.getLeft() + parent.getTranslationX();
            parent = (parent.getParent() instanceof View) ? (View) parent.getParent() : null;
        }
        return x;
    }

    private float getAbsoluteViewTop(View v) {
        float y = v.getTop() + v.getTranslationY();
        View parent = (v.getParent() instanceof View) ? (View) v.getParent() : null;
        while (parent != null && parent != mHudView && parent != this) {
            y += parent.getTop() + parent.getTranslationY();
            parent = (parent.getParent() instanceof View) ? (View) parent.getParent() : null;
        }
        return y;
    }

    private void loadSavedPositions() {
        if (mStorageDir == null) return;
        File file = new File(mStorageDir, "custom_hud_layout.json");
        if (!file.exists()) return;

        try (FileInputStream fis = new FileInputStream(file)) {
            byte[] bytes = new byte[(int) file.length()];
            int read = fis.read(bytes);
            if (read <= 0) return;

            JSONObject root = new JSONObject(new String(bytes, StandardCharsets.UTF_8));

            applySavedPosition(root, "status", R.id.hud_top_left_status);
            applySavedPosition(root, "btn_a", R.id.hud_btn_a);
            applySavedPosition(root, "btn_b", R.id.hud_btn_b);
            applySavedPosition(root, "diamond_cluster", R.id.hud_diamond_cluster);
            applySavedPosition(root, "rupees", R.id.hud_bottom_left_collectibles);
            applySavedPosition(root, "minimap", R.id.hud_minimap_container);

        } catch (Throwable t) {
            Log.w(TAG, "Failed to load saved HUD positions", t);
        }
    }

    private void applySavedPosition(JSONObject root, String key, int viewId) {
        if (!root.has(key)) return;
        View target = mHudView.findViewById(viewId);
        if (target == null) return;

        try {
            JSONObject obj = root.getJSONObject(key);
            float canvasX = (float) obj.getDouble("x");
            float canvasY = (float) obj.getDouble("y");

            float targetPixelX = canvasX * mScale + mOffsetX;
            float targetPixelY = canvasY * mScale + mOffsetY;

            // Calcula o deslocamento necessário em relação à posição original do layout
            float curLeft = getAbsoluteViewLeft(target) - target.getTranslationX();
            float curTop = getAbsoluteViewTop(target) - target.getTranslationY();

            target.setTranslationX(targetPixelX - curLeft);
            target.setTranslationY(targetPixelY - curTop);

        } catch (Throwable ignored) {}
    }

    private void resetToDefaults() {
        // Reseta as translações de todas as views
        resetViewTranslation(R.id.hud_top_left_status);
        resetViewTranslation(R.id.hud_btn_a);
        resetViewTranslation(R.id.hud_btn_b);
        resetViewTranslation(R.id.hud_diamond_cluster);
        resetViewTranslation(R.id.hud_bottom_left_collectibles);
        resetViewTranslation(R.id.hud_minimap_container);
        Toast.makeText(getContext(), "Posições restauradas para o padrão do XML", Toast.LENGTH_SHORT).show();
    }

    private void resetViewTranslation(int viewId) {
        View target = mHudView.findViewById(viewId);
        if (target != null) {
            target.setTranslationX(0.0f);
            target.setTranslationY(0.0f);
        }
    }

    private void saveHudLayout() {
        try {
            JSONObject hudJson = new JSONObject();
            hudJson.put("version", 1);
            hudJson.put("screen_width", mScreenWidth);
            hudJson.put("screen_height", mScreenHeight);

            exportViewToCanvas(R.id.hud_btn_a, "btn_a", hudJson);
            exportViewToCanvas(R.id.hud_btn_b, "btn_b", hudJson);
            exportViewToCanvas(R.id.hud_btn_x, "btn_x", hudJson);
            exportViewToCanvas(R.id.hud_btn_y, "btn_y", hudJson);
            exportViewToCanvas(R.id.hud_btn_zr, "btn_zr", hudJson);
            exportViewToCanvas(R.id.hud_btn_zl, "btn_zl", hudJson);
            exportViewToCanvas(R.id.hud_diamond_cluster, "diamond_cluster", hudJson);
            exportViewToCanvas(R.id.hud_top_left_status, "status", hudJson);
            exportViewToCanvas(R.id.hud_bottom_left_collectibles, "rupees", hudJson);
            exportViewToCanvas(R.id.hud_minimap_container, "minimap", hudJson);

            File targetFile = new File(mStorageDir, "custom_hud_layout.json");
            try (FileOutputStream fos = new FileOutputStream(targetFile)) {
                fos.write(hudJson.toString(2).getBytes(StandardCharsets.UTF_8));
                fos.flush();
            }

            Log.i(TAG, "HUD layout saved successfully to " + targetFile.getAbsolutePath());
            Toast.makeText(getContext(), "Posições do HUD salvas com sucesso!", Toast.LENGTH_SHORT).show();

            if (mCallback != null) {
                mCallback.onSaved(this);
            }

        } catch (Throwable t) {
            Log.e(TAG, "Failed to save HUD layout", t);
            Toast.makeText(getContext(), "Erro ao salvar HUD layout: " + t.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void exportViewToCanvas(int viewId, String key, JSONObject out) {
        View target = mHudView.findViewById(viewId);
        if (target == null) return;

        float x = getAbsoluteViewLeft(target);
        float y = getAbsoluteViewTop(target);
        float w = target.getWidth() > 0 ? target.getWidth() : target.getMeasuredWidth();
        float h = target.getHeight() > 0 ? target.getHeight() : target.getMeasuredHeight();

        float rawCanvasX = (x - mOffsetX) / mScale;
        float rawCanvasY = (y - mOffsetY) / mScale;
        float canvasW = w / mScale;
        float canvasH = h / mScale;

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

    private int dpToPx(int dp) {
        float density = getResources().getDisplayMetrics().density;
        return Math.round((float) dp * density);
    }
}
