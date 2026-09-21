#include "oot3d_player_sprint_runtime.h"
#include "a32_runtime.h"

#include <bit>
#include <cmath>
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

    void WriteFloat(uint32_t address, float value) {
        Write32(address, std::bit_cast<uint32_t>(value));
    }

    float ReadFloat(uint32_t address) {
        uint32_t raw = 0;
        Read32(address, &raw);
        return std::bit_cast<float>(raw);
    }

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

    std::cout << "Running PlayerSprintRuntime tests...\n";

    // Teste 1: Pressionar e soltar A rapidamente (tap) resulta apenas em rolamento normal, sem corrida
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        // Inicia rolamento
        auto status = sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        Require(status.State == PlayerSprintState::RollWaiting, "A newly pressed during movement must enter RollWaiting");
        Require(status.SpeedMultiplier == 1.0f, "Speed multiplier must be 1.0 during roll");

        // Solta o botão A após 3 quadros (antes do rolamento terminar)
        for (int i = 0; i < 3; ++i) {
            status = sprint.Update(false, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        }
        Require(status.State == PlayerSprintState::Idle, "Releasing A before roll completes must return to Idle (normal roll only)");
        Require(status.SpeedMultiplier == 1.0f, "Speed multiplier must remain 1.0");
    }

    // Teste 2: Pressionar e manter A segurado durante todo o rolamento -> entra em corrida (Sprint) ao levantar
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        // Frame 0: Aperta A
        auto status = sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        Require(status.State == PlayerSprintState::RollWaiting, "Initial press must trigger RollWaiting");

        // Avança o rolamento mantendo A pressionado por 24 quadros (0.8s, superando os 0.75s de rolamento)
        for (int frame = 0; frame < 24; ++frame) {
            status = sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        }

        // Link terminou o rolamento e levantou; deve entrar em Sprinting
        Require(status.State == PlayerSprintState::Sprinting, "Holding A through roll completion must enter Sprinting state");
        Require(status.IsSprinting, "IsSprinting must be true");
    }

    // Teste 3: Aceleração gradual e limite estrito (não ultrapassa o teto máximo configurado)
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        // Inicia e completa rolamento mantendo A
        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        for (int frame = 0; frame < 24; ++frame) {
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        }

        // Continua correndo por vários segundos
        float previousMultiplier = 1.0f;
        for (int frame = 0; frame < 60; ++frame) { // 2 segundos correndo
            auto status = sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
            Require(status.SpeedMultiplier >= previousMultiplier, "Speed must increase gradually (monotonically non-decreasing)");
            Require(status.SpeedMultiplier <= sprint.Config().MaxSprintMultiplier + 0.0001f, "Speed multiplier must never exceed max limit");
            previousMultiplier = status.SpeedMultiplier;
        }

        auto finalStatus = sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        Require(std::abs(finalStatus.SpeedMultiplier - sprint.Config().MaxSprintMultiplier) < 0.001f, "Speed multiplier must reach exactly max limit");
    }

    // Teste 4: Restrição de empurrar caixas / blocos
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        // Tentar iniciar com Link empurrando caixa (flag 0x00000004)
        uint32_t pushingBoxFlags = 0x00000004U;
        auto status = sprint.Update(true, true, 0.0f, 100.0f, pushingBoxFlags, 0, 0, 5.0f, dt);
        Require(status.State == PlayerSprintState::Idle, "Cannot enter roll-waiting or sprint when pushing boxes");

        // Agora inicia sprint normalmente
        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        for (int frame = 0; frame < 24; ++frame) {
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        }
        status = sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        Require(status.IsSprinting, "Link should be sprinting");

        // Link colide com uma caixa e engaja na ação de empurrar
        status = sprint.Update(true, false, 0.0f, 100.0f, pushingBoxFlags, 0, 0, 5.0f, dt);
        Require(status.State == PlayerSprintState::Idle, "Pushing box must immediately cancel sprint");
        Require(!status.IsSprinting, "IsSprinting must be false when pushing boxes");
    }

    // Teste 5: Stamina de 15 segundos e Cooldown de 15 segundos
    {
        PlayerSprintRuntime sprint;
        const float dt = 0.1f; // passos de 100ms para acelerar simulação

        // Inicia sprint
        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        for (int frame = 0; frame < 8; ++frame) { // 0.8s de rolamento
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        }

        // Corre por 14 segundos (ainda deve ter stamina)
        for (int step = 0; step < 140; ++step) {
            auto s = sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
            Require(s.IsSprinting, "Must remain sprinting while stamina > 0");
        }

        // Corre mais 1.1 segundo para esgotar os 15 segundos totais
        PlayerSprintStatus status{};
        for (int step = 0; step < 12; ++step) {
            status = sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        }

        // Stamina deve ter esgotado e entrado em ExhaustedCooldown
        Require(status.State == PlayerSprintState::ExhaustedCooldown, "Exhausting 15s stamina must enter ExhaustedCooldown");
        Require(!status.IsSprinting, "Cannot be sprinting in cooldown");
        Require(status.StaminaRemainingSeconds == 0.0f, "Stamina must be 0");
        Require(status.CooldownRemainingSeconds > 14.0f, "Cooldown must start at 15s");

        // Durante o cooldown de 15 segundos, Link tenta apertar A de novo: NÃO PODE CORRER
        for (int step = 0; step < 100; ++step) { // 10 segundos de cooldown transcorridos
            status = sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
            Require(status.State == PlayerSprintState::ExhaustedCooldown, "Must remain locked in cooldown");
            Require(!status.IsSprinting, "Cannot sprint while cooldown is active");
        }

        // Passam os 5 segundos finais do cooldown (total 15s completados)
        for (int step = 0; step < 55; ++step) {
            status = sprint.Update(false, false, 0.0f, 0.0f, 0, 0, 0, 0.0f, dt);
        }

        Require(status.State == PlayerSprintState::Idle, "Cooldown must expire and return to Idle");
        Require(status.CooldownRemainingSeconds == 0.0f, "Cooldown must be 0");
        Require(status.StaminaRemainingSeconds == 15.0f, "Stamina must be refilled to 15s");

        // Agora que o cooldown passou, pode correr de novo!
        status = sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt);
        Require(status.State == PlayerSprintState::RollWaiting, "Can trigger sprint again after cooldown");
    }

    // Teste 6: ApplyGuestPlayerSprint integração direta com memória guest A32
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U); // stateFlags1
        memory.Write32(kPlayer + 0x1224U, 0U); // heldActor
        memory.Write8(kPlayer + 0x12BCU, 0U);  // cutsceneAction
        memory.WriteFloat(kPlayer + 0x006CU, 5.5f); // speedXZ
        memory.WriteFloat(kPlayer + 0x221CU, 5.5f); // linearVelocity
        memory.WriteFloat(kPlayer + 0x0060U, 3.0f); // velX
        memory.WriteFloat(kPlayer + 0x0068U, 4.0f); // velZ
        memory.WriteFloat(kPlayer + 0x0028U, 100.0f); // worldPosX
        memory.WriteFloat(kPlayer + 0x0030U, 200.0f); // worldPosZ
        memory.Write16(kPlayer + 0x0090U, 0x0001U); // bgCheckFlags (no chão, sem parede)
        memory.WriteFloat(kPlayer + 0x0290U, 5.0f); // SkelAnime currentFrame
        memory.WriteFloat(kPlayer + 0x0294U, 1.0f); // SkelAnime playSpeed
        memory.WriteFloat(kPlayer + 0x02A0U, 24.0f); // SkelAnime animLength

        // 1. Inicia rolamento pressionando A (frame 0)
        bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, true, 0.0f, 100.0f, dt);
        Require(!sprinting, "Should not be sprinting during initial roll");
        Require(memory.ReadFloat(kPlayer + 0x0294U) == 1.0f, "PlaySpeed must be 1.0 during roll");

        // 2. Avança rolamento mantendo A segurado por 24 frames
        for (int frame = 0; frame < 24; ++frame) {
            sprinting = ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
        }
        Require(sprinting, "Must be sprinting after roll finishes while holding A");
        Require(memory.ReadFloat(kPlayer + 0x0294U) > 1.0f, "PlaySpeed in SkelAnime must accelerate to match sprint speed");
        Require(memory.ReadFloat(kPlayer + 0x0290U) > 5.0f, "CurrentFrame in SkelAnime must advance in lockstep with sprint speed");
        Require(memory.ReadFloat(kPlayer + 0x0028U) > 100.0f, "Link worldPosX must advance with extra sprint speed");

        // 3. Empurra caixa durante sprint: cancela sprint imediatamente
        memory.Write32(kPlayer + 0x1710U, 0x00000004U); // kState1PullingPushing
        sprinting = ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
        Require(!sprinting, "Sprint must immediately cancel upon pushing box");
        Require(memory.ReadFloat(kPlayer + 0x0294U) == 1.0f, "PlaySpeed must reset to 1.0f when sprint cancels");
    }

    // Teste 7: Quando o Link pula (sai do chão), a velocidade para na hora (cancela o sprint)
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        // Inicia e completa o rolamento até entrar em Sprinting no chão (isGrounded = true)
        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        for (int frame = 0; frame < 24; ++frame) {
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        }
        Require(sprint.IsSprinting(), "Must be sprinting while on ground");
        Require(sprint.SpeedMultiplier() > 1.0f, "Speed multiplier must be boosted");

        // Link salta / sai do chão (isGrounded = false)
        auto status = sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, false);
        Require(!status.IsSprinting, "Sprint must immediately cancel upon jumping (leaving ground)");
        Require(status.SpeedMultiplier == 1.0f, "Speed must immediately stop / reset to 1.0f upon jumping (no ramp-down)");
        Require(status.State == PlayerSprintState::Idle, "Must transition to Idle upon jumping");
    }

    // Teste 8: Subindo / escalando escadas, vinhas ou beiradas (Climbing)
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;
        constexpr uint32_t kState1ClimbingLadder = 0x00000200U;
        constexpr uint32_t kState1ClimbingLedge = 0x00002000U;
        constexpr uint32_t kState1ClimbingStart = 0x00200000U;

        // 1. Tentar iniciar sprint enquanto sobe escada
        auto status = sprint.Update(true, true, 0.0f, 100.0f, kState1ClimbingLadder, 0, 0, 5.0f, dt, true);
        Require(status.State == PlayerSprintState::Idle, "Cannot enter roll-waiting while climbing ladder");
        Require(!status.IsSprinting, "Cannot sprint while climbing ladder");
        Require(status.IsClimbingOrHanging, "IsClimbingOrHanging flag must be reported");

        // 2. Tentar iniciar sprint enquanto sobe degrau / borda (ledge climb)
        status = sprint.Update(true, true, 0.0f, 100.0f, kState1ClimbingLedge, 0, 0, 5.0f, dt, true);
        Require(status.State == PlayerSprintState::Idle, "Cannot enter roll-waiting while climbing ledge");

        // 3. Tentar iniciar sprint ao engajar em escalada (climbing start)
        status = sprint.Update(true, true, 0.0f, 100.0f, kState1ClimbingStart, 0, 0, 5.0f, dt, true);
        Require(status.State == PlayerSprintState::Idle, "Cannot enter roll-waiting at climbing start");

        // 4. Iniciar sprint no chão e depois engajar em escalada: cancela imediatamente sem velocidade residual
        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        for (int frame = 0; frame < 24; ++frame) {
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        }
        Require(sprint.IsSprinting(), "Sprint should be active before climbing ladder");

        status = sprint.Update(true, false, 0.0f, 100.0f, kState1ClimbingLadder, 0, 0, 5.0f, dt, true);
        Require(status.State == PlayerSprintState::Idle, "Engaging in ladder climb must immediately cancel sprint to Idle");
        Require(!status.IsSprinting, "Cannot be sprinting while climbing ladder");
        Require(status.SpeedMultiplier == 1.0f, "Speed multiplier must drop immediately to 1.0f with no ramp-down on ladder");
    }

    // Teste 9: Se pendurar em beiradas ou grades do teto (Hanging)
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;
        constexpr uint32_t kState1HangingOffLedge = 0x00000400U;
        constexpr uint32_t kState1HangingFromCeiling = 0x00040000U;

        // 1. Tentar iniciar sprint enquanto pendurado na beirada
        auto status = sprint.Update(true, true, 0.0f, 100.0f, kState1HangingOffLedge, 0, 0, 0.0f, dt, false);
        Require(status.State == PlayerSprintState::Idle, "Cannot trigger sprint while hanging off ledge");
        Require(!status.IsSprinting, "Cannot sprint while hanging off ledge");
        Require(status.IsClimbingOrHanging, "IsClimbingOrHanging must be true when hanging off ledge");

        // 2. Tentar iniciar sprint enquanto pendurado no teto / grade
        status = sprint.Update(true, true, 0.0f, 100.0f, kState1HangingFromCeiling, 0, 0, 0.0f, dt, false);
        Require(status.State == PlayerSprintState::Idle, "Cannot trigger sprint while hanging from ceiling");

        // 3. Estava correndo e Link se pendura na borda: cancela sprint na hora
        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        for (int frame = 0; frame < 24; ++frame) {
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        }
        Require(sprint.IsSprinting(), "Link should be sprinting");

        status = sprint.Update(true, false, 0.0f, 100.0f, kState1HangingOffLedge, 0, 0, 0.0f, dt, false);
        Require(!status.IsSprinting, "Sprint must immediately cancel upon hanging off ledge");
        Require(status.SpeedMultiplier == 1.0f, "Speed multiplier must immediately drop to 1.0f upon hanging off ledge");
    }

    // Teste 10: Puxando algo (Pulling)
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;
        constexpr uint32_t kState1StartPullingPushing = 0x00000002U;

        // 1. Não pode iniciar corrida ao puxar objeto
        auto status = sprint.Update(true, true, 0.0f, -100.0f, kState1StartPullingPushing, 0, 0, 2.0f, dt, true);
        Require(status.State == PlayerSprintState::Idle, "Cannot trigger sprint while initiating pull");
        Require(!status.IsSprinting, "Cannot sprint while pulling");

        // 2. Estava correndo e agarra objeto para puxar: cancela na hora
        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        for (int frame = 0; frame < 24; ++frame) {
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        }
        Require(sprint.IsSprinting(), "Link should be sprinting");

        status = sprint.Update(true, false, 0.0f, -100.0f, kState1StartPullingPushing, 0, 0, 2.0f, dt, true);
        Require(!status.IsSprinting, "Sprint must immediately cancel upon pulling");
        Require(status.SpeedMultiplier == 1.0f, "Speed multiplier must immediately reset to 1.0f with no ramp-down on pull");
    }

    // Teste 11: Em diálogos (Talking / Textboxes)
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;
        constexpr uint32_t kState1Talking = 0x00000020U;

        // 1. Durante diálogo com NPC (botão A sendo apertado para avançar texto): NÃO deve acionar sprint
        auto status = sprint.Update(true, true, 0.0f, 0.0f, kState1Talking, 0, 0, 0.0f, dt, true, true);
        Require(status.State == PlayerSprintState::Idle, "Pressing A in dialogue must never enter RollWaiting");
        Require(!status.IsSprinting, "Cannot sprint in dialogue");
        Require(status.IsInDialogue, "IsInDialogue flag must be true");

        // 2. Mesmo se o analógico estiver inclinado e botão A for pressionado em diálogo
        status = sprint.Update(true, true, 50.0f, 50.0f, kState1Talking, 0, 0, 0.0f, dt, true, true);
        Require(status.State == PlayerSprintState::Idle, "Cannot trigger sprint during dialogue even with stick deflected");

        // 3. Se por acaso estivesse correndo e entrar em diálogo: cancela na hora
        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        for (int frame = 0; frame < 24; ++frame) {
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        }
        Require(sprint.IsSprinting(), "Link should be sprinting");

        status = sprint.Update(true, false, 0.0f, 0.0f, kState1Talking, 0, 0, 0.0f, dt, true, true);
        Require(!status.IsSprinting, "Entering dialogue must immediately cancel sprint");
        Require(status.SpeedMultiplier == 1.0f, "Speed multiplier must reset immediately to 1.0f in dialogue");
    }

    // Teste 12: Salto automático de beirada / abismo (Hopping)
    {
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;
        constexpr uint32_t kState1Hopping = 0x20000000U;

        sprint.Update(true, true, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        for (int frame = 0; frame < 24; ++frame) {
            sprint.Update(true, false, 0.0f, 100.0f, 0, 0, 0, 5.0f, dt, true);
        }
        Require(sprint.IsSprinting(), "Link should be sprinting");

        auto status = sprint.Update(true, false, 0.0f, 100.0f, kState1Hopping, 0, 0, 5.0f, dt, false);
        Require(!status.IsSprinting, "Hopping ledge must immediately cancel sprint");
        Require(status.SpeedMultiplier == 1.0f, "Speed multiplier must immediately drop to 1.0f upon hopping");
    }

    // Teste 13: ApplyGuestPlayerSprint com MessageContext (msgMode != 0) na memória
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U); // stateFlags1
        memory.Write32(kPlayer + 0x1714U, 0U); // stateFlags2
        memory.Write32(kPlayer + 0x1224U, 0U); // heldActor
        memory.Write8(kPlayer + 0x12BCU, 0U);  // cutsceneAction
        memory.WriteFloat(kPlayer + 0x006CU, 0.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 0.0f);
        memory.Write16(kPlayer + 0x0090U, 0x0001U); // no chão

        // Diálogo ativo: msgCtx (PlayState + 0x32C0U) com msgMode = 0x07 (await input) em +0x0FA0U
        memory.Write8(kPlayState + 0x32C0U + 0x0FA0U, 0x07U);

        // Jogador aperta A durante diálogo
        bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, true, 0.0f, 100.0f, dt);
        Require(!sprinting, "ApplyGuestPlayerSprint must not trigger sprint when msgMode != 0");
        Require(sprint.State() == PlayerSprintState::Idle, "Must stay Idle when message is active");
    }

    // Teste 14: ApplyGuestPlayerSprint na base da escada (kBgCheckFlagGround ligado, mas escalando)
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0x00000200U); // kState1ClimbingLadder ativo
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.WriteFloat(kPlayer + 0x006CU, 1.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 1.0f);
        memory.Write16(kPlayer + 0x0090U, 0x0001U); // flag de chão está setada porque os pés tocam a base da escada

        // Pressionar A enquanto na escada: não pode correr
        bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, true, 0.0f, 100.0f, dt);
        Require(!sprinting, "ApplyGuestPlayerSprint must NOT sprint when climbing ladder even if ground flag is set");
        Require(sprint.State() == PlayerSprintState::Idle, "Must stay Idle when on ladder");
    }

    std::cout << "All PlayerSprintRuntime tests PASSED!\n";
    return 0;
}

