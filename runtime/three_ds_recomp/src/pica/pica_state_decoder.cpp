#include "three_ds_recomp/pica/pica_state_decoder.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace ThreeDsRecomp::Pica {
namespace {

constexpr uint64_t kFnvPrime = 1099511628211ULL;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;

inline uint64_t FnvHashBytes(const void* data, size_t length, uint64_t hash = kFnvOffsetBasis) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    return hash;
}

template <typename T>
inline uint64_t FnvHashValue(const T& value, uint64_t hash) noexcept {
    return FnvHashBytes(&value, sizeof(T), hash);
}

constexpr std::array<uint32_t, 6> kTevStageBases{ 0x0C0U, 0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U, 0x0F8U };

} // namespace

bool DecodePicaDrawState(
    std::span<const uint32_t> registers,
    PicaDrawState& outState,
    std::string* error) {
    if (registers.size() < 0x300U) {
        if (error) {
            *error = "Register span size less than 0x300";
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // 1. Primitive and Rasterizer State
    // -------------------------------------------------------------------------
    const uint32_t cullRaw = registers[0x040U] & 0x3U;
    outState.Culling = cullRaw <= 2U ? static_cast<CullMode>(cullRaw) : CullMode::KeepAll;

    const uint32_t corner = registers[0x041U];
    outState.Viewport.CornerX = static_cast<int16_t>(corner & 0xFFFFU);
    outState.Viewport.CornerY = static_cast<int16_t>((corner >> 16U) & 0xFFFFU);

    const uint32_t size = registers[0x043U];
    outState.Viewport.HalfWidth = static_cast<float>(size & 0xFFFFU);
    outState.Viewport.HalfHeight = static_cast<float>((size >> 16U) & 0xFFFFU);

    const uint32_t scissorModeRaw = registers[0x065U] & 0x3U;
    outState.Scissor.Mode = static_cast<uint8_t>(scissorModeRaw);
    const uint32_t scissor1 = registers[0x066U];
    outState.Scissor.X1 = static_cast<uint16_t>(scissor1 & 0xFFFFU);
    outState.Scissor.Y1 = static_cast<uint16_t>((scissor1 >> 16U) & 0xFFFFU);
    const uint32_t scissor2 = registers[0x067U];
    outState.Scissor.X2 = static_cast<uint16_t>(scissor2 & 0xFFFFU);
    outState.Scissor.Y2 = static_cast<uint16_t>((scissor2 >> 16U) & 0xFFFFU);

    const uint32_t topologyRaw = registers[0x242U] & 0x3U;
    outState.Topology = static_cast<PrimitiveTopology>(topologyRaw);

    // -------------------------------------------------------------------------
    // 2. Vertex State
    // -------------------------------------------------------------------------
    outState.Vertex.PhysicalBaseAddress = registers[0x200U];
    const uint32_t indexConfig = registers[0x244U];
    outState.Vertex.Indexed = (indexConfig & 1U) != 0U;
    outState.Vertex.IndicesAre16Bit = ((indexConfig >> 31U) & 1U) != 0U;
    outState.Vertex.IndexPhysicalAddress = registers[0x245U];
    outState.Vertex.VertexCount = registers[0x247U];
    outState.Vertex.VertexOffset = registers[0x248U];

    for (size_t i = 0; i < 16; ++i) {
        const uint32_t attr = registers[0x220U + i];
        outState.Vertex.Attributes[i].Format = static_cast<VertexFormat>(attr & 0x3U);
        outState.Vertex.Attributes[i].ComponentCount = static_cast<uint8_t>(((attr >> 2U) & 0x3U) + 1U);
        outState.Vertex.Attributes[i].ByteOffset = static_cast<uint16_t>((attr >> 8U) & 0xFFFFU);
        outState.Vertex.Attributes[i].Binding = static_cast<uint8_t>((attr >> 24U) & 0xFU);
        outState.Vertex.Attributes[i].Enabled = ((attr >> 31U) & 1U) != 0U;
    }

    // -------------------------------------------------------------------------
    // 3. Texture State (Units 0, 1, 2)
    // -------------------------------------------------------------------------
    const uint32_t texConfig = registers[0x080U];
    struct TexRegOffsets {
        uint32_t Size;
        uint32_t Address;
        uint32_t FormatWrap;
        uint32_t Filter;
    };
    constexpr std::array<TexRegOffsets, 3> texOffsets{{
        { 0x081U, 0x082U, 0x085U, 0x086U },
        { 0x08BU, 0x08CU, 0x08EU, 0x08FU },
        { 0x091U, 0x092U, 0x095U, 0x096U },
    }};

    for (size_t u = 0; u < 3; ++u) {
        TexState& tex = outState.Textures[u];
        tex.Enabled = ((texConfig >> u) & 1U) != 0U;
        const auto& off = texOffsets[u];
        const uint32_t tSize = registers[off.Size];
        tex.Width = static_cast<uint16_t>(tSize & 0xFFFFU);
        tex.Height = static_cast<uint16_t>((tSize >> 16U) & 0xFFFFU);
        tex.PhysicalAddress = registers[off.Address];

        const uint32_t formatWrap = registers[off.FormatWrap];
        const uint32_t fmt = formatWrap & 0x0FU;
        tex.Format = fmt <= 13U ? static_cast<TextureFormat>(fmt) : TextureFormat::Unknown;
        tex.WrapS = static_cast<TextureWrap>((formatWrap >> 8U) & 0x3U);
        tex.WrapT = static_cast<TextureWrap>((formatWrap >> 12U) & 0x3U);

        const uint32_t filter = registers[off.Filter];
        tex.MagFilter = (filter & 1U) ? TextureFilter::Linear : TextureFilter::Nearest;
        tex.MinFilter = ((filter >> 1U) & 1U) ? TextureFilter::Linear : TextureFilter::Nearest;
        tex.MipLinear = ((filter >> 2U) & 1U) != 0U;
    }

    // -------------------------------------------------------------------------
    // 4. TEV State (6 Stages + Combiner Buffer)
    // -------------------------------------------------------------------------
    for (size_t s = 0; s < 6; ++s) {
        TevStage& stage = outState.Tev.Stages[s];
        const uint32_t base = kTevStageBases[s];

        const uint32_t sources = registers[base];
        stage.SourceRgb[0] = static_cast<TevSource>(sources & 0xFU);
        stage.SourceRgb[1] = static_cast<TevSource>((sources >> 4U) & 0xFU);
        stage.SourceRgb[2] = static_cast<TevSource>((sources >> 8U) & 0xFU);
        stage.SourceAlpha[0] = static_cast<TevSource>((sources >> 16U) & 0xFU);
        stage.SourceAlpha[1] = static_cast<TevSource>((sources >> 20U) & 0xFU);
        stage.SourceAlpha[2] = static_cast<TevSource>((sources >> 24U) & 0xFU);

        const uint32_t operands = registers[base + 1U];
        stage.OperandRgb[0] = static_cast<uint8_t>(operands & 0xFU);
        stage.OperandRgb[1] = static_cast<uint8_t>((operands >> 4U) & 0xFU);
        stage.OperandRgb[2] = static_cast<uint8_t>((operands >> 8U) & 0xFU);
        stage.OperandAlpha[0] = static_cast<uint8_t>((operands >> 12U) & 0xFU);
        stage.OperandAlpha[1] = static_cast<uint8_t>((operands >> 16U) & 0xFU);
        stage.OperandAlpha[2] = static_cast<uint8_t>((operands >> 20U) & 0xFU);

        const uint32_t ops = registers[base + 2U];
        const uint32_t opRgb = ops & 0xFU;
        const uint32_t opAlpha = (ops >> 16U) & 0xFU;
        stage.OpRgb = opRgb <= 9U ? static_cast<TevOp>(opRgb) : TevOp::Replace;
        stage.OpAlpha = opAlpha <= 9U ? static_cast<TevOp>(opAlpha) : TevOp::Replace;

        stage.ConstantColor = registers[base + 3U];

        const uint32_t scales = registers[base + 4U];
        stage.ScaleRgb = static_cast<TevScale>(scales & 0x3U);
        stage.ScaleAlpha = static_cast<TevScale>((scales >> 16U) & 0x3U);
    }

    const uint32_t combBuf = registers[0x0FDU];
    outState.Tev.CombinerBufferInputMask = static_cast<uint8_t>(combBuf & 0x3FU);
    outState.Tev.CombinerBufferOutputMask = static_cast<uint8_t>((combBuf >> 8U) & 0x3FU);

    // -------------------------------------------------------------------------
    // 5. Fragment Lighting and Fog
    // -------------------------------------------------------------------------
    outState.Lighting.Enabled = (registers[0x08FU] & 1U) != 0U;
    if (outState.Lighting.Enabled) {
        outState.Lighting.ActiveLightCount = static_cast<uint8_t>((registers[0x1C2U] & 0x7U) + 1U);
        const uint32_t envCfg = registers[0x1C0U];
        outState.Lighting.ConfigIndex = static_cast<uint8_t>(envCfg & 0x7U);
        outState.Lighting.FresnelSelector = static_cast<uint8_t>((envCfg >> 4U) & 0x3U);
        outState.Lighting.BumpTextureUnit = static_cast<uint8_t>((envCfg >> 8U) & 0x3U);
    }

    const uint32_t fogModeRaw = registers[0x0E0U] & 0x7U;
    outState.Fog.Mode = static_cast<uint8_t>(fogModeRaw);
    outState.Fog.Enabled = (fogModeRaw == 5U);
    outState.Fog.Flip = ((registers[0x0E0U] >> 16U) & 1U) != 0U;
    outState.Fog.Color = registers[0x0E1U];

    // -------------------------------------------------------------------------
    // 6. Framebuffer, Depth, Stencil, Blend, Logic Op
    // -------------------------------------------------------------------------
    outState.Framebuffer.ColorAddress = registers[0x110U];
    outState.Framebuffer.DepthAddress = registers[0x111U];
    const uint32_t fbDim = registers[0x112U];
    outState.Framebuffer.Width = static_cast<uint16_t>(fbDim & 0xFFFFU);
    outState.Framebuffer.Height = static_cast<uint16_t>((fbDim >> 16U) & 0xFFFFU);
    const uint32_t fbFmt = registers[0x117U];
    outState.Framebuffer.ColorFormat = static_cast<uint8_t>(fbFmt & 0x7U);
    outState.Framebuffer.DepthFormat = static_cast<uint8_t>((fbFmt >> 8U) & 0x7U);

    // Depth & Stencil
    const uint32_t depthReg = registers[0x107U];
    outState.DepthStencil.DepthTest = (depthReg & 1U) != 0U;
    outState.DepthStencil.DepthCompare = static_cast<CompareFunc>((depthReg >> 4U) & 0x7U);
    outState.DepthStencil.DepthWrite = ((depthReg >> 12U) & 1U) != 0U;

    const uint32_t stencilTestReg = registers[0x105U];
    outState.DepthStencil.StencilTest = (stencilTestReg & 1U) != 0U;
    outState.DepthStencil.StencilCompare = static_cast<CompareFunc>((stencilTestReg >> 4U) & 0x7U);
    outState.DepthStencil.StencilWriteMask = static_cast<uint8_t>((stencilTestReg >> 8U) & 0xFFU);
    outState.DepthStencil.StencilRef = static_cast<uint8_t>((stencilTestReg >> 16U) & 0xFFU);
    outState.DepthStencil.StencilCompareMask = static_cast<uint8_t>((stencilTestReg >> 24U) & 0xFFU);

    const uint32_t stencilOpReg = registers[0x106U];
    outState.DepthStencil.StencilFail = static_cast<StencilOp>(stencilOpReg & 0x7U);
    outState.DepthStencil.StencilDepthFail = static_cast<StencilOp>((stencilOpReg >> 4U) & 0x7U);
    outState.DepthStencil.StencilPass = static_cast<StencilOp>((stencilOpReg >> 8U) & 0x7U);

    const uint32_t alphaTestReg = registers[0x104U];
    outState.DepthStencil.AlphaTest = (alphaTestReg & 1U) != 0U;
    outState.DepthStencil.AlphaCompare = static_cast<CompareFunc>((alphaTestReg >> 4U) & 0x7U);
    outState.DepthStencil.AlphaRef = static_cast<uint8_t>((alphaTestReg >> 8U) & 0xFFU);

    // Blend
    const uint32_t fragOp = registers[0x100U];
    outState.Blend.Enabled = ((fragOp >> 8U) & 1U) != 0U;
    if (outState.Blend.Enabled) {
        const uint32_t blendReg = registers[0x101U];
        outState.Blend.EquationRgb = static_cast<BlendEq>(blendReg & 0x7U);
        outState.Blend.EquationAlpha = static_cast<BlendEq>((blendReg >> 8U) & 0x7U);
        outState.Blend.SrcRgb = static_cast<BlendFactor>((blendReg >> 16U) & 0xFU);
        outState.Blend.DestRgb = static_cast<BlendFactor>((blendReg >> 20U) & 0xFU);
        outState.Blend.SrcAlpha = static_cast<BlendFactor>((blendReg >> 24U) & 0xFU);
        outState.Blend.DestAlpha = static_cast<BlendFactor>((blendReg >> 28U) & 0xFU);
        outState.Blend.ConstantColor = registers[0x102U];
    } else {
        outState.Blend.LogicOpEnabled = true;
        outState.Blend.LogicOperation = static_cast<LogicOp>(registers[0x103U] & 0xFU);
    }

    outState.StateHash = ComputePicaStateHash(outState);
    return true;
}

uint64_t ComputePicaStateHash(const PicaDrawState& state) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    hash = FnvHashValue(state.Topology, hash);
    hash = FnvHashValue(state.Culling, hash);

    // Raster & depth/stencil
    hash = FnvHashValue(state.DepthStencil.DepthTest, hash);
    hash = FnvHashValue(state.DepthStencil.DepthWrite, hash);
    hash = FnvHashValue(state.DepthStencil.DepthCompare, hash);
    hash = FnvHashValue(state.DepthStencil.AlphaTest, hash);
    hash = FnvHashValue(state.DepthStencil.AlphaCompare, hash);
    hash = FnvHashValue(state.DepthStencil.AlphaRef, hash);
    hash = FnvHashValue(state.DepthStencil.StencilTest, hash);
    if (state.DepthStencil.StencilTest) {
        hash = FnvHashValue(state.DepthStencil.StencilCompare, hash);
        hash = FnvHashValue(state.DepthStencil.StencilCompareMask, hash);
        hash = FnvHashValue(state.DepthStencil.StencilWriteMask, hash);
        hash = FnvHashValue(state.DepthStencil.StencilFail, hash);
        hash = FnvHashValue(state.DepthStencil.StencilDepthFail, hash);
        hash = FnvHashValue(state.DepthStencil.StencilPass, hash);
    }

    // Blend
    hash = FnvHashValue(state.Blend.Enabled, hash);
    if (state.Blend.Enabled) {
        hash = FnvHashValue(state.Blend.EquationRgb, hash);
        hash = FnvHashValue(state.Blend.EquationAlpha, hash);
        hash = FnvHashValue(state.Blend.SrcRgb, hash);
        hash = FnvHashValue(state.Blend.DestRgb, hash);
        hash = FnvHashValue(state.Blend.SrcAlpha, hash);
        hash = FnvHashValue(state.Blend.DestAlpha, hash);
    } else {
        hash = FnvHashValue(state.Blend.LogicOperation, hash);
    }

    // Textures
    for (const auto& tex : state.Textures) {
        hash = FnvHashValue(tex.Enabled, hash);
        if (tex.Enabled) {
            hash = FnvHashValue(tex.Format, hash);
            hash = FnvHashValue(tex.WrapS, hash);
            hash = FnvHashValue(tex.WrapT, hash);
            hash = FnvHashValue(tex.MinFilter, hash);
            hash = FnvHashValue(tex.MagFilter, hash);
        }
    }

    // TEV stages
    for (const auto& stage : state.Tev.Stages) {
        hash = FnvHashBytes(stage.SourceRgb, sizeof(stage.SourceRgb), hash);
        hash = FnvHashBytes(stage.SourceAlpha, sizeof(stage.SourceAlpha), hash);
        hash = FnvHashBytes(stage.OperandRgb, sizeof(stage.OperandRgb), hash);
        hash = FnvHashBytes(stage.OperandAlpha, sizeof(stage.OperandAlpha), hash);
        hash = FnvHashValue(stage.OpRgb, hash);
        hash = FnvHashValue(stage.OpAlpha, hash);
        hash = FnvHashValue(stage.ScaleRgb, hash);
        hash = FnvHashValue(stage.ScaleAlpha, hash);
        hash = FnvHashValue(stage.ConstantColor, hash);
    }
    hash = FnvHashValue(state.Tev.CombinerBufferInputMask, hash);
    hash = FnvHashValue(state.Tev.CombinerBufferOutputMask, hash);

    // Lighting & Fog
    hash = FnvHashValue(state.Lighting.Enabled, hash);
    if (state.Lighting.Enabled) {
        hash = FnvHashValue(state.Lighting.ActiveLightCount, hash);
        hash = FnvHashValue(state.Lighting.ConfigIndex, hash);
        hash = FnvHashValue(state.Lighting.FresnelSelector, hash);
        hash = FnvHashValue(state.Lighting.BumpTextureUnit, hash);
    }
    hash = FnvHashValue(state.Fog.Enabled, hash);
    hash = FnvHashValue(state.Fog.Mode, hash);
    hash = FnvHashValue(state.Fog.Color, hash);

    return hash;
}

std::string DumpReadablePicaState(const PicaDrawState& state) {
    std::ostringstream ss;
    ss << "=== PICA DRAW STATE (Hash: 0x" << std::hex << std::setw(16) << std::setfill('0')
       << state.StateHash << ") ===\n" << std::dec;
    ss << "Topology: " << static_cast<int>(state.Topology) << " | Culling: " << static_cast<int>(state.Culling) << "\n";
    ss << "DepthTest: " << (state.DepthStencil.DepthTest ? "ON" : "OFF")
       << " | DepthWrite: " << (state.DepthStencil.DepthWrite ? "ON" : "OFF")
       << " | AlphaTest: " << (state.DepthStencil.AlphaTest ? "ON" : "OFF") << "\n";
    ss << "Blend: " << (state.Blend.Enabled ? "ON" : "OFF") << " (Src=" << static_cast<int>(state.Blend.SrcRgb)
       << ", Dst=" << static_cast<int>(state.Blend.DestRgb) << ")\n";
    ss << "Lighting: " << (state.Lighting.Enabled ? "ON" : "OFF")
       << " (" << static_cast<int>(state.Lighting.ActiveLightCount) << " lights, Cfg="
       << static_cast<int>(state.Lighting.ConfigIndex) << ")\n";
    ss << "Fog: " << (state.Fog.Enabled ? "ON" : "OFF") << " (Mode=" << static_cast<int>(state.Fog.Mode) << ")\n";

    for (size_t s = 0; s < 6; ++s) {
        const auto& stage = state.Tev.Stages[s];
        ss << "  TEV[" << s << "]: OpRGB=" << static_cast<int>(stage.OpRgb)
           << " OpA=" << static_cast<int>(stage.OpAlpha)
           << " SrcRGB={" << static_cast<int>(stage.SourceRgb[0]) << ","
           << static_cast<int>(stage.SourceRgb[1]) << "," << static_cast<int>(stage.SourceRgb[2]) << "}\n";
    }

    return ss.str();
}

std::string PicaDrawState::DumpReadable() const {
    return DumpReadablePicaState(*this);
}

} // namespace ThreeDsRecomp::Pica
