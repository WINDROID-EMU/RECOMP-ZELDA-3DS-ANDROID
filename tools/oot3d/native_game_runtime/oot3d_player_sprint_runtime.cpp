#include "oot3d_player_sprint_runtime.h"
#include "a32_runtime.h"

#include <bit>

#if defined(__ANDROID__)
#include <android/log.h>
#define SPRINT_LOG(...) __android_log_print(ANDROID_LOG_INFO, "TriAevumSprint", __VA_ARGS__)
#else
#include <cstdio>
#define SPRINT_LOG(...) do {} while (0)
#endif

namespace Oot3dNativeGame {

namespace {

// stateFlags1 (offset 0x1710)
constexpr uint32_t kState1StartPullingPushing = 0x00000002U;
constexpr uint32_t kState1PullingPushing = 0x00000004U;
constexpr uint32_t kState1Talking = 0x00000020U;
constexpr uint32_t kState1Dead = 0x00000080U;
constexpr uint32_t kState1ClimbingLadder = 0x00000200U;
constexpr uint32_t kState1HangingOffLedge = 0x00000400U;
constexpr uint32_t kState1CarryingActor = 0x00000800U;
constexpr uint32_t kState1Swimming = 0x00001000U;
constexpr uint32_t kState1ClimbingLedge = 0x00002000U;
constexpr uint32_t kState1RidingHorse = 0x00004000U;
constexpr uint32_t kState1HangingFromCeiling = 0x00040000U;
constexpr uint32_t kState1InCutscene = 0x00080000U;
constexpr uint32_t kState1ClimbingStart = 0x00200000U;
constexpr uint32_t kState1Paralyzed = 0x00800000U;
constexpr uint32_t kState1InCutsceneMovement = 0x02000000U;
constexpr uint32_t kState1InWater = 0x08000000U;
constexpr uint32_t kState1Hopping = 0x20000000U;
constexpr uint32_t kState1Jumping = 0x80000000U;

// stateFlags2 (offset 0x1714)
constexpr uint32_t kState2Crawling = 0x00040000U;

// Constantes de layout de memória de Link e PlayState em espaço A32 guest
constexpr uint32_t kPauseRoot = 0x005043D4U;
constexpr uint32_t kPlayStateOffset = 0x0CU;
constexpr uint32_t kPlayerOffsetInPlay = 0x20ACU;

// Cutscene Context (csCtx) em PlayState + 0x2298U
constexpr uint32_t kCsCtxOffset = 0x2298U;
constexpr uint32_t kCsStateOffset = 0x08U;

// Message Context (msgCtx) em PlayState + 0x32C0U
constexpr uint32_t kMsgCtxOffset = 0x32C0U;
constexpr uint32_t kMsgModeOffset = 0x0FA0U;

constexpr uint32_t kActorWorldPosXOffset = 0x0028U;
constexpr uint32_t kActorWorldPosZOffset = 0x0030U;
constexpr uint32_t kActorVelXOffset = 0x0060U;
constexpr uint32_t kActorVelZOffset = 0x0068U;
constexpr uint32_t kActorSpeedXZOffset = 0x006CU;
constexpr uint32_t kActorBgCheckFlagsOffset = 0x0090U;
constexpr uint32_t kSkelAnimeCurrentFrameOffset = 0x0290U;
constexpr uint32_t kSkelAnimePlaySpeedOffset = 0x0294U;
constexpr uint32_t kSkelAnimeStartFrameOffset = 0x0298U;
constexpr uint32_t kSkelAnimeEndFrameOffset = 0x029CU;
constexpr uint32_t kSkelAnimeAnimLengthOffset = 0x02A0U;
constexpr uint32_t kPlayerHeldActorOffset = 0x1224U;
constexpr uint32_t kPlayerCutsceneActionOffset = 0x12BCU;
constexpr uint32_t kPlayerStateFlags1Offset = 0x1710U;
constexpr uint32_t kPlayerStateFlags2Offset = 0x1714U;
constexpr uint32_t kPlayerLinearVelocityOffset = 0x221CU;
constexpr uint16_t kBgCheckFlagGround = 0x0001U;
constexpr uint16_t kBgCheckFlagWall = 0x0008U;

} // namespace

PlayerSprintRuntime::PlayerSprintRuntime(PlayerSprintConfig config)
    : mConfig(config),
      mStaminaRemaining(config.MaxSprintDurationSeconds) {
}

void PlayerSprintRuntime::Reset() {
    mState = PlayerSprintState::Idle;
    mSpeedMultiplier = 1.0f;
    mStaminaRemaining = mConfig.MaxSprintDurationSeconds;
    mCooldownRemaining = 0.0f;
    mRollTimer = 0.0f;
    mSprintTimer = 0.0f;
    mPreviousAButtonHeld = false;
}

bool PlayerSprintRuntime::IsPushingBoxesOrCarrying(uint32_t stateFlags1, uint32_t heldActor) noexcept {
    const bool pushingOrPulling = (stateFlags1 & (kState1StartPullingPushing | kState1PullingPushing)) != 0U;
    const bool carryingActor = (stateFlags1 & kState1CarryingActor) != 0U || heldActor != 0U;
    return pushingOrPulling || carryingActor;
}

bool PlayerSprintRuntime::IsClimbingOrHanging(uint32_t stateFlags1) noexcept {
    constexpr uint32_t kClimbOrHangMask = kState1ClimbingLadder |
                                          kState1HangingOffLedge |
                                          kState1ClimbingLedge |
                                          kState1HangingFromCeiling |
                                          kState1ClimbingStart;
    return (stateFlags1 & kClimbOrHangMask) != 0U;
}

bool PlayerSprintRuntime::IsInDialogueOrCutscene(uint32_t stateFlags1, uint8_t cutsceneAction, bool isInDialogue) noexcept {
    constexpr uint32_t kDialogueOrCsMask = kState1Talking |
                                           kState1InCutscene |
                                           kState1InCutsceneMovement;
    return isInDialogue || cutsceneAction != 0U || (stateFlags1 & kDialogueOrCsMask) != 0U;
}

PlayerSprintStatus PlayerSprintRuntime::Update(bool aButtonHeld,
                                              bool aButtonPressed,
                                              float stickX,
                                              float stickY,
                                              uint32_t stateFlags1,
                                              uint32_t heldActor,
                                              uint8_t cutsceneAction,
                                              float speedXZ,
                                              float deltaSeconds,
                                              bool isGrounded,
                                              bool isInDialogue,
                                              uint32_t stateFlags2) {
    if (deltaSeconds <= 0.0f) {
        deltaSeconds = 1.0f / 30.0f;
    }

    const bool justPressedA = aButtonPressed || (aButtonHeld && !mPreviousAButtonHeld);
    mPreviousAButtonHeld = aButtonHeld;

    const float stickMagnitude = std::sqrt(stickX * stickX + stickY * stickY);
    const bool hasMovementIntent = stickMagnitude >= mConfig.MinStickMagnitude || speedXZ > 0.5f;

    const bool pushingBoxes = IsPushingBoxesOrCarrying(stateFlags1, heldActor);
    const bool climbingOrHanging = IsClimbingOrHanging(stateFlags1);
    const bool inDialogueOrCs = IsInDialogueOrCutscene(stateFlags1, cutsceneAction, isInDialogue);
    const bool jumpingOrAirborne = !isGrounded || (stateFlags1 & (kState1Jumping | kState1Hopping)) != 0U;
    const bool inWater = (stateFlags1 & (kState1Dead | kState1InWater | kState1Swimming)) != 0U;
    const bool disabled = (stateFlags1 & (kState1Paralyzed | kState1RidingHorse)) != 0U ||
                          (stateFlags2 & kState2Crawling) != 0U;

    const bool invalidState = jumpingOrAirborne ||
                              pushingBoxes ||
                              climbingOrHanging ||
                              inDialogueOrCs ||
                              inWater ||
                              disabled;

    if (invalidState) {
        if (mState == PlayerSprintState::RollWaiting || mState == PlayerSprintState::Sprinting) {
            SPRINT_LOG("Sprint invalidated: grounded=%d, jumping=%d, pushing=%d, climbHang=%d, dialogue=%d, flags1=0x%08X",
                       (int)isGrounded, (int)jumpingOrAirborne, (int)pushingBoxes, (int)climbingOrHanging, (int)inDialogueOrCs, stateFlags1);
            mState = PlayerSprintState::Idle;
            mSpeedMultiplier = 1.0f; // Para a velocidade na hora
            mRollTimer = 0.0f;
            mSprintTimer = 0.0f;
        }
    }

    switch (mState) {
    case PlayerSprintState::ExhaustedCooldown: {
        mCooldownRemaining -= deltaSeconds;
        if (mCooldownRemaining <= 0.0f) {
            mCooldownRemaining = 0.0f;
            mStaminaRemaining = mConfig.MaxSprintDurationSeconds;
            mState = PlayerSprintState::Idle;
            mSprintTimer = 0.0f;
            SPRINT_LOG("Sprint cooldown finished! Stamina fully restored.");
        }
        break;
    }

    case PlayerSprintState::Idle: {
        mSprintTimer = 0.0f;
        // Regenera stamina suavemente quando em repouso
        if (mStaminaRemaining < mConfig.MaxSprintDurationSeconds) {
            mStaminaRemaining = std::min(mConfig.MaxSprintDurationSeconds,
                                         mStaminaRemaining + deltaSeconds);
        }

        // Se o botão A foi pressionado durante movimento no chão e sem estados inválidos,
        // inicia o monitoramento do rolamento
        if (!invalidState && justPressedA && hasMovementIntent && mStaminaRemaining > 1.0f) {
            mState = PlayerSprintState::RollWaiting;
            mRollTimer = 0.0f;
            SPRINT_LOG("Button A pressed during movement -> Entered RollWaiting (speedXZ=%.2f, stickMag=%.1f)",
                       speedXZ, stickMagnitude);
        }
        break;
    }

    case PlayerSprintState::RollWaiting: {
        mSprintTimer = 0.0f;
        mRollTimer += deltaSeconds;

        // Se o jogador soltou o botão A antes de terminar o rolamento, ou se entrou em estado inválido
        if (!aButtonHeld || invalidState) {
            SPRINT_LOG("RollWaiting cancelled: aButtonHeld=%d, invalidState=%d, rollTimer=%.2fs",
                       (int)aButtonHeld, (int)invalidState, mRollTimer);
            mState = PlayerSprintState::Idle;
            mRollTimer = 0.0f;
        } else if (mRollTimer >= mConfig.RollDurationSeconds) {
            // O rolamento terminou e o Link se levantou; botão A continua segurado e direcional ativo
            if (hasMovementIntent && mStaminaRemaining > 0.0f) {
                mState = PlayerSprintState::Sprinting;
                mSprintTimer = 0.0f;
                SPRINT_LOG("Roll finished while holding A -> SPRINT STARTED! (targetMultiplier=%.2f)",
                           mConfig.MaxSprintMultiplier);
            } else {
                mState = PlayerSprintState::Idle;
                SPRINT_LOG("Roll finished but no movement intent -> Returning to Idle.");
            }
            mRollTimer = 0.0f;
        }
        break;
    }

    case PlayerSprintState::Sprinting: {
        // Condições para interromper a corrida
        if (!aButtonHeld || !hasMovementIntent || invalidState) {
            SPRINT_LOG("Sprint ending: aHeld=%d, hasMoveIntent=%d, invalid=%d",
                       (int)aButtonHeld, (int)hasMovementIntent, (int)invalidState);
            mState = PlayerSprintState::Idle;
            mSpeedMultiplier = 1.0f; // Para a velocidade na hora
            mSprintTimer = 0.0f;
        } else {
            mSprintTimer += deltaSeconds;
            // Consome stamina durante a corrida
            mStaminaRemaining -= deltaSeconds;
            if (mStaminaRemaining <= 0.0f) {
                mStaminaRemaining = 0.0f;
                mCooldownRemaining = mConfig.CooldownDurationSeconds; // 15 segundos estritos de cooldown
                mState = PlayerSprintState::ExhaustedCooldown;
                mSpeedMultiplier = 1.0f;
                mSprintTimer = 0.0f;
                SPRINT_LOG("Sprint stamina exhausted! Entering 15s cooldown.");
            }
        }
        break;
    }
    }

    // Gerenciamento da velocidade: se estiver em estado inválido, para imediatamente ("na hora")
    if (mState == PlayerSprintState::Sprinting) {
        const float t = mConfig.SprintRampUpTimeSeconds > 0.0f
                            ? std::clamp(mSprintTimer / mConfig.SprintRampUpTimeSeconds, 0.0f, 1.0f)
                            : 1.0f;
        const float smoothProgress = t * t * (3.0f - 2.0f * t);
        mSpeedMultiplier = 1.0f + (mConfig.MaxSprintMultiplier - 1.0f) * smoothProgress;
    } else {
        if (invalidState) {
            // Em qualquer estado inválido (pulou, empurrando, subindo, pendurado, diálogo, etc.),
            // a velocidade para IMEDIATAMENTE na hora (sem ramp-down que empurre caixas/objetos)
            mSpeedMultiplier = 1.0f;
        } else {
            const float rampDownStep = ((mConfig.MaxSprintMultiplier - 1.0f) / mConfig.SprintRampDownTimeSeconds) * deltaSeconds;
            mSpeedMultiplier = std::max(1.0f, mSpeedMultiplier - rampDownStep);
        }
    }

    PlayerSprintStatus status;
    status.State = mState;
    status.SpeedMultiplier = mSpeedMultiplier;
    status.StaminaRemainingSeconds = mStaminaRemaining;
    status.CooldownRemainingSeconds = mCooldownRemaining;
    status.IsSprinting = (mState == PlayerSprintState::Sprinting);
    status.IsPushingBoxes = pushingBoxes;
    status.IsClimbingOrHanging = climbingOrHanging;
    status.IsInDialogue = inDialogueOrCs;
    return status;
}

bool ApplyGuestPlayerSprint(
    oot3d::recomp::a32::MemoryBus& memory,
    PlayerSprintRuntime& sprintRuntime,
    bool aButtonHeld,
    bool aButtonPressed,
    float stickX,
    float stickY,
    float deltaSeconds) {
    uint32_t playState = 0;
    if (!memory.Read32(kPauseRoot + kPlayStateOffset, &playState) || playState == 0U) {
        return false;
    }

    uint32_t player = 0;
    if (!memory.Read32(playState + kPlayerOffsetInPlay, &player) || player == 0U) {
        return false;
    }

    uint32_t stateFlags1 = 0;
    if (!memory.Read32(player + kPlayerStateFlags1Offset, &stateFlags1)) {
        return false;
    }

    uint32_t stateFlags2 = 0;
    memory.Read32(player + kPlayerStateFlags2Offset, &stateFlags2);

    uint32_t heldActor = 0;
    if (!memory.Read32(player + kPlayerHeldActorOffset, &heldActor)) {
        return false;
    }

    uint8_t cutsceneAction = 0;
    if (!memory.Read8(player + kPlayerCutsceneActionOffset, &cutsceneAction)) {
        return false;
    }

    // Checa se há diálogo/caixa de mensagem ativa em msgCtx ou cutscene ativa em csCtx
    uint8_t msgMode = 0;
    memory.Read8(playState + kMsgCtxOffset + kMsgModeOffset, &msgMode);
    const bool isDialogueActive = (msgMode != 0U);

    uint8_t csState = 0;
    memory.Read8(playState + kCsCtxOffset + kCsStateOffset, &csState);
    const bool isCsActive = (csState != 0U);

    const bool inDialogueOrCs = isDialogueActive || isCsActive;

    float speedXZ = 0.0f;
    uint32_t speedRaw = 0;
    if (!memory.Read32(player + kActorSpeedXZOffset, &speedRaw)) {
        return false;
    }
    speedXZ = std::bit_cast<float>(speedRaw);

    uint16_t bgCheckFlags = 0;
    memory.Read16(player + kActorBgCheckFlagsOffset, &bgCheckFlags);

    // Para ser considerado grounded:
    // Deve ter a flag de ground do bgCheck E NÃO pode estar suspenso, escalando, pendurado nem no ar
    const bool isClimbingOrHanging = PlayerSprintRuntime::IsClimbingOrHanging(stateFlags1);
    const bool isAirborneOrJumping = (stateFlags1 & (kState1Jumping | kState1Hopping | kState1Swimming | kState1InWater)) != 0U;
    const bool isGrounded = ((bgCheckFlags & kBgCheckFlagGround) != 0U) &&
                            !isClimbingOrHanging &&
                            !isAirborneOrJumping;

    const auto status = sprintRuntime.Update(
        aButtonHeld, aButtonPressed, stickX, stickY,
        stateFlags1, heldActor, cutsceneAction, speedXZ, deltaSeconds,
        isGrounded, inDialogueOrCs, stateFlags2);

    if (!isGrounded || !status.IsSprinting) {
        // Quando não estiver no chão ou quando não estiver correndo ativamente:
        // Restabelece speedXZ e linearVelocity para os valores normais do jogo (máx 5.66f)
        if (speedXZ > 5.66f) {
            memory.Write32(player + kActorSpeedXZOffset, std::bit_cast<uint32_t>(5.66f));
        }
        float currentLinVel = 0.0f;
        uint32_t linVelRaw = 0;
        if (memory.Read32(player + kPlayerLinearVelocityOffset, &linVelRaw)) {
            currentLinVel = std::bit_cast<float>(linVelRaw);
            if (currentLinVel > 5.66f) {
                memory.Write32(player + kPlayerLinearVelocityOffset, std::bit_cast<uint32_t>(5.66f));
            }
        }
    }

    if (status.IsSprinting) {
        float currentLinVel = 0.0f;
        uint32_t linVelRaw = 0;
        if (memory.Read32(player + kPlayerLinearVelocityOffset, &linVelRaw)) {
            currentLinVel = std::bit_cast<float>(linVelRaw);
        }

        // Garante que o motor de locomoção de Link reconheça a corrida (PLAYER_ANIMGROUP_run).
        // O limiar no OoT3D para acionar a animação de corrida é linearVelocity >= 3.70f (REG(48)/100).
        // Forçando linearVelocity e speedXZ para velocidade plena de corrida (mínimo 5.66f),
        // a engine nativa seleciona imediatamente a animação verdadeira de corrida em vez de caminhada.
        const float targetLinearVelocity = 5.66f * status.SpeedMultiplier;
        if (currentLinVel < 5.66f) {
            memory.Write32(player + kPlayerLinearVelocityOffset, std::bit_cast<uint32_t>(targetLinearVelocity));
            memory.Write32(player + kActorSpeedXZOffset, std::bit_cast<uint32_t>(targetLinearVelocity));
        }

        uint32_t animIndex = 0;
        memory.Read32(player + 0x0284U, &animIndex);
        SPRINT_LOG("Sprinting active: speedXZ=%.2f, linVel=%.2f, animIdx=%u, mult=%.2f",
                   speedXZ, currentLinVel, animIndex, status.SpeedMultiplier);
    }

    float currentPlaySpeed = 1.0f;
    uint32_t playSpeedRaw = 0;
    if (memory.Read32(player + kSkelAnimePlaySpeedOffset, &playSpeedRaw)) {
        currentPlaySpeed = std::bit_cast<float>(playSpeedRaw);
    }

    if (status.SpeedMultiplier > 1.0f && status.IsSprinting) {
        const float extraFactor = status.SpeedMultiplier - 1.0f;
        const bool hitWall = (bgCheckFlags & kBgCheckFlagWall) != 0U;

        if (!hitWall && extraFactor > 0.0f) {
            uint32_t vxRaw = 0;
            uint32_t vzRaw = 0;
            uint32_t posXRaw = 0;
            uint32_t posZRaw = 0;
            if (memory.Read32(player + kActorVelXOffset, &vxRaw) &&
                memory.Read32(player + kActorVelZOffset, &vzRaw) &&
                memory.Read32(player + kActorWorldPosXOffset, &posXRaw) &&
                memory.Read32(player + kActorWorldPosZOffset, &posZRaw)) {
                float vx = std::bit_cast<float>(vxRaw);
                float vz = std::bit_cast<float>(vzRaw);
                float posX = std::bit_cast<float>(posXRaw);
                float posZ = std::bit_cast<float>(posZRaw);

                const float currentSpeed = std::sqrt(vx * vx + vz * vz);
                if (currentSpeed > 0.05f) {
                    // Limita a velocidade base calculada para o deslocamento extra (máx 5.66f de corrida normal),
                    // prevenindo acelerações supersônicas se Link estiver com impulso residual de rolamento
                    const float baseSpeed = std::min(currentSpeed, 5.66f);
                    const float dirX = vx / currentSpeed;
                    const float dirZ = vz / currentSpeed;
                    const float extraDist = baseSpeed * extraFactor;

                    posX += dirX * extraDist;
                    posZ += dirZ * extraDist;

                    memory.Write32(player + kActorWorldPosXOffset, std::bit_cast<uint32_t>(posX));
                    memory.Write32(player + kActorWorldPosZOffset, std::bit_cast<uint32_t>(posZ));
                }
            }
        }

        // Sincroniza a animação de corrida de Link (SkelAnime):
        // Avança o frame da animação proporcionalmente ao extraFactor a cada tick,
        // garantindo que o ritmo das passadas case exatamente com o deslocamento no chão,
        // eliminando qualquer efeito de deslizar/patinar (ice-skating).
        uint32_t curFrameRaw = 0;
        uint32_t animLenRaw = 0;
        if (memory.Read32(player + kSkelAnimeCurrentFrameOffset, &curFrameRaw) &&
            memory.Read32(player + kSkelAnimeAnimLengthOffset, &animLenRaw)) {
            float curFrame = std::bit_cast<float>(curFrameRaw);
            float animLength = std::bit_cast<float>(animLenRaw);

            uint32_t startFrameRaw = 0;
            uint32_t endFrameRaw = 0;
            float startFrame = 0.0f;
            float endFrame = animLength;

            if (memory.Read32(player + kSkelAnimeStartFrameOffset, &startFrameRaw)) {
                startFrame = std::bit_cast<float>(startFrameRaw);
            }
            if (memory.Read32(player + kSkelAnimeEndFrameOffset, &endFrameRaw)) {
                endFrame = std::bit_cast<float>(endFrameRaw);
            }

            float loopEnd = animLength;
            if (endFrame > startFrame && endFrame <= animLength) {
                loopEnd = endFrame;
            }

            if (loopEnd > startFrame && std::isfinite(curFrame)) {
                const float loopSpan = loopEnd - startFrame;
                curFrame += extraFactor;
                while (curFrame >= loopEnd) {
                    curFrame -= loopSpan;
                }
                while (curFrame < startFrame) {
                    curFrame += loopSpan;
                }
                memory.Write32(player + kSkelAnimeCurrentFrameOffset, std::bit_cast<uint32_t>(curFrame));
            }
        }

        memory.Write32(player + kSkelAnimePlaySpeedOffset, std::bit_cast<uint32_t>(status.SpeedMultiplier));
    } else {
        if (currentPlaySpeed != 1.0f) {
            memory.Write32(player + kSkelAnimePlaySpeedOffset, std::bit_cast<uint32_t>(1.0f));
        }
    }

    return status.IsSprinting;
}

} // namespace Oot3dNativeGame

