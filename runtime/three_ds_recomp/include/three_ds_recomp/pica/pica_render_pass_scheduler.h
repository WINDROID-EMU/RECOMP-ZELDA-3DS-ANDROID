#pragma once

#include "three_ds_recomp/pica/pica_state.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Pica {

/**
 * Signature representing a distinct hardware render target.
 */
struct RenderTargetSignature {
    uint32_t ColorAddress = 0;
    uint32_t DepthAddress = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t ColorFormat = 0;
    uint8_t DepthFormat = 0;

    [[nodiscard]] bool Matches(const FramebufferState& fb) const noexcept {
        return ColorAddress == fb.ColorAddress &&
               DepthAddress == fb.DepthAddress &&
               Width == fb.Width &&
               Height == fb.Height &&
               ColorFormat == fb.ColorFormat &&
               DepthFormat == fb.DepthFormat;
    }

    [[nodiscard]] bool operator==(const RenderTargetSignature& other) const noexcept = default;

    [[nodiscard]] bool operator<(const RenderTargetSignature& other) const noexcept {
        if (ColorAddress != other.ColorAddress) return ColorAddress < other.ColorAddress;
        if (DepthAddress != other.DepthAddress) return DepthAddress < other.DepthAddress;
        if (Width != other.Width) return Width < other.Width;
        if (Height != other.Height) return Height < other.Height;
        if (ColorFormat != other.ColorFormat) return ColorFormat < other.ColorFormat;
        return DepthFormat < other.DepthFormat;
    }
};

/**
 * Single draw command scheduled for submission.
 */
struct ScheduledDrawCommand {
    uint32_t DrawId = 0;
    PicaDrawState State;
    bool ReadsCurrentRenderTarget = false; // Identifies local pixel read for subpass input attachment
};

/**
 * Texture or buffer upload queued to execute strictly before render passes begin.
 */
struct PendingUpload {
    uint32_t DestinationAddress = 0;
    size_t SizeBytes = 0;
    bool IsTexture = true;
};

/**
 * Subpass containing one or more draw calls executed within on-chip tile memory.
 */
struct ScheduledSubpass {
    uint32_t SubpassIndex = 0;
    std::vector<ScheduledDrawCommand> Draws;
    bool UsesInputAttachment = false;
};

/**
 * Consolidated Vulkan Render Pass optimized for Qualcomm Adreno TBDR architecture.
 */
struct ScheduledRenderPass {
    uint32_t PassId = 0;
    RenderTargetSignature Target;

    // Adreno tile-memory (GMEM) load and store operations
    VkAttachmentLoadOp ColorLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp ColorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentLoadOp DepthLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp DepthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // Default DONT_CARE saves massive DRAM bandwidth
    VkAttachmentLoadOp StencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp StencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    std::vector<ScheduledSubpass> Subpasses;

    [[nodiscard]] size_t TotalDrawCount() const noexcept {
        size_t count = 0;
        for (const auto& sp : Subpasses) {
            count += sp.Draws.size();
        }
        return count;
    }
};

/**
 * Performance metrics validating Adreno GMEM efficiency and tile-friendly rendering (Rule R17, Quality Gate 6).
 */
struct GmemMetrics {
    size_t TotalDrawCalls = 0;
    size_t TotalRenderPassesCreated = 0;
    size_t DrawCallsGrouped = 0;
    size_t ColorLoadsAvoided = 0;
    size_t DepthStoresAvoided = 0;
    size_t StencilStoresAvoided = 0;
    size_t SubpassesUtilized = 0;
    size_t UploadsBatchedBeforePass = 0;
    size_t ExternalMidPassReadViolations = 0; // STRICT: MUST BE 0!
    size_t EstimatedDramBandwidthSavedBytes = 0;
};

/**
 * Render Pass Scheduler and Consolidator for Adreno TBDR.
 *
 * Implements Phase 6 requirements:
 * 1. Groups consecutive draw calls targeting the same render surface into single passes.
 * 2. Applies LOAD_OP_CLEAR/DONT_CARE and STORE_OP_DONT_CARE for transient depth/stencil.
 * 3. Restructures local framebuffer reads into subpass input attachments.
 * 4. Batches texture uploads strictly before vkCmdBeginRenderPass to prevent pipeline splits.
 * 5. Measures and enforces GMEM loads/stores without external mid-pass reads.
 */
class PicaRenderPassScheduler {
public:
    PicaRenderPassScheduler() = default;

    /**
     * Queues a texture or buffer transfer before render passes begin.
     */
    void QueueUpload(uint32_t address, size_t sizeBytes, bool isTexture = true);

    /**
     * Records a draw call with its decoded PICA state.
     */
    void RecordDraw(const PicaDrawState& state, uint32_t drawId = 0);

    /**
     * Flushes the recorded frame: separates pre-pass uploads and consolidates draws into passes.
     */
    void FlushFrame(
        std::vector<PendingUpload>& outUploads,
        std::vector<ScheduledRenderPass>& outPasses);

    /**
     * Builds VkRenderPassCreateInfo with attachments, subpasses, and memory dependencies.
     */
    static VkRenderPassCreateInfo BuildVulkanRenderPassInfo(
        const ScheduledRenderPass& pass,
        std::vector<VkAttachmentDescription>& outAttachments,
        std::vector<VkSubpassDescription>& outSubpasses,
        std::vector<VkSubpassDependency>& outDependencies) noexcept;

    [[nodiscard]] GmemMetrics GetMetrics() const noexcept { return mMetrics; }
    void ResetMetrics() noexcept { mMetrics = GmemMetrics{}; }

private:
    std::vector<PendingUpload> mPendingUploads;
    std::vector<ScheduledDrawCommand> mRecordedDraws;
    GmemMetrics mMetrics{};
};

} // namespace ThreeDsRecomp::Pica
