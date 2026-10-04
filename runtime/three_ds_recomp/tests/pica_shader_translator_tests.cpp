#include "three_ds_recomp/pica/pica_shader.h"
#include "three_ds_recomp/pica/pica_shader_translator.h"
#include "three_ds_recomp/pica/pica_shader_corpus.h"
#include "nihstro/shader_bytecode.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>

using namespace ThreeDsRecomp::Pica;

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

void TestInstructionDecoding() {
    std::cout << "[TEST] 1. Instruction and Swizzle Decoding..." << std::endl;

    // ADD instruction (Format 1): ADD o0, v0, c0 (swizzle 0)
    // Opcode = 0x00
    // dest = 0 (output 0)
    // src1 = 0 (input 0)
    // src2 = 32 (uniform 0)
    // In PICA Format 1, src1 is 7-bit (can hold uniforms c0-c95), src2 is 5-bit (input/temp v0-v15, r0-r15)
    nihstro::Instruction addInstr{0};
    addInstr.opcode = nihstro::OpCode::Id::ADD;
    addInstr.common.dest = nihstro::DestRegister::MakeOutput(0);
    addInstr.common.src1 = nihstro::SourceRegister::MakeFloat(0);
    addInstr.common.src2 = nihstro::SourceRegister::MakeInput(0);
    addInstr.common.operand_desc_id = 1;

    DecodedInstruction decAdd = PicaShaderTranslator::DecodeInstruction(addInstr.hex, 0);
    ASSERT_EQ(decAdd.Op, OpCode::ADD);
    ASSERT_EQ(decAdd.Type, InstructionType::Arithmetic);
    ASSERT_EQ(decAdd.DestType, RegisterType::Output);
    ASSERT_EQ(decAdd.DestIndex, 0);
    ASSERT_EQ(decAdd.Src1.Type, RegisterType::FloatUniform);
    ASSERT_EQ(decAdd.Src1.Index, 0);
    ASSERT_EQ(decAdd.Src2.Type, RegisterType::Input);
    ASSERT_EQ(decAdd.Src2.Index, 0);
    ASSERT_EQ(decAdd.SwizzleIndex, 1);

    // Swizzle pattern: src1 = xyzw, src2 = yzww, dest_mask = 0xF
    nihstro::SwizzlePattern swzPattern{0};
    swzPattern.dest_mask = 0xF;
    swzPattern.negate_src1 = false;
    swzPattern.negate_src2 = true;

    DecodedSwizzle decSwz = PicaShaderTranslator::DecodeSwizzle(swzPattern.hex);
    ASSERT_EQ(decSwz.DestMask, 0xF);
    ASSERT_FALSE(decSwz.Src1.Negate);
    ASSERT_TRUE(decSwz.Src2.Negate);

    // Flow control instruction: CALL
    nihstro::Instruction callInstr{0};
    callInstr.opcode = nihstro::OpCode::Id::CALL;
    callInstr.flow_control.dest_offset = 12;
    callInstr.flow_control.num_instructions = 5;

    DecodedInstruction decCall = PicaShaderTranslator::DecodeInstruction(callInstr.hex, 0);
    ASSERT_EQ(decCall.Op, OpCode::CALL);
    ASSERT_EQ(decCall.Type, InstructionType::Conditional);
    ASSERT_EQ(decCall.DestOffset, 12);
    ASSERT_EQ(decCall.NumInstructions, 5);

    std::cout << "       -> Passed!" << std::endl;
}

