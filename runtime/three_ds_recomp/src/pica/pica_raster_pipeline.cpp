#include "three_ds_recomp/pica/pica_raster_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ThreeDsRecomp::Pica {

// =============================================================================
// Translation Functions
// =============================================================================

VkCompareOp TranslateCompareOp(CompareFunc func) noexcept {
    switch (func) {
    case CompareFunc::Never:          return VK_COMPARE_OP_NEVER;
    case CompareFunc::Always:         return VK_COMPARE_OP_ALWAYS;
    case CompareFunc::Equal:          return VK_COMPARE_OP_EQUAL;
    case CompareFunc::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
    case CompareFunc::Less:           return VK_COMPARE_OP_LESS;
    case CompareFunc::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareFunc::Greater:        return VK_COMPARE_OP_GREATER;
    case CompareFunc::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    default:                          return VK_COMPARE_OP_ALWAYS;
    }
}

VkStencilOp TranslateStencilOp(StencilOp op) noexcept {
    switch (op) {
    case StencilOp::Keep:          return VK_STENCIL_OP_KEEP;
    case StencilOp::Zero:          return VK_STENCIL_OP_ZERO;
    case StencilOp::Replace:       return VK_STENCIL_OP_REPLACE;
    case StencilOp::Increment:     return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOp::Decrement:     return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOp::Invert:        return VK_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOp::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:                       return VK_STENCIL_OP_KEEP;
    }
}

VkBlendOp TranslateBlendOp(BlendEq eq) noexcept {
    switch (eq) {
    case BlendEq::Add:             return VK_BLEND_OP_ADD;
    case BlendEq::Subtract:        return VK_BLEND_OP_SUBTRACT;
    case BlendEq::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendEq::Min:             return VK_BLEND_OP_MIN;
    case BlendEq::Max:             return VK_BLEND_OP_MAX;
    default:                       return VK_BLEND_OP_ADD;
    }
}

VkBlendFactor TranslateBlendFactor(BlendFactor factor) noexcept {
    switch (factor) {
    case BlendFactor::Zero:                  return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:                   return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SourceColor:           return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSourceColor:   return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DestColor:             return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDestColor:     return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SourceAlpha:           return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:   return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestAlpha:             return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDestAlpha:     return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::ConstantColor:         return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case BlendFactor::ConstantAlpha:         return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    case BlendFactor::SourceAlphaSaturate:   return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default:                                 return VK_BLEND_FACTOR_ONE;
    }
}

VkLogicOp TranslateLogicOp(LogicOp op) noexcept {
    switch (op) {
    case LogicOp::Clear:        return VK_LOGIC_OP_CLEAR;
    case LogicOp::And:          return VK_LOGIC_OP_AND;
    case LogicOp::AndReverse:   return VK_LOGIC_OP_AND_REVERSE;
    case LogicOp::Copy:         return VK_LOGIC_OP_COPY;
    case LogicOp::Set:          return VK_LOGIC_OP_SET;
    case LogicOp::CopyInverted: return VK_LOGIC_OP_COPY_INVERTED;
    case LogicOp::NoOp:         return VK_LOGIC_OP_NO_OP;
    case LogicOp::Invert:       return VK_LOGIC_OP_INVERT;
    case LogicOp::Nand:         return VK_LOGIC_OP_NAND;
    case LogicOp::Or:           return VK_LOGIC_OP_OR;
    case LogicOp::Nor:          return VK_LOGIC_OP_NOR;
    case LogicOp::Xor:          return VK_LOGIC_OP_XOR;
    case LogicOp::Equivalent:   return VK_LOGIC_OP_EQUIVALENT;
    case LogicOp::AndInverted:  return VK_LOGIC_OP_AND_INVERTED;
    case LogicOp::OrReverse:    return VK_LOGIC_OP_OR_REVERSE;
    case LogicOp::OrInverted:   return VK_LOGIC_OP_OR_INVERTED;
    default:                    return VK_LOGIC_OP_COPY;
    }
}

