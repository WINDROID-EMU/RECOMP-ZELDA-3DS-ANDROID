#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ThreeDsRecomp::Pica {

/**
 * PicaCommandHeader represents the 32-bit packed word at the start of each
 * PICA200 command packet (3dbrew GPU/Command Processor).
 *
 * Bits 0..15:  Register index (0x000 to 0x2FF)
 * Bits 16..19: Byte mask (4 bits: bit 0 = byte 0, ..., bit 3 = byte 3)
 * Bits 20..30: Parameter count (number of following 32-bit data words)
 * Bit 31:      Consecutive flag (1 = increment register per parameter, 0 = write to same register)
 */
struct PicaCommandHeader {
    uint16_t RegisterId = 0;
    uint8_t ByteMask = 0xF;
    uint16_t ParameterCount = 0;
    bool Consecutive = false;

    static constexpr PicaCommandHeader Decode(uint32_t raw) noexcept {
        PicaCommandHeader h;
        h.RegisterId = static_cast<uint16_t>(raw & 0xFFFFU);
        h.ByteMask = static_cast<uint8_t>((raw >> 16U) & 0x0FU);
        h.ParameterCount = static_cast<uint16_t>((raw >> 20U) & 0x7FFU);
        h.Consecutive = ((raw >> 31U) & 1U) != 0U;
        return h;
    }

    constexpr uint32_t Encode() const noexcept {
        return static_cast<uint32_t>(RegisterId) |
               (static_cast<uint32_t>(ByteMask & 0x0FU) << 16U) |
               (static_cast<uint32_t>(ParameterCount & 0x7FFU) << 20U) |
               (Consecutive ? (1U << 31U) : 0U);
    }
};

class PicaCommandProcessor {
public:
    static constexpr size_t kRegisterCount = 0x300U;

    PicaCommandProcessor() = default;

    void Reset();

    // Write a single register respecting the 4-bit byte write mask
    void WriteRegister(uint16_t regId, uint8_t byteMask, uint32_t value);

    // Read a register
    [[nodiscard]] uint32_t ReadRegister(uint16_t regId) const;

    // Direct access to register space (0x000 to 0x2FF)
    [[nodiscard]] const std::array<uint32_t, kRegisterCount>& GetRegisters() const noexcept {
        return mRegisters;
    }

    [[nodiscard]] std::array<uint32_t, kRegisterCount>& GetRegistersMutable() noexcept {
        return mRegisters;
    }

    // Process a stream of raw 32-bit command words
    // Returns number of words successfully consumed. Never throws or aborts (R12).
    size_t ProcessCommandList(std::span<const uint32_t> words);

private:
    std::array<uint32_t, kRegisterCount> mRegisters{};
    uint32_t mUnknownWriteCount = 0;
};

} // namespace ThreeDsRecomp::Pica
