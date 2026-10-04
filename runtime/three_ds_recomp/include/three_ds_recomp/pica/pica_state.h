#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ThreeDsRecomp::Pica {

// =============================================================================
// Primitive and Vertex States
// =============================================================================

enum class PrimitiveTopology : uint8_t {
    TriangleList = 0,
    TriangleStrip = 1,
    TriangleFan = 2,
    GeometryShader = 3,
};

enum class VertexFormat : uint8_t {
    SignedByte = 0,
    UnsignedByte = 1,
    SignedShort = 2,
    Float = 3,
};

struct VertexAttribute {
    VertexFormat Format = VertexFormat::Float;
    uint8_t ComponentCount = 4; // 1..4
    uint16_t ByteOffset = 0;
    uint8_t Binding = 0;
    bool Enabled = false;
};

struct VertexLoader {
    uint32_t PhysicalAddress = 0;
    uint16_t ByteStride = 0;
    uint8_t ComponentCount = 0;
    std::array<uint8_t, 12> Components{};
};

struct VertexState {
    uint32_t PhysicalBaseAddress = 0;
    uint8_t AttributeCount = 0;
    std::array<VertexAttribute, 16> Attributes{};
    std::array<VertexLoader, 12> Loaders{};

    bool Indexed = false;
    bool IndicesAre16Bit = false;
    uint32_t IndexPhysicalAddress = 0;
    uint32_t VertexCount = 0;
    uint32_t VertexOffset = 0;
};

// =============================================================================
// Texture State (3 Units)
// =============================================================================

enum class TextureFormat : uint8_t {
    Rgba8 = 0,
    Rgb8 = 1,
    Rgb5a1 = 2,
    Rgb565 = 3,
    Rgba4 = 4,
    Ia8 = 5,
    Rg8 = 6,
    I8 = 7,
    A8 = 8,
    Ia4 = 9,
    I4 = 10,
    A4 = 11,
    Etc1 = 12,
    Etc1a4 = 13,
    Unknown = 255,
};

enum class TextureWrap : uint8_t {
    ClampToEdge = 0,
    Repeat = 1,
    MirroredRepeat = 2,
    ClampToBorder = 3,
};

enum class TextureFilter : uint8_t {
    Nearest = 0,
    Linear = 1,
};

struct TexState {
    bool Enabled = false;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint32_t PhysicalAddress = 0;
    TextureFormat Format = TextureFormat::Rgba8;
    uint8_t Type = 0; // 0 = 2D, 1 = Cube, 2 = Shadow2D, etc.
    TextureWrap WrapS = TextureWrap::Repeat;
    TextureWrap WrapT = TextureWrap::Repeat;
    TextureFilter MinFilter = TextureFilter::Nearest;
    TextureFilter MagFilter = TextureFilter::Nearest;
    bool MipLinear = false;
    int16_t LodBiasRaw = 0;
    uint8_t MinMipLevel = 0;
    uint8_t MaxMipLevel = 0;
};

// =============================================================================
// TEV (Texture Environment Combiner - 6 Stages)
// =============================================================================

enum class TevOp : uint8_t {
    Replace = 0,
    Modulate = 1,
    Add = 2,
    AddSigned = 3,
    Interpolate = 4,
    Subtract = 5,
    Dot3Rgb = 6,
    Dot3Rgba = 7,
    MultiplyAdd = 8,
    AddMultiply = 9,
};

enum class TevSource : uint8_t {
    PrimaryColor = 0,
    PrimaryFragmentColor = 1,
    SecondaryFragmentColor = 2,
    Texture0 = 3,
    Texture1 = 4,
    Texture2 = 5,
    Texture3 = 6,
    PreviousBuffer = 13,
    Constant = 14,
    Previous = 15,
};

enum class TevScale : uint8_t {
    Scale1x = 0,
    Scale2x = 1,
    Scale4x = 2,
};

struct TevStage {
    TevSource SourceRgb[3] = { TevSource::Previous, TevSource::Previous, TevSource::Previous };
    TevSource SourceAlpha[3] = { TevSource::Previous, TevSource::Previous, TevSource::Previous };
    uint8_t OperandRgb[3] = { 0, 0, 0 };
    uint8_t OperandAlpha[3] = { 0, 0, 0 };
    TevOp OpRgb = TevOp::Replace;
    TevOp OpAlpha = TevOp::Replace;
    TevScale ScaleRgb = TevScale::Scale1x;
    TevScale ScaleAlpha = TevScale::Scale1x;
    uint32_t ConstantColor = 0xFFFFFFFFU;
};

struct TevState {
    std::array<TevStage, 6> Stages{};
    uint8_t CombinerBufferInputMask = 0;  // Which stages write to buffer
    uint8_t CombinerBufferOutputMask = 0; // Which stages read from buffer
};

// =============================================================================
// Fragment Lighting & Fog States
// =============================================================================

struct LightState {
    bool Directional = false;
    bool TwoSided = false;
    bool ShadowEnabled = false;
    bool SpotAttenuation = false;
    bool DistanceAttenuation = false;
};

struct LightingState {
    bool Enabled = false;
    uint8_t ActiveLightCount = 0; // 0..8
    uint8_t ConfigIndex = 0;      // 0..7 (determines which LUTs are active)
    uint8_t FresnelSelector = 0;
    uint8_t BumpTextureUnit = 0;
    uint8_t ShadowTextureUnit = 0;
    std::array<LightState, 8> Lights{};
    uint64_t LutContentHash = 0;
};