VkCullModeFlags TranslateCullMode(CullMode cull) noexcept {
    switch (cull) {
    case CullMode::KeepAll:              return VK_CULL_MODE_NONE;
    case CullMode::KeepClockwise:        return VK_CULL_MODE_BACK_BIT;
    case CullMode::KeepCounterClockwise: return VK_CULL_MODE_FRONT_BIT;
    default:                             return VK_CULL_MODE_NONE;
    }
}

VkFrontFace TranslateFrontFace(CullMode /*cull*/) noexcept {
    return VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

VkViewport TranslateViewport(
    const ViewportState& vp,
    uint32_t fbWidth,
    uint32_t fbHeight,
    bool flipY) noexcept {
    VkViewport out{};
    if (vp.HalfWidth <= 0.0f || vp.HalfHeight <= 0.0f) {
        out.x = 0.0f;
        out.y = flipY ? static_cast<float>(fbHeight) : 0.0f;
        out.width = static_cast<float>(fbWidth);
        out.height = flipY ? -static_cast<float>(fbHeight) : static_cast<float>(fbHeight);
        out.minDepth = 0.0f;
        out.maxDepth = 1.0f;
        return out;
    }

    const float width = 2.0f * vp.HalfWidth;
    const float height = 2.0f * vp.HalfHeight;
    const float x = static_cast<float>(vp.CornerX) - vp.HalfWidth;
    const float y = static_cast<float>(vp.CornerY) - vp.HalfHeight;

    out.x = x;
    if (flipY) {
        out.y = y + height;
        out.height = -height;
    } else {
        out.y = y;
        out.height = height;
    }
    out.width = width;
    out.minDepth = std::clamp(vp.NearPlane, 0.0f, 1.0f);
    out.maxDepth = std::clamp(vp.NearPlane + vp.DepthRange, 0.0f, 1.0f);
    return out;
}

VkRect2D TranslateScissor(
    const ScissorState& sc,
    uint32_t fbWidth,
    uint32_t fbHeight) noexcept {
    VkRect2D out{};
    if (sc.Mode == 0) {
        // Disabled: full framebuffer extent
        out.offset = { 0, 0 };
        out.extent = { fbWidth, fbHeight };
        return out;
    }

    const uint32_t x1 = std::min(static_cast<uint32_t>(sc.X1), fbWidth);
    const uint32_t y1 = std::min(static_cast<uint32_t>(sc.Y1), fbHeight);
    const uint32_t x2 = std::clamp(static_cast<uint32_t>(sc.X2), x1, fbWidth);
    const uint32_t y2 = std::clamp(static_cast<uint32_t>(sc.Y2), y1, fbHeight);

    out.offset = { static_cast<int32_t>(x1), static_cast<int32_t>(y1) };
    out.extent = { x2 - x1, y2 - y1 };
    return out;
}

VkPipelineRasterizationStateCreateInfo CreateRasterizationState(
    CullMode cull,
    const PolygonOffsetState& offset) noexcept {
    VkPipelineRasterizationStateCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    ci.depthClampEnable = VK_FALSE;
    ci.rasterizerDiscardEnable = VK_FALSE;
    ci.polygonMode = VK_POLYGON_MODE_FILL;
    ci.cullMode = TranslateCullMode(cull);
    ci.frontFace = TranslateFrontFace(cull);
    ci.depthBiasEnable = offset.Enabled ? VK_TRUE : VK_FALSE;
    ci.depthBiasConstantFactor = offset.Unit;
    ci.depthBiasClamp = offset.Clamp;
    ci.depthBiasSlopeFactor = offset.Factor;
    ci.lineWidth = 1.0f;
    return ci;
}

VkPipelineDepthStencilStateCreateInfo CreateDepthStencilState(
    const DepthStencilState& ds) noexcept {
    VkPipelineDepthStencilStateCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ci.depthTestEnable = ds.DepthTest ? VK_TRUE : VK_FALSE;
    ci.depthWriteEnable = ds.DepthWrite ? VK_TRUE : VK_FALSE;
    ci.depthCompareOp = TranslateCompareOp(ds.DepthCompare);
    ci.depthBoundsTestEnable = VK_FALSE;
    ci.stencilTestEnable = ds.StencilTest ? VK_TRUE : VK_FALSE;

    ci.front.failOp = TranslateStencilOp(ds.StencilFail);
    ci.front.passOp = TranslateStencilOp(ds.StencilPass);
    ci.front.depthFailOp = TranslateStencilOp(ds.StencilDepthFail);
    ci.front.compareOp = TranslateCompareOp(ds.StencilCompare);
    ci.front.compareMask = ds.StencilCompareMask;
    ci.front.writeMask = ds.StencilWriteMask;
    ci.front.reference = ds.StencilRef;

    ci.back = ci.front;
    return ci;
}

VkPipelineColorBlendAttachmentState CreateColorBlendAttachment(
    const BlendState& blend,
    uint8_t colorWriteMask) noexcept {
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = blend.Enabled ? VK_TRUE : VK_FALSE;

    att.srcColorBlendFactor = TranslateBlendFactor(blend.SrcRgb);
    att.dstColorBlendFactor = TranslateBlendFactor(blend.DestRgb);
    att.colorBlendOp = TranslateBlendOp(blend.EquationRgb);

    att.srcAlphaBlendFactor = TranslateBlendFactor(blend.SrcAlpha);
    att.dstAlphaBlendFactor = TranslateBlendFactor(blend.DestAlpha);
    att.alphaBlendOp = TranslateBlendOp(blend.EquationAlpha);

    VkColorComponentFlags mask = 0;
    if (colorWriteMask & 1U) mask |= VK_COLOR_COMPONENT_R_BIT;
    if (colorWriteMask & 2U) mask |= VK_COLOR_COMPONENT_G_BIT;
    if (colorWriteMask & 4U) mask |= VK_COLOR_COMPONENT_B_BIT;
    if (colorWriteMask & 8U) mask |= VK_COLOR_COMPONENT_A_BIT;
    att.colorWriteMask = mask;

    return att;
}

VkPipelineColorBlendStateCreateInfo CreateColorBlendState(
    const BlendState& blend,
    const VkPipelineColorBlendAttachmentState* attachment) noexcept {
    VkPipelineColorBlendStateCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ci.logicOpEnable = (!blend.Enabled && blend.LogicOpEnabled) ? VK_TRUE : VK_FALSE;
    ci.logicOp = TranslateLogicOp(blend.LogicOperation);
    ci.attachmentCount = attachment ? 1U : 0U;
    ci.pAttachments = attachment;

    ci.blendConstants[0] = static_cast<float>((blend.ConstantColor >> 16U) & 0xFFU) / 255.0f;
    ci.blendConstants[1] = static_cast<float>((blend.ConstantColor >> 8U) & 0xFFU) / 255.0f;
    ci.blendConstants[2] = static_cast<float>(blend.ConstantColor & 0xFFU) / 255.0f;
    ci.blendConstants[3] = static_cast<float>((blend.ConstantColor >> 24U) & 0xFFU) / 255.0f;

    return ci;
}

// =============================================================================
// Raster Oracle Simulation Engine
// =============================================================================

namespace {

inline bool EvaluateCompare(float a, float b, CompareFunc func) noexcept {
    constexpr float kEps = 1e-6f;
    switch (func) {
    case CompareFunc::Never:          return false;
    case CompareFunc::Always:         return true;
    case CompareFunc::Equal:          return std::fabs(a - b) <= kEps;
    case CompareFunc::NotEqual:       return std::fabs(a - b) > kEps;
    case CompareFunc::Less:           return a < b;
    case CompareFunc::LessOrEqual:    return a <= (b + kEps);
    case CompareFunc::Greater:        return a > b;
    case CompareFunc::GreaterOrEqual: return a >= (b - kEps);
    default:                          return true;
    }
}

inline bool EvaluateCompareInt(uint32_t a, uint32_t b, CompareFunc func) noexcept {
    switch (func) {
    case CompareFunc::Never:          return false;
    case CompareFunc::Always:         return true;
    case CompareFunc::Equal:          return a == b;
    case CompareFunc::NotEqual:       return a != b;
    case CompareFunc::Less:           return a < b;
    case CompareFunc::LessOrEqual:    return a <= b;
    case CompareFunc::Greater:        return a > b;
    case CompareFunc::GreaterOrEqual: return a >= b;
    default:                          return true;
    }
}

inline uint8_t ApplyStencilOp(uint8_t current, uint8_t ref, StencilOp op) noexcept {
    switch (op) {
    case StencilOp::Keep:          return current;
    case StencilOp::Zero:          return 0;
    case StencilOp::Replace:       return ref;
    case StencilOp::Increment:     return current < 255 ? static_cast<uint8_t>(current + 1) : 255;
    case StencilOp::Decrement:     return current > 0 ? static_cast<uint8_t>(current - 1) : 0;
    case StencilOp::Invert:        return static_cast<uint8_t>(~current);
    case StencilOp::IncrementWrap: return static_cast<uint8_t>(current + 1);
    case StencilOp::DecrementWrap: return static_cast<uint8_t>(current - 1);
    default:                       return current;
    }
}

inline std::array<float, 3> EvaluateBlendFactors(
    BlendFactor factor,
    const std::array<float, 4>& src,
    const std::array<float, 4>& dst,
    const std::array<float, 4>& constant) noexcept {
    switch (factor) {
    case BlendFactor::Zero:
        return { 0.0f, 0.0f, 0.0f };
    case BlendFactor::One:
        return { 1.0f, 1.0f, 1.0f };
    case BlendFactor::SourceColor:
        return { src[0], src[1], src[2] };
    case BlendFactor::OneMinusSourceColor:
        return { 1.0f - src[0], 1.0f - src[1], 1.0f - src[2] };
    case BlendFactor::DestColor:
        return { dst[0], dst[1], dst[2] };
    case BlendFactor::OneMinusDestColor:
        return { 1.0f - dst[0], 1.0f - dst[1], 1.0f - dst[2] };
    case BlendFactor::SourceAlpha:
        return { src[3], src[3], src[3] };
    case BlendFactor::OneMinusSourceAlpha:
        return { 1.0f - src[3], 1.0f - src[3], 1.0f - src[3] };
    case BlendFactor::DestAlpha:
        return { dst[3], dst[3], dst[3] };
    case BlendFactor::OneMinusDestAlpha:
        return { 1.0f - dst[3], 1.0f - dst[3], 1.0f - dst[3] };
    case BlendFactor::ConstantColor:
        return { constant[0], constant[1], constant[2] };
    case BlendFactor::OneMinusConstantColor:
        return { 1.0f - constant[0], 1.0f - constant[1], 1.0f - constant[2] };
    case BlendFactor::ConstantAlpha:
        return { constant[3], constant[3], constant[3] };
    case BlendFactor::OneMinusConstantAlpha:
        return { 1.0f - constant[3], 1.0f - constant[3], 1.0f - constant[3] };
    case BlendFactor::SourceAlphaSaturate: {
        const float f = std::min(src[3], 1.0f - dst[3]);
        return { f, f, f };
    }
    default:
        return { 1.0f, 1.0f, 1.0f };
    }
}

inline float EvaluateAlphaFactor(
    BlendFactor factor,
    const std::array<float, 4>& src,
    const std::array<float, 4>& dst,
    const std::array<float, 4>& constant) noexcept {
    switch (factor) {
    case BlendFactor::Zero:                  return 0.0f;
    case BlendFactor::One:                   return 1.0f;
    case BlendFactor::SourceColor:
    case BlendFactor::SourceAlpha:           return src[3];
    case BlendFactor::OneMinusSourceColor:
    case BlendFactor::OneMinusSourceAlpha:   return 1.0f - src[3];
    case BlendFactor::DestColor:
    case BlendFactor::DestAlpha:             return dst[3];
    case BlendFactor::OneMinusDestColor:
    case BlendFactor::OneMinusDestAlpha:     return 1.0f - dst[3];
    case BlendFactor::ConstantColor:
    case BlendFactor::ConstantAlpha:         return constant[3];
    case BlendFactor::OneMinusConstantColor:
    case BlendFactor::OneMinusConstantAlpha: return 1.0f - constant[3];
    case BlendFactor::SourceAlphaSaturate:   return 1.0f;
    default:                                 return 1.0f;
    }
}

inline float EvaluateBlendEquation(float s, float sf, float d, float df, BlendEq eq) noexcept {
    switch (eq) {
    case BlendEq::Add:             return std::clamp(s * sf + d * df, 0.0f, 1.0f);
    case BlendEq::Subtract:        return std::clamp(s * sf - d * df, 0.0f, 1.0f);
    case BlendEq::ReverseSubtract: return std::clamp(d * df - s * sf, 0.0f, 1.0f);
    case BlendEq::Min:             return std::min(s, d);
    case BlendEq::Max:             return std::max(s, d);
    default:                       return std::clamp(s * sf + d * df, 0.0f, 1.0f);
    }
}

inline uint8_t EvaluateLogicOp(uint8_t s, uint8_t d, LogicOp op) noexcept {
    switch (op) {
    case LogicOp::Clear:        return 0x00;
    case LogicOp::And:          return static_cast<uint8_t>(s & d);
    case LogicOp::AndReverse:   return static_cast<uint8_t>(s & ~d);
    case LogicOp::Copy:         return s;
    case LogicOp::Set:          return 0xFF;
    case LogicOp::CopyInverted: return static_cast<uint8_t>(~s);
    case LogicOp::NoOp:         return d;
    case LogicOp::Invert:       return static_cast<uint8_t>(~d);
    case LogicOp::Nand:         return static_cast<uint8_t>(~(s & d));
    case LogicOp::Or:           return static_cast<uint8_t>(s | d);
    case LogicOp::Nor:          return static_cast<uint8_t>(~(s | d));
    case LogicOp::Xor:          return static_cast<uint8_t>(s ^ d);
    case LogicOp::Equivalent:   return static_cast<uint8_t>(~(s ^ d));
    case LogicOp::AndInverted:  return static_cast<uint8_t>(~s & d);
    case LogicOp::OrReverse:    return static_cast<uint8_t>(s | ~d);
    case LogicOp::OrInverted:   return static_cast<uint8_t>(~s | d);
    default:                    return s;
    }
}

} // namespace

PicaRasterOracle::ExecutionResult PicaRasterOracle::ProcessFragment(
    const FragmentInput& frag,
    const PicaDrawState& state,
    RasterSurface& surface) noexcept {
    ExecutionResult res{};

    // 1. Scissor Test
    if (state.Scissor.Mode != 0) {
        if (frag.X < state.Scissor.X1 || frag.X >= state.Scissor.X2 ||
            frag.Y < state.Scissor.Y1 || frag.Y >= state.Scissor.Y2) {
            res.PassedScissor = false;
            return res;
        }
    }

    if (frag.X < 0 || static_cast<uint32_t>(frag.X) >= surface.Width ||
        frag.Y < 0 || static_cast<uint32_t>(frag.Y) >= surface.Height) {
        res.PassedScissor = false;
        return res;
    }

    const uint32_t pixelIndex = static_cast<uint32_t>(frag.Y) * surface.Width + static_cast<uint32_t>(frag.X);

    // 2. Alpha Test (Discard on fail)
    if (state.DepthStencil.AlphaTest) {
        const float alphaRef = static_cast<float>(state.DepthStencil.AlphaRef) / 255.0f;
        if (!EvaluateCompare(frag.Color[3], alphaRef, state.DepthStencil.AlphaCompare)) {
            res.PassedAlpha = false;
            return res;
        }
    }

    // 3. Depth Bias (Polygon Offset)
    float biasedZ = frag.Z;
    if (state.PolygonOffset.Enabled) {
        constexpr float kR24 = 1.0f / 16777215.0f; // 24-bit depth resolution
        float bias = state.PolygonOffset.Factor * frag.Slope + state.PolygonOffset.Unit * kR24;
        if (state.PolygonOffset.Clamp > 0.0f) {
            bias = std::min(bias, state.PolygonOffset.Clamp);
        } else if (state.PolygonOffset.Clamp < 0.0f) {
            bias = std::max(bias, state.PolygonOffset.Clamp);
        }
        biasedZ = std::clamp(biasedZ + bias, 0.0f, 1.0f);
    }
    const uint32_t fragDepth24 = static_cast<uint32_t>(std::clamp(biasedZ * 16777215.0f + 0.5f, 0.0f, 16777215.0f));

    // Current Depth and Stencil values
    const uint32_t currentDS = surface.DepthStencilBuffer[pixelIndex];
    const uint32_t currentDepth24 = currentDS & 0x00FFFFFFU;
    const uint8_t currentStencil = static_cast<uint8_t>((currentDS >> 24U) & 0xFFU);

    // 4. Stencil Test
    bool stencilPassed = true;
    if (state.DepthStencil.StencilTest) {
        const uint8_t maskedRef = state.DepthStencil.StencilRef & state.DepthStencil.StencilCompareMask;
        const uint8_t maskedBuf = currentStencil & state.DepthStencil.StencilCompareMask;
        stencilPassed = EvaluateCompareInt(maskedRef, maskedBuf, state.DepthStencil.StencilCompare);
    }

    if (!stencilPassed) {
        res.PassedStencil = false;
        if (state.DepthStencil.StencilTest && state.DepthStencil.StencilWriteMask != 0) {
            const uint8_t newStencil = ApplyStencilOp(currentStencil, state.DepthStencil.StencilRef, state.DepthStencil.StencilFail);
            const uint8_t finalStencil = (currentStencil & ~state.DepthStencil.StencilWriteMask) | (newStencil & state.DepthStencil.StencilWriteMask);
            if (finalStencil != currentStencil) {
                surface.DepthStencilBuffer[pixelIndex] = (static_cast<uint32_t>(finalStencil) << 24U) | currentDepth24;
                res.WroteStencil = true;
            }
        }
        return res;
    }

    // 5. Depth Test
    bool depthPassed = true;
    if (state.DepthStencil.DepthTest) {
        depthPassed = EvaluateCompareInt(fragDepth24, currentDepth24, state.DepthStencil.DepthCompare);
    }

    if (!depthPassed) {
        res.PassedDepth = false;
        // Stencil Depth Fail Op
        if (state.DepthStencil.StencilTest && state.DepthStencil.StencilWriteMask != 0) {
            const uint8_t newStencil = ApplyStencilOp(currentStencil, state.DepthStencil.StencilRef, state.DepthStencil.StencilDepthFail);
            const uint8_t finalStencil = (currentStencil & ~state.DepthStencil.StencilWriteMask) | (newStencil & state.DepthStencil.StencilWriteMask);
            if (finalStencil != currentStencil) {
                surface.DepthStencilBuffer[pixelIndex] = (static_cast<uint32_t>(finalStencil) << 24U) | currentDepth24;
                res.WroteStencil = true;
            }
        }
        return res;
    }

    // Both Stencil and Depth Passed -> Apply Stencil Pass Op
    uint8_t finalStencil = currentStencil;
    if (state.DepthStencil.StencilTest && state.DepthStencil.StencilWriteMask != 0) {
        const uint8_t newStencil = ApplyStencilOp(currentStencil, state.DepthStencil.StencilRef, state.DepthStencil.StencilPass);
        finalStencil = (currentStencil & ~state.DepthStencil.StencilWriteMask) | (newStencil & state.DepthStencil.StencilWriteMask);
        if (finalStencil != currentStencil) {
            res.WroteStencil = true;
        }
    }

    // Depth Write
    uint32_t finalDepth24 = currentDepth24;
    if (state.DepthStencil.DepthWrite) {
        finalDepth24 = fragDepth24;
        res.WroteDepth = true;
    }
    surface.DepthStencilBuffer[pixelIndex] = (static_cast<uint32_t>(finalStencil) << 24U) | finalDepth24;

    // 6. Color Blending & Logic Op
    const uint32_t currentPixel = surface.ColorBuffer[pixelIndex];
    const std::array<float, 4> dstColor{
        static_cast<float>(currentPixel & 0xFFU) / 255.0f,
        static_cast<float>((currentPixel >> 8U) & 0xFFU) / 255.0f,
        static_cast<float>((currentPixel >> 16U) & 0xFFU) / 255.0f,
        static_cast<float>((currentPixel >> 24U) & 0xFFU) / 255.0f
    };

    const std::array<float, 4> constColor{
        static_cast<float>(state.Blend.ConstantColor & 0xFFU) / 255.0f,
        static_cast<float>((state.Blend.ConstantColor >> 8U) & 0xFFU) / 255.0f,
        static_cast<float>((state.Blend.ConstantColor >> 16U) & 0xFFU) / 255.0f,
        static_cast<float>((state.Blend.ConstantColor >> 24U) & 0xFFU) / 255.0f
    };

    uint8_t outR = 0;
    uint8_t outG = 0;
    uint8_t outB = 0;
    uint8_t outA = 0;

    if (state.Blend.Enabled) {
        // Color Blending
        const auto srcFactor = EvaluateBlendFactors(state.Blend.SrcRgb, frag.Color, dstColor, constColor);
        const auto dstFactor = EvaluateBlendFactors(state.Blend.DestRgb, frag.Color, dstColor, constColor);

        const float resR = EvaluateBlendEquation(frag.Color[0], srcFactor[0], dstColor[0], dstFactor[0], state.Blend.EquationRgb);
        const float resG = EvaluateBlendEquation(frag.Color[1], srcFactor[1], dstColor[1], dstFactor[1], state.Blend.EquationRgb);
        const float resB = EvaluateBlendEquation(frag.Color[2], srcFactor[2], dstColor[2], dstFactor[2], state.Blend.EquationRgb);

        const float srcAFactor = EvaluateAlphaFactor(state.Blend.SrcAlpha, frag.Color, dstColor, constColor);
        const float dstAFactor = EvaluateAlphaFactor(state.Blend.DestAlpha, frag.Color, dstColor, constColor);
        const float resA = EvaluateBlendEquation(frag.Color[3], srcAFactor, dstColor[3], dstAFactor, state.Blend.EquationAlpha);

        outR = static_cast<uint8_t>(std::clamp(resR * 255.0f + 0.5f, 0.0f, 255.0f));
        outG = static_cast<uint8_t>(std::clamp(resG * 255.0f + 0.5f, 0.0f, 255.0f));
        outB = static_cast<uint8_t>(std::clamp(resB * 255.0f + 0.5f, 0.0f, 255.0f));
        outA = static_cast<uint8_t>(std::clamp(resA * 255.0f + 0.5f, 0.0f, 255.0f));
    } else if (state.Blend.LogicOpEnabled) {
        // Bitwise Logic Op
        const uint8_t sR = static_cast<uint8_t>(std::clamp(frag.Color[0] * 255.0f + 0.5f, 0.0f, 255.0f));
        const uint8_t sG = static_cast<uint8_t>(std::clamp(frag.Color[1] * 255.0f + 0.5f, 0.0f, 255.0f));
        const uint8_t sB = static_cast<uint8_t>(std::clamp(frag.Color[2] * 255.0f + 0.5f, 0.0f, 255.0f));
        const uint8_t sA = static_cast<uint8_t>(std::clamp(frag.Color[3] * 255.0f + 0.5f, 0.0f, 255.0f));

        const uint8_t dR = static_cast<uint8_t>(currentPixel & 0xFFU);
        const uint8_t dG = static_cast<uint8_t>((currentPixel >> 8U) & 0xFFU);
        const uint8_t dB = static_cast<uint8_t>((currentPixel >> 16U) & 0xFFU);
        const uint8_t dA = static_cast<uint8_t>((currentPixel >> 24U) & 0xFFU);

        outR = EvaluateLogicOp(sR, dR, state.Blend.LogicOperation);
        outG = EvaluateLogicOp(sG, dG, state.Blend.LogicOperation);
        outB = EvaluateLogicOp(sB, dB, state.Blend.LogicOperation);
        outA = EvaluateLogicOp(sA, dA, state.Blend.LogicOperation);
    } else {
        // Replace
        outR = static_cast<uint8_t>(std::clamp(frag.Color[0] * 255.0f + 0.5f, 0.0f, 255.0f));
        outG = static_cast<uint8_t>(std::clamp(frag.Color[1] * 255.0f + 0.5f, 0.0f, 255.0f));
        outB = static_cast<uint8_t>(std::clamp(frag.Color[2] * 255.0f + 0.5f, 0.0f, 255.0f));
        outA = static_cast<uint8_t>(std::clamp(frag.Color[3] * 255.0f + 0.5f, 0.0f, 255.0f));
    }

    // Apply Color Write Mask
    const uint8_t mask = state.Framebuffer.ColorWrite ? state.Framebuffer.ColorWriteMask : 0;
    const uint8_t finalR = (mask & 1U) ? outR : static_cast<uint8_t>(currentPixel & 0xFFU);
    const uint8_t finalG = (mask & 2U) ? outG : static_cast<uint8_t>((currentPixel >> 8U) & 0xFFU);
    const uint8_t finalB = (mask & 4U) ? outB : static_cast<uint8_t>((currentPixel >> 16U) & 0xFFU);
    const uint8_t finalA = (mask & 8U) ? outA : static_cast<uint8_t>((currentPixel >> 24U) & 0xFFU);

    surface.ColorBuffer[pixelIndex] = static_cast<uint32_t>(finalR) |
                                      (static_cast<uint32_t>(finalG) << 8U) |
                                      (static_cast<uint32_t>(finalB) << 16U) |
                                      (static_cast<uint32_t>(finalA) << 24U);
    res.WroteColor = (mask != 0);

    return res;
}

size_t PicaRasterOracle::RenderQuad(
    int x0, int y0, int x1, int y1,
    float z,
    const std::array<float, 4>& color,
    const PicaDrawState& state,
    RasterSurface& surface) noexcept {
    const int startX = std::max(0, std::min(x0, x1));
    const int endX = std::min(static_cast<int>(surface.Width), std::max(x0, x1));
    const int startY = std::max(0, std::min(y0, y1));
    const int endY = std::min(static_cast<int>(surface.Height), std::max(y0, y1));

    size_t writtenPixels = 0;
    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            FragmentInput frag;
            frag.X = x;
            frag.Y = y;
            frag.Z = z;
            frag.Slope = 0.0f;
            frag.Color = color;

            auto res = ProcessFragment(frag, state, surface);
            if (res.WroteColor || res.WroteDepth || res.WroteStencil) {
                writtenPixels++;
            }
        }
    }
    return writtenPixels;
}

} // namespace ThreeDsRecomp::Pica
