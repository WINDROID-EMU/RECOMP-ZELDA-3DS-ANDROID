#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Pica {

// =============================================================================
// PICA200 Vertex Shader Register Types
// =============================================================================

enum class RegisterType : uint8_t {
    Input = 0,         // v0 - v15
    Output = 1,        // o0 - o15
    Temporary = 2,     // r0 - r15
    FloatUniform = 3,  // c0 - c95
    IntUniform = 4,    // i0 - i3
    BoolUniform = 5,   // b0 - b7
    Address = 6,       // a0 - a1
    Loop = 7,          // aL (iteration counter)
    Unknown = 255
};

// =============================================================================
// PICA200 Instruction Opcodes (MAESTRO-2G / 3dbrew)
// =============================================================================

enum class OpCode : uint8_t {
    ADD     = 0x00,
    DP3     = 0x01,
    DP4     = 0x02,
    DPH     = 0x03,
    DST     = 0x04,
    EX2     = 0x05,
    LG2     = 0x06,
    LIT     = 0x07,
    MUL     = 0x08,
    SGE     = 0x09,
    SLT     = 0x0A,
    FLR     = 0x0B,
    MAX     = 0x0C,
    MIN     = 0x0D,
    RCP     = 0x0E,
    RSQ     = 0x0F,
    MOVA    = 0x12,
    MOV     = 0x13,
    DPHI    = 0x18,
    DSTI    = 0x19,
    SGEI    = 0x1A,
    SLTI    = 0x1B,
    BREAK   = 0x20,
    NOP     = 0x21,
    END     = 0x22,
    BREAKC  = 0x23,
    CALL    = 0x24,
    CALLC   = 0x25,
    CALLU   = 0x26,
    IFU     = 0x27,
    IFC     = 0x28,
    LOOP    = 0x29,
    EMIT    = 0x2A,
    SETEMIT = 0x2B,
    JMPC    = 0x2C,
    JMPU    = 0x2D,
    CMP     = 0x2E,
    MADI    = 0x30,
    MAD     = 0x38,
    UNKNOWN = 0xFF
};

enum class InstructionType : uint8_t {
    Trivial,            // Format 0 (END, NOP)
    Arithmetic,         // Format 1 (ADD, MUL, DP3, DP4, etc.)
    Conditional,        // Format 2 (CMP)
    UniformFlowControl, // Format 3 (CALL, IF, LOOP, JMP)
    SetEmit,            // Format 4
    MultiplyAdd,        // Format 5 (MAD, MADI)
    Unknown
};

// =============================================================================
// Decoded Operand, Swizzle and Instruction
// =============================================================================

struct OperandSource {
    RegisterType Type = RegisterType::Temporary;
    uint8_t Index = 0;
    bool RelativeAddressing = false;
    uint8_t AddressRegister = 0; // 0 = a0.x, 1 = a0.y, 2 = aL
};

struct OperandSwizzle {
    std::array<uint8_t, 4> Selectors = {0, 1, 2, 3}; // x, y, z, w (0..3)
    bool Negate = false;
};

struct DecodedSwizzle {
    OperandSwizzle Src1;
    OperandSwizzle Src2;
    OperandSwizzle Src3;
    uint8_t DestMask = 0xFU; // Bit 0=x, 1=y, 2=z, 3=w
};

struct DecodedInstruction {
    uint32_t RawWord = 0;
    OpCode Op = OpCode::UNKNOWN;
    InstructionType Type = InstructionType::Unknown;

    RegisterType DestType = RegisterType::Temporary;
    uint8_t DestIndex = 0;
    uint8_t SwizzleIndex = 0;

    OperandSource Src1;
    OperandSource Src2;
    OperandSource Src3;

    // Flow control fields
    uint32_t DestOffset = 0;
    uint32_t NumInstructions = 0;
    uint8_t BoolUniformIndex = 0;
    uint8_t IntUniformIndex = 0;
    bool InvertCondition = false;
    uint8_t CmpOpX = 0;
    uint8_t CmpOpY = 0;
};

// =============================================================================
// Uniform State (UBO std140 Layout for Vulkan)
// =============================================================================

// Layout std140:
// alignas(4)  uint32_t b (bitmask for b0-b7)
// alignas(4)  int32_t flip_viewport
// alignas(8)  float depth_scale, depth_offset
// alignas(16) uvec4 i[4]
// alignas(16) vec4 f[96]
struct alignas(16) PicaVertexUniformState {
    uint32_t BooleanMask = 0;
    int32_t FlipViewport = 0;
    float DepthScale = 1.0f;
    float DepthOffset = 0.0f;
    std::array<std::array<uint32_t, 4>, 4> Integers{};
    std::array<std::array<float, 4>, 96> Floats{};
};

static_assert(sizeof(PicaVertexUniformState) == 1616, "PicaVertexUniformState std140 layout mismatch");

// =============================================================================
// Configuration and Translation Results
// =============================================================================

struct PicaShaderConfig {
    bool SanitizeMul = true;                  // Emulate non-IEEE inf*0=0
    bool FlipViewport = false;                 // Invert Y coordinate for Vulkan
    bool EmulateDepthInVertexShader = true;    // z' = z * scale + offset * w (R17, ADR-0003)
    bool GenerateDebugComments = false;
};

struct PicaShaderOutput {
    uint64_t StateKey = 0;
    std::string GlslSource;
    std::vector<uint32_t> SpirvBinary;
    bool ValidSpirv = false;
};

} // namespace ThreeDsRecomp::Pica
