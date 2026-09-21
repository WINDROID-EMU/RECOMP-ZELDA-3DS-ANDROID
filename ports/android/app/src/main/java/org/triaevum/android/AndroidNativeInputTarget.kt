package org.triaevum.android

import org.triaevum.android.controls.Native3dsInputTarget

class AndroidNativeInputTarget : Native3dsInputTarget {
    companion object {
        @JvmStatic external fun nativeButton(hidMask: Int, pressed: Boolean)
        @JvmStatic external fun nativeCirclePad(x: Float, y: Float)
        @JvmStatic external fun nativeCStick(x: Float, y: Float)
        @JvmStatic external fun nativeTouch(x: Float, y: Float, pressed: Boolean)
        @JvmStatic external fun nativeSetTouchEnabled(enabled: Boolean)
        @JvmStatic external fun nativeSwapScreens(enabled: Boolean)
        @JvmStatic external fun nativeReleaseAll()
    }

    override fun button(hidMask: Int, pressed: Boolean) {
        nativeButton(hidMask, pressed)
    }

    override fun circlePad(x: Float, y: Float) {
        nativeCirclePad(x, y)
    }

    override fun cStick(x: Float, y: Float) {
        nativeCStick(x, y)
    }

    override fun home(pressed: Boolean) {
        // Presentation / system home action
    }

    override fun touchPixels(x: Float, y: Float, pressed: Boolean) {
        nativeTouch(x, y, pressed)
    }

    override fun moveTouchPixels(x: Float, y: Float) {
        nativeTouch(x, y, true)
    }

    override fun swapScreens(enabled: Boolean, displayRotation: Int) {
        nativeSwapScreens(enabled)
    }

    override fun toggleTurbo(fromOverlay: Boolean) {
        // Turbo toggle
    }

    override fun releaseAll() {
        nativeReleaseAll()
    }
}
