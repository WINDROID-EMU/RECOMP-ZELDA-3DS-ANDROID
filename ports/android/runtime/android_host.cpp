#include "android_host.h"

#include <adrenotools/driver.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <jni.h>
#include <mutex>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <ship/config/Config.h>

#include "fast/backends/gfx_android.h"
#include "fast/Fast3dWindow.h"
#include "fast/backends/gfx_rendering_api.h"
#include "fast/oot3d/graphics_settings_persistence.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "oot3d/renderer/azahar_texture_pack.h"
#include "oot3d_top_screen_config.h"

static AndroidOverlayInputState gOverlayInputState;
static std::string gAndroidStoragePath;
static std::mutex gStorageMutex;
static std::mutex gTopScreenConfigMutex;
static std::weak_ptr<Oot3dNativeGame::TopScreenUiConfigRuntime> gAndroidTopScreenConfigRuntime;

void RegisterAndroidTopScreenConfigRuntime(
    std::shared_ptr<Oot3dNativeGame::TopScreenUiConfigRuntime> runtime) {
  std::lock_guard<std::mutex> lock(gTopScreenConfigMutex);
  gAndroidTopScreenConfigRuntime = runtime;
  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "Registered TopScreenUiConfigRuntime with Android host");
}

void NotifyAndroidTopScreenConfigChanged() {
  std::shared_ptr<Oot3dNativeGame::TopScreenUiConfigRuntime> runtime;
  {
    std::lock_guard<std::mutex> lock(gTopScreenConfigMutex);
    runtime = gAndroidTopScreenConfigRuntime.lock();
  }
  if (runtime) {
    std::string error;
    if (runtime->Reload(&error)) {
      __android_log_print(ANDROID_LOG_INFO, "TriAevum", "TopScreenUiConfigRuntime reloaded successfully via Android host");
    } else {
      __android_log_print(ANDROID_LOG_WARN, "TriAevum", "TopScreenUiConfigRuntime reload failed: %s", error.c_str());
    }
  } else {
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "NotifyAndroidTopScreenConfigChanged: No active runtime registered");
  }
}

AndroidOverlayInputState &GetAndroidOverlayInputState() {
  return gOverlayInputState;
}

void SetAndroidStoragePath(const std::string &path) {
  std::lock_guard<std::mutex> lock(gStorageMutex);
  gAndroidStoragePath = path;
}

const std::string &GetAndroidStoragePath() {
  std::lock_guard<std::mutex> lock(gStorageMutex);
  return gAndroidStoragePath;
}

void InitializeAndroidGameHost() {
  setenv("TRIAEVUM_VULKAN_PRESENT_DISPATCH", "graphics", 1);

  std::string root = GetAndroidStoragePath();
  if (root.empty()) {
    const char *envRoot = getenv("EXTERNAL_STORAGE");
    if (envRoot && *envRoot) {
      root = envRoot;
    } else {
      root = "/sdcard/Android/data/org.triaevum.android/files";
    }
  }

  setenv("TRIAEVUM_STORAGE_PATH", root.c_str(), 1);
  setenv("OOT3D_AZAHAR_USER_DIRECTORY", root.c_str(), 1);

  try {
    std::filesystem::current_path(root);
    std::filesystem::create_directories("logs");
    if (!std::freopen("logs/native-stdout.log", "w", stdout) ||
        !std::freopen("logs/native-stderr.log", "w", stderr)) {
      __android_log_print(ANDROID_LOG_WARN, "TriAevum", "Cannot open Android runtime logs");
    } else {
      std::setvbuf(stdout, nullptr, _IOLBF, 0);
      std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
  } catch (const std::exception &e) {
    __android_log_print(ANDROID_LOG_ERROR, "TriAevum", "Failed to setup game storage directories: %s", e.what());
  }

  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "TriAevum game data: %s", root.c_str());
}

void ShutdownAndroidGameHost() {
  auto &state = GetAndroidOverlayInputState();
  state.buttons.store(0, std::memory_order_relaxed);
  state.circlePadX.store(0.0f, std::memory_order_relaxed);
  state.circlePadY.store(0.0f, std::memory_order_relaxed);
  state.cStickX.store(0.0f, std::memory_order_relaxed);
  state.cStickY.store(0.0f, std::memory_order_relaxed);
  state.touchPressed.store(false, std::memory_order_relaxed);
}

