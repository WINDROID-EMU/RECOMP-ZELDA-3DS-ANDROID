#include "three_ds_recomp/pica/pica_command_processor.h"

#include <algorithm>
#include <cstdio>

namespace ThreeDsRecomp::Pica {

void PicaCommandProcessor::Reset() {
    mRegisters.fill(0U);
    mUnknownWriteCount = 0;
}

void PicaCommandProcessor::WriteRegister(uint16_t regId, uint8_t byteMask, uint32_t value) {
    if (regId >= kRegisterCount) {
        // Rule R12: Never abort/throw on unknown or out-of-bounds register. Log with rate limit.
        if (mUnknownWriteCount < 5U) {
            std::fprintf(stderr, "[PICA WARN] Write out of bounds: reg 0x%04X, mask 0x%X, val 0x%08X\n",
                         regId, byteMask, value);
        }
        ++mUnknownWriteCount;
        return;
    }

    if (byteMask == 0x0FU) {
        mRegisters[regId] = value;
    } else {
        uint32_t bitmask = 0U;
        if (byteMask & 1U) bitmask |= 0x000000FFU;
        if (byteMask & 2U) bitmask |= 0x0000FF00U;
        if (byteMask & 4U) bitmask |= 0x00FF0000U;
        if (byteMask & 8U) bitmask |= 0xFF000000U;

        mRegisters[regId] = (mRegisters[regId] & ~bitmask) | (value & bitmask);
    }
}

uint32_t PicaCommandProcessor::ReadRegister(uint16_t regId) const {
    if (regId >= kRegisterCount) {
        return 0U;
    }
    return mRegisters[regId];
}

size_t PicaCommandProcessor::ProcessCommandList(std::span<const uint32_t> words) {
    size_t index = 0;
    while (index < words.size()) {
        const uint32_t headerWord = words[index++];
        const PicaCommandHeader header = PicaCommandHeader::Decode(headerWord);
        const uint16_t reg = header.RegisterId;
        const uint8_t mask = header.ByteMask;
        const uint32_t paramCount = header.ParameterCount;
        const bool consecutive = header.Consecutive;

        for (uint32_t p = 0; p < paramCount; ++p) {
            if (index >= words.size()) {
                break;
            }
            const uint32_t paramVal = words[index++];
            const uint16_t targetReg = consecutive ? static_cast<uint16_t>(reg + p) : reg;
            WriteRegister(targetReg, mask, paramVal);
        }

        // Align to 64 bits if next word is zero padding
        if ((paramCount & 1U) == 0U && index < words.size() && words[index] == 0U) {
            index++;
        }
    }
    return index;
}

} // namespace ThreeDsRecomp::Pica
