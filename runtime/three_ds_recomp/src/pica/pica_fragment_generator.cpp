#include "three_ds_recomp/pica/pica_fragment_generator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>

namespace ThreeDsRecomp::Pica {

namespace {

bool RunProcess(const std::vector<std::string>& args, std::string* output = nullptr) {
    if (args.empty()) return false;

    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmd += " ";
        cmd += "'" + args[i] + "'";
    }
    cmd += " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    int status = pclose(pipe);
    if (output) {
        *output = result;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

std::string SourceToString(TevSource src, int stage) {
    switch (src) {
    case TevSource::PrimaryColor: return "rounded_primary_color";
    case TevSource::PrimaryFragmentColor: return "primary_fragment_color";
    case TevSource::SecondaryFragmentColor: return "secondary_fragment_color";
    case TevSource::Texture0: return "tex0_color";
    case TevSource::Texture1: return "tex1_color";
    case TevSource::Texture2: return "tex2_color";
    case TevSource::Texture3: return "vec4(0.0)";
    case TevSource::PreviousBuffer: return "combiner_buffer";
    case TevSource::Constant: return "fragment_uniforms.tev_constants[" + std::to_string(stage) + "]";
    case TevSource::Previous: return "combiner_output";
    default: return "combiner_output";
    }
}

std::string ApplyRgbModifier(const std::string& val, uint8_t mod) {
    switch (mod) {
    case 0: return val + ".rgb";
    case 1: return "(vec3(1.0) - " + val + ".rgb)";
    case 2: return val + ".aaa";
    case 3: return "(vec3(1.0) - " + val + ".aaa)";
    case 4: return val + ".rrr";
    case 5: return "(vec3(1.0) - " + val + ".rrr)";
    case 6: return val + ".ggg";
    case 7: return "(vec3(1.0) - " + val + ".ggg)";
    case 8: return val + ".bbb";
    case 9: return "(vec3(1.0) - " + val + ".bbb)";
    default: return val + ".rgb";
    }
}

std::string ApplyAlphaModifier(const std::string& val, uint8_t mod) {
    switch (mod) {
    case 0: return val + ".a";
    case 1: return "(1.0 - " + val + ".a)";
    case 2: return val + ".r";
    case 3: return "(1.0 - " + val + ".r)";
    case 4: return val + ".g";
    case 5: return "(1.0 - " + val + ".g)";
    case 6: return val + ".b";
    case 7: return "(1.0 - " + val + ".b)";
    default: return val + ".a";
    }
}

std::string ScaleMultiplier(TevScale scale) {
    switch (scale) {
    case TevScale::Scale1x: return "1.0";
    case TevScale::Scale2x: return "2.0";
    case TevScale::Scale4x: return "4.0";
    default: return "1.0";
    }
}

std::string CompareOpString(CompareFunc func, const std::string& a, const std::string& b) {
    switch (func) {
    case CompareFunc::Never: return "false";
    case CompareFunc::Always: return "true";
    case CompareFunc::Equal: return a + " == " + b;
    case CompareFunc::NotEqual: return a + " != " + b;
    case CompareFunc::Less: return a + " < " + b;
    case CompareFunc::LessOrEqual: return a + " <= " + b;
    case CompareFunc::Greater: return a + " > " + b;
    case CompareFunc::GreaterOrEqual: return a + " >= " + b;
    default: return "true";
    }
}

} // namespace

bool PicaFragmentGenerator::GenerateGlsl(
    const PicaDrawState& drawState,
    const PicaFragmentShaderConfig& config,
    PicaFragmentShaderOutput& outShader,
    std::string* errorLog) {

    std::ostringstream ss;
    ss << "#version 450\n\n";

    if (config.UseMediumpTev) {
        ss << "#define TEV_PRECISION mediump\n\n";
    } else {
        ss << "#define TEV_PRECISION highp\n\n";
    }

    // Vertex attributes inputs from vertex shader
    ss << "layout(location = 0) in vec4 pica_primary_color;\n"
          "layout(location = 1) in vec2 pica_texcoord0;\n"
          "layout(location = 2) in vec2 pica_texcoord1;\n"
          "layout(location = 3) in vec2 pica_texcoord2;\n"
          "layout(location = 4) in float pica_texcoord0_w;\n"
          "layout(location = 5) in vec4 pica_normquat;\n"
          "layout(location = 6) in vec3 pica_view;\n\n";

    // Textures
    ss << "layout(set = 0, binding = 1) uniform sampler2D pica_texture0;\n"
          "layout(set = 0, binding = 2) uniform sampler2D pica_texture1;\n"
          "layout(set = 0, binding = 3) uniform sampler2D pica_texture2;\n\n";

    // PICA Fragment Uniforms UBO
    ss << "layout(set = 0, binding = 4, std140) uniform PicaFragmentUniforms {\n"
          "    vec4 tev_constants[6];\n"
          "    vec4 combiner_buffer_color;\n"
          "    float alpha_reference;\n"
          "    float reserved0;\n"
          "    float reserved1;\n"
          "    float reserved2;\n"
          "    vec4 fog_color;\n"
          "    vec4 fog_lut[64];\n"
          "    vec4 texture_lod_bias;\n"
          "    vec4 lighting_specular0[8];\n"
          "    vec4 lighting_diffuse[8];\n"
          "    vec4 lighting_ambient[8];\n"
          "    vec4 lighting_position[8];\n"
          "    vec4 lighting_global_ambient;\n"
          "} fragment_uniforms;\n\n";

    // Output fragment color
    ss << "layout(location = 0) out vec4 pica_color;\n\n";

    // Helper functions
    ss << "vec3 byteround3(vec3 val) { return floor(val * 255.0 + 0.5) / 255.0; }\n"
          "float byteround1(float val) { return floor(val * 255.0 + 0.5) / 255.0; }\n\n";

    // Main entrypoint
    ss << "void main() {\n"
          "    vec4 rounded_primary_color = vec4(byteround3(pica_primary_color.rgb), byteround1(pica_primary_color.a));\n"
          "    vec4 primary_fragment_color = vec4(0.0);\n"
          "    vec4 secondary_fragment_color = vec4(0.0);\n"
          "    vec4 combiner_buffer = vec4(0.0);\n"
          "    vec4 next_combiner_buffer = fragment_uniforms.combiner_buffer_color;\n"
          "    vec4 combiner_output = rounded_primary_color;\n\n";

    // Sample Textures
    if (config.EnableTexture0) {
        ss << "    vec4 tex0_color = texture(pica_texture0, pica_texcoord0);\n";
    } else {
        ss << "    vec4 tex0_color = vec4(1.0);\n";
    }
    if (config.EnableTexture1) {
        ss << "    vec4 tex1_color = texture(pica_texture1, pica_texcoord1);\n";
    } else {
        ss << "    vec4 tex1_color = vec4(1.0);\n";
    }
    if (config.EnableTexture2) {
        ss << "    vec4 tex2_color = texture(pica_texture2, pica_texcoord2);\n";
    } else {
        ss << "    vec4 tex2_color = vec4(1.0);\n";
    }
    ss << "\n";

    // Fragment Lighting Evaluation
    if (config.EnableFragmentLighting && drawState.Lighting.Enabled) {
        ss << "    // Fragment Lighting (LUTs and Directional Lights)\n"
              "    vec3 N = normalize(pica_normquat.xyz);\n"
              "    vec3 light_diffuse_accum = fragment_uniforms.lighting_global_ambient.rgb;\n"
              "    vec3 light_specular_accum = vec3(0.0);\n"
              "    for (int l = 0; l < " << static_cast<int>(drawState.Lighting.ActiveLightCount) << "; ++l) {\n"
              "        vec3 L = normalize(fragment_uniforms.lighting_position[l].xyz);\n"
              "        float NdotL = max(dot(N, L), 0.0);\n"
              "        light_diffuse_accum += fragment_uniforms.lighting_diffuse[l].rgb * NdotL;\n"
              "        vec3 H = normalize(L + normalize(pica_view));\n"
              "        float NdotH = max(dot(N, H), 0.0);\n"
              "        light_specular_accum += fragment_uniforms.lighting_specular0[l].rgb * pow(NdotH, 16.0);\n"
              "    }\n"
              "    primary_fragment_color = vec4(clamp(light_diffuse_accum, 0.0, 1.0), 1.0);\n"
              "    secondary_fragment_color = vec4(clamp(light_specular_accum, 0.0, 1.0), 1.0);\n\n";
    }

    // TEV 6-stage Combiners
    for (size_t stage = 0; stage < 6; ++stage) {
        const auto& s = drawState.Tev.Stages[stage];
        ss << "    // TEV Stage " << stage << "\n";

        // Combiner buffer update (2-stage delay)
        if (config.EnableCombinerBuffer) {
            ss << "    combiner_buffer = next_combiner_buffer;\n";
        }

        // Sources and modifiers for RGB
        std::string rgbSrc0 = ApplyRgbModifier(SourceToString(s.SourceRgb[0], stage), s.OperandRgb[0]);
        std::string rgbSrc1 = ApplyRgbModifier(SourceToString(s.SourceRgb[1], stage), s.OperandRgb[1]);
        std::string rgbSrc2 = ApplyRgbModifier(SourceToString(s.SourceRgb[2], stage), s.OperandRgb[2]);

        // Sources and modifiers for Alpha
        std::string aSrc0 = ApplyAlphaModifier(SourceToString(s.SourceAlpha[0], stage), s.OperandAlpha[0]);
        std::string aSrc1 = ApplyAlphaModifier(SourceToString(s.SourceAlpha[1], stage), s.OperandAlpha[1]);
        std::string aSrc2 = ApplyAlphaModifier(SourceToString(s.SourceAlpha[2], stage), s.OperandAlpha[2]);

        // RGB Op
        std::string rgbExpr;
        switch (s.OpRgb) {
        case TevOp::Replace: rgbExpr = rgbSrc0; break;
        case TevOp::Modulate: rgbExpr = rgbSrc0 + " * " + rgbSrc1; break;
        case TevOp::Add: rgbExpr = rgbSrc0 + " + " + rgbSrc1; break;
        case TevOp::AddSigned: rgbExpr = rgbSrc0 + " + " + rgbSrc1 + " - vec3(0.5)"; break;
        case TevOp::Interpolate: rgbExpr = "mix(" + rgbSrc1 + ", " + rgbSrc0 + ", " + rgbSrc2 + ")"; break;
        case TevOp::Subtract: rgbExpr = rgbSrc0 + " - " + rgbSrc1; break;
        case TevOp::Dot3Rgb: rgbExpr = "vec3(clamp(dot(" + rgbSrc0 + " - vec3(0.5), " + rgbSrc1 + " - vec3(0.5)) * 4.0, 0.0, 1.0))"; break;
        case TevOp::Dot3Rgba: rgbExpr = "vec3(clamp(dot(vec4(" + rgbSrc0 + ", 1.0) - vec4(0.5), vec4(" + rgbSrc1 + ", 1.0) - vec4(0.5)) * 4.0, 0.0, 1.0))"; break;
        case TevOp::MultiplyAdd: rgbExpr = rgbSrc0 + " * " + rgbSrc1 + " + " + rgbSrc2; break;
        case TevOp::AddMultiply: rgbExpr = "(" + rgbSrc0 + " + " + rgbSrc1 + ") * " + rgbSrc2; break;
        }

        // Alpha Op
        std::string aExpr;
        switch (s.OpAlpha) {
        case TevOp::Replace: aExpr = aSrc0; break;
        case TevOp::Modulate: aExpr = aSrc0 + " * " + aSrc1; break;
        case TevOp::Add: aExpr = aSrc0 + " + " + aSrc1; break;
        case TevOp::AddSigned: aExpr = aSrc0 + " + " + aSrc1 + " - 0.5"; break;
        case TevOp::Interpolate: aExpr = "mix(" + aSrc1 + ", " + aSrc0 + ", " + aSrc2 + ")"; break;
        case TevOp::Subtract: aExpr = aSrc0 + " - " + aSrc1; break;
        case TevOp::Dot3Rgb:
        case TevOp::Dot3Rgba: aExpr = "clamp(dot(vec4(" + rgbSrc0 + ", " + aSrc0 + ") - vec4(0.5), vec4(" + rgbSrc1 + ", " + aSrc1 + ") - vec4(0.5)) * 4.0, 0.0, 1.0)"; break;
        case TevOp::MultiplyAdd: aExpr = aSrc0 + " * " + aSrc1 + " + " + aSrc2; break;
        case TevOp::AddMultiply: aExpr = "(" + aSrc0 + " + " + aSrc1 + ") * " + aSrc2; break;
        }

        ss << "    combiner_output.rgb = clamp((" << rgbExpr << ") * " << ScaleMultiplier(s.ScaleRgb) << ", vec3(0.0), vec3(1.0));\n"
              "    combiner_output.a = clamp((" << aExpr << ") * " << ScaleMultiplier(s.ScaleAlpha) << ", 0.0, 1.0);\n";

        if (config.EnableCombinerBuffer && (drawState.Tev.CombinerBufferInputMask & (1 << stage))) {
            ss << "    next_combiner_buffer = combiner_output;\n";
        }
        ss << "\n";
    }

    // Fog blending
    if (config.EnableFog && drawState.Fog.Enabled) {
        ss << "    // Fog Blending\n"
              "    float fog_factor = clamp(gl_FragCoord.z, 0.0, 1.0);\n"
              "    combiner_output.rgb = mix(combiner_output.rgb, fragment_uniforms.fog_color.rgb, fog_factor);\n\n";
    }

    // Alpha test
    if (config.EnableAlphaTest && drawState.DepthStencil.AlphaTest) {
        ss << "    // Alpha Test\n"
              "    if (!(" << CompareOpString(drawState.DepthStencil.AlphaCompare, "combiner_output.a", "fragment_uniforms.alpha_reference") << ")) {\n"
              "        discard;\n"
              "    }\n\n";
    }

    ss << "    pica_color = combiner_output;\n"
          "}\n";

    outShader.GlslSource = ss.str();
    outShader.StateKey = drawState.StateHash;
    return true;
}

bool PicaFragmentGenerator::CompileGlslToSpirv(
    const std::string& glslSource,
    std::vector<uint32_t>& outSpirv,
    std::string* errorLog) {

    char tmpFragPath[] = "/tmp/pica_frag_XXXXXX.frag";
    char tmpSpvPath[] = "/tmp/pica_frag_spv_XXXXXX.spv";

    int fdFrag = mkstemps(tmpFragPath, 5);
    int fdSpv = mkstemps(tmpSpvPath, 4);

    if (fdFrag < 0 || fdSpv < 0) {
        if (fdFrag >= 0) { close(fdFrag); unlink(tmpFragPath); }
        if (fdSpv >= 0) { close(fdSpv); unlink(tmpSpvPath); }
        if (errorLog) *errorLog = "Failed to create temporary files for fragment shader compilation";
        return false;
    }

    write(fdFrag, glslSource.data(), glslSource.size());
    close(fdFrag);
    close(fdSpv);

    std::string procOutput;
    bool success = RunProcess({"glslangValidator", "-V", tmpFragPath, "-o", tmpSpvPath}, &procOutput);

    if (!success) {
        if (errorLog) *errorLog = "glslangValidator failed:\n" + procOutput;
        unlink(tmpFragPath);
        unlink(tmpSpvPath);
        return false;
    }

    std::ifstream spvFile(tmpSpvPath, std::ios::binary | std::ios::ate);
    if (!spvFile) {
        if (errorLog) *errorLog = "Could not open generated fragment SPIR-V binary";
        unlink(tmpFragPath);
        unlink(tmpSpvPath);
        return false;
    }

    const size_t fileSize = static_cast<size_t>(spvFile.tellg());
    spvFile.seekg(0);

    outSpirv.resize(fileSize / sizeof(uint32_t));
    spvFile.read(reinterpret_cast<char*>(outSpirv.data()), fileSize);
    spvFile.close();

    unlink(tmpFragPath);
    unlink(tmpSpvPath);
    return true;
}

bool PicaFragmentGenerator::ValidateSpirv(
    std::span<const uint32_t> spirvWords,
    std::string* errorLog) {

    if (spirvWords.empty()) {
        if (errorLog) *errorLog = "Empty SPIR-V word buffer";
        return false;
    }

    char tmpSpvPath[] = "/tmp/pica_frag_val_XXXXXX.spv";
    int fd = mkstemps(tmpSpvPath, 4);
    if (fd < 0) {
        if (errorLog) *errorLog = "Failed to create temporary file for spirv-val";
        return false;
    }

    write(fd, spirvWords.data(), spirvWords.size() * sizeof(uint32_t));
    close(fd);

    std::string procOutput;
    bool ok = RunProcess({"spirv-val", tmpSpvPath}, &procOutput);
    unlink(tmpSpvPath);

    if (!ok) {
        if (errorLog) *errorLog = "spirv-val validation failed:\n" + procOutput;
        return false;
    }

    return true;
}

std::array<float, 4> PicaFragmentGenerator::ExecuteCpuFragmentPipeline(
    const PicaDrawState& drawState,
    const PicaFragmentUniformState& uniforms,
    const std::array<float, 4>& primaryColor,
    const std::array<float, 4>& tex0Color,
    const std::array<float, 4>& tex1Color,
    const std::array<float, 4>& tex2Color,
    const std::array<float, 3>& normal,
    float fragmentDepth,
    bool* outDiscarded) {

    if (outDiscarded) *outDiscarded = false;

    auto byteround = [](float val) -> float {
        return std::floor(val * 255.0f + 0.5f) / 255.0f;
    };
    std::array<float, 4> roundedPrimary = {
        byteround(primaryColor[0]),
        byteround(primaryColor[1]),
        byteround(primaryColor[2]),
        byteround(primaryColor[3])
    };

    std::array<float, 4> primaryFragmentColor = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> secondaryFragmentColor = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> combinerBuffer = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> nextCombinerBuffer = uniforms.CombinerBufferColor;
    std::array<float, 4> combinerOutput = roundedPrimary;

    // Fragment Lighting (LUTs and Directional Lights)
    if (drawState.Lighting.Enabled) {
        std::array<float, 3> N = normal;
        float lenN = std::sqrt(N[0]*N[0] + N[1]*N[1] + N[2]*N[2]);
        if (lenN > 0.0f) { N[0] /= lenN; N[1] /= lenN; N[2] /= lenN; }

        std::array<float, 3> lightDiffuse = {uniforms.GlobalAmbient[0], uniforms.GlobalAmbient[1], uniforms.GlobalAmbient[2]};
        std::array<float, 3> lightSpecular = {0.0f, 0.0f, 0.0f};

        for (uint8_t l = 0; l < drawState.Lighting.ActiveLightCount && l < 8; ++l) {
            std::array<float, 3> L = {uniforms.LightPosition[l][0], uniforms.LightPosition[l][1], uniforms.LightPosition[l][2]};
            float lenL = std::sqrt(L[0]*L[0] + L[1]*L[1] + L[2]*L[2]);
            if (lenL > 0.0f) { L[0] /= lenL; L[1] /= lenL; L[2] /= lenL; }

            float NdotL = std::max(0.0f, N[0]*L[0] + N[1]*L[1] + N[2]*L[2]);
            for (int c = 0; c < 3; ++c) {
                lightDiffuse[c] += uniforms.LightDiffuse[l][c] * NdotL;
            }
        }
        for (int c = 0; c < 3; ++c) {
            primaryFragmentColor[c] = std::clamp(lightDiffuse[c], 0.0f, 1.0f);
        }
        primaryFragmentColor[3] = 1.0f;
    }

    auto getSource = [&](TevSource src, int stage) -> std::array<float, 4> {
        switch (src) {
        case TevSource::PrimaryColor: return roundedPrimary;
        case TevSource::PrimaryFragmentColor: return primaryFragmentColor;
        case TevSource::SecondaryFragmentColor: return secondaryFragmentColor;
        case TevSource::Texture0: return tex0Color;
        case TevSource::Texture1: return tex1Color;
        case TevSource::Texture2: return tex2Color;
        case TevSource::Texture3: return {0.0f, 0.0f, 0.0f, 0.0f};
        case TevSource::PreviousBuffer: return combinerBuffer;
        case TevSource::Constant: return uniforms.TevConstants[stage];
        case TevSource::Previous: return combinerOutput;
        default: return combinerOutput;
        }
    };

    auto applyRgbMod = [](const std::array<float, 4>& val, uint8_t mod) -> std::array<float, 3> {
        switch (mod) {
        case 0: return {val[0], val[1], val[2]};
        case 1: return {1.0f - val[0], 1.0f - val[1], 1.0f - val[2]};
        case 2: return {val[3], val[3], val[3]};
        case 3: return {1.0f - val[3], 1.0f - val[3], 1.0f - val[3]};
        case 4: return {val[0], val[0], val[0]};
        case 5: return {1.0f - val[0], 1.0f - val[0], 1.0f - val[0]};
        case 6: return {val[1], val[1], val[1]};
        case 7: return {1.0f - val[1], 1.0f - val[1], 1.0f - val[1]};
        case 8: return {val[2], val[2], val[2]};
        case 9: return {1.0f - val[2], 1.0f - val[2], 1.0f - val[2]};
        default: return {val[0], val[1], val[2]};
        }
    };

    auto applyAlphaMod = [](const std::array<float, 4>& val, uint8_t mod) -> float {
        switch (mod) {
        case 0: return val[3];
        case 1: return 1.0f - val[3];
        case 2: return val[0];
        case 3: return 1.0f - val[0];
        case 4: return val[1];
        case 5: return 1.0f - val[1];
        case 6: return val[2];
        case 7: return 1.0f - val[2];
        default: return val[3];
        }
    };

    auto getScale = [](TevScale scale) -> float {
        switch (scale) {
        case TevScale::Scale1x: return 1.0f;
        case TevScale::Scale2x: return 2.0f;
        case TevScale::Scale4x: return 4.0f;
        default: return 1.0f;
        }
    };

    for (size_t stage = 0; stage < 6; ++stage) {
        const auto& s = drawState.Tev.Stages[stage];
        combinerBuffer = nextCombinerBuffer;

        std::array<float, 3> s0Rgb = applyRgbMod(getSource(s.SourceRgb[0], stage), s.OperandRgb[0]);
        std::array<float, 3> s1Rgb = applyRgbMod(getSource(s.SourceRgb[1], stage), s.OperandRgb[1]);
        std::array<float, 3> s2Rgb = applyRgbMod(getSource(s.SourceRgb[2], stage), s.OperandRgb[2]);

        float s0A = applyAlphaMod(getSource(s.SourceAlpha[0], stage), s.OperandAlpha[0]);
        float s1A = applyAlphaMod(getSource(s.SourceAlpha[1], stage), s.OperandAlpha[1]);
        float s2A = applyAlphaMod(getSource(s.SourceAlpha[2], stage), s.OperandAlpha[2]);

        std::array<float, 3> resRgb{};
        switch (s.OpRgb) {
        case TevOp::Replace:
            resRgb = s0Rgb;
            break;
        case TevOp::Modulate:
            for (int i = 0; i < 3; ++i) resRgb[i] = s0Rgb[i] * s1Rgb[i];
            break;
        case TevOp::Add:
            for (int i = 0; i < 3; ++i) resRgb[i] = s0Rgb[i] + s1Rgb[i];
            break;
        case TevOp::AddSigned:
            for (int i = 0; i < 3; ++i) resRgb[i] = s0Rgb[i] + s1Rgb[i] - 0.5f;
            break;
        case TevOp::Interpolate:
            for (int i = 0; i < 3; ++i) resRgb[i] = s1Rgb[i] * (1.0f - s2Rgb[i]) + s0Rgb[i] * s2Rgb[i];
            break;
        case TevOp::Subtract:
            for (int i = 0; i < 3; ++i) resRgb[i] = s0Rgb[i] - s1Rgb[i];
            break;
        case TevOp::Dot3Rgb: {
            float d = (s0Rgb[0] - 0.5f)*(s1Rgb[0] - 0.5f) + (s0Rgb[1] - 0.5f)*(s1Rgb[1] - 0.5f) + (s0Rgb[2] - 0.5f)*(s1Rgb[2] - 0.5f);
            float clamped = std::clamp(d * 4.0f, 0.0f, 1.0f);
            resRgb = {clamped, clamped, clamped};
            break;
        }
        case TevOp::Dot3Rgba: {
            float d = (s0Rgb[0] - 0.5f)*(s1Rgb[0] - 0.5f) + (s0Rgb[1] - 0.5f)*(s1Rgb[1] - 0.5f) + (s0Rgb[2] - 0.5f)*(s1Rgb[2] - 0.5f);
            float clamped = std::clamp(d * 4.0f, 0.0f, 1.0f);
            resRgb = {clamped, clamped, clamped};
            break;
        }
        case TevOp::MultiplyAdd:
            for (int i = 0; i < 3; ++i) resRgb[i] = s0Rgb[i] * s1Rgb[i] + s2Rgb[i];
            break;
        case TevOp::AddMultiply:
            for (int i = 0; i < 3; ++i) resRgb[i] = (s0Rgb[i] + s1Rgb[i]) * s2Rgb[i];
            break;
        }

        float resA = 1.0f;
        switch (s.OpAlpha) {
        case TevOp::Replace: resA = s0A; break;
        case TevOp::Modulate: resA = s0A * s1A; break;
        case TevOp::Add: resA = s0A + s1A; break;
        case TevOp::AddSigned: resA = s0A + s1A - 0.5f; break;
        case TevOp::Interpolate: resA = s1A * (1.0f - s2A) + s0A * s2A; break;
        case TevOp::Subtract: resA = s0A - s1A; break;
        case TevOp::Dot3Rgb:
        case TevOp::Dot3Rgba: {
            float d = (s0Rgb[0] - 0.5f)*(s1Rgb[0] - 0.5f) + (s0Rgb[1] - 0.5f)*(s1Rgb[1] - 0.5f) + (s0Rgb[2] - 0.5f)*(s1Rgb[2] - 0.5f) + (s0A - 0.5f)*(s1A - 0.5f);
            resA = std::clamp(d * 4.0f, 0.0f, 1.0f);
            break;
        }
        case TevOp::MultiplyAdd: resA = s0A * s1A + s2A; break;
        case TevOp::AddMultiply: resA = (s0A + s1A) * s2A; break;
        }

        float scaleR = getScale(s.ScaleRgb);
        float scaleA = getScale(s.ScaleAlpha);
        for (int i = 0; i < 3; ++i) {
            combinerOutput[i] = std::clamp(resRgb[i] * scaleR, 0.0f, 1.0f);
        }
        combinerOutput[3] = std::clamp(resA * scaleA, 0.0f, 1.0f);

        if (drawState.Tev.CombinerBufferInputMask & (1 << stage)) {
            nextCombinerBuffer = combinerOutput;
        }
    }

    // Fog
    if (drawState.Fog.Enabled) {
        float f = std::clamp(fragmentDepth, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) {
            combinerOutput[i] = combinerOutput[i] * (1.0f - f) + uniforms.FogColor[i] * f;
        }
    }

    // Alpha test
    if (drawState.DepthStencil.AlphaTest) {
        bool pass = true;
        float ref = uniforms.AlphaReference;
        float a = combinerOutput[3];
        switch (drawState.DepthStencil.AlphaCompare) {
        case CompareFunc::Never: pass = false; break;
        case CompareFunc::Always: pass = true; break;
        case CompareFunc::Equal: pass = (std::abs(a - ref) < 1e-4f); break;
        case CompareFunc::NotEqual: pass = (std::abs(a - ref) >= 1e-4f); break;
        case CompareFunc::Less: pass = (a < ref); break;
        case CompareFunc::LessOrEqual: pass = (a <= ref); break;
        case CompareFunc::Greater: pass = (a > ref); break;
        case CompareFunc::GreaterOrEqual: pass = (a >= ref); break;
        }
        if (!pass) {
            if (outDiscarded) *outDiscarded = true;
        }
    }

    return combinerOutput;
}

} // namespace ThreeDsRecomp::Pica
