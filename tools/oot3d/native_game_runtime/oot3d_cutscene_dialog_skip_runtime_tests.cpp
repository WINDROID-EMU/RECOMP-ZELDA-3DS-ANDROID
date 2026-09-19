#include "oot3d_cutscene_dialog_skip_runtime.h"
#include "a32_runtime.h"

#include <iostream>
#include <map>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class MockMemoryBus final : public oot3d::recomp::a32::MemoryBus {
public:
    bool Read32(uint32_t address, uint32_t* value) override {
        if (!value) return false;
        *value = static_cast<uint32_t>(GetByte(address)) |
                 (static_cast<uint32_t>(GetByte(address + 1)) << 8) |
                 (static_cast<uint32_t>(GetByte(address + 2)) << 16) |
                 (static_cast<uint32_t>(GetByte(address + 3)) << 24);
        return true;
    }

    bool Write32(uint32_t address, uint32_t value) override {
        SetByte(address, static_cast<uint8_t>(value & 0xFF));
        SetByte(address + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
        SetByte(address + 2, static_cast<uint8_t>((value >> 16) & 0xFF));
        SetByte(address + 3, static_cast<uint8_t>((value >> 24) & 0xFF));
        return true;
    }

    bool Read16(uint32_t address, uint16_t* value) override {
        if (!value) return false;
        *value = static_cast<uint16_t>(GetByte(address)) |
                 (static_cast<uint16_t>(GetByte(address + 1)) << 8);
        return true;
    }

    bool Write16(uint32_t address, uint16_t value) override {
        SetByte(address, static_cast<uint8_t>(value & 0xFF));
        SetByte(address + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
        return true;
    }

    bool Read8(uint32_t address, uint8_t* value) override {
        if (!value) return false;
        *value = GetByte(address);
        return true;
    }

    bool Write8(uint32_t address, uint8_t value) override {
        SetByte(address, value);
        return true;
    }

    bool Read64(uint32_t, uint64_t*, uint32_t*) override { return false; }
    bool Write64(uint32_t, uint64_t, uint32_t*) override { return false; }
    bool LoadExclusive(uint32_t, uint8_t, uint64_t*, uint64_t*, uint32_t*) override { return false; }
    oot3d::recomp::a32::ExclusiveStoreResult StoreExclusive(uint32_t, uint8_t, uint64_t, uint64_t, uint32_t*) override {
        return oot3d::recomp::a32::ExclusiveStoreResult::MemoryFault;
    }
    bool AtomicSwap(uint32_t, uint8_t, uint32_t, uint32_t*, uint32_t*) override { return false; }

private:
    uint8_t GetByte(uint32_t address) const {
        auto it = mBytes.find(address);
        return it != mBytes.end() ? it->second : 0U;
    }

    void SetByte(uint32_t address, uint8_t val) {
        mBytes[address] = val;
    }

    std::map<uint32_t, uint8_t> mBytes;
};

} // namespace

