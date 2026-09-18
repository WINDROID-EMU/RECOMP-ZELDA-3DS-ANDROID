#include "android_host.h"

#include <SDL.h>
#include <cstdio>
#include <filesystem>
#include <jni.h>
#include <sched.h>
#include <stdexcept>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <ship/config/Config.h>
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/graphics_settings_persistence.h"

static AndroidOverlayInputState gOverlayInputState;

AndroidOverlayInputState &GetAndroidOverlayInputState() {
  return gOverlayInputState;
}

void InitializeAndroidGameHost() {
  SDL_setenv("SDL_AUDIODRIVER", "aaudio,openslES", 1);
  SDL_setenv("TRIAEVUM_VULKAN_PRESENT_DISPATCH", "graphics", 1);

  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  const char *root = SDL_AndroidGetExternalStoragePath();
  if (!root || !*root) {
    throw std::runtime_error(
        "Android application data directory is unavailable");
  }
  std::filesystem::current_path(root);
  std::filesystem::create_directories("logs");
  // Android does not preserve a console stream; retain diagnostics beside user
  // data.
  if (!std::freopen("logs/native-stdout.log", "w", stdout) ||
      !std::freopen("logs/native-stderr.log", "w", stderr)) {
    throw std::runtime_error("Cannot open Android runtime logs");
  }
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  SDL_Log("TriAevum game data: %s", root);
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
  state.touchX.store(x, std::memory_order_relaxed);
  state.touchY.store(y, std::memory_order_relaxed);
  state.touchPressed.store(pressed, std::memory_order_relaxed);
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
        SDL_Log("TriAevum: Live graphics settings reloaded and applied successfully");
      }
    }
  } catch (const std::exception &e) {
    SDL_Log("TriAevum: Failed to reload live graphics settings: %s", e.what());
  }
}

} // extern "C"
