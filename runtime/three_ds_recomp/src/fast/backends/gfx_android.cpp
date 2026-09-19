#if defined(__ANDROID__)

#include "fast/backends/gfx_android.h"

#include <android/log.h>
#include <android/native_window.h>
#include <condition_variable>
#include <mutex>
#include <spdlog/spdlog.h>
#include <ship/Context.h>
#include "fast/Fast3dGui.h"
#include "fast/Fast3dWindow.h"

namespace Fast {

static std::mutex sWindowMutex;
static std::condition_variable sWindowCond;
static std::condition_variable sRenderPausedCond;
static ANativeWindow* sGlobalNativeWindow = nullptr;
static uint32_t sGlobalWidth = 1280;
static uint32_t sGlobalHeight = 720;
static std::atomic<bool> sIsRunningGlobal{true};
static std::atomic<bool> sSurfaceAvailable{false};
static std::atomic<bool> sRenderThreadWaiting{false};

void GfxWindowBackendAndroid::NotifySurfaceCreated(ANativeWindow* window) {
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "NotifySurfaceCreated: ANativeWindow=%p", window);
    {
        std::unique_lock<std::mutex> lock(sWindowMutex);
        sGlobalNativeWindow = window;
        sSurfaceAvailable.store(window != nullptr, std::memory_order_release);
    }
    sWindowCond.notify_all();
}

void GfxWindowBackendAndroid::NotifySurfaceChanged(ANativeWindow* window, uint32_t width, uint32_t height) {
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "NotifySurfaceChanged: ANativeWindow=%p, %ux%u", window, width, height);
    {
        std::unique_lock<std::mutex> lock(sWindowMutex);
        sGlobalNativeWindow = window;
        if (width > 0 && height > 0) {
            sGlobalWidth = width;
            sGlobalHeight = height;
        }
        sSurfaceAvailable.store(window != nullptr, std::memory_order_release);
    }
    sWindowCond.notify_all();
}

void GfxWindowBackendAndroid::NotifySurfaceDestroyed() {
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "NotifySurfaceDestroyed: waiting for render thread to pause");
    {
        std::unique_lock<std::mutex> lock(sWindowMutex);
        sSurfaceAvailable.store(false, std::memory_order_release);
        sGlobalNativeWindow = nullptr;

        // Give the render thread up to 250ms to reach IsFrameReady and pause
        sRenderPausedCond.wait_for(lock, std::chrono::milliseconds(250), [] {
            return sRenderThreadWaiting.load(std::memory_order_acquire) ||
                   !sIsRunningGlobal.load(std::memory_order_relaxed);
        });
    }
    sWindowCond.notify_all();
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "NotifySurfaceDestroyed: render thread acknowledged, surface safely detached");
}

void GfxWindowBackendAndroid::SetGlobalNativeWindow(ANativeWindow* window) {
    if (window != nullptr) {
        NotifySurfaceCreated(window);
    } else {
        NotifySurfaceDestroyed();
    }
}

ANativeWindow* GfxWindowBackendAndroid::GetGlobalNativeWindow() {
    std::unique_lock<std::mutex> lock(sWindowMutex);
    return sGlobalNativeWindow;
}

void GfxWindowBackendAndroid::SetGlobalDimensions(uint32_t width, uint32_t height) {
    std::unique_lock<std::mutex> lock(sWindowMutex);
    if (width > 0 && height > 0) {
        sGlobalWidth = width;
        sGlobalHeight = height;
    }
}

bool GfxWindowBackendAndroid::IsSurfaceAvailable() {
    return sSurfaceAvailable.load(std::memory_order_acquire);
}

void GfxWindowBackendAndroid::SignalStop() {
    sIsRunningGlobal.store(false, std::memory_order_relaxed);
    sWindowCond.notify_all();
    sRenderPausedCond.notify_all();
}

GfxWindowBackendAndroid::GfxWindowBackendAndroid()
    : mNativeWindow(sGlobalNativeWindow),
      mWidth(sGlobalWidth),
      mHeight(sGlobalHeight),
      mStartTime(std::chrono::steady_clock::now()) {
    mFullScreen = true;
    mIsRunning = true;
    mTargetFps = 60;
    mVsyncEnabled = true;
    mWindowedFullscreen = true;
}

GfxWindowBackendAndroid::~GfxWindowBackendAndroid() {
    Close();
}

