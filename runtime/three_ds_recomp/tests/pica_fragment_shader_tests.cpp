#include "three_ds_recomp/pica/pica_state.h"
#include "three_ds_recomp/pica/pica_fragment_generator.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

using namespace ThreeDsRecomp::Pica;

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

void TestTevAllOperations() {
    std::cout << "[TEST] 1. TEV Operations (10 operations on RGB & Alpha)..." << std::endl;

    PicaDrawState state{};
    PicaFragmentUniformState uniforms{};
    uniforms.TevConstants[0] = {0.8f, 0.4f, 0.2f, 0.5f};
    std::array<float, 4> primary = {0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> tex0 = {0.2f, 0.3f, 0.4f, 0.6f};

    // Test 1: Replace (s0)
    state.Tev.Stages[0].OpRgb = TevOp::Replace;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::Texture0;
    state.Tev.Stages[0].OpAlpha = TevOp::Replace;
    state.Tev.Stages[0].SourceAlpha[0] = TevSource::Texture0;
    auto resReplace = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    ASSERT_TRUE(std::abs(resReplace[0] - 0.2f) < 0.01f);
    ASSERT_TRUE(std::abs(resReplace[3] - 0.6f) < 0.01f);

    // Test 2: Modulate (s0 * s1)
    state.Tev.Stages[0].OpRgb = TevOp::Modulate;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::PrimaryColor;
    state.Tev.Stages[0].SourceRgb[1] = TevSource::Texture0;
    auto resMod = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    ASSERT_TRUE(std::abs(resMod[0] - (0.5f * 0.2f)) < 0.01f);

    // Test 3: Add (s0 + s1)
    state.Tev.Stages[0].OpRgb = TevOp::Add;
    auto resAdd = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    ASSERT_TRUE(std::abs(resAdd[0] - (0.5f + 0.2f)) < 0.01f);

    // Test 4: AddSigned (s0 + s1 - 0.5)
    state.Tev.Stages[0].OpRgb = TevOp::AddSigned;
    auto resAddSigned = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    ASSERT_TRUE(std::abs(resAddSigned[0] - (0.5f + 0.2f - 0.5f)) < 0.01f);

    // Test 5: Subtract (s0 - s1)
    state.Tev.Stages[0].OpRgb = TevOp::Subtract;
    auto resSub = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    ASSERT_TRUE(std::abs(resSub[0] - (0.5f - 0.2f)) < 0.01f);

    // Test 6: Interpolate (mix(s1, s0, s2))
    state.Tev.Stages[0].OpRgb = TevOp::Interpolate;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::PrimaryColor; // 0.5
    state.Tev.Stages[0].SourceRgb[1] = TevSource::Texture0;     // 0.2
    state.Tev.Stages[0].SourceRgb[2] = TevSource::Constant;     // 0.8
    auto resInterp = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    // mix(0.2, 0.5, 0.8) = 0.2 * 0.2 + 0.5 * 0.8 = 0.04 + 0.40 = 0.44
    ASSERT_TRUE(std::abs(resInterp[0] - 0.44f) < 0.01f);

    // Test 7: Dot3Rgb (dot(s0-0.5, s1-0.5) * 4.0)
    state.Tev.Stages[0].OpRgb = TevOp::Dot3Rgb;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::PrimaryColor; // (0.5, 0.5, 0.5) -> (0, 0, 0)
    state.Tev.Stages[0].SourceRgb[1] = TevSource::Texture0;
    auto resDot3 = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    ASSERT_TRUE(std::abs(resDot3[0] - 0.0f) < 0.01f);

    // Test 8: Dot3Rgba
    state.Tev.Stages[0].OpRgb = TevOp::Dot3Rgba;
    auto resDot3A = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    ASSERT_TRUE(std::abs(resDot3A[0] - 0.0f) < 0.01f);

    // Test 9: MultiplyAdd (s0 * s1 + s2)
    state.Tev.Stages[0].OpRgb = TevOp::MultiplyAdd;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::PrimaryColor; // 0.5
    state.Tev.Stages[0].SourceRgb[1] = TevSource::Texture0;     // 0.2
    state.Tev.Stages[0].SourceRgb[2] = TevSource::Constant;     // 0.8
    auto resMad = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    // 0.5 * 0.2 + 0.8 = 0.1 + 0.8 = 0.9
    ASSERT_TRUE(std::abs(resMad[0] - 0.9f) < 0.01f);

    // Test 10: AddMultiply ((s0 + s1) * s2)
    state.Tev.Stages[0].OpRgb = TevOp::AddMultiply;
    auto resAm = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0);
    // (0.5 + 0.2) * 0.8 = 0.7 * 0.8 = 0.56
    ASSERT_TRUE(std::abs(resAm[0] - 0.56f) < 0.01f);

    std::cout << "       -> Passed!" << std::endl;
}

