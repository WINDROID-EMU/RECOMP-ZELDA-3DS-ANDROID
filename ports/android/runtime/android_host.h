#pragma once

#include <atomic>
#include <cstdint>

struct AndroidOverlayInputState {
  std::atomic<uint32_t> buttons{0};
  std::atomic<float> circlePadX{0.0f};
  std::atomic<float> circlePadY{0.0f};
  std::atomic<float> cStickX{0.0f};
  std::atomic<float> cStickY{0.0f};
  std::atomic<float> touchX{0.0f};
  std::atomic<float> touchY{0.0f};
  std::atomic<bool> touchPressed{false};
};

AndroidOverlayInputState &GetAndroidOverlayInputState();

void InitializeAndroidGameHost();
void ShutdownAndroidGameHost();
