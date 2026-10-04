#include "three_ds_recomp/pica/pica_shader_translator.h"

#include "nihstro/shader_bytecode.h"
#include "video_core/shader/generator/glsl_shader_decompiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/wait.h>

namespace ThreeDsRecomp::Pica {

namespace {

uint64_t ComputeShaderHash(std::span<const uint32_t> program, std::span<const uint32_t> swizzles, uint32_t mainOffset) {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffset;

    for (uint32_t word : program) {
        for (int i = 0; i < 4; ++i) {
            hash = (hash ^ static_cast<uint8_t>(word >> (i * 8))) * kPrime;
        }
    }
    for (uint32_t word : swizzles) {
        for (int i = 0; i < 4; ++i) {
            hash = (hash ^ static_cast<uint8_t>(word >> (i * 8))) * kPrime;
        }
    }
    for (int i = 0; i < 4; ++i) {
        hash = (hash ^ static_cast<uint8_t>(mainOffset >> (i * 8))) * kPrime;
    }
    return hash == 0 ? 1 : hash;
}

std::string InputRegName(uint32_t index) {
    return "pica_input" + std::to_string(index);
}

std::string OutputRegName(uint32_t index) {
    return "pica_output" + std::to_string(index);
}

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

} // namespace

DecodedInstruction PicaShaderTranslator::DecodeInstruction(uint32_t rawWord, uint32_t swizzleWord) {
    DecodedInstruction decoded{};
    decoded.RawWord = rawWord;

    nihstro::Instruction instr{rawWord};
    const auto effectiveOp = instr.opcode.Value().EffectiveOpCode();
    decoded.Op = static_cast<OpCode>(effectiveOp);

    const auto opInfo = instr.opcode.Value().GetInfo();
    switch (opInfo.type) {
    case nihstro::OpCode::Type::Trivial:
        decoded.Type = InstructionType::Trivial;
        break;
    case nihstro::OpCode::Type::Arithmetic:
        decoded.Type = InstructionType::Arithmetic;
        break;
    case nihstro::OpCode::Type::Conditional:
        decoded.Type = InstructionType::Conditional;
        break;
    case nihstro::OpCode::Type::UniformFlowControl:
        decoded.Type = InstructionType::UniformFlowControl;
        break;
    case nihstro::OpCode::Type::SetEmit:
        decoded.Type = InstructionType::SetEmit;
        break;
    case nihstro::OpCode::Type::MultiplyAdd:
        decoded.Type = InstructionType::MultiplyAdd;
        break;
    default:
        decoded.Type = InstructionType::Unknown;
        break;
    }

    if (decoded.Type == InstructionType::Arithmetic) {
        const bool isInverted = (0 != (opInfo.subtype & nihstro::OpCode::Info::SrcInversed));
        const auto destReg = instr.common.dest.Value();
        decoded.DestType = (destReg.GetRegisterType() == nihstro::RegisterType::Output) 
            ? RegisterType::Output : RegisterType::Temporary;
        decoded.DestIndex = static_cast<uint8_t>(destReg.GetIndex());
        decoded.SwizzleIndex = static_cast<uint8_t>(instr.common.operand_desc_id.Value());

        auto decodeSrcReg = [](nihstro::SourceRegister reg) -> OperandSource {
            OperandSource op;
            if (reg.GetRegisterType() == nihstro::RegisterType::Input) {
                op.Type = RegisterType::Input;
            } else if (reg.GetRegisterType() == nihstro::RegisterType::Temporary) {
                op.Type = RegisterType::Temporary;
            } else {
                op.Type = RegisterType::FloatUniform;
            }
            op.Index = static_cast<uint8_t>(reg.GetIndex());
            return op;
        };

        decoded.Src1 = decodeSrcReg(instr.common.GetSrc1(isInverted));
        decoded.Src2 = decodeSrcReg(instr.common.GetSrc2(isInverted));
    } else if (decoded.Type == InstructionType::UniformFlowControl || decoded.Type == InstructionType::Conditional) {
        decoded.DestOffset = instr.flow_control.dest_offset.Value();
        decoded.NumInstructions = instr.flow_control.num_instructions.Value();
        decoded.BoolUniformIndex = static_cast<uint8_t>(instr.flow_control.bool_uniform_id.Value());
        decoded.IntUniformIndex = static_cast<uint8_t>(instr.flow_control.int_uniform_id.Value());
    } else if (decoded.Type == InstructionType::MultiplyAdd) {
        const auto destReg = instr.mad.dest.Value();
        decoded.DestType = (destReg.GetRegisterType() == nihstro::RegisterType::Output)
            ? RegisterType::Output : RegisterType::Temporary;
        decoded.DestIndex = static_cast<uint8_t>(destReg.GetIndex());

        auto decodeMadSrc = [](nihstro::SourceRegister src) -> OperandSource {
            OperandSource op;
            if (src.GetRegisterType() == nihstro::RegisterType::Input) {
                op.Type = RegisterType::Input;
            } else if (src.GetRegisterType() == nihstro::RegisterType::Temporary) {
                op.Type = RegisterType::Temporary;
            } else {
                op.Type = RegisterType::FloatUniform;
            }
            op.Index = static_cast<uint8_t>(src.GetIndex());
            return op;
        };

        decoded.Src1 = decodeMadSrc(instr.mad.src1.Value());
        decoded.Src2 = decodeMadSrc(instr.mad.src2.Value());
        decoded.Src3 = decodeMadSrc(instr.mad.src3.Value());
    }

    return decoded;
}

DecodedSwizzle PicaShaderTranslator::DecodeSwizzle(uint32_t swizzleWord) {
    DecodedSwizzle res{};
    nihstro::SwizzlePattern pattern{swizzleWord};

    res.DestMask = static_cast<uint8_t>(pattern.dest_mask.Value());

    for (int i = 0; i < 4; ++i) {
        res.Src1.Selectors[i] = static_cast<uint8_t>(pattern.GetSelectorSrc1(i));
        res.Src2.Selectors[i] = static_cast<uint8_t>(pattern.GetSelectorSrc2(i));
        res.Src3.Selectors[i] = static_cast<uint8_t>(pattern.GetSelectorSrc3(i));
    }
    res.Src1.Negate = pattern.negate_src1.Value();
    res.Src2.Negate = pattern.negate_src2.Value();
    res.Src3.Negate = pattern.negate_src3.Value();

    return res;
}

bool PicaShaderTranslator::TranslateGlsl(
    std::span<const uint32_t> programCode,
    std::span<const uint32_t> swizzleData,
    uint32_t mainOffset,
    const PicaShaderConfig& config,
    PicaShaderOutput& outShader,
    std::string* errorLog) {

    if (programCode.empty() || mainOffset >= programCode.size()) {
        if (errorLog) *errorLog = "Invalid program code size or entrypoint offset";
        return false;
    }

    ::Pica::ProgramCode picaProgram{};
    for (size_t i = 0; i < programCode.size() && i < picaProgram.size(); ++i) {
        picaProgram[i] = programCode[i];
    }

    ::Pica::SwizzleData picaSwizzles{};
    for (size_t i = 0; i < swizzleData.size() && i < picaSwizzles.size(); ++i) {
        picaSwizzles[i] = swizzleData[i];
    }

    std::string decompiledBody;
    try {
        decompiledBody = ::Pica::Shader::Generator::GLSL::DecompileProgram(
            picaProgram, picaSwizzles, mainOffset,
            InputRegName, OutputRegName, config.SanitizeMul);
    } catch (const std::exception& e) {
        if (errorLog) *errorLog = std::string("Decompiler exception: ") + e.what();
        return false;
    }

    if (decompiledBody.empty()) {
        if (errorLog) *errorLog = "Decompiler produced empty shader body";
        return false;
    }

    std::ostringstream ss;
    ss << "#version 450\n\n";

    // 16 Vertex Input Attributes (v0 - v15)
    for (int i = 0; i < 16; ++i) {
        ss << "layout(location = " << i << ") in vec4 " << InputRegName(i) << ";\n";
    }
    ss << "\n";

    // Vertex Output Interface
    ss << "layout(location = 0) out vec4 pica_primary_color;\n"
          "layout(location = 1) out vec2 pica_texcoord0;\n"
          "layout(location = 2) out vec2 pica_texcoord1;\n"
          "layout(location = 3) out vec2 pica_texcoord2;\n"
          "layout(location = 4) out float pica_texcoord0_w;\n"
          "layout(location = 5) out vec4 pica_normquat;\n"
          "layout(location = 6) out vec3 pica_view;\n\n";

    // PICA Uniforms UBO (std140 layout matching PicaVertexUniformState)
    ss << "layout(set = 0, binding = 0, std140) uniform PicaVertexUniforms {\n"
          "    uint b;\n"
          "    int flip_viewport;\n"
          "    vec2 depth_scale_offset;\n"
          "    uvec4 i[4];\n"
          "    vec4 f[96];\n"
          "} uniforms;\n\n";

    // Output Registers (o0 - o15)
    for (int i = 0; i < 16; ++i) {
        ss << "vec4 " << OutputRegName(i) << " = vec4(0.0);\n";
    }
    ss << "\n";

    // Decompiled program functions and subroutines
    ss << decompiledBody << "\n\n";

    // Shader Entrypoint: Vertex transformation and depth mapping (R17, ADR-0003)
    ss << "void main() {\n"
          "    exec_shader();\n"
          "    vec4 pica_position = pica_output0;\n\n";

    if (config.EmulateDepthInVertexShader) {
        ss << "    // PICA NDC depth [-1, 0] mapping to Vulkan [0, 1] in vertex stage (R17, ADR-0003)\n"
              "    float pica_ndc_z = pica_position.z / pica_position.w;\n"
              "    if (pica_ndc_z > 0.0 && pica_ndc_z < 0.000001) pica_position.z = 0.0;\n"
              "    if (pica_ndc_z < -1.0 && pica_ndc_z > -1.00001) pica_position.z = -pica_position.w;\n"
              "    pica_position.z = pica_position.z * uniforms.depth_scale_offset.x + uniforms.depth_scale_offset.y * pica_position.w;\n";
    }

    if (config.FlipViewport) {
        ss << "    if (uniforms.flip_viewport != 0) pica_position.y = -pica_position.y;\n";
    }

    ss << "    gl_Position = vec4(pica_position.x, pica_position.y, -pica_position.z, pica_position.w);\n"
          "    pica_normquat = pica_output5;\n"
          "    pica_primary_color = min(abs(pica_output1), vec4(1.0));\n"
          "    pica_texcoord0 = pica_output2.xy;\n"
          "    pica_texcoord0_w = pica_output2.z;\n"
          "    pica_texcoord1 = pica_output3.xy;\n"
          "    pica_view = pica_output6.xyz;\n"
          "    pica_texcoord2 = pica_output4.xy;\n"
          "}\n";

    outShader.GlslSource = ss.str();
    outShader.StateKey = ComputeShaderHash(programCode, swizzleData, mainOffset);
    return true;
}

bool PicaShaderTranslator::CompileGlslToSpirv(
    const std::string& glslSource,
    std::vector<uint32_t>& outSpirv,
    std::string* errorLog) {

    char tmpVertPath[] = "/tmp/pica_vert_XXXXXX.vert";
    char tmpSpvPath[] = "/tmp/pica_spv_XXXXXX.spv";

    int fdVert = mkstemps(tmpVertPath, 5);
    int fdSpv = mkstemps(tmpSpvPath, 4);

    if (fdVert < 0 || fdSpv < 0) {
        if (fdVert >= 0) { close(fdVert); unlink(tmpVertPath); }
        if (fdSpv >= 0) { close(fdSpv); unlink(tmpSpvPath); }
        if (errorLog) *errorLog = "Failed to create temporary files for shader compilation";
        return false;
    }

    write(fdVert, glslSource.data(), glslSource.size());
    close(fdVert);
    close(fdSpv);

    std::string procOutput;
    bool success = RunProcess({"glslangValidator", "-V", tmpVertPath, "-o", tmpSpvPath}, &procOutput);

    if (!success) {
        if (errorLog) *errorLog = "glslangValidator failed:\n" + procOutput;
        unlink(tmpVertPath);
        unlink(tmpSpvPath);
        return false;
    }

    std::ifstream spvFile(tmpSpvPath, std::ios::binary | std::ios::ate);
    if (!spvFile) {
        if (errorLog) *errorLog = "Could not open generated SPIR-V binary";
        unlink(tmpVertPath);
        unlink(tmpSpvPath);
        return false;
    }

    const size_t fileSize = static_cast<size_t>(spvFile.tellg());
    spvFile.seekg(0);

    outSpirv.resize(fileSize / sizeof(uint32_t));
    spvFile.read(reinterpret_cast<char*>(outSpirv.data()), fileSize);
    spvFile.close();

    unlink(tmpVertPath);
    unlink(tmpSpvPath);
    return true;
}

bool PicaShaderTranslator::ValidateSpirv(
    std::span<const uint32_t> spirvWords,
    std::string* errorLog) {

    if (spirvWords.empty()) {
        if (errorLog) *errorLog = "Empty SPIR-V word buffer";
        return false;
    }

    char tmpSpvPath[] = "/tmp/pica_val_XXXXXX.spv";
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

std::array<float, 4> PicaShaderTranslator::ExecuteCpuVertexProgram(
    std::span<const uint32_t> programCode,
    std::span<const uint32_t> swizzleData,
    uint32_t mainOffset,
    const PicaVertexUniformState& uniforms,
    const std::array<float, 4>& inPosition,
    const std::array<float, 4>& inNormal,
    const PicaShaderConfig& config) {

    std::array<std::array<float, 4>, 16> r{}; // Temporary registers r0-r15
    std::array<std::array<float, 4>, 16> o{}; // Output registers o0-o15
    std::array<std::array<float, 4>, 16> v{}; // Input registers v0-v15
    std::array<int32_t, 2> a = {0, 0};        // Address registers a0.x, a0.y

    v[0] = inPosition;
    v[1] = inNormal;

    auto readSource = [&](const OperandSource& src, const OperandSwizzle& swz) -> std::array<float, 4> {
        std::array<float, 4> raw{};
        if (src.Type == RegisterType::Input) {
            raw = (src.Index < 16) ? v[src.Index] : std::array<float, 4>{};
        } else if (src.Type == RegisterType::Temporary) {
            raw = (src.Index < 16) ? r[src.Index] : std::array<float, 4>{};
        } else if (src.Type == RegisterType::FloatUniform) {
            int idx = src.Index;
            if (src.RelativeAddressing) {
                idx += a[src.AddressRegister & 1];
            }
            if (idx >= 0 && idx < 96) {
                raw = uniforms.Floats[idx];
            }
        }

        std::array<float, 4> result{};
        for (int i = 0; i < 4; ++i) {
            float val = raw[swz.Selectors[i] & 3];
            result[i] = swz.Negate ? -val : val;
        }
        return result;
    };

    auto writeDest = [&](RegisterType type, uint8_t index, uint8_t mask, const std::array<float, 4>& val) {
        auto applyMask = [&](std::array<float, 4>& target) {
            for (int i = 0; i < 4; ++i) {
                if (mask & (1 << i)) {
                    target[i] = val[i];
                }
            }
        };
        if (type == RegisterType::Temporary && index < 16) {
            applyMask(r[index]);
        } else if (type == RegisterType::Output && index < 16) {
            applyMask(o[index]);
        }
    };

    auto sanitizeMul = [&](float x, float y) -> float {
        if (!config.SanitizeMul) return x * y;
        if ((x == 0.0f && (std::isinf(y) || std::isnan(y))) ||
            (y == 0.0f && (std::isinf(x) || std::isnan(x)))) {
            return 0.0f;
        }
        float p = x * y;
        return std::isnan(p) ? 0.0f : p;
    };

    for (uint32_t pc = mainOffset; pc < programCode.size(); ++pc) {
        uint32_t rawWord = programCode[pc];
        nihstro::Instruction instr{rawWord};
        const auto effectiveOp = instr.opcode.Value().EffectiveOpCode();

        if (effectiveOp == nihstro::OpCode::Id::END) {
            break;
        }

        uint32_t swizzleDescId = instr.common.operand_desc_id.Value();
        uint32_t swizzleWord = (swizzleDescId < swizzleData.size()) ? swizzleData[swizzleDescId] : 0;
        DecodedInstruction dec = DecodeInstruction(rawWord, swizzleWord);
        DecodedSwizzle swz = DecodeSwizzle(swizzleWord);

        std::array<float, 4> s1 = readSource(dec.Src1, swz.Src1);
        std::array<float, 4> s2 = readSource(dec.Src2, swz.Src2);
        std::array<float, 4> s3 = readSource(dec.Src3, swz.Src3);
        std::array<float, 4> outVal{};

        switch (dec.Op) {
        case OpCode::ADD:
            for (int i = 0; i < 4; ++i) outVal[i] = s1[i] + s2[i];
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        case OpCode::MUL:
            for (int i = 0; i < 4; ++i) outVal[i] = sanitizeMul(s1[i], s2[i]);
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        case OpCode::DP3: {
            float d = sanitizeMul(s1[0], s2[0]) + sanitizeMul(s1[1], s2[1]) + sanitizeMul(s1[2], s2[2]);
            outVal = {d, d, d, d};
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        }
        case OpCode::DP4: {
            float d = sanitizeMul(s1[0], s2[0]) + sanitizeMul(s1[1], s2[1]) + sanitizeMul(s1[2], s2[2]) + sanitizeMul(s1[3], s2[3]);
            outVal = {d, d, d, d};
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        }
        case OpCode::MOV:
            outVal = s1;
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        case OpCode::MOVA:
            a[0] = static_cast<int32_t>(s1[0]);
            a[1] = static_cast<int32_t>(s1[1]);
            break;
        case OpCode::MAD:
        case OpCode::MADI:
            for (int i = 0; i < 4; ++i) {
                outVal[i] = sanitizeMul(s1[i], s2[i]) + s3[i];
            }
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        case OpCode::MAX:
            for (int i = 0; i < 4; ++i) outVal[i] = std::max(s1[i], s2[i]);
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        case OpCode::MIN:
            for (int i = 0; i < 4; ++i) outVal[i] = std::min(s1[i], s2[i]);
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        case OpCode::FLR:
            for (int i = 0; i < 4; ++i) outVal[i] = std::floor(s1[i]);
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        case OpCode::RCP:
            for (int i = 0; i < 4; ++i) outVal[i] = (s1[0] != 0.0f) ? (1.0f / s1[0]) : 0.0f;
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        case OpCode::RSQ:
            for (int i = 0; i < 4; ++i) outVal[i] = (s1[0] > 0.0f) ? (1.0f / std::sqrt(s1[0])) : 0.0f;
            writeDest(dec.DestType, dec.DestIndex, swz.DestMask, outVal);
            break;
        default:
            break;
        }
    }

    // Apply Vulkan final depth transform:
    std::array<float, 4> pos = o[0];
    if (config.EmulateDepthInVertexShader) {
        float ndc_z = pos[3] != 0.0f ? (pos[2] / pos[3]) : 0.0f;
        if (ndc_z > 0.0f && ndc_z < 0.000001f) pos[2] = 0.0f;
        if (ndc_z < -1.0f && ndc_z > -1.00001f) pos[2] = -pos[3];
        pos[2] = pos[2] * uniforms.DepthScale + uniforms.DepthOffset * pos[3];
    }
    if (config.FlipViewport && uniforms.FlipViewport != 0) {
        pos[1] = -pos[1];
    }
    // Vulkan clip space
    return {pos[0], pos[1], -pos[2], pos[3]};
}

} // namespace ThreeDsRecomp::Pica
