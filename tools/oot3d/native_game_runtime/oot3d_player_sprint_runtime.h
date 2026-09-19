#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oot3d::recomp::a32 {
class MemoryBus;
}

namespace Oot3dNativeGame {

enum class PlayerSprintState : uint8_t {
    Idle,
    RollWaiting,
    Sprinting,
    ExhaustedCooldown
};

struct PlayerSprintConfig {
    float MaxSprintDurationSeconds = 15.0f;
    float CooldownDurationSeconds = 15.0f;
    float MaxSprintMultiplier = 1.325f; // 1.325x: metade do bônus anterior (era 1.65x)
    float SprintRampUpTimeSeconds = 1.2f; // Rampa gradual suave
    float SprintRampDownTimeSeconds = 0.3f;
    float MinStickMagnitude = 20.0f;
    float RollDurationSeconds = 0.75f; // 22.5 quadros a 30 FPS para Link levantar totalmente
};

struct PlayerSprintStatus {
    PlayerSprintState State = PlayerSprintState::Idle;
    float SpeedMultiplier = 1.0f;
    float StaminaRemainingSeconds = 15.0f;
    float CooldownRemainingSeconds = 0.0f;
    bool IsSprinting = false;
    bool IsPushingBoxes = false;
};

class PlayerSprintRuntime {
public:
    PlayerSprintRuntime() = default;
    explicit PlayerSprintRuntime(PlayerSprintConfig config);

    void Reset();

    PlayerSprintStatus Update(bool aButtonHeld,
                              bool aButtonPressed,
                              float stickX,
                              float stickY,
                              uint32_t stateFlags1,
                              uint32_t heldActor,
                              uint8_t cutsceneAction,
                              float speedXZ,
                              float deltaSeconds,
                              bool isGrounded = true);

    [[nodiscard]] PlayerSprintState State() const noexcept { return mState; }
    [[nodiscard]] float SpeedMultiplier() const noexcept { return mSpeedMultiplier; }
    [[nodiscard]] float StaminaRemaining() const noexcept { return mStaminaRemaining; }
    [[nodiscard]] float CooldownRemaining() const noexcept { return mCooldownRemaining; }
    [[nodiscard]] bool IsSprinting() const noexcept { return mState == PlayerSprintState::Sprinting; }
    [[nodiscard]] const PlayerSprintConfig& Config() const noexcept { return mConfig; }

    static bool IsPushingBoxesOrCarrying(uint32_t stateFlags1, uint32_t heldActor) noexcept;

private:
    PlayerSprintConfig mConfig;
    PlayerSprintState mState = PlayerSprintState::Idle;

    float mSpeedMultiplier = 1.0f;
    float mStaminaRemaining = 15.0f;
    float mCooldownRemaining = 0.0f;
    float mRollTimer = 0.0f;
    float mSprintTimer = 0.0f;
    bool mPreviousAButtonHeld = false;
};

// Hook nativo de baixo overhead que lê o Link da memória guest A32 e aplica o sprint
bool ApplyGuestPlayerSprint(
    oot3d::recomp::a32::MemoryBus& memory,
    PlayerSprintRuntime& sprintRuntime,
    bool aButtonHeld,
    bool aButtonPressed,
    float stickX,
    float stickY,
    float deltaSeconds = 1.0f / 30.0f);

} // namespace Oot3dNativeGame

