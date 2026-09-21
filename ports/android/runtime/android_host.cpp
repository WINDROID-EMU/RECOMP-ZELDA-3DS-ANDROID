#include "android_host.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <cstdio>
#include <cstdlib>
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
#include "fast/oot3d/graphics_settings_persistence.h"
#include "fast/oot3d/graphics_settings_runtime.h"
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
        runtime.Apply(loaded.Value, false);
        __android_log_print(ANDROID_LOG_INFO, "TriAevum", "Live graphics settings reloaded and applied successfully");
      }
    }
  } catch (const std::exception &e) {
    __android_log_print(ANDROID_LOG_ERROR, "TriAevum", "Failed to reload live graphics settings: %s", e.what());
  }

  NotifyAndroidTopScreenConfigChanged();
}

// Native activity lifecycle and surface management (replacing SDLActivity)

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

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeSurfaceDestroyed(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeSurfaceDestroyed");
  Fast::GfxWindowBackendAndroid::NotifySurfaceDestroyed();
}

JNIEXPORT void JNICALL
Java_org_triaevum_android_TriAevumActivity_nativeOnPause(
    JNIEnv * /*env*/, jclass /*clazz*/) {
  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "nativeOnPause");
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
  int result = RunOot3dNativeGameMain(argc, argv.data());
  __android_log_print(ANDROID_LOG_INFO, "TriAevum", "RunOot3dNativeGameMain exited with code %d", result);
}

} // extern "C"
