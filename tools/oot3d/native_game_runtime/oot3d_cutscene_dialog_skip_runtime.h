#pragma once

#include <cstdint>

namespace oot3d::recomp::a32 {
class MemoryBus;
}

namespace Oot3dNativeGame {

struct CutsceneDialogSkipConfig {
    float HoldThresholdSeconds = 0.20f; // 6 quadros a 30 FPS segurando B
    uint16_t CutsceneFrameStep = 10;    // Avanço de 10 quadros por tick (~10x velocidade)
};

enum class CutsceneDialogSkipTarget : uint8_t {
    None,
    Dialog,
    Cutscene
};

struct CutsceneDialogSkipStatus {
    CutsceneDialogSkipTarget Target = CutsceneDialogSkipTarget::None;
    bool IsActive = false;
    bool ShouldPulseAdvance = false;
    float HoldDurationSeconds = 0.0f;
    uint16_t CutsceneFramesAdvanced = 0;
};

class CutsceneDialogSkipRuntime {
public:
    CutsceneDialogSkipRuntime() = default;
    explicit CutsceneDialogSkipRuntime(CutsceneDialogSkipConfig config);

    void Reset();

    CutsceneDialogSkipStatus Update(bool bButtonHeld, float deltaSeconds);

    [[nodiscard]] bool IsActive() const noexcept { return mHoldTime >= mConfig.HoldThresholdSeconds; }
    [[nodiscard]] float HoldDuration() const noexcept { return mHoldTime; }
    [[nodiscard]] const CutsceneDialogSkipConfig& Config() const noexcept { return mConfig; }

private:
    CutsceneDialogSkipConfig mConfig;
    float mHoldTime = 0.0f;
    uint32_t mPulseFrame = 0;
};

// Hook nativo de alto desempenho que lê a memória guest e acelera diálogos / cutscenes
CutsceneDialogSkipStatus ApplyGuestCutsceneDialogSkip(
    oot3d::recomp::a32::MemoryBus& memory,
    CutsceneDialogSkipRuntime& skipRuntime,
    bool bButtonHeld,
    uint32_t* inOutGuestButtons = nullptr,
    float deltaSeconds = 1.0f / 30.0f);

} // namespace Oot3dNativeGame