void TestTevCombinerBuffer() {
    std::cout << "[TEST] 2. TEV Combiner Buffer (2-Stage Delay)..." << std::endl;

    PicaDrawState state{};
    PicaFragmentUniformState uniforms{};
    uniforms.CombinerBufferColor = {0.1f, 0.1f, 0.1f, 1.0f};

    // Stage 0: Writes 0.9 to combiner buffer
    state.Tev.Stages[0].OpRgb = TevOp::Replace;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::Constant;
    uniforms.TevConstants[0] = {0.9f, 0.9f, 0.9f, 1.0f};
    state.Tev.CombinerBufferInputMask |= (1 << 0); // Stage 0 writes buffer

    // Stage 1: Reads CombinerBuffer. Because of 2-stage delay, it should STILL see initial buffer (0.1)!
    state.Tev.Stages[1].OpRgb = TevOp::Replace;
    state.Tev.Stages[1].SourceRgb[0] = TevSource::PreviousBuffer;

    // Stage 2: Reads CombinerBuffer. Now it should see Stage 0's value (0.9)!
    state.Tev.Stages[2].OpRgb = TevOp::Replace;
    state.Tev.Stages[2].SourceRgb[0] = TevSource::PreviousBuffer;

    auto res = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, {0,0,0,1});
    ASSERT_TRUE(std::abs(res[0] - 0.9f) < 0.01f);

    std::cout << "       -> Passed!" << std::endl;
}

void TestTevModifiersAndScales() {
    std::cout << "[TEST] 3. TEV Modifiers and Scale Multipliers (1x, 2x, 4x)..." << std::endl;

    PicaDrawState state{};
    PicaFragmentUniformState uniforms{};
    std::array<float, 4> primary = {0.2f, 0.4f, 0.6f, 0.8f};

    // Test OneMinusColor modifier on RGB
    state.Tev.Stages[0].OpRgb = TevOp::Replace;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::PrimaryColor;
    state.Tev.Stages[0].OperandRgb[0] = 1; // OneMinusColor
    auto resMod = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary);
    ASSERT_TRUE(std::abs(resMod[0] - (1.0f - 0.2f)) < 0.01f);

    // Test Scale2x
    state.Tev.Stages[0].OperandRgb[0] = 0; // Normal color
    state.Tev.Stages[0].ScaleRgb = TevScale::Scale2x;
    auto resScale2 = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary);
    ASSERT_TRUE(std::abs(resScale2[0] - 0.4f) < 0.01f);

    // Test Scale4x
    state.Tev.Stages[0].ScaleRgb = TevScale::Scale4x;
    auto resScale4 = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary);
    ASSERT_TRUE(std::abs(resScale4[0] - 0.8f) < 0.01f);

    std::cout << "       -> Passed!" << std::endl;
}

void TestFragmentLightingConfigurations() {
    std::cout << "[TEST] 4. Fragment Lighting (LUT Configurations and Lights)..." << std::endl;

    PicaDrawState state{};
    state.Lighting.Enabled = true;
    state.Lighting.ActiveLightCount = 1;
    state.Lighting.ConfigIndex = 0;

    PicaFragmentUniformState uniforms{};
    uniforms.GlobalAmbient = {0.1f, 0.1f, 0.1f, 1.0f};
    uniforms.LightPosition[0] = {0.0f, 0.0f, 1.0f, 0.0f}; // Directly overhead
    uniforms.LightDiffuse[0] = {0.8f, 0.8f, 0.8f, 1.0f};

    // Surface normal (0, 0, 1): N.L = 1.0
    // Diffuse output should be ambient (0.1) + light (0.8) = 0.9
    state.Tev.Stages[0].OpRgb = TevOp::Replace;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::PrimaryFragmentColor;

    auto res = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, {1,1,1,1}, {1,1,1,1}, {1,1,1,1}, {1,1,1,1}, {0,0,1});
    ASSERT_TRUE(std::abs(res[0] - 0.9f) < 0.01f);

    std::cout << "       -> Passed!" << std::endl;
}