struct FogState {
    bool Enabled = false;
    bool Flip = false;
    uint8_t Mode = 0; // 0 = disabled, 5 = fog lut, 7 = gas
    uint32_t Color = 0;
};

// =============================================================================
// Output Merger: Framebuffer, Depth, Stencil, Blend, Logic Op
// =============================================================================

enum class CompareFunc : uint8_t {
    Never = 0,
    Always = 1,
    Equal = 2,
    NotEqual = 3,
    Less = 4,
    LessOrEqual = 5,
    Greater = 6,
    GreaterOrEqual = 7,
};

enum class StencilOp : uint8_t {
    Keep = 0,
    Zero = 1,
    Replace = 2,
    Increment = 3,
    Decrement = 4,
    Invert = 5,
    IncrementWrap = 6,
    DecrementWrap = 7,
};

enum class BlendEq : uint8_t {
    Add = 0,
    Subtract = 1,
    ReverseSubtract = 2,
    Min = 3,
    Max = 4,
};

enum class BlendFactor : uint8_t {
    Zero = 0,
    One = 1,
    SourceColor = 2,
    OneMinusSourceColor = 3,
    DestColor = 4,
    OneMinusDestColor = 5,
    SourceAlpha = 6,
    OneMinusSourceAlpha = 7,
    DestAlpha = 8,
    OneMinusDestAlpha = 9,
    ConstantColor = 10,
    OneMinusConstantColor = 11,
    ConstantAlpha = 12,
    OneMinusConstantAlpha = 13,
    SourceAlphaSaturate = 14,
};

enum class LogicOp : uint8_t {
    Clear = 0,
    And = 1,
    AndReverse = 2,
    Copy = 3,
    Set = 4,
    CopyInverted = 5,
    NoOp = 6,
    Invert = 7,
    Nand = 8,
    Or = 9,
    Nor = 10,
    Xor = 11,
    Equivalent = 12,
    AndInverted = 13,
    OrReverse = 14,
    OrInverted = 15,
};

enum class CullMode : uint8_t {
    KeepAll = 0,
    KeepClockwise = 1,
    KeepCounterClockwise = 2,
};

struct FramebufferState {
    uint32_t ColorAddress = 0;
    uint32_t DepthAddress = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t ColorFormat = 0;
    uint8_t DepthFormat = 0;
    bool ColorWrite = true;
    bool DepthWrite = true;
    uint8_t ColorWriteMask = 0xF;
};

struct DepthStencilState {
    bool DepthTest = false;
    bool DepthWrite = false;
    CompareFunc DepthCompare = CompareFunc::Always;

    bool StencilTest = false;
    CompareFunc StencilCompare = CompareFunc::Always;
    uint8_t StencilRef = 0;
    uint8_t StencilCompareMask = 0xFF;
    uint8_t StencilWriteMask = 0xFF;
    StencilOp StencilFail = StencilOp::Keep;
    StencilOp StencilDepthFail = StencilOp::Keep;
    StencilOp StencilPass = StencilOp::Keep;

    bool AlphaTest = false;
    CompareFunc AlphaCompare = CompareFunc::Always;
    uint8_t AlphaRef = 0;
};

struct BlendState {
    bool Enabled = false;
    BlendEq EquationRgb = BlendEq::Add;
    BlendEq EquationAlpha = BlendEq::Add;
    BlendFactor SrcRgb = BlendFactor::One;
    BlendFactor DestRgb = BlendFactor::Zero;
    BlendFactor SrcAlpha = BlendFactor::One;
    BlendFactor DestAlpha = BlendFactor::Zero;
    uint32_t ConstantColor = 0;

    bool LogicOpEnabled = false;
    LogicOp LogicOperation = LogicOp::Copy;
};

struct ViewportState {
    int16_t CornerX = 0;
    int16_t CornerY = 0;
    float HalfWidth = 0.0f;
    float HalfHeight = 0.0f;
    float DepthRange = 1.0f;
    float NearPlane = 0.0f;
};

struct ScissorState {
    uint8_t Mode = 0; // 0 = Disabled, 1 = Exclude, 3 = Include
    uint16_t X1 = 0;
    uint16_t Y1 = 0;
    uint16_t X2 = 0;
    uint16_t Y2 = 0;
};

struct PolygonOffsetState {
    bool Enabled = false;
    float Unit = 0.0f;    // Constant depth bias factor
    float Factor = 0.0f;  // Slope-scaled factor
    float Clamp = 0.0f;   // Depth bias clamp
};

// =============================================================================
// Complete PICA Draw State
// =============================================================================

struct PicaDrawState {
    PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
    CullMode Culling = CullMode::KeepAll;
    ViewportState Viewport;
    ScissorState Scissor;
    PolygonOffsetState PolygonOffset;

    VertexState Vertex;
    std::array<TexState, 3> Textures{};
    TevState Tev;
    LightingState Lighting;
    FogState Fog;
    FramebufferState Framebuffer;
    DepthStencilState DepthStencil;
    BlendState Blend;

    uint64_t StateHash = 0;

    [[nodiscard]] std::string DumpReadable() const;
};

} // namespace ThreeDsRecomp::Pica