int main() {
    using namespace Oot3dNativeGame;

    std::cout << "Running CutsceneDialogSkipRuntime tests...\n";

    constexpr float dt = 1.0f / 30.0f; // 33.3ms por quadro

    // Teste 1: Toque rápido no botão B (< 0.20s) não ativa o skip
    {
        CutsceneDialogSkipRuntime skip;
        auto st1 = skip.Update(true, dt); // 1 quadro
        Require(!st1.IsActive, "1 frame de B nao deve ativar o skip");
        auto st2 = skip.Update(true, dt); // 2 quadros
        Require(!st2.IsActive, "2 frames de B nao devem ativar o skip");
        auto st3 = skip.Update(false, dt); // soltou
        Require(!st3.IsActive, "Soltar B deve manter inativo");
        Require(skip.HoldDuration() == 0.0f, "HoldDuration deve resetar ao soltar");
    }

    // Teste 2: Segurar B por >= 0.20s (6 quadros a 30 FPS) ativa o skip com pulsos alternados
    {
        CutsceneDialogSkipRuntime skip;
        for (int i = 0; i < 5; ++i) {
            skip.Update(true, dt);
        }
        Require(!skip.IsActive(), "5 quadros ainda deve estar inativo");

        auto st6 = skip.Update(true, dt); // 6 quadros = 0.20s
        Require(st6.IsActive, "6 quadros deve ativar o skip");
        Require(st6.ShouldPulseAdvance, "Primeiro quadro ativo deve pulsar A");

        auto st7 = skip.Update(true, dt); // 7 quadros
        Require(st7.IsActive, "7 quadros deve permanecer ativo");
        Require(!st7.ShouldPulseAdvance, "Quadro par subsequente deve soltar pulso");

        auto st8 = skip.Update(true, dt); // 8 quadros
        Require(st8.IsActive, "8 quadros deve permanecer ativo");
        Require(st8.ShouldPulseAdvance, "Quadro impar subsequente deve pulsar A");
    }

    // Teste 3: Reset limpa o estado
    {
        CutsceneDialogSkipRuntime skip;
        for (int i = 0; i < 10; ++i) {
            skip.Update(true, dt);
        }
        Require(skip.IsActive(), "Deve estar ativo");
        skip.Reset();
        Require(!skip.IsActive(), "Apos Reset deve estar inativo");
        Require(skip.HoldDuration() == 0.0f, "Apos Reset HoldDuration deve ser 0");
    }

    // Preparação do mock para testes com memória de jogo
    constexpr uint32_t kPauseRoot = 0x005043D4U;
    constexpr uint32_t kPlayStateAddr = 0x00600000U;
    constexpr uint32_t kCsCtx = kPlayStateAddr + 0x2298U;
    constexpr uint32_t kMsgCtx = kPlayStateAddr + 0x32C0U;

    // Teste 4: Memória nula / sem PlayState
    {
        MockMemoryBus bus;
        CutsceneDialogSkipRuntime skip;
        uint32_t buttons = 1U << 1; // B segurado
        for (int i = 0; i < 7; ++i) {
            auto st = ApplyGuestCutsceneDialogSkip(bus, skip, true, &buttons, dt);
            if (i < 5) {
                Require(!st.IsActive, "Pre-threshold deve ser inativo");
            } else {
                Require(st.IsActive, "Post-threshold deve ser ativo");
                Require(st.Target == CutsceneDialogSkipTarget::None, "Sem PlayState target deve ser None");
            }
        }
    }

    // Teste 5: Diálogo exibindo texto (máquina de escrever) -> Zera temporizadores instantaneamente
    {
        MockMemoryBus bus;
        bus.Write32(kPauseRoot + 0x0CU, kPlayStateAddr);
        // msgMode = 0x06 (MSGMODE_TEXT_DISPLAYING)
        bus.Write8(kMsgCtx + 0x0FA0U, 0x06U);
        bus.Write16(kMsgCtx + 0x0F38U, 15U); // stateTimer
        bus.Write16(kMsgCtx + 0x0FA4U, 8U);  // textDelayTimer

        CutsceneDialogSkipRuntime skip;
        uint32_t buttons = 1U << 1; // B segurado

        // Acumula hold até threshold
        for (int i = 0; i < 6; ++i) {
            ApplyGuestCutsceneDialogSkip(bus, skip, true, &buttons, dt);
        }

        auto st = ApplyGuestCutsceneDialogSkip(bus, skip, true, &buttons, dt);
        Require(st.IsActive, "Deve estar ativo");
        Require(st.Target == CutsceneDialogSkipTarget::Dialog, "Target deve ser Dialog");

        uint16_t stateTimer = 999;
        uint16_t textDelay = 999;
        bus.Read16(kMsgCtx + 0x0F38U, &stateTimer);
        bus.Read16(kMsgCtx + 0x0FA4U, &textDelay);
        Require(stateTimer == 0, "stateTimer deve ser zerado para adiantar texto");
        Require(textDelay == 0, "textDelayTimer deve ser zerado para adiantar texto");
        Require((buttons & (1U << 1)) == 0, "Botao B deve ser suprimido durante dialogo");
    }

    // Teste 6: Diálogo aguardando input do jogador -> Pulsa A para avançar páginas
    {
        MockMemoryBus bus;
        bus.Write32(kPauseRoot + 0x0CU, kPlayStateAddr);
        // msgMode = 0x07 (MSGMODE_TEXT_AWAIT_INPUT)
        bus.Write8(kMsgCtx + 0x0FA0U, 0x07U);

        CutsceneDialogSkipRuntime skip;
        // Avança até ativar
        for (int i = 0; i < 5; ++i) {
            skip.Update(true, dt);
        }

        uint32_t buttons = 1U << 1; // B
        auto st1 = ApplyGuestCutsceneDialogSkip(bus, skip, true, &buttons, dt);
        Require(st1.IsActive, "Deve estar ativo");
        Require(st1.ShouldPulseAdvance, "Quadro 1 ativo deve pulsar A");
        Require((buttons & (1U << 0)) != 0, "Bit de A deve ser sintetizado");
        Require((buttons & (1U << 1)) == 0, "Bit de B deve ser mascarado");

        buttons = 1U << 1;
        auto st2 = ApplyGuestCutsceneDialogSkip(bus, skip, true, &buttons, dt);
        Require(st2.IsActive, "Deve estar ativo");
        Require(!st2.ShouldPulseAdvance, "Quadro 2 ativo deve soltar pulso de A");
        Require((buttons & (1U << 0)) == 0, "Bit de A nao deve ser sintetizado no quadro de release");
        Require((buttons & (1U << 1)) == 0, "Bit de B deve ser mascarado");
    }

    // Teste 7: Diálogo de Escolha (Sim/Não - Kaepora Gaebora / Owl) -> NÃO pular
    {
        MockMemoryBus bus;
        bus.Write32(kPauseRoot + 0x0CU, kPlayStateAddr);
        bus.Write8(kMsgCtx + 0x0FA0U, 0x07U);
        bus.Write8(kMsgCtx + 0x000EU, 0x04U); // secondaryState = TEXT_STATE_CHOICE

        CutsceneDialogSkipRuntime skip;
        for (int i = 0; i < 5; ++i) skip.Update(true, dt);

        uint32_t buttons = 1U << 1;
        auto st = ApplyGuestCutsceneDialogSkip(bus, skip, true, &buttons, dt);
        Require(st.Target != CutsceneDialogSkipTarget::Dialog, "Caixa de escolha nao deve ser pulada");
    }

    // Teste 8: Cutscene ativa -> Avança quadros por tick (~10x velocidade)
    {
        MockMemoryBus bus;
        bus.Write32(kPauseRoot + 0x0CU, kPlayStateAddr);
        bus.Write32(kCsCtx + 0x04U, 0x00800000U); // activeCutsceneData != 0
        bus.Write8(kCsCtx + 0x08U, 1U);           // cutsceneState = 1
        bus.Write16(kCsCtx + 0x18U, 200U);        // endFrame = 200
        bus.Write16(kCsCtx + 0x20U, 10U);         // curFrame = 10

        CutsceneDialogSkipRuntime skip;
        for (int i = 0; i < 5; ++i) skip.Update(true, dt);

        uint32_t buttons = 1U << 1;
        auto st = ApplyGuestCutsceneDialogSkip(bus, skip, true, &buttons, dt);
        Require(st.IsActive, "Deve estar ativo");
        Require(st.Target == CutsceneDialogSkipTarget::Cutscene, "Target deve ser Cutscene");
        Require(st.CutsceneFramesAdvanced == 10, "Deve ter avancado 10 quadros");

        uint16_t newCurFrame = 0;
        bus.Read16(kCsCtx + 0x20U, &newCurFrame);
        Require(newCurFrame == 20, "curFrame na memoria deve ter avancado de 10 para 20");
        Require((buttons & (1U << 1)) == 0, "Botao B deve ser mascarado");
    }

    // Teste 9: Cutscene próxima do fim -> Cobre até endFrame - 1 sem passar do limite
    {
        MockMemoryBus bus;
        bus.Write32(kPauseRoot + 0x0CU, kPlayStateAddr);
        bus.Write32(kCsCtx + 0x04U, 0x00800000U);
        bus.Write8(kCsCtx + 0x08U, 1U);
        bus.Write16(kCsCtx + 0x18U, 100U); // endFrame = 100
        bus.Write16(kCsCtx + 0x20U, 95U);  // curFrame = 95

        CutsceneDialogSkipRuntime skip;
        for (int i = 0; i < 5; ++i) skip.Update(true, dt);

        uint32_t buttons = 1U << 1;
        auto st = ApplyGuestCutsceneDialogSkip(bus, skip, true, &buttons, dt);
        Require(st.CutsceneFramesAdvanced == 4, "Deve avancar apenas 4 quadros (de 95 para 99)");

        uint16_t newCurFrame = 0;
        bus.Read16(kCsCtx + 0x20U, &newCurFrame);
        Require(newCurFrame == 99, "curFrame deve limitar em endFrame - 1 (99)");
    }

    std::cout << "All CutsceneDialogSkipRuntime tests passed successfully!\n";
    return 0;
}
