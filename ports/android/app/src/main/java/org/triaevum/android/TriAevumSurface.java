package org.triaevum.android;

import android.content.Context;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;

/** Pure Android SurfaceView providing native Vulkan swapchain surface without SDL. */
final class TriAevumSurface extends SurfaceView implements SurfaceHolder.Callback {
    private final int maximumShortEdge;

    TriAevumSurface(Context context, int maximumShortEdge) {
        super(context);
        this.maximumShortEdge = maximumShortEdge;
        setLayoutParams(new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        getHolder().addCallback(this);
    }

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);
        int[] extent = SurfaceExtentPolicy.fit(width, height, maximumShortEdge);
        getHolder().setFixedSize(extent[0], extent[1]);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Surface surface = holder.getSurface();
        Log.i("TriAevum", "surfaceCreated: " + surface);
        TriAevumActivity.nativeSurfaceCreated(surface);
        if (getContext() instanceof TriAevumActivity) {
            ((TriAevumActivity) getContext()).onSurfaceReady();
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        int viewWidth = getWidth() > 0 ? getWidth() : width;
        int viewHeight = getHeight() > 0 ? getHeight() : height;
        int[] extent = SurfaceExtentPolicy.fit(viewWidth, viewHeight, maximumShortEdge);
        if (width != extent[0] || height != extent[1]) {
            holder.setFixedSize(extent[0], extent[1]);
            return;
        }
        Log.i("TriAevum", "Render surface " + width + "x" + height
            + ", view " + viewWidth + "x" + viewHeight);
        TriAevumActivity.nativeSurfaceChanged(holder.getSurface(), width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i("TriAevum", "surfaceDestroyed");
        TriAevumActivity.nativeSurfaceDestroyed();
    }
}
