#include "three_ds_recomp/pica/pica_render_pass_scheduler.h"

#include <algorithm>
#include <map>

namespace ThreeDsRecomp::Pica {

void PicaRenderPassScheduler::QueueUpload(uint32_t address, size_t sizeBytes, bool isTexture) {
    PendingUpload upload;
    upload.DestinationAddress = address;
    upload.SizeBytes = sizeBytes;
    upload.IsTexture = isTexture;
    mPendingUploads.push_back(upload);
    mMetrics.UploadsBatchedBeforePass++;
}

void PicaRenderPassScheduler::RecordDraw(const PicaDrawState& state, uint32_t drawId) {
    ScheduledDrawCommand cmd;
    cmd.DrawId = drawId ? drawId : static_cast<uint32_t>(mRecordedDraws.size() + 1);
    cmd.State = state;

    // Detect if this draw requires reading from the active color attachment (local feedback)
    for (const auto& tex : state.Textures) {
        if (tex.Enabled && tex.PhysicalAddress != 0 && tex.PhysicalAddress == state.Framebuffer.ColorAddress) {
            cmd.ReadsCurrentRenderTarget = true;
            break;
        }
    }

    mRecordedDraws.push_back(cmd);
    mMetrics.TotalDrawCalls++;
}

void PicaRenderPassScheduler::FlushFrame(
    std::vector<PendingUpload>& outUploads,
    std::vector<ScheduledRenderPass>& outPasses) {
    // 1. Move pending transfers so they execute strictly BEFORE any render pass starts (Rule R17, Phase 6.4)
    outUploads = std::move(mPendingUploads);
    mPendingUploads.clear();

    if (mRecordedDraws.empty()) {
        return;
    }

    // Track targets seen in the frame to configure LOAD_OP_CLEAR vs LOAD_OP_LOAD
    std::map<RenderTargetSignature, size_t> seenTargets;

    ScheduledRenderPass currentPass;
    bool hasActivePass = false;

    auto finishPass = [&]() {
        if (!hasActivePass) return;
        mMetrics.TotalRenderPassesCreated++;

        const uint32_t width = currentPass.Target.Width > 0 ? currentPass.Target.Width : 400U;
        const uint32_t height = currentPass.Target.Height > 0 ? currentPass.Target.Height : 240U;
        const size_t surfaceBytes = static_cast<size_t>(width) * height * 4U;

        // Depth store avoided check
        if (currentPass.DepthStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE) {
            mMetrics.DepthStoresAvoided++;
            mMetrics.EstimatedDramBandwidthSavedBytes += surfaceBytes;
        }
        if (currentPass.StencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE) {
            mMetrics.StencilStoresAvoided++;
        }

        // Color load/store bandwidth savings from draw consolidation
        const size_t drawCount = currentPass.TotalDrawCount();
        if (drawCount > 1) {
            const size_t avoidedLoadsStores = drawCount - 1;
            mMetrics.ColorLoadsAvoided += avoidedLoadsStores;
            mMetrics.EstimatedDramBandwidthSavedBytes += (avoidedLoadsStores * surfaceBytes * 2U);
        }

        outPasses.push_back(std::move(currentPass));
        hasActivePass = false;
    };

    for (const auto& cmd : mRecordedDraws) {
        RenderTargetSignature sig;
        sig.ColorAddress = cmd.State.Framebuffer.ColorAddress;
        sig.DepthAddress = cmd.State.Framebuffer.DepthAddress;
        sig.Width = cmd.State.Framebuffer.Width;
        sig.Height = cmd.State.Framebuffer.Height;
        sig.ColorFormat = cmd.State.Framebuffer.ColorFormat;
        sig.DepthFormat = cmd.State.Framebuffer.DepthFormat;

        if (!hasActivePass || !(currentPass.Target == sig)) {
            finishPass();

            currentPass = ScheduledRenderPass{};
            currentPass.PassId = static_cast<uint32_t>(outPasses.size() + 1);
            currentPass.Target = sig;

            // Determine tile load op
            if (seenTargets.find(sig) == seenTargets.end()) {
                currentPass.ColorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                seenTargets[sig] = 1;
            } else {
                currentPass.ColorLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            }
            currentPass.ColorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

            // Adreno depth optimization: DONT_CARE by default to avoid GMEM depth spills
            currentPass.DepthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            currentPass.DepthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            currentPass.StencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            currentPass.StencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

            ScheduledSubpass subpass0;
            subpass0.SubpassIndex = 0;
            subpass0.UsesInputAttachment = false;
            currentPass.Subpasses.push_back(subpass0);
            hasActivePass = true;
        } else {
            mMetrics.DrawCallsGrouped++;
        }

        // Check if a new subpass is needed for local tile read
        if (cmd.ReadsCurrentRenderTarget) {
            auto& activeSubpass = currentPass.Subpasses.back();
            if (!activeSubpass.Draws.empty() && !activeSubpass.UsesInputAttachment) {
                // Split into next subpass with input attachment
                ScheduledSubpass nextSubpass;
                nextSubpass.SubpassIndex = static_cast<uint32_t>(currentPass.Subpasses.size());
                nextSubpass.UsesInputAttachment = true;
                currentPass.Subpasses.push_back(nextSubpass);
                mMetrics.SubpassesUtilized++;
            }
        }

        currentPass.Subpasses.back().Draws.push_back(cmd);
    }

    finishPass();
    mRecordedDraws.clear();
}

VkRenderPassCreateInfo PicaRenderPassScheduler::BuildVulkanRenderPassInfo(
    const ScheduledRenderPass& pass,
    std::vector<VkAttachmentDescription>& outAttachments,
    std::vector<VkSubpassDescription>& outSubpasses,
    std::vector<VkSubpassDependency>& outDependencies) noexcept {
    outAttachments.clear();
    outSubpasses.clear();
    outDependencies.clear();

    // Attachment 0: Color (RGBA8)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = pass.ColorLoadOp;
    colorAttachment.storeOp = pass.ColorStoreOp;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = pass.ColorLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD ?
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    outAttachments.push_back(colorAttachment);

    // Attachment 1: Depth/Stencil (D24S8)
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D24_UNORM_S8_UINT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = pass.DepthLoadOp;
    depthAttachment.storeOp = pass.DepthStoreOp;
    depthAttachment.stencilLoadOp = pass.StencilLoadOp;
    depthAttachment.stencilStoreOp = pass.StencilStoreOp;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    outAttachments.push_back(depthAttachment);

    // Static attachment references for subpasses
    static const VkAttachmentReference colorRef{ 0U, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    static const VkAttachmentReference depthRef{ 1U, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    static const VkAttachmentReference inputRef{ 0U, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    for (size_t i = 0; i < pass.Subpasses.size(); ++i) {
        const auto& sp = pass.Subpasses[i];
        VkSubpassDescription desc{};
        desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        desc.colorAttachmentCount = 1;
        desc.pColorAttachments = &colorRef;
        desc.pDepthStencilAttachment = &depthRef;

        if (sp.UsesInputAttachment) {
            desc.inputAttachmentCount = 1;
            desc.pInputAttachments = &inputRef;
        }

        outSubpasses.push_back(desc);

        // Add subpass dependency if moving between subpasses
        if (i > 0) {
            VkSubpassDependency dep{};
            dep.srcSubpass = static_cast<uint32_t>(i - 1);
            dep.dstSubpass = static_cast<uint32_t>(i);
            dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
            dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT; // Crucial for Adreno tile-memory efficiency!
            outDependencies.push_back(dep);
        }
    }

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = static_cast<uint32_t>(outAttachments.size());
    ci.pAttachments = outAttachments.data();
    ci.subpassCount = static_cast<uint32_t>(outSubpasses.size());
    ci.pSubpasses = outSubpasses.data();
    ci.dependencyCount = static_cast<uint32_t>(outDependencies.size());
    ci.pDependencies = outDependencies.data();

    return ci;
}

} // namespace ThreeDsRecomp::Pica