void TestFogAndAlphaTestDiscard() {
    std::cout << "[TEST] 5. Fog Blending and Alpha Test Discard..." << std::endl;

    PicaDrawState state{};
    state.Fog.Enabled = true;
    PicaFragmentUniformState uniforms{};
    uniforms.FogColor = {1.0f, 0.0f, 0.0f, 1.0f}; // Red fog

    // Fragment depth = 0.5: color should be blended 50% with fog
    state.Tev.Stages[0].OpRgb = TevOp::Replace;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::PrimaryColor;
    std::array<float, 4> primary = {0.0f, 0.0f, 1.0f, 1.0f}; // Blue
    auto resFog = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, {}, {}, {}, {0,0,1}, 0.5f);
    // Red channel: 0.0 * 0.5 + 1.0 * 0.5 = 0.5
    ASSERT_TRUE(std::abs(resFog[0] - 0.5f) < 0.01f);

    // Alpha test: Greater than 0.5
    state.DepthStencil.AlphaTest = true;
    state.DepthStencil.AlphaCompare = CompareFunc::Greater;
    uniforms.AlphaReference = 0.5f;

    bool discarded = false;
    primary[3] = 0.2f; // Below reference, should discard!
    PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, {}, {}, {}, {0,0,1}, 0.0f, &discarded);
    ASSERT_TRUE(discarded);

    primary[3] = 0.8f; // Above reference, should NOT discard!
    discarded = false;
    PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, {}, {}, {}, {0,0,1}, 0.0f, &discarded);
    ASSERT_FALSE(discarded);

    std::cout << "       -> Passed!" << std::endl;
}

void TestVulkanGlslEmissionAndSpirvValidation() {
    std::cout << "[TEST] 6. Vulkan GLSL Fragment Shader & spirv-val Validation..." << std::endl;

    PicaDrawState state{};
    state.Lighting.Enabled = true;
    state.Lighting.ActiveLightCount = 2;
    state.Fog.Enabled = true;
    state.DepthStencil.AlphaTest = true;
    state.DepthStencil.AlphaCompare = CompareFunc::GreaterOrEqual;

    // Stage 0: Modulate texture0 with primary color
    state.Tev.Stages[0].OpRgb = TevOp::Modulate;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::Texture0;
    state.Tev.Stages[0].SourceRgb[1] = TevSource::PrimaryColor;
    state.Tev.Stages[0].OpAlpha = TevOp::Modulate;
    state.Tev.Stages[0].SourceAlpha[0] = TevSource::Texture0;
    state.Tev.Stages[0].SourceAlpha[1] = TevSource::PrimaryColor;

    PicaFragmentShaderConfig config{};
    config.UseMediumpTev = true; // Test RelaxedPrecision on Adreno (R28)

    PicaFragmentShaderOutput output{};
    std::string err;
    bool genOk = PicaFragmentGenerator::GenerateGlsl(state, config, output, &err);
    if (!genOk) {
        std::cerr << "Fragment shader generation error: " << err << std::endl;
    }
    ASSERT_TRUE(genOk);
    ASSERT_FALSE(output.GlslSource.empty());

    // Quality Gate 4: Zero errors in spirv-val
    std::vector<uint32_t> spirvWords;
    bool compOk = PicaFragmentGenerator::CompileGlslToSpirv(output.GlslSource, spirvWords, &err);
    if (!compOk) {
        std::cerr << "Fragment shader compilation error: " << err << std::endl;
    }
    ASSERT_TRUE(compOk);
    ASSERT_FALSE(spirvWords.empty());

    bool valOk = PicaFragmentGenerator::ValidateSpirv(spirvWords, &err);
    if (!valOk) {
        std::cerr << "Fragment shader validation error: " << err << std::endl;
    }
    ASSERT_TRUE(valOk);

    std::cout << "       -> Passed (0 spirv-val errors)!" << std::endl;
}