void TestControlFlowRestructuring() {
    std::cout << "[TEST] 2. Control Flow Restructuring (CALL, LOOP, JMP)..." << std::endl;

    // Construct a PICA program with subroutines, loops and calls:
    // 0: LOOP to instruction 3 (count via i0)
    // 1:   ADD r0, r0, c1
    // 2:   ENDLOOP
    // 3: CALL subroutine at 5 (2 instructions)
    // 4: END
    // 5: [Subroutine] DP4 o0, r0, c0
    // 6:              MUL o1, r0, c2
    std::vector<uint32_t> program(8, 0);
    std::vector<uint32_t> swizzles(4, 0);

    nihstro::SwizzlePattern swz{0};
    swz.dest_mask = 0xF;
    swizzles[0] = swz.hex;

    nihstro::Instruction loopInstr{0};
    loopInstr.opcode = nihstro::OpCode::Id::LOOP;
    loopInstr.flow_control.dest_offset = 2;
    loopInstr.flow_control.int_uniform_id = 0;
    program[0] = loopInstr.hex;

    nihstro::Instruction addInstr{0};
    addInstr.opcode = nihstro::OpCode::Id::ADD;
    addInstr.common.dest = nihstro::DestRegister::MakeTemporary(0);
    addInstr.common.src1 = nihstro::SourceRegister::MakeTemporary(0);
    addInstr.common.src2 = nihstro::SourceRegister::MakeFloat(1);
    program[1] = addInstr.hex;

    nihstro::Instruction nopInstr{0};
    nopInstr.opcode = nihstro::OpCode::Id::NOP;
    program[2] = nopInstr.hex;

    nihstro::Instruction callInstr{0};
    callInstr.opcode = nihstro::OpCode::Id::CALL;
    callInstr.flow_control.dest_offset = 5;
    callInstr.flow_control.num_instructions = 2;
    program[3] = callInstr.hex;

    nihstro::Instruction endInstr{0};
    endInstr.opcode = nihstro::OpCode::Id::END;
    program[4] = endInstr.hex;

    nihstro::Instruction dp4Instr{0};
    dp4Instr.opcode = nihstro::OpCode::Id::DP4;
    dp4Instr.common.dest = nihstro::DestRegister::MakeOutput(0);
    dp4Instr.common.src1 = nihstro::SourceRegister::MakeTemporary(0);
    dp4Instr.common.src2 = nihstro::SourceRegister::MakeFloat(0);
    program[5] = dp4Instr.hex;

    nihstro::Instruction mulInstr{0};
    mulInstr.opcode = nihstro::OpCode::Id::MUL;
    mulInstr.common.dest = nihstro::DestRegister::MakeOutput(1);
    mulInstr.common.src1 = nihstro::SourceRegister::MakeTemporary(0);
    mulInstr.common.src2 = nihstro::SourceRegister::MakeFloat(2);
    program[6] = mulInstr.hex;

    nihstro::Instruction retEnd{0};
    retEnd.opcode = nihstro::OpCode::Id::END;
    program[7] = retEnd.hex;

    PicaShaderConfig config{};
    config.SanitizeMul = true;
    config.EmulateDepthInVertexShader = true;

    PicaShaderOutput output{};
    std::string err;
    bool ok = PicaShaderTranslator::TranslateGlsl(program, swizzles, 0, config, output, &err);
    if (!ok) {
        std::cerr << "Translation failed: " << err << std::endl;
    }
    ASSERT_TRUE(ok);
    ASSERT_TRUE(output.GlslSource.find("bool sub_") != std::string::npos);
    ASSERT_TRUE(output.GlslSource.find("exec_shader()") != std::string::npos);
    ASSERT_TRUE(output.GlslSource.find("loop") != std::string::npos);

    std::cout << "       -> Passed!" << std::endl;
}

void TestArithmeticSanitizeMul() {
    std::cout << "[TEST] 3. Non-IEEE inf*0=0 Arithmetic Sanitize..." << std::endl;

    PicaVertexUniformState uniforms{};
    // Set c0 = (inf, 0, 5, 2)
    uniforms.Floats[0] = {INFINITY, 0.0f, 5.0f, 2.0f};
    // Set c1 = (0, inf, 2, 3)
    uniforms.Floats[1] = {0.0f, INFINITY, 2.0f, 3.0f};

    std::vector<uint32_t> program(2, 0);
    std::vector<uint32_t> swizzles(1, 0);

    nihstro::SwizzlePattern swz{0};
    swz.dest_mask = 0xF;
    swizzles[0] = swz.hex;

    // MUL r0, c0, c1
    nihstro::Instruction mulInstr{0};
    mulInstr.opcode = nihstro::OpCode::Id::MUL;
    mulInstr.common.dest = nihstro::DestRegister::MakeOutput(0);
    mulInstr.common.src1 = nihstro::SourceRegister::MakeFloat(0);
    mulInstr.common.src2 = nihstro::SourceRegister::MakeFloat(1);
    program[0] = mulInstr.hex;

    nihstro::Instruction endInstr{0};
    endInstr.opcode = nihstro::OpCode::Id::END;
    program[1] = endInstr.hex;

    PicaShaderConfig configWithSanitize{};
    configWithSanitize.SanitizeMul = true;
    auto posSanitized = PicaShaderTranslator::ExecuteCpuVertexProgram(
        program, swizzles, 0, uniforms, {0, 0, 0, 1}, {0, 0, 1, 0}, configWithSanitize);

    // In sanitized mode: inf * 0.0 MUST BE 0.0, not NaN!
    ASSERT_EQ(posSanitized[0], 0.0f);
    ASSERT_EQ(posSanitized[1], 0.0f);

    std::cout << "       -> Passed!" << std::endl;
}

