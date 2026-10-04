#pragma once

#include "pica_state.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <span>

namespace ThreeDsRecomp::Pica {

struct PicaFragmentShaderConfig {
    bool UseMediumpTev = true;               // RelaxedPrecision for TEV (Adreno optimization, R28)
    bool EnableAlphaTest = true;              // Alpha test discard
    bool EnableFog = true;                    // Fog LUT blending
    bool EnableFragmentLighting = true;       // Fragment LUT lighting
    bool EnableCombinerBuffer = true;         // Combiner buffer 2-stage delay
    bool EnableTexture0 = true;
    bool EnableTexture1 = true;
    bool EnableTexture2 = true;
};

struct alignas(16) PicaFragmentUniformState {
    std::array<std::array<float, 4>, 6> TevConstants{};
    std::array<float, 4> CombinerBufferColor{};
    float AlphaReference = 0.0f;
    float Reserved0 = 0.0f;
    float Reserved1 = 0.0f;
    float Reserved2 = 0.0f;
    std::array<float, 4> FogColor{};
    std::array<std::array<float, 4>, 64> FogLut{};
    std::array<float, 4> TextureLodBias{};
    std::array<std::array<float, 4>, 8> LightSpecular0{};
    std::array<std::array<float, 4>, 8> LightDiffuse{};
    std::array<std::array<float, 4>, 8> LightAmbient{};
    std::array<std::array<float, 4>, 8> LightPosition{};
    std::array<float, 4> GlobalAmbient{};
};

struct PicaFragmentShaderOutput {
    uint64_t StateKey = 0;
    std::string GlslSource;
    std::vector<uint32_t> SpirvBinary;
    bool ValidSpirv = false;
};

class PicaFragmentGenerator {
public:
    /// Generates structured Vulkan Fragment Shader GLSL (#version 450) from canonical PICA state.
    static bool GenerateGlsl(
        const PicaDrawState& drawState,
        const PicaFragmentShaderConfig& config,
        PicaFragmentShaderOutput& outShader,
        std::string* errorLog = nullptr);

    /// Compiles GLSL fragment shader source to SPIR-V words using glslangValidator.
    static bool CompileGlslToSpirv(
        const std::string& glslSource,
        std::vector<uint32_t>& outSpirv,
        std::string* errorLog = nullptr);

    /// Validates SPIR-V binary words using spirv-val tool. Ensures 0 validation errors.
    static bool ValidateSpirv(
        std::span<const uint32_t> spirvWords,
        std::string* errorLog = nullptr);

    /// Software reference execution oracle for the PICA fragment pipeline.
    /// Evaluates TEV combiner stages, lighting, fog and alpha test pixel-by-pixel
    /// to verify numerical parity against Azahar dumps within pixel tolerance.
    static std::array<float, 4> ExecuteCpuFragmentPipeline(
        const PicaDrawState& drawState,
        const PicaFragmentUniformState& uniforms,
        const std::array<float, 4>& primaryColor,
        const std::array<float, 4>& tex0Color = {1.0f, 1.0f, 1.0f, 1.0f},
        const std::array<float, 4>& tex1Color = {1.0f, 1.0f, 1.0f, 1.0f},
        const std::array<float, 4>& tex2Color = {1.0f, 1.0f, 1.0f, 1.0f},
        const std::array<float, 3>& normal = {0.0f, 0.0f, 1.0f},
        float fragmentDepth = 0.5f,
        bool* outDiscarded = nullptr);
};

} // namespace ThreeDsRecomp::Pica