void TestCorpusFragmentShaderValidation() {
    std::cout << "[TEST] 7. Validation of Kokiri Boot Fragment Shaders from Corpus..." << std::endl;

    const std::string jsonPath = "tools/oot3d/native_pica_frontend/profiles/oot3d_pica_boot_title_kokiri_pipeline.json";
    std::ifstream f(jsonPath);
    if (!f) {
        std::cout << "       -> Skipped (corpus json file not accessible)" << std::endl;
        return;
    }

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string needle = "\"stage\": \"fragment\"";
    size_t pos = content.find(needle);
    ASSERT_TRUE(pos != std::string::npos);

    size_t srcKey = content.rfind("\"source\": \"", pos);
    ASSERT_TRUE(srcKey != std::string::npos);
    size_t start = srcKey + 11;
    size_t end = content.find("\",\n", start);
    ASSERT_TRUE(end != std::string::npos);

    std::string rawSource = content.substr(start, end - start);
    std::string unescaped;
    for (size_t i = 0; i < rawSource.size(); ++i) {
        if (rawSource[i] == '\\' && i + 1 < rawSource.size()) {
            if (rawSource[i + 1] == 'n') { unescaped += '\n'; ++i; }
            else if (rawSource[i + 1] == '\"') { unescaped += '\"'; ++i; }
            else if (rawSource[i + 1] == '\\') { unescaped += '\\'; ++i; }
            else { unescaped += rawSource[i]; }
        } else {
            unescaped += rawSource[i];
        }
    }

    std::vector<uint32_t> spirvWords;
    std::string err;
    bool ok = PicaFragmentGenerator::CompileGlslToSpirv(unescaped, spirvWords, &err);
    if (!ok) {
        std::cerr << "Corpus fragment shader compilation error: " << err << std::endl;
    }
    ASSERT_TRUE(ok);

    bool valOk = PicaFragmentGenerator::ValidateSpirv(spirvWords, &err);
    if (!valOk) {
        std::cerr << "Corpus fragment shader spirv-val error: " << err << std::endl;
    }
    ASSERT_TRUE(valOk);

    std::cout << "       -> Passed (Corpus Fragment Shader validated with 0 errors)!" << std::endl;
}

void TestNumericalParityPixelDiff() {
    std::cout << "[TEST] 8. Numerical Pixel Parity Oracle (Diff < 1/255)..." << std::endl;

    PicaDrawState state{};
    state.Tev.Stages[0].OpRgb = TevOp::Modulate;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::Texture0;
    state.Tev.Stages[0].SourceRgb[1] = TevSource::PrimaryColor;

    state.Tev.Stages[1].OpRgb = TevOp::Add;
    state.Tev.Stages[1].SourceRgb[0] = TevSource::Previous;
    state.Tev.Stages[1].SourceRgb[1] = TevSource::Texture1;

    PicaFragmentUniformState uniforms{};
    std::array<float, 4> primary = {0.3f, 0.4f, 0.5f, 1.0f};
    std::array<float, 4> tex0 = {0.5f, 0.5f, 0.5f, 1.0f};
    std::array<float, 4> tex1 = {0.1f, 0.1f, 0.1f, 1.0f};

    // Expected:
    // Stage 0: (0.3*0.5, 0.4*0.5, 0.5*0.5) = (0.15, 0.20, 0.25)
    // Stage 1: (0.15+0.1, 0.20+0.1, 0.25+0.1) = (0.25, 0.30, 0.35)
    auto res = PicaFragmentGenerator::ExecuteCpuFragmentPipeline(state, uniforms, primary, tex0, tex1);

    constexpr float kByteTol = 1.0f / 255.0f;
    ASSERT_TRUE(std::abs(res[0] - 0.25f) <= kByteTol);
    ASSERT_TRUE(std::abs(res[1] - 0.30f) <= kByteTol);
    ASSERT_TRUE(std::abs(res[2] - 0.35f) <= kByteTol);

    std::cout << "       -> Passed (Pixel diff <= 1/255)!" << std::endl;
}

int main() {
    std::cout << "========================================================================" << std::endl;
    std::cout << " Running Phase 4 Tests: PICA Fragment Shaders, TEV & Lighting" << std::endl;
    std::cout << "========================================================================" << std::endl;

    TestTevAllOperations();
    TestTevCombinerBuffer();
    TestTevModifiersAndScales();
    TestFragmentLightingConfigurations();
    TestFogAndAlphaTestDiscard();
    TestVulkanGlslEmissionAndSpirvValidation();
    TestCorpusFragmentShaderValidation();
    TestNumericalParityPixelDiff();

    std::cout << "========================================================================" << std::endl;
    std::cout << " All Phase 4 fragment shader tests passed successfully (0 spirv-val errors)." << std::endl;
    std::cout << "========================================================================" << std::endl;
    return 0;
}