void GfxWindowBackendAndroid::Init(const char* gameName, const char* apiName, bool startFullScreen,
                                   uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    (void)gameName;
    (void)apiName;
    (void)startFullScreen;

    mWidth = width > 0 ? width : sGlobalWidth;
    mHeight = height > 0 ? height : sGlobalHeight;
    mPosX = posX;
    mPosY = posY;
    mFullScreen = true;
    mIsRunning = true;

    if (mNativeWindow == nullptr) {
        mNativeWindow = sGlobalNativeWindow;
    }

    if (mNativeWindow != nullptr) {
        int32_t realWidth = ANativeWindow_getWidth(mNativeWindow);
        int32_t realHeight = ANativeWindow_getHeight(mNativeWindow);
        if (realWidth > 0 && realHeight > 0) {
            mWidth = static_cast<uint32_t>(realWidth);
            mHeight = static_cast<uint32_t>(realHeight);
        }
        SPDLOG_INFO("GfxWindowBackendAndroid initialized with native window {} ({}x{})",
                    fmt::ptr(mNativeWindow), mWidth, mHeight);
    } else {
        SPDLOG_WARN("GfxWindowBackendAndroid initialized without native window (provisional {}x{}); "
                    "will retry from global on first GetNativeWindow() call",
                    mWidth, mHeight);
    }

    GuiWindowInitData windowImpl{};
    windowImpl.Vulkan.Window = static_cast<void*>(mNativeWindow);
    windowImpl.Backend = WindowBackend::FAST3D_SDL_OOT3D_VULKAN;
    auto context = Ship::Context::GetRawInstance();
    if (context && context->GetWindow()) {
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(context->GetWindow()->GetGui());
        if (gui) {
            gui->Init(windowImpl);
        }
    }
}

void GfxWindowBackendAndroid::Close() {
    mIsRunning = false;
}

void GfxWindowBackendAndroid::Destroy() {
    Close();
}

void GfxWindowBackendAndroid::SetKeyboardCallbacks(bool (*onKeyDown)(int scancode),
                                                  bool (*onKeyUp)(int scancode),
                                                  void (*onAllKeysUp)()) {
    mOnKeyDown = onKeyDown;
    mOnKeyUp = onKeyUp;
    (void)onAllKeysUp;
}

void GfxWindowBackendAndroid::SetMouseCallbacks(bool (*onMouseButtonDown)(int btn),
                                                bool (*onMouseButtonUp)(int btn)) {
    mOnMouseButtonDown = onMouseButtonDown;
    mOnMouseButtonUp = onMouseButtonUp;
}

void GfxWindowBackendAndroid::SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool is_now_fullscreen)) {
    mOnFullscreenChanged = onFullscreenChanged;
}

void GfxWindowBackendAndroid::SetFullscreen(bool fullscreen) {
    mFullScreen = fullscreen;
    if (mOnFullscreenChanged != nullptr) {
        mOnFullscreenChanged(mFullScreen);
    }
}

bool GfxWindowBackendAndroid::IsFullscreen() {
    return mFullScreen;
}

bool GfxWindowBackendAndroid::IsWindowedFullscreen() const {
    return mWindowedFullscreen;
}

void GfxWindowBackendAndroid::SetWindowedFullscreen(bool enabled) {
    mWindowedFullscreen = enabled;
}

bool GfxWindowBackendAndroid::SetExclusiveFullscreenDisplayMode(uint32_t width, uint32_t height) {
    (void)width;
    (void)height;
    return true;
}

void GfxWindowBackendAndroid::GetActiveWindowRefreshRate(uint32_t* refreshRate) {
    if (refreshRate != nullptr) {
        *refreshRate = 60;
    }
}

void GfxWindowBackendAndroid::SetCursorVisibility(bool visibility) {
    (void)visibility;
}

void GfxWindowBackendAndroid::SetMousePos(int32_t posX, int32_t posY) {
    (void)posX;
    (void)posY;
}

void GfxWindowBackendAndroid::GetMousePos(int32_t* x, int32_t* y) {
    if (x != nullptr) *x = 0;
    if (y != nullptr) *y = 0;
}

void GfxWindowBackendAndroid::GetMouseDelta(int32_t* x, int32_t* y) {
    if (x != nullptr) *x = 0;
    if (y != nullptr) *y = 0;
}

