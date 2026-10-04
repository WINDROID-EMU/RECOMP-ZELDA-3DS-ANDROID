#pragma once

#include "pica_shader.h"
#include <string>
#include <vector>
#include <span>
#include <array>

namespace ThreeDsRecomp::Pica {

class PicaShaderTranslator {
public:
    /// Decodes a 32-bit PICA instruction and its corresponding swizzle descriptor.
    static DecodedInstruction DecodeInstruction(uint32_t rawWord, uint32_t swizzleWord = 0);

    /// Decodes a 32-bit PICA swizzle descriptor into individual operand selectors and dest mask.
    static DecodedSwizzle DecodeSwizzle(uint32_t swizzleWord);

    /// Translates a PICA vertex shader bytecode program into structured Vulkan GLSL (#version 450).
    static bool TranslateGlsl(
        std::span<const uint32_t> programCode,
        std::span<const uint32_t> swizzleData,
        uint32_t mainOffset,
        const PicaShaderConfig& config,
        PicaShaderOutput& outShader,
        std::string* errorLog = nullptr);

    /// Compiles a GLSL shader string to SPIR-V binary words via glslangValidator.
    static bool CompileGlslToSpirv(
        const std::string& glslSource,
        std::vector<uint32_t>& outSpirv,
        std::string* errorLog = nullptr);

    /// Validates SPIR-V binary words using spirv-val tool. Ensures 0 validation errors.
    static bool ValidateSpirv(
        std::span<const uint32_t> spirvWords,
        std::string* errorLog = nullptr);

    /// Reference software vertex processor (oracle) to compute numerical transformations
    /// vertex-by-vertex for exact comparison against Azahar / hardware output.
    static std::array<float, 4> ExecuteCpuVertexProgram(
        std::span<const uint32_t> programCode,
        std::span<const uint32_t> swizzleData,
        uint32_t mainOffset,
        const PicaVertexUniformState& uniforms,
        const std::array<float, 4>& inPosition,
        const std::array<float, 4>& inNormal = {0.0f, 0.0f, 1.0f, 0.0f},
        const PicaShaderConfig& config = {});
};

} // namespace ThreeDsRecomp::Pica