extern "C" {

JNIEXPORT void JNICALL
Java_org_triaevum_android_AndroidNativeInputTarget_nativeButton(
    JNIEnv * /*env*/, jclass /*clazz*/, jint hidMask, jboolean pressed) {
  auto &state = GetAndroidOverlayInputState();
  uint32_t current = state.buttons.load(std::memory_order_relaxed);
  if (pressed) {
    current |= static_cast<uint32_t>(hidMask);
  } else {
    current &= ~static_cast<uint32_t>(hidMask);
  }
  state.buttons.store(current, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_AndroidNativeInputTarget_nativeCirclePad(
    JNIEnv * /*env*/, jclass /*clazz*/, jfloat x, jfloat y) {
  auto &state = GetAndroidOverlayInputState();
  state.circlePadX.store(x, std::memory_order_relaxed);
  state.circlePadY.store(y, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_AndroidNativeInputTarget_nativeCStick(
    JNIEnv * /*env*/, jclass /*clazz*/, jfloat x, jfloat y) {
  auto &state = GetAndroidOverlayInputState();
  state.cStickX.store(x, std::memory_order_relaxed);
  state.cStickY.store(y, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_AndroidNativeInputTarget_nativeTouch(
    JNIEnv * /*env*/, jclass /*clazz*/, jfloat x, jfloat y, jboolean pressed) {
  auto &state = GetAndroidOverlayInputState();
  if (!state.touchEnabled.load(std::memory_order_relaxed)) {
    state.touchPressed.store(false, std::memory_order_relaxed);
    return;
  }
  state.touchX.store(x, std::memory_order_relaxed);
  state.touchY.store(y, std::memory_order_relaxed);
  state.touchPressed.store(pressed, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_AndroidNativeInputTarget_nativeSetTouchEnabled(
    JNIEnv * /*env*/, jclass /*clazz*/, jboolean enabled) {
  auto &state = GetAndroidOverlayInputState();
  state.touchEnabled.store(enabled, std::memory_order_relaxed);
  if (!enabled) {
    state.touchPressed.store(false, std::memory_order_relaxed);
  }
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_AndroidNativeInputTarget_nativeReleaseAll(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  auto &state = GetAndroidOverlayInputState();
  state.buttons.store(0, std::memory_order_relaxed);
  state.circlePadX.store(0.0f, std::memory_order_relaxed);
  state.circlePadY.store(0.0f, std::memory_order_relaxed);
  state.cStickX.store(0.0f, std::memory_order_relaxed);
  state.cStickY.store(0.0f, std::memory_order_relaxed);
  state.touchPressed.store(false, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_AndroidNativeInputTarget_nativeSwapScreens(
    JNIEnv * /*env*/, jclass /*clazz*/, jboolean enabled) {
  auto &state = GetAndroidOverlayInputState();
  state.swapScreens.store(enabled, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumConfigManager_nativeReloadGraphicsSettings(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  try {
    auto *context = Ship::Context::GetRawInstance();
    if (context != nullptr && context->GetConfig() != nullptr) {
      context->GetConfig()->Reload();
      nlohmann::json root = context->GetConfig()->GetNestedJson();
      auto &runtime = Fast::Oot3d::GraphicsSettingsRuntime::Instance();
      const auto loaded = Fast::Oot3d::LoadGraphicsSettingsConfig(root, runtime.Snapshot());
      if (loaded.Found && !loaded.UnsupportedFutureVersion) {
        auto settingsToApply = loaded.Value;
        settingsToApply.Window = runtime.Snapshot().Window;
        settingsToApply.OutputWidth = runtime.Snapshot().OutputWidth;
        settingsToApply.OutputHeight = runtime.Snapshot().OutputHeight;
        if (settingsToApply.InternalResolutionScale != 1.0f ||
            settingsToApply.AntiAliasing != Fast::Oot3d::AntiAliasingMode::Off ||
            settingsToApply.FrameRate != Fast::Oot3d::FrameRateMode::Original30 ||
            settingsToApply.Grass.Quality != Fast::Oot3d::GrassQuality::Off ||
            settingsToApply.Effects.Toon != Fast::Oot3d::ToonMode::Off ||
            settingsToApply.Effects.DirectionalShadows.Mode != Fast::Oot3d::DirectionalShadowMode::Off) {
          settingsToApply.Preset = Fast::Oot3d::GraphicsPreset::Custom;
        }
        if (runtime.NativePresentationOverrideActive()) {
          runtime.ToggleNativePresentationOverride();
        }
        setenv("OOT3D_GRAPHICS_GRASS_AUTO", "1", 0);

        const auto &caps = runtime.Capabilities();
        __android_log_print(
            ANDROID_LOG_INFO, "TriAevum",
            "Live reload capabilities: NriInterop=%d ('%s'), NriDirectionalShadows=%d ('%s')",
            (int)caps.Has(Fast::Oot3d::GraphicsCapability::NriInterop),
            caps.Get(Fast::Oot3d::GraphicsCapability::NriInterop).Reason.c_str(),
            (int)caps.Has(Fast::Oot3d::GraphicsCapability::NriDirectionalShadows),
            caps.Get(Fast::Oot3d::GraphicsCapability::NriDirectionalShadows).Reason.c_str());
        __android_log_print(
            ANDROID_LOG_INFO, "TriAevum",
            "Live reload candidate DirectionalShadows.Mode=%d",
            static_cast<int>(settingsToApply.Effects.DirectionalShadows.Mode));

        auto valResult = runtime.Apply(settingsToApply, false);
        for (const auto &issue : valResult.Issues) {
          __android_log_print(
              ANDROID_LOG_WARN, "TriAevum",
              "Live reload validation issue: field='%s' msg='%s' corrected=%d",
              issue.Field.c_str(), issue.Message.c_str(), (int)issue.Corrected);
        }
        ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance().Configure({
            .DumpTextures = loaded.Value.TexturePacks.Azahar.DumpTextures,
            .LoadCustomTextures = loaded.Value.TexturePacks.Azahar.LoadCustomTextures,
            .PreloadTextures = loaded.Value.TexturePacks.Azahar.PreloadTextures,
            .LoadDirectory = loaded.Value.TexturePacks.Azahar.LoadDirectory,
            .DumpDirectory = loaded.Value.TexturePacks.Azahar.DumpDirectory,
        });
        __android_log_print(
            ANDROID_LOG_INFO, "TriAevum",
            "Live graphics settings reloaded and applied: preset=%d scale=%.2f aa=%d fps=%d vsync=%d (custom textures: %d, dir: %s)",
            static_cast<int>(settingsToApply.Preset),
            settingsToApply.InternalResolutionScale,
            static_cast<int>(settingsToApply.AntiAliasing),
            static_cast<int>(settingsToApply.FrameRate),
            settingsToApply.VSync,
            loaded.Value.TexturePacks.Azahar.LoadCustomTextures,
            loaded.Value.TexturePacks.Azahar.LoadDirectory.c_str());
      }
    }
  } catch (const std::exception &e) {
    __android_log_print(ANDROID_LOG_ERROR, "TriAevum", "Failed to reload live graphics settings: %s", e.what());
  }

  NotifyAndroidTopScreenConfigChanged();
}

JNIEXPORT jboolean JNICALL
Java_org_triaevum_android_TriAevumConfigManager_nativeIsDirectionalShadowsSupported(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  auto &runtime = Fast::Oot3d::GraphicsSettingsRuntime::Instance();
  return runtime.Capabilities().Has(Fast::Oot3d::GraphicsCapability::NriDirectionalShadows) ? JNI_TRUE : JNI_FALSE;
}

// Native activity lifecycle and surface management (replacing SDLActivity)

static void *gCustomDriverHandle = nullptr;

JNIEXPORT jboolean JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeInitCustomDriver(
    JNIEnv *env, jclass /*clazz*/, jstring hookDir, jstring customDriverDir, jstring customDriverName, jstring tmpDir) {
  if (customDriverDir == nullptr || customDriverName == nullptr) {
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeInitCustomDriver: Using System Default Vulkan Driver");
    return JNI_FALSE;
  }

  const char *hookChars = hookDir ? env->GetStringUTFChars(hookDir, nullptr) : nullptr;
  const char *driverDirChars = env->GetStringUTFChars(customDriverDir, nullptr);
  const char *driverNameChars = env->GetStringUTFChars(customDriverName, nullptr);
  const char *tmpDirChars = tmpDir ? env->GetStringUTFChars(tmpDir, nullptr) : nullptr;

  std::string sHookDir = hookChars ? hookChars : "";
  std::string sDriverDir = driverDirChars ? driverDirChars : "";
  std::string sDriverName = driverNameChars ? driverNameChars : "";
  std::string sTmpDir = tmpDirChars ? tmpDirChars : "";

  if (hookChars) env->ReleaseStringUTFChars(hookDir, hookChars);
  if (driverDirChars) env->ReleaseStringUTFChars(customDriverDir, driverDirChars);
  if (driverNameChars) env->ReleaseStringUTFChars(customDriverName, driverNameChars);
  if (tmpDirChars) env->ReleaseStringUTFChars(tmpDir, tmpDirChars);

  if (sDriverDir.empty() || sDriverName.empty()) {
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeInitCustomDriver: No custom driver configured, using System Driver");
    return JNI_FALSE;
  }

  if (sDriverDir.back() != '/') {
    sDriverDir += '/';
  }

  __android_log_print(ANDROID_LOG_INFO, "TriAevum",
                      "nativeInitCustomDriver: Initializing adrenotools for driver '%s' at '%s' (hookDir='%s', tmpDir='%s')",
                      sDriverName.c_str(), sDriverDir.c_str(), sHookDir.c_str(), sTmpDir.c_str());

  dlerror(); // Clear any existing dlerror
  void *handle = adrenotools_open_libvulkan(
      RTLD_NOW,
      ADRENOTOOLS_DRIVER_CUSTOM,
      sTmpDir.empty() ? nullptr : sTmpDir.c_str(),
      sHookDir.empty() ? nullptr : sHookDir.c_str(),
      sDriverDir.c_str(),
      sDriverName.c_str(),
      nullptr,
      nullptr);

  if (handle != nullptr) {
    gCustomDriverHandle = handle;
    __android_log_print(ANDROID_LOG_INFO, "TriAevum",
                        "SUCCESS: Custom Turnip Vulkan driver loaded via adrenotools: handle=%p", handle);
    return JNI_TRUE;
  } else {
    const char *err = dlerror();
    __android_log_print(ANDROID_LOG_ERROR, "TriAevum",
                        "FAILED: adrenotools_open_libvulkan returned null for '%s'! dlerror='%s'. Reverting to System Vulkan Driver.",
                        sDriverName.c_str(), err ? err : "none");
    return JNI_FALSE;
  }
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeSetStoragePath(
    JNIEnv *env, jclass /*clazz*/, jstring path) {
  if (path != nullptr) {
    const char *chars = env->GetStringUTFChars(path, nullptr);
    if (chars != nullptr) {
      SetAndroidStoragePath(chars);
      setenv("TRIAEVUM_STORAGE_PATH", chars, 1);
      env->ReleaseStringUTFChars(path, chars);
    }
  }
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeSurfaceCreated(
    JNIEnv *env, jclass /*clazz*/, jobject surface) {
  if (surface != nullptr) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeSurfaceCreated: ANativeWindow=%p", win);
    Fast::GfxWindowBackendAndroid::NotifySurfaceCreated(win);
  }
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeSurfaceChanged(
    JNIEnv *env, jclass /*clazz*/, jobject surface, jint width, jint height) {
  if (surface != nullptr) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeSurfaceChanged: ANativeWindow=%p, %dx%d", win, width, height);
    Fast::GfxWindowBackendAndroid::NotifySurfaceChanged(win, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  }
}

static void FlushAndroidPipelineCache() {
  try {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx) return;
    auto window = ctx->GetWindow();
    if (!window) return;
    auto fastWindow = std::dynamic_pointer_cast<Fast::Fast3dWindow>(window);
    if (!fastWindow) return;
    auto rapi = fastWindow->GetCurrentRenderingAPI();
    if (rapi) {
      __android_log_print(ANDROID_LOG_INFO, "TriAevum", "Flushing Vulkan pipeline cache from Android lifecycle");
      rapi->FlushPipelineCache();
    }
  } catch (const std::exception &e) {
    __android_log_print(ANDROID_LOG_WARN, "TriAevum", "Failed to flush pipeline cache: %s", e.what());
  } catch (...) {
    __android_log_print(ANDROID_LOG_WARN, "TriAevum", "Unknown error while flushing pipeline cache");
  }
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeSurfaceDestroyed(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeSurfaceDestroyed");
  Fast::GfxWindowBackendAndroid::NotifySurfaceDestroyed();
  FlushAndroidPipelineCache();
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeOnPause(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeOnPause");
  FlushAndroidPipelineCache();
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeOnResume(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeOnResume");
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeMain(
    JNIEnv *env, jclass /*clazz*/, jobjectArray args) {
  std::vector<std::string> argStrings;
  std::vector<char *> argv;

  if (args != nullptr) {
    jsize count = env->GetArrayLength(args);
    argStrings.reserve(count);
    for (jsize i = 0; i < count; ++i) {
      jstring str = (jstring)env->GetObjectArrayElement(args, i);
      if (str != nullptr) {
        const char *utf = env->GetStringUTFChars(str, nullptr);
        argStrings.emplace_back(utf ? utf : "");
        env->ReleaseStringUTFChars(str, utf);
        env->DeleteLocalRef(str);
      }
    }
  }

  argv.reserve(argStrings.size());
  for (auto &s : argStrings) {
    argv.push_back(s.data());
  }

  int argc = static_cast<int>(argv.size());
  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "Starting RunOot3dNativeGameMain with %d args", argc);
  try {
    int result = RunOot3dNativeGameMain(argc, argv.data());
    __android_log_print(ANDROID_LOG_INFO, "TriAevum", "RunOot3dNativeGameMain exited with code %d", result);
  } catch (const std::exception &e) {
    __android_log_print(ANDROID_LOG_ERROR, "TriAevum", "FATAL unhandled exception in RunOot3dNativeGameMain: %s", e.what());
  } catch (...) {
    __android_log_print(ANDROID_LOG_ERROR, "TriAevum", "FATAL unknown exception in RunOot3dNativeGameMain");
  }
}

JNIEXPORT jint JNICALL
Java_org_triaevum_android_TriAevumActivity_nativePreloadCustomTextures(
    JNIEnv *env, jclass /*clazz*/, jstring loadDirectory, jobject callback) {
  std::string dirStr;
  if (loadDirectory != nullptr) {
    const char *chars = env->GetStringUTFChars(loadDirectory, nullptr);
    if (chars != nullptr) {
      dirStr = chars;
      env->ReleaseStringUTFChars(loadDirectory, chars);
    }
  }

  __android_log_print(ANDROID_LOG_INFO, "TriAevum",
                      "nativePreloadCustomTextures starting for directory: %s",
                      dirStr.c_str());

  auto &textureRuntime = ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance();
  textureRuntime.Configure({
      .DumpTextures = false,
      .LoadCustomTextures = true,
      .PreloadTextures = true,
      .LoadDirectory = dirStr.empty() ? std::filesystem::path{} : std::filesystem::path{dirStr},
  });

  const auto snap = textureRuntime.Snapshot();
  __android_log_print(ANDROID_LOG_INFO, "TriAevum",
                      "nativePreloadCustomTextures snapshot: indexed=%zu, dir=%s, err=%s",
                      snap.IndexedTextures, snap.LoadDirectory.c_str(), snap.LastError.c_str());

  jclass callbackClass = callback != nullptr ? env->GetObjectClass(callback) : nullptr;
  jmethodID onProgressMethod = callbackClass != nullptr
                                   ? env->GetMethodID(callbackClass, "onProgress", "(II)V")
                                   : nullptr;

  size_t loaded = textureRuntime.PreloadAll([&](size_t current, size_t total) {
    if (env != nullptr && callback != nullptr && onProgressMethod != nullptr) {
      env->CallVoidMethod(callback, onProgressMethod, static_cast<jint>(current),
                          static_cast<jint>(total));
      if (env->ExceptionCheck()) {
        env->ExceptionClear();
      }
    }
  });

  __android_log_print(ANDROID_LOG_INFO, "TriAevum",
                      "nativePreloadCustomTextures completed: %zu textures preloaded into cache",
                      loaded);
  return static_cast<jint>(loaded);
}

static std::atomic<float> gAndroidGameFps{0.0f};

void RecordAndroidGamePresentationFrame() {
  static auto sLastFpsTime = std::chrono::steady_clock::now();
  static uint32_t sFrameCount = 0;

  sFrameCount++;
  auto now = std::chrono::steady_clock::now();
  auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - sLastFpsTime).count();
  if (elapsedMs >= 500) {
    float fps = (static_cast<float>(sFrameCount) * 1000.0f) / static_cast<float>(elapsedMs);
    gAndroidGameFps.store(fps, std::memory_order_relaxed);
    sFrameCount = 0;
    sLastFpsTime = now;
  }
}

float GetAndroidGameFps() {
  return gAndroidGameFps.load(std::memory_order_relaxed);
}

JNIEXPORT jfloat JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeGetGameFps(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  return GetAndroidGameFps();
}

} // extern "C"