void GfxWindowBackendAndroid::GetMouseWheel(float* x, float* y) {
    if (x != nullptr) *x = 0.0f;
    if (y != nullptr) *y = 0.0f;
}

bool GfxWindowBackendAndroid::GetMouseState(uint32_t btn) {
    (void)btn;
    return false;
}

void GfxWindowBackendAndroid::SetMouseCapture(bool capture) {
    (void)capture;
}

bool GfxWindowBackendAndroid::IsMouseCaptured() {
    return false;
}

void GfxWindowBackendAndroid::GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    std::unique_lock<std::mutex> lock(sWindowMutex);
    ANativeWindow* win = sGlobalNativeWindow ? sGlobalNativeWindow : mNativeWindow;
    if (win != nullptr) {
        int32_t realWidth = ANativeWindow_getWidth(win);
        int32_t realHeight = ANativeWindow_getHeight(win);
        if (realWidth > 0 && realHeight > 0) {
            mWidth = static_cast<uint32_t>(realWidth);
            mHeight = static_cast<uint32_t>(realHeight);
        }
    } else if (sGlobalWidth > 0 && sGlobalHeight > 0) {
        mWidth = sGlobalWidth;
        mHeight = sGlobalHeight;
    }
    if (width != nullptr) *width = mWidth;
    if (height != nullptr) *height = mHeight;
    if (posX != nullptr) *posX = mPosX;
    if (posY != nullptr) *posY = mPosY;
}

void GfxWindowBackendAndroid::SetDimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    mWidth = width;
    mHeight = height;
    mPosX = posX;
    mPosY = posY;
}

Ship::WindowRect GfxWindowBackendAndroid::GetPrimaryMonitorRect() {
    Ship::WindowRect rect{};
    rect.Left = 0;
    rect.Top = 0;
    rect.Right = static_cast<int32_t>(mWidth);
    rect.Bottom = static_cast<int32_t>(mHeight);
    return rect;
}

void GfxWindowBackendAndroid::HandleEvents() {
    if (!sIsRunningGlobal.load(std::memory_order_relaxed)) {
        mIsRunning = false;
    }
    std::unique_lock<std::mutex> lock(sWindowMutex);
    mNativeWindow = sGlobalNativeWindow;
}

void GfxWindowBackendAndroid::RequestFocus() {}

bool GfxWindowBackendAndroid::IsFrameReady() {
    std::unique_lock<std::mutex> lock(sWindowMutex);
    while (!sSurfaceAvailable.load(std::memory_order_acquire) || sGlobalNativeWindow == nullptr) {
        if (!sIsRunningGlobal.load(std::memory_order_relaxed)) {
            return false;
        }
        sRenderThreadWaiting.store(true, std::memory_order_release);
        sRenderPausedCond.notify_all();

        // Wait on condition variable while in background without burning CPU
        sWindowCond.wait(lock, [] {
            return (sSurfaceAvailable.load(std::memory_order_acquire) && sGlobalNativeWindow != nullptr) ||
                   !sIsRunningGlobal.load(std::memory_order_relaxed);
        });

        sRenderThreadWaiting.store(false, std::memory_order_release);
    }
    mNativeWindow = sGlobalNativeWindow;
    return true;
}

void GfxWindowBackendAndroid::SwapBuffersBegin() {}

void GfxWindowBackendAndroid::SwapBuffersEnd() {}

double GfxWindowBackendAndroid::GetTime() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - mStartTime).count();
}

int GfxWindowBackendAndroid::GetTargetFps() {
    return static_cast<int>(mTargetFps);
}

void GfxWindowBackendAndroid::SetTargetFps(int fps) {
    mTargetFps = fps > 0 ? static_cast<uint32_t>(fps) : 60;
}

void GfxWindowBackendAndroid::SetMaxFrameLatency(int latency) {
    (void)latency;
}

const char* GfxWindowBackendAndroid::GetKeyName(int scancode) {
    (void)scancode;
    return "Unknown";
}

bool GfxWindowBackendAndroid::CanDisableVsync() {
    return false;
}

bool GfxWindowBackendAndroid::IsRunning() {
    return mIsRunning;
}

void* GfxWindowBackendAndroid::GetNativeWindow() const {
    std::unique_lock<std::mutex> lock(sWindowMutex);
    return sGlobalNativeWindow;
}

bool GfxWindowBackendAndroid::UsesVulkan() const {
    return true;
}

} // namespace Fast

#endif // defined(__ANDROID__)
