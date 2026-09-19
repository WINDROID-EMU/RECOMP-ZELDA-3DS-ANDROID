#pragma once

#if defined(__ANDROID__)

#include <android/native_window.h>
#include <atomic>
#include <chrono>
#include "gfx_window_manager_api.h"

namespace Fast {

class GfxWindowBackendAndroid final : public GfxWindowBackend {
  public:
    GfxWindowBackendAndroid();
    ~GfxWindowBackendAndroid() override;

    void Init(const char* gameName, const char* apiName, bool startFullScreen, uint32_t width, uint32_t height,
              int32_t posX, int32_t posY) override;
    void Close() override;
    void SetKeyboardCallbacks(bool (*onKeyDown)(int scancode), bool (*onKeyUp)(int scancode),
                              void (*onAllKeysUp)()) override;
    void SetMouseCallbacks(bool (*onMouseButtonDown)(int btn), bool (*onMouseButtonUp)(int btn)) override;
    void SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool is_now_fullscreen)) override;
    void SetFullscreen(bool fullscreen) override;
    void GetActiveWindowRefreshRate(uint32_t* refreshRate) override;
    void SetCursorVisibility(bool visibility) override;
    void SetMousePos(int32_t posX, int32_t posY) override;
    void GetMousePos(int32_t* x, int32_t* y) override;
    void GetMouseDelta(int32_t* x, int32_t* y) override;
    void GetMouseWheel(float* x, float* y) override;
    bool GetMouseState(uint32_t btn) override;
    void SetMouseCapture(bool capture) override;
    bool IsMouseCaptured() override;
    void GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) override;
    void SetDimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) override;
    Ship::WindowRect GetPrimaryMonitorRect() override;
    void HandleEvents() override;
    void RequestFocus() override;
    bool IsFrameReady() override;
    void SwapBuffersBegin() override;
    void SwapBuffersEnd() override;
    double GetTime() override;
    int GetTargetFps() override;
    void SetTargetFps(int fps) override;
    void SetMaxFrameLatency(int latency) override;
    const char* GetKeyName(int scancode) override;
    bool CanDisableVsync() override;
    bool IsRunning() override;
    void Destroy() override;
    bool IsFullscreen() override;
    bool IsWindowedFullscreen() const override;
    void SetWindowedFullscreen(bool enabled) override;
    bool SetExclusiveFullscreenDisplayMode(uint32_t width, uint32_t height) override;
    void* GetNativeWindow() const override;
    bool UsesVulkan() const override;

    static void NotifySurfaceCreated(ANativeWindow* window);
    static void NotifySurfaceChanged(ANativeWindow* window, uint32_t width, uint32_t height);
    static void NotifySurfaceDestroyed();
    static void SetGlobalNativeWindow(ANativeWindow* window);
    static ANativeWindow* GetGlobalNativeWindow();
    static void SetGlobalDimensions(uint32_t width, uint32_t height);
    static void SignalStop();
    static bool IsSurfaceAvailable();

  private:
    ANativeWindow* mNativeWindow = nullptr;
    uint32_t mWidth = 1280;
    uint32_t mHeight = 720;
    int32_t mPosX = 0;
    int32_t mPosY = 0;
    bool mWindowedFullscreen = true;
    std::chrono::steady_clock::time_point mStartTime;

    bool (*mOnKeyDown)(int) = nullptr;
    bool (*mOnKeyUp)(int) = nullptr;
    bool (*mOnMouseButtonDown)(int) = nullptr;
    bool (*mOnMouseButtonUp)(int) = nullptr;
    void (*mOnFullscreenChanged)(bool) = nullptr;
};

} // namespace Fast

#endif // defined(__ANDROID__)