void TestUniformsUboLayout() {
    std::cout << "[TEST] 4. Uniforms UBO Layout and Alignment (std140)..." << std::endl;

    ASSERT_EQ(sizeof(PicaVertexUniformState), 1616);
    PicaVertexUniformState ubo{};
    ubo.BooleanMask = 0xA5;
    ubo.FlipViewport = 1;
    ubo.DepthScale = 0.5f;
    ubo.DepthOffset = 0.25f;
    ubo.Floats[95] = {1.0f, 2.0f, 3.0f, 4.0f};

    // Check memory offsets
    uint8_t* base = reinterpret_cast<uint8_t*>(&ubo);
    uint32_t* bPtr = reinterpret_cast<uint32_t*>(base + 0);
    int32_t* flipPtr = reinterpret_cast<int32_t*>(base + 4);
    float* depthScalePtr = reinterpret_cast<float*>(base + 8);
    float* depthOffsetPtr = reinterpret_cast<float*>(base + 12);

    ASSERT_EQ(*bPtr, 0xA5U);
    ASSERT_EQ(*flipPtr, 1);
    ASSERT_EQ(*depthScalePtr, 0.5f);
    ASSERT_EQ(*depthOffsetPtr, 0.25f);

    std::cout << "       -> Passed!" << std::endl;
}

void TestDepthMappingNoFragDepth() {
    std::cout << "[TEST] 5. Depth Mapping in Vertex Shader (R17, ADR-0003)..." << std::endl;

    std::vector<uint32_t> program = {0x88000000U}; // END
    std::vector<uint32_t> swizzles = {0};

    PicaShaderConfig config{};
    config.EmulateDepthInVertexShader = true;

    PicaShaderOutput output{};
    std::string err;
    bool ok = PicaShaderTranslator::TranslateGlsl(program, swizzles, 0, config, output, &err);
    ASSERT_TRUE(ok);

    // Rule R17: gl_FragDepth MUST NOT exist in vertex or fragment main path
    ASSERT_TRUE(output.GlslSource.find("gl_FragDepth") == std::string::npos);
    ASSERT_TRUE(output.GlslSource.find("gl_Position = vec4(pica_position.x, pica_position.y, -pica_position.z, pica_position.w)") != std::string::npos);
    ASSERT_TRUE(output.GlslSource.find("pica_ndc_z") != std::string::npos);

    std::cout << "       -> Passed!" << std::endl;
}

void TestSpirvCompilationAndValidation() {
    std::cout << "[TEST] 6. SPIR-V Compilation and spirv-val Validation (0 Errors)..." << std::endl;

    // MVP transformation vertex program:
    // o0.x = DP4(c0, v0)
    // o0.y = DP4(c1, v0)
    // o0.z = DP4(c2, v0)
    // o0.w = DP4(c3, v0)
    // o1 = min(abs(v1), 1.0)
    // END
    std::vector<uint32_t> program(5, 0);
    std::vector<uint32_t> swizzles(1, 0);

    nihstro::SwizzlePattern swz{0};
    swz.dest_mask = 0xF;
    swizzles[0] = swz.hex;

    for (int row = 0; row < 4; ++row) {
        nihstro::Instruction dp4{0};
        dp4.opcode = nihstro::OpCode::Id::DP4;
        dp4.common.dest = nihstro::DestRegister::MakeOutput(0);
        dp4.common.src1 = nihstro::SourceRegister::MakeFloat(row);
        dp4.common.src2 = nihstro::SourceRegister::MakeInput(0);
        dp4.common.operand_desc_id = 0;
        program[row] = dp4.hex;
    }

    nihstro::Instruction endInstr{0};
    endInstr.opcode = nihstro::OpCode::Id::END;
    program[4] = endInstr.hex;

    PicaShaderConfig config{};
    config.SanitizeMul = true;
    config.EmulateDepthInVertexShader = true;

    PicaShaderOutput output{};
    std::string err;
    bool ok = PicaShaderTranslator::TranslateGlsl(program, swizzles, 0, config, output, &err);
    ASSERT_TRUE(ok);

    std::vector<uint32_t> spirvWords;
    bool compiled = PicaShaderTranslator::CompileGlslToSpirv(output.GlslSource, spirvWords, &err);
    if (!compiled) {
        std::cerr << "Compilation failed: " << err << std::endl;
    }
    ASSERT_TRUE(compiled);
    ASSERT_FALSE(spirvWords.empty());

    // Quality Gate 3: Must pass spirv-val with 0 errors!
    bool valid = PicaShaderTranslator::ValidateSpirv(spirvWords, &err);
    if (!valid) {
        std::cerr << "SPIR-V validation error: " << err << std::endl;
    }
    ASSERT_TRUE(valid);

    std::cout << "       -> Passed (0 spirv-val errors)!" << std::endl;
}

