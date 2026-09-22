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

    // Teste 15: Escalada em vinhas/paredes com deslocamento lateral do analógico (CirclePadX != 0)
    // Evita o bug onde Link 'sai correndo de lado' rapidamente ao caminhar/escalar pro lado em vinhas
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U); // stateFlags1 pode não ter a flag de escada ao escalar vinhas
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        // Ação nativa de escalada de superfície/vinhas (SURFACE_CLIMB_ACTION_FUNCTION = 0x004BE20CU)
        memory.Write32(kPlayer + 0x1708U, 0x004BE20CU);
        // Animação de escalada lateral em vinha (ex: 0x011CU = free_climb_side_left)
        memory.Write32(kPlayer + 0x0284U, 0x011CU);
        memory.Write16(kPlayer + 0x0090U, 0x0200U); // kBgCheckFlagPlayerWallInteract

        const float initialPosX = 150.0f;
        const float initialPosZ = -300.0f;
        memory.WriteFloat(kPlayer + 0x0028U, initialPosX);
        memory.WriteFloat(kPlayer + 0x0030U, initialPosZ);
        memory.WriteFloat(kPlayer + 0x0060U, 1.2f); // velX
        memory.WriteFloat(kPlayer + 0x0068U, 0.0f); // velZ
        memory.WriteFloat(kPlayer + 0x006CU, 1.2f); // speedXZ
        memory.WriteFloat(kPlayer + 0x221CU, 1.2f); // linearVelocity

        // Usuário segura o botão A e empurra o analógico 100% para o lado enquanto escala a vinha
        for (int frame = 0; frame < 30; ++frame) {
            bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, (frame == 0), 100.0f, 0.0f, dt);
            Require(!sprinting, "ApplyGuestPlayerSprint must NOT activate sprint when climbing vines sideways");
            Require(sprint.State() == PlayerSprintState::Idle, "Sprint state must remain Idle while climbing");
        }

        // Garante que a posição de Link NÃO foi deslocada pelo bônus de corrida
        const float finalPosX = memory.ReadFloat(kPlayer + 0x0028U);
        const float finalPosZ = memory.ReadFloat(kPlayer + 0x0030U);
        Require(finalPosX == initialPosX, "Link posX must NOT be warped sideways when climbing vines");
        Require(finalPosZ == initialPosZ, "Link posZ must NOT be warped when climbing vines");

        // Garante que a velocidade não foi forçada para a velocidade de corrida
        const float finalSpeedXZ = memory.ReadFloat(kPlayer + 0x006CU);
        const float finalLinVel = memory.ReadFloat(kPlayer + 0x221CU);
        Require(finalSpeedXZ <= 1.2f, "speedXZ must NOT be boosted to sprint velocity during climb");
        Require(finalLinVel <= 1.2f, "linearVelocity must NOT be boosted to sprint velocity during climb");
    }

    // Teste 16: Interrupção imediata de sprint ao agarrar vinhas/muros durante a corrida
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U);
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.Write16(kPlayer + 0x0090U, 0x0001U); // no chão
        memory.WriteFloat(kPlayer + 0x006CU, 5.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 5.0f);

        // 1. Inicia corrida normal em solo plano
        for (int frame = 0; frame < 30; ++frame) {
            ApplyGuestPlayerSprint(memory, sprint, true, (frame == 0), 0.0f, 100.0f, dt);
        }
        Require(sprint.IsSprinting(), "Sprint should be active on flat ground");
        Require(sprint.SpeedMultiplier() > 1.0f, "SpeedMultiplier should be boosted");

        // 2. Link colide e agarra a vinha/parede (ação de escalada ativa, kBgCheckFlagPlayerWallInteract)
        memory.Write32(kPlayer + 0x1708U, 0x004BE20CU); // SURFACE_CLIMB_ACTION_FUNCTION
        memory.Write16(kPlayer + 0x0090U, 0x0200U);
        memory.Write32(kPlayer + 0x0284U, 0x0104U); // Animação de escalada

        bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, false, 80.0f, 0.0f, dt);
        Require(!sprinting, "Sprint must immediately cancel upon grabbing climbable surface");
        Require(sprint.State() == PlayerSprintState::Idle, "State must immediately drop to Idle");
        Require(sprint.SpeedMultiplier() == 1.0f, "Speed multiplier must instantly drop to 1.0f");
    }

    // Teste 17: Escalada de borda / Ledge climb (ledgeClimbType != 0)
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U);
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.Write8(kPlayer + 0x2278U, 2U); // ledgeClimbType = 2 (subindo em parapeito/borda)
        memory.Write16(kPlayer + 0x0090U, 0x0001U); // ground flag

        bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, true, 90.0f, 0.0f, dt);
        Require(!sprinting, "Must not sprint while climbing up a ledge");
        Require(sprint.State() == PlayerSprintState::Idle, "Must stay Idle during ledge climbing");
    }

    // Teste 18: Link bate na parede durante o rolamento (RollWaiting cancelado na hora, corrida NÃO inicia)
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U);
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.Write16(kPlayer + 0x0090U, 0x0001U); // no chão, sem parede inicialmente
        memory.Write32(kPlayer + 0x0078U, 0U);      // wallPoly nulo

        const float initialPosX = 100.0f;
        const float initialPosZ = 200.0f;
        memory.WriteFloat(kPlayer + 0x0028U, initialPosX);
        memory.WriteFloat(kPlayer + 0x0030U, initialPosZ);
        memory.WriteFloat(kPlayer + 0x0060U, 0.0f);
        memory.WriteFloat(kPlayer + 0x0068U, 5.0f);
        memory.WriteFloat(kPlayer + 0x006CU, 5.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 5.0f);

        // Frame 0: Pressiona A e segura para rolar e correr depois
        ApplyGuestPlayerSprint(memory, sprint, true, true, 0.0f, 100.0f, dt);
        Require(sprint.State() == PlayerSprintState::RollWaiting, "Must enter RollWaiting when starting roll");

        // Frames 1 a 5: Link rolando em direção à parede
        for (int f = 1; f < 5; ++f) {
            ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
            Require(sprint.State() == PlayerSprintState::RollWaiting, "Must remain RollWaiting before impact");
        }

        // Frame 6: Link BATE na parede durante o rolamento!
        memory.Write16(kPlayer + 0x0090U, 0x0009U); // kBgCheckFlagGround | kBgCheckFlagWall
        memory.Write32(kPlayer + 0x0078U, 0x00223344U); // wallPoly não nulo
        // A física nativa reage: velocidade vai para 0 ou recuo negativo
        memory.WriteFloat(kPlayer + 0x006CU, 0.0f);
        memory.WriteFloat(kPlayer + 0x221CU, -2.0f);

        ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
        Require(sprint.State() == PlayerSprintState::Idle, "Hitting wall during roll MUST cancel RollWaiting to Idle immediately");
        Require(!sprint.IsSprinting(), "Must not be sprinting after hitting wall");

        // Frames 7 a 35: Usuário continua segurando A e analógico contra a parede esperando correr
        for (int f = 7; f < 35; ++f) {
            bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
            Require(!sprinting, "Must NOT start sprinting after hitting wall, even if A is held");
            Require(sprint.State() == PlayerSprintState::Idle, "Must remain Idle while holding A after bonk");
        }

        // Garante que a velocidade não foi forçada para corrida plena e a posição não sofreu teleporte
        const float linVel = memory.ReadFloat(kPlayer + 0x221CU);
        Require(linVel < 5.0f, "linearVelocity must NOT be boosted to sprint velocity after wall bonk");
        const float finalPosX = memory.ReadFloat(kPlayer + 0x0028U);
        const float finalPosZ = memory.ReadFloat(kPlayer + 0x0030U);
        Require(finalPosX == initialPosX, "Link posX must not slide into wall");
        Require(finalPosZ == initialPosZ, "Link posZ must not slide into wall");
    }

    // Teste 19: Link bate em obstáculo e senta no chão ao terminar rolamento (bonk/sitting recoil)
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U);
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.Write16(kPlayer + 0x0090U, 0x0001U);
        memory.WriteFloat(kPlayer + 0x006CU, 5.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 5.0f);

        // Inicia rolamento
        ApplyGuestPlayerSprint(memory, sprint, true, true, 0.0f, 100.0f, dt);

        // Rola até quase o fim (frame 21 = 0.70s)
        for (int f = 1; f < 22; ++f) {
            ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
        }

        // No fim do rolamento (frame 22 e 23), Link bateu em algo e está sentado no chão!
        // (speedXZ = 0.0f, linearVelocity = 0.0f, colisão de parede ativa)
        memory.Write16(kPlayer + 0x0090U, 0x0009U); // kBgCheckFlagWall
        memory.Write32(kPlayer + 0x0078U, 0x00112233U);
        memory.WriteFloat(kPlayer + 0x006CU, 0.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 0.0f);

        // Frame 23 (>= 0.75s): Rolamento finaliza enquanto Link está sentado/bonked contra parede
        bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
        Require(!sprinting, "Link must NOT sprint when finishing roll against a wall / sitting down");
        Require(sprint.State() == PlayerSprintState::Idle, "Must drop to Idle instead of Sprinting");

        // Verifica que linearVelocity e speedXZ NÃO foram sobrescritos com 5.66f * multiplier
        const float linVel = memory.ReadFloat(kPlayer + 0x221CU);
        const float speedXZ = memory.ReadFloat(kPlayer + 0x006CU);
        Require(linVel == 0.0f, "linearVelocity must stay 0.0f so Link does not slide while sitting on the ground");
        Require(speedXZ == 0.0f, "speedXZ must stay 0.0f so Link does not slide while sitting on the ground");
    }

    // Teste 20: Link colide com parede enquanto já está em velocidade plena de corrida
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U);
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.Write16(kPlayer + 0x0090U, 0x0001U);
        memory.WriteFloat(kPlayer + 0x006CU, 5.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 5.0f);

        // Inicia e atinge velocidade plena de corrida
        for (int f = 0; f < 30; ++f) {
            ApplyGuestPlayerSprint(memory, sprint, true, (f == 0), 0.0f, 100.0f, dt);
        }
        Require(sprint.IsSprinting(), "Must be sprinting");
        Require(sprint.SpeedMultiplier() > 1.0f, "Multiplier must be boosted");

        // Link bate de frente na parede durante a corrida
        memory.Write16(kPlayer + 0x0090U, 0x0009U); // kBgCheckFlagWall
        memory.Write32(kPlayer + 0x0078U, 0x00998877U);
        memory.WriteFloat(kPlayer + 0x006CU, 0.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 0.0f);

        bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
        Require(!sprinting, "Sprint must immediately cancel upon wall impact");
        Require(sprint.State() == PlayerSprintState::Idle, "Must return to Idle");
        Require(sprint.SpeedMultiplier() == 1.0f, "Speed multiplier must instantly reset to 1.0f");
    }

    // Teste 21: Flag de knockback / recoil (stateFlags1 & 0x04000000U) invalida sprint
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0x04000000U); // kState1KnockbackRecoil
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.Write16(kPlayer + 0x0090U, 0x0001U);
        memory.WriteFloat(kPlayer + 0x006CU, 4.0f);
        memory.WriteFloat(kPlayer + 0x221CU, 4.0f);

        bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, true, 0.0f, 100.0f, dt);
        Require(!sprinting, "Must not sprint while in recoil/knockback state");
        Require(sprint.State() == PlayerSprintState::Idle, "Must stay in Idle");
    }

    // Teste 22: Link rola com velocidade nativa plena (speedXZ = 8.5f, linVel = 8.5f);
    // a velocidade NÃO pode ser limitada a 5.66f durante o rolamento, garantindo que
    // caixas de madeira (OBJ_KIBAKO2) quebrem (exige speedXZ >= 7.0f no OoT/OoT3D).
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U);
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.Write16(kPlayer + 0x0090U, 0x0001U); // Chão (kBgCheckFlagGround)
        memory.Write32(kPlayer + 0x0078U, 0U);      // wallPoly nulo

        // No OoT3D original, a velocidade de rolamento atinge 1.5x a velocidade máxima de corrida (~8.49f)
        const float rollSpeed = 8.49f;
        memory.WriteFloat(kPlayer + 0x006CU, rollSpeed);
        memory.WriteFloat(kPlayer + 0x221CU, rollSpeed);

        // Frame 0: Pressiona A para rolar
        ApplyGuestPlayerSprint(memory, sprint, true, true, 0.0f, 100.0f, dt);

        // Frames 1 a 10: Link continua no meio do rolamento em alta velocidade
        for (int f = 1; f <= 10; ++f) {
            memory.WriteFloat(kPlayer + 0x006CU, rollSpeed);
            memory.WriteFloat(kPlayer + 0x221CU, rollSpeed);
            ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);

            // A velocidade de rolamento deve permanecer intocada (> 7.0f)
            const float currentSpeedXZ = memory.ReadFloat(kPlayer + 0x006CU);
            const float currentLinVel = memory.ReadFloat(kPlayer + 0x221CU);
            Require(currentSpeedXZ >= 7.0f, "speedXZ must NOT be clamped during roll, must remain >= 7.0f to break crates");
            Require(currentSpeedXZ == rollSpeed, "speedXZ must remain unmodified by sprint runtime while rolling");
            Require(currentLinVel == rollSpeed, "linearVelocity must remain unmodified by sprint runtime while rolling");
        }
    }

    // Teste 23: Link rola contra caixa de madeira com speedXZ = 8.5f, quebra a caixa e sofre recuo (bonk).
    // O sprint runtime NÃO deve acionar sprint nem fazer o Link deslizar no chão após o impacto.
    {
        MockMemoryBus memory;
        PlayerSprintRuntime sprint;
        const float dt = 1.0f / 30.0f;

        const uint32_t kPauseRoot = 0x005043D4U;
        const uint32_t kPlayState = 0x10000000U;
        const uint32_t kPlayer = 0x10002000U;

        const float initialPosX = 50.0f;
        const float initialPosZ = 100.0f;
        memory.WriteFloat(kPlayer + 0x0028U, initialPosX);
        memory.WriteFloat(kPlayer + 0x0030U, initialPosZ);
        memory.WriteFloat(kPlayer + 0x0060U, 0.0f);
        memory.WriteFloat(kPlayer + 0x0068U, 8.5f);
        memory.WriteFloat(kPlayer + 0x006CU, 8.5f);
        memory.WriteFloat(kPlayer + 0x221CU, 8.5f);
        memory.Write32(kPauseRoot + 0x0CU, kPlayState);
        memory.Write32(kPlayState + 0x20ACU, kPlayer);
        memory.Write32(kPlayer + 0x1710U, 0U);
        memory.Write32(kPlayer + 0x1714U, 0U);
        memory.Write32(kPlayer + 0x1224U, 0U);
        memory.Write8(kPlayer + 0x12BCU, 0U);
        memory.Write16(kPlayer + 0x0090U, 0x0001U);

        // Frame 0: Pressiona A e segura para rolar
        ApplyGuestPlayerSprint(memory, sprint, true, true, 0.0f, 100.0f, dt);
        Require(sprint.State() == PlayerSprintState::RollWaiting, "Must enter RollWaiting");

        // Frames 1 a 4: Rolando em direção à caixa a 8.5f
        for (int f = 1; f < 5; ++f) {
            memory.WriteFloat(kPlayer + 0x006CU, 8.5f);
            memory.WriteFloat(kPlayer + 0x221CU, 8.5f);
            ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
            Require(sprint.State() == PlayerSprintState::RollWaiting, "Must remain RollWaiting before impact");
            Require(memory.ReadFloat(kPlayer + 0x006CU) == 8.5f, "Must preserve 8.5f roll speed before impact");
        }

        // Frame 5: Link colide com a caixa (DynaPoly wallPoly ativo)! A velocidade de 8.5f quebra a caixa.
        // O motor nativo ativa recuo bonk (speedXZ = -3.0f, linVel = -3.0f).
        memory.Write16(kPlayer + 0x0090U, 0x0009U); // kBgCheckFlagWall
        memory.Write32(kPlayer + 0x0078U, 0x00445566U); // wallPoly da caixa
        memory.WriteFloat(kPlayer + 0x006CU, -3.0f);
        memory.WriteFloat(kPlayer + 0x221CU, -3.0f);

        ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
        Require(sprint.State() == PlayerSprintState::Idle, "Impact must cancel RollWaiting to Idle");
        Require(!sprint.IsSprinting(), "Must not be sprinting after crate impact");

        // Frames 6 a 30: Jogador continua segurando A enquanto Link se recupera do bonk no chão
        for (int f = 6; f < 30; ++f) {
            bool sprinting = ApplyGuestPlayerSprint(memory, sprint, true, false, 0.0f, 100.0f, dt);
            Require(!sprinting, "Must not sprint while holding A after crate bonk");
            Require(sprint.State() == PlayerSprintState::Idle, "Must stay in Idle");
        }

        Require(memory.ReadFloat(kPlayer + 0x0028U) == initialPosX, "Link must not slide on the ground after crate bonk");
        Require(memory.ReadFloat(kPlayer + 0x0030U) == initialPosZ, "Link must not slide on the ground after crate bonk");
    }

    std::cout << "All PlayerSprintRuntime tests PASSED!\n";
    return 0;
}

