#pragma once

#include "three_ds_recomp/pica/pica_state.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Pica {

// =============================================================================
// Vulkan Translation Functions for PICA200 Raster, Depth, Stencil & Blend
// =============================================================================

/**
 * Translates PICA compare functions to Vulkan compare operations.
 */
VkCompareOp TranslateCompareOp(CompareFunc func) noexcept;

/**
 * Translates PICA stencil operations to Vulkan stencil operations.
 */
VkStencilOp TranslateStencilOp(StencilOp op) noexcept;

/**
 * Translates PICA blend equations to Vulkan blend operations.
 */
VkBlendOp TranslateBlendOp(BlendEq eq) noexcept;

/**
 * Translates PICA blend factors to Vulkan blend factors.
 */
VkBlendFactor TranslateBlendFactor(BlendFactor factor) noexcept;

/**
 * Translates PICA logic operations to Vulkan logic operations.
 */
VkLogicOp TranslateLogicOp(LogicOp op) noexcept;

/**
 * Translates PICA culling mode to Vulkan cull flags.
 */
VkCullModeFlags TranslateCullMode(CullMode cull) noexcept;

/**
 * Determines front face winding corresponding to PICA convention.
 */
VkFrontFace TranslateFrontFace(CullMode cull) noexcept;

/**
 * Translates PICA Viewport configuration to standard Vulkan VkViewport.
 * Handles Adreno / Vulkan Y-flip and depth range [0, 1].
 */
VkViewport TranslateViewport(
    const ViewportState& vp,
    uint32_t fbWidth,
    uint32_t fbHeight,
    bool flipY = true) noexcept;

/**
 * Translates PICA Scissor configuration to Vulkan VkRect2D.
 * Clamps coordinates against the target framebuffer dimensions.
 */
VkRect2D TranslateScissor(
    const ScissorState& sc,
    uint32_t fbWidth,
    uint32_t fbHeight) noexcept;

/**
 * Builds VkPipelineRasterizationStateCreateInfo from PICA state and polygon offset.
 */
VkPipelineRasterizationStateCreateInfo CreateRasterizationState(
    CullMode cull,
    const PolygonOffsetState& offset) noexcept;

/**
 * Builds VkPipelineDepthStencilStateCreateInfo from PICA DepthStencilState.
 */
VkPipelineDepthStencilStateCreateInfo CreateDepthStencilState(
    const DepthStencilState& ds) noexcept;

/**
 * Builds VkPipelineColorBlendAttachmentState from PICA BlendState and write mask.
 */
VkPipelineColorBlendAttachmentState CreateColorBlendAttachment(
    const BlendState& blend,
    uint8_t colorWriteMask = 0xFU) noexcept;

/**
 * Builds VkPipelineColorBlendStateCreateInfo from PICA BlendState.
 */
VkPipelineColorBlendStateCreateInfo CreateColorBlendState(
    const BlendState& blend,
    const VkPipelineColorBlendAttachmentState* attachment) noexcept;

// =============================================================================
// Pixel Surface Buffer & Oracle for Numerical Validation
// =============================================================================

/**
 * 2D Framebuffer surface buffer holding RGBA8 color and D24S8 depth-stencil.
 */
struct RasterSurface {
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::vector<uint32_t> ColorBuffer;        // RGBA8 (0xAABBGGRR in memory)
    std::vector<uint32_t> DepthStencilBuffer; // 24-bit depth [0..16777215], 8-bit stencil in top byte

    RasterSurface() = default;
    RasterSurface(uint32_t w, uint32_t h, uint32_t clearColor = 0x00000000U, uint32_t clearDepth24 = 0x00FFFFFFU, uint8_t clearStencil = 0)
        : Width(w), Height(h), ColorBuffer(w * h, clearColor), DepthStencilBuffer(w * h, (static_cast<uint32_t>(clearStencil) << 24U) | (clearDepth24 & 0x00FFFFFFU)) {}

    void Clear(uint32_t color = 0x00000000U, uint32_t depth24 = 0x00FFFFFFU, uint8_t stencil = 0) {
        std::fill(ColorBuffer.begin(), ColorBuffer.end(), color);
        std::fill(DepthStencilBuffer.begin(), DepthStencilBuffer.end(), (static_cast<uint32_t>(stencil) << 24U) | (depth24 & 0x00FFFFFFU));
    }

    [[nodiscard]] uint32_t GetColor(uint32_t x, uint32_t y) const {
        return ColorBuffer[y * Width + x];
    }
    [[nodiscard]] uint32_t GetDepth24(uint32_t x, uint32_t y) const {
        return DepthStencilBuffer[y * Width + x] & 0x00FFFFFFU;
    }
    [[nodiscard]] uint8_t GetStencil(uint32_t x, uint32_t y) const {
        return static_cast<uint8_t>((DepthStencilBuffer[y * Width + x] >> 24U) & 0xFFU);
    }
};

/**
 * Software rasterization and output-merger oracle.
 * Accurately models PICA200 hardware rasterizer, depth bias, alpha-test discard,
 * depth compare & write, stencil operations, blend equations and logic ops.
 */
class PicaRasterOracle {
public:
    struct FragmentInput {
        int X = 0;
        int Y = 0;
        float Z = 0.0f;           // Normalized depth in [0, 1]
        float Slope = 0.0f;       // Surface slope for slope-scaled depth bias
        std::array<float, 4> Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // RGBA in [0, 1]
    };

    struct ExecutionResult {
        bool PassedAlpha = true;
        bool PassedScissor = true;
        bool PassedStencil = true;
        bool PassedDepth = true;
        bool WroteColor = false;
        bool WroteDepth = false;
        bool WroteStencil = false;
    };

    /**
     * Executes the complete fragment test & blend pipeline on a single fragment.
     */
    static ExecutionResult ProcessFragment(
        const FragmentInput& frag,
        const PicaDrawState& state,
        RasterSurface& surface) noexcept;

    /**
     * Renders a solid or textured rectangle to the surface, simulating draws.
     */
    static size_t RenderQuad(
        int x0, int y0, int x1, int y1,
        float z,
        const std::array<float, 4>& color,
        const PicaDrawState& state,
        RasterSurface& surface) noexcept;
};

} // namespace ThreeDsRecomp::Pica