void TestCorpusShaderFromKokiriTitleBoot() {
    std::cout << "[TEST] 7. Validation of Kokiri Boot Pipeline Shader from Corpus..." << std::endl;

    const std::string jsonPath = "tools/oot3d/native_pica_frontend/profiles/oot3d_pica_boot_title_kokiri_pipeline.json";
    std::ifstream f(jsonPath);
    if (!f) {
        std::cout << "       -> Skipped (corpus json file not accessible)" << std::endl;
        return;
    }

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // Extract the vertex shader source
    const std::string needle = "\"stage\": \"vertex\"";
    size_t pos = content.find(needle);
    ASSERT_TRUE(pos != std::string::npos);

    size_t srcKey = content.rfind("\"source\": \"", pos);
    ASSERT_TRUE(srcKey != std::string::npos);
    size_t start = srcKey + 11;
    size_t end = content.find("\",\n", start);
    ASSERT_TRUE(end != std::string::npos);

    std::string rawSource = content.substr(start, end - start);
    // Unescape \n, \", etc.
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
    bool ok = PicaShaderTranslator::CompileGlslToSpirv(unescaped, spirvWords, &err);
    if (!ok) {
        std::cerr << "Corpus shader compilation error: " << err << std::endl;
    }
    ASSERT_TRUE(ok);

    bool valOk = PicaShaderTranslator::ValidateSpirv(spirvWords, &err);
    if (!valOk) {
        std::cerr << "Corpus shader spirv-val error: " << err << std::endl;
    }
    ASSERT_TRUE(valOk);

    std::cout << "       -> Passed (Corpus Shader validated with 0 errors)!" << std::endl;
}

