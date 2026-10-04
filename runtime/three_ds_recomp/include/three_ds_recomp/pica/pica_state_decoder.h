#pragma once

#include "three_ds_recomp/pica/pica_state.h"

#include <cstdint>
#include <span>
#include <string>

namespace ThreeDsRecomp::Pica {

/**
 * Decodes the 0x300 PICA200 register array into a structured PicaDrawState.
 *
 * Implements Phase 1 decoder according to 'regras agente.md':
 * - Safe fallback on unknown state without abort (R12).
 * - Computes deterministic state hash for Level 2 pipeline lookup.
 */
bool DecodePicaDrawState(
    std::span<const uint32_t> registers,
    PicaDrawState& outState,
    std::string* error = nullptr);

/**
 * Computes a 64-bit deterministic hash of the draw state.
 * Encodes all rasterizer, blend, depth, stencil, TEV, lighting, and texture configuration.
 */
uint64_t ComputePicaStateHash(const PicaDrawState& state) noexcept;

/**
 * Produces a human-readable, canonical state dump matching Azahar oracle format.
 */
std::string DumpReadablePicaState(const PicaDrawState& state);

} // namespace ThreeDsRecomp::Pica
