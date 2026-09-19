#include "oot3d_cutscene_dialog_skip_runtime.h"
#include "a32_runtime.h"

#include <algorithm>

#if defined(__ANDROID__)
#include <android/log.h>
#define SKIP_LOG(...) __android_log_print(ANDROID_LOG_INFO, "TriAevumSkip", __VA_ARGS__)
#else
#include <cstdio>
#define SKIP_LOG(...) do {} while (0)
#endif

namespace Oot3dNativeGame {

namespace {

// Constantes de layout de memória de PlayState, CsContext e MessageContext no OoT3D A32
constexpr uint32_t kPauseRoot = 0x005043D4U;
constexpr uint32_t kPlayStateOffset = 0x0CU;

// Cutscene Context (csCtx) em PlayState + 0x2298U
constexpr uint32_t kCsCtxOffset = 0x2298U;
constexpr uint32_t kActiveCsDataOffset = 0x04U; // PlayState + 0x229CU
constexpr uint32_t kCsStateOffset = 0x08U;      // PlayState + 0x22A0U
constexpr uint32_t kEndFrameOffset = 0x18U;     // PlayState + 0x22B0U
constexpr uint32_t kCurFrameOffset = 0x20U;     // PlayState + 0x22B8U

// Message Context (msgCtx) em PlayState + 0x32C0U
constexpr uint32_t kMsgCtxOffset = 0x32C0U;
constexpr uint32_t kSecondaryStateOffset = 0x000EU;
constexpr uint32_t kPrimaryStateOffset = 0x0F38U;
constexpr uint32_t kMsgModeOffset = 0x0FA0U;
constexpr uint32_t kTextDelayTimerOffset = 0x0FA4U;

// Modos de mensagem OoT3D
constexpr uint8_t kMsgModeNone = 0x00U;
constexpr uint8_t kMsgModeTextAwaitInput = 0x07U;
constexpr uint8_t kMsgModeTextAwaitNext = 0x34U;
constexpr uint8_t kMsgModeTextDone = 0x35U;

// Estado de escolha (para evitar pular seleções Sim/Não acidentalmente)
constexpr uint8_t kTextStateChoice = 0x04U;

// Máscaras de botão 3DS HID
constexpr uint32_t kButtonMaskA = 1U << 0;
constexpr uint32_t kButtonMaskB = 1U << 1;

} // namespace

CutsceneDialogSkipRuntime::CutsceneDialogSkipRuntime(CutsceneDialogSkipConfig config)
    : mConfig(config) {
}

void CutsceneDialogSkipRuntime::Reset() {
    mHoldTime = 0.0f;
    mPulseFrame = 0;
}

CutsceneDialogSkipStatus CutsceneDialogSkipRuntime::Update(bool bButtonHeld, float deltaSeconds) {
    if (deltaSeconds <= 0.0f) {
        deltaSeconds = 1.0f / 30.0f;
    }

    if (bButtonHeld) {
        mHoldTime += deltaSeconds;
    } else {
        mHoldTime = 0.0f;
        mPulseFrame = 0;
    }

    CutsceneDialogSkipStatus status;
    status.HoldDurationSeconds = mHoldTime;
    status.IsActive = (mHoldTime >= mConfig.HoldThresholdSeconds);

    if (status.IsActive) {
        ++mPulseFrame;
        // Pulso alternado a 15Hz (a 30 FPS): quadro ímpar = pressionado, quadro par = solto
        status.ShouldPulseAdvance = ((mPulseFrame % 2) != 0);
    }

    return status;
}

CutsceneDialogSkipStatus ApplyGuestCutsceneDialogSkip(
    oot3d::recomp::a32::MemoryBus& memory,
    CutsceneDialogSkipRuntime& skipRuntime,
    bool bButtonHeld,
    uint32_t* inOutGuestButtons,
    float deltaSeconds) {
    auto status = skipRuntime.Update(bButtonHeld, deltaSeconds);
    if (!status.IsActive) {
        return status;
    }

    uint32_t playState = 0;
    if (!memory.Read32(kPauseRoot + kPlayStateOffset, &playState) || playState == 0U) {
        return status;
    }

    // 1. Verificar Diálogo ativo (MessageContext em playState + 0x32C0)
    const uint32_t msgCtx = playState + kMsgCtxOffset;
    uint8_t msgMode = kMsgModeNone;
    memory.Read8(msgCtx + kMsgModeOffset, &msgMode);

    if (msgMode != kMsgModeNone) {
        uint8_t secondaryState = 0;
        memory.Read8(msgCtx + kSecondaryStateOffset, &secondaryState);
        uint16_t primaryState = 0;
        memory.Read16(msgCtx + kPrimaryStateOffset, &primaryState);

        const bool isChoiceActive = (secondaryState == kTextStateChoice) ||
                                   ((primaryState & 0xFF) == kTextStateChoice);

        if (!isChoiceActive) {
            status.Target = CutsceneDialogSkipTarget::Dialog;

            // Zera temporizadores de efeito máquina de escrever para completar o texto imediatamente
            memory.Write16(msgCtx + kPrimaryStateOffset, 0);
            memory.Write16(msgCtx + kTextDelayTimerOffset, 0);

            // Se estiver aguardando confirmação do jogador para avançar ou fechar
            const bool awaitingInput = (msgMode == kMsgModeTextAwaitInput ||
                                        msgMode == kMsgModeTextAwaitNext ||
                                        msgMode == kMsgModeTextDone);

            if (inOutGuestButtons != nullptr) {
                // Suprime o botão B para evitar que Link desferir espadada ao sair da mensagem
                *inOutGuestButtons &= ~kButtonMaskB;

                if (awaitingInput && status.ShouldPulseAdvance) {
                    *inOutGuestButtons |= kButtonMaskA;
                }
            }

            SKIP_LOG("Dialog skip active: msgMode=0x%02X, awaitingInput=%d, pulse=%d",
                     msgMode, awaitingInput ? 1 : 0, status.ShouldPulseAdvance ? 1 : 0);
            return status;
        }
    }

    // 2. Verificar Cutscene ativa (CsContext em playState + 0x2298)
    const uint32_t csCtx = playState + kCsCtxOffset;
    uint32_t activeCsData = 0;
    memory.Read32(csCtx + kActiveCsDataOffset, &activeCsData);
    uint8_t csState = 0;
    memory.Read8(csCtx + kCsStateOffset, &csState);

    if (activeCsData != 0U || csState != 0U) {
        uint16_t endFrame = 0;
        uint16_t curFrame = 0;
        memory.Read16(csCtx + kEndFrameOffset, &endFrame);
        memory.Read16(csCtx + kCurFrameOffset, &curFrame);

        if (endFrame > 0 && curFrame < endFrame) {
            status.Target = CutsceneDialogSkipTarget::Cutscene;
            const uint16_t step = skipRuntime.Config().CutsceneFrameStep;
            const uint16_t maxAdvance = (endFrame > 1) ? static_cast<uint16_t>(endFrame - 1) : endFrame;
            const uint16_t targetFrame = static_cast<uint16_t>(std::min<uint32_t>(curFrame + step, maxAdvance));

            if (targetFrame > curFrame) {
                status.CutsceneFramesAdvanced = targetFrame - curFrame;
                memory.Write16(csCtx + kCurFrameOffset, targetFrame);
            }

            if (inOutGuestButtons != nullptr) {
                *inOutGuestButtons &= ~kButtonMaskB;
            }

            SKIP_LOG("Cutscene skip active: curFrame=%u -> %u / %u (advanced %u)",
                     curFrame, targetFrame, endFrame, status.CutsceneFramesAdvanced);
            return status;
        }
    }

    return status;
}

} // namespace Oot3dNativeGame