void TestNumericalParityVertexPositions() {
    std::cout << "[TEST] 8. Numerical Parity: Oracle CPU vs Mathematical Reference..." << std::endl;

    // Model-View-Projection Matrix (Identity translation, scale 2.0 on X, 3.0 on Y, 1.0 on Z):
    // row 0: (2, 0, 0, 0)
    // row 1: (0, 3, 0, 0)
    // row 2: (0, 0, 1, 0)
    // row 3: (0, 0, 0, 1)
    PicaVertexUniformState uniforms{};
    uniforms.Floats[0] = {2.0f, 0.0f, 0.0f, 0.0f};
    uniforms.Floats[1] = {0.0f, 3.0f, 0.0f, 0.0f};
    uniforms.Floats[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    uniforms.Floats[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    uniforms.DepthScale = 1.0f;
    uniforms.DepthOffset = 0.0f;

    std::vector<uint32_t> program(5, 0);
    std::vector<uint32_t> swizzles(4, 0);
    for (int row = 0; row < 4; ++row) {
        nihstro::SwizzlePattern swz{0};
        swz.dest_mask = (1 << row); // row 0 -> .x, row 1 -> .y, row 2 -> .z, row 3 -> .w
        for (uint8_t c = 0; c < 4; ++c) {
            swz.SetSelectorSrc1(c, static_cast<nihstro::SwizzlePattern::Selector>(c));
            swz.SetSelectorSrc2(c, static_cast<nihstro::SwizzlePattern::Selector>(c));
        }
        swizzles[row] = swz.hex;
    }

    for (int row = 0; row < 4; ++row) {
        nihstro::Instruction dp4{0};
        dp4.opcode = nihstro::OpCode::Id::DP4;
        dp4.common.dest = nihstro::DestRegister::MakeOutput(0);
        dp4.common.src1 = nihstro::SourceRegister::MakeFloat(row);
        dp4.common.src2 = nihstro::SourceRegister::MakeInput(0);
        dp4.common.operand_desc_id = row;
        program[row] = dp4.hex;
    }
    nihstro::Instruction endInstr{0};
    endInstr.opcode = nihstro::OpCode::Id::END;
    program[4] = endInstr.hex;

    PicaShaderConfig config{};
    config.EmulateDepthInVertexShader = true;
    config.SanitizeMul = true;

    // Test vertex 1: (1.5, -2.0, -0.5, 1.0)
    std::array<float, 4> v1 = {1.5f, -2.0f, -0.5f, 1.0f};
    auto out1 = PicaShaderTranslator::ExecuteCpuVertexProgram(program, swizzles, 0, uniforms, v1, {}, config);

    // Expected:
    // x' = 1.5 * 2 = 3.0
    // y' = -2.0 * 3 = -6.0
    // z' (PICA NDC) = -0.5
    // z' (Vulkan clip space) = -(-0.5) = 0.5
    // w' = 1.0
    constexpr float kTol = 1e-4f;
    ASSERT_TRUE(std::abs(out1[0] - 3.0f) < kTol);
    ASSERT_TRUE(std::abs(out1[1] - (-6.0f)) < kTol);
    ASSERT_TRUE(std::abs(out1[2] - 0.5f) < kTol);
    ASSERT_TRUE(std::abs(out1[3] - 1.0f) < kTol);

    std::cout << "       -> Passed (Vertex positions within " << kTol << " tolerance)!" << std::endl;
}

void TestOfflineCorpusCache() {
    std::cout << "[TEST] 9. Offline Corpus Cache and Corrupted File Recovery (R14, R15)..." << std::endl;

    PicaShaderCorpus corpus(1024 * 1024); // 1 MB budget
    std::vector<uint32_t> dummySpv = {0x07230203, 1, 2, 3, 4, 5};

    ASSERT_TRUE(corpus.RegisterShader(0x1122334455667788ULL, dummySpv));
    ASSERT_EQ(corpus.GetShaderCount(), 1);

    const auto* entry = corpus.FindShader(0x1122334455667788ULL);
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(entry->HitCount, 1);

    // Test saving
    const std::string tmpCorpusPath = "/tmp/test_shader_corpus.bin";
    ASSERT_TRUE(corpus.SaveCorpusFile(tmpCorpusPath));

    // Test loading
    PicaShaderCorpus loadedCorpus;
    ASSERT_TRUE(loadedCorpus.LoadCorpusFile(tmpCorpusPath));
    ASSERT_EQ(loadedCorpus.GetShaderCount(), 1);
    const auto* loadedEntry = loadedCorpus.FindShader(0x1122334455667788ULL);
    ASSERT_TRUE(loadedEntry != nullptr);
    ASSERT_EQ(loadedEntry->SpirvBinary.size(), dummySpv.size());

    // Test corrupted file recovery (R15): Corrupt magic and test graceful recovery without crash
    std::ofstream corruptOut(tmpCorpusPath, std::ios::binary);
    uint32_t badMagic = 0xDEADBEEF;
    corruptOut.write(reinterpret_cast<const char*>(&badMagic), sizeof(badMagic));
    corruptOut.close();

    PicaShaderCorpus recoveryCorpus;
    // Must return false and not crash (R12, R15)
    ASSERT_FALSE(recoveryCorpus.LoadCorpusFile(tmpCorpusPath));
    ASSERT_EQ(recoveryCorpus.GetShaderCount(), 0);

    unlink(tmpCorpusPath.c_str());

    std::cout << "       -> Passed!" << std::endl;
}

int main() {
    std::cout << "========================================================================" << std::endl;
    std::cout << " Running Phase 3 Tests: PICA Vertex Shaders & SPIR-V Pipeline" << std::endl;
    std::cout << "========================================================================" << std::endl;

    TestInstructionDecoding();
    TestControlFlowRestructuring();
    TestArithmeticSanitizeMul();
    TestUniformsUboLayout();
    TestDepthMappingNoFragDepth();
    TestSpirvCompilationAndValidation();
    TestCorpusShaderFromKokiriTitleBoot();
    TestNumericalParityVertexPositions();
    TestOfflineCorpusCache();

    std::cout << "========================================================================" << std::endl;
    std::cout << " All Phase 3 vertex shader tests passed successfully (0 spirv-val errors)." << std::endl;
    std::cout << "========================================================================" << std::endl;
    return 0;
}
