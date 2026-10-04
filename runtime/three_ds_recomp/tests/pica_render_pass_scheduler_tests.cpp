#include "three_ds_recomp/pica/pica_render_pass_scheduler.h"

#include <cassert>
#include <iostream>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #cond << std::endl; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #a " != " #b \
                      << " (" << (a) << " != " << (b) << ")" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #a " == " #b \
                      << " (" << (a) << " == " << (b) << ")" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_GT(a, b) \
    do { \
        if ((a) <= (b)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #a " <= " #b \
                      << " (" << (a) << " <= " << (b) << ")" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

namespace {

using namespace ThreeDsRecomp::Pica;

void TestDrawCallBatchingAndConsolidation() {
    std::cout << "[TEST] 1. Draw Call Batching and Render Pass Consolidation..." << std::endl;

    PicaRenderPassScheduler scheduler;

    PicaDrawState state;
    state.Framebuffer.ColorAddress = 0x20000000U;
    state.Framebuffer.DepthAddress = 0x20040000U;
    state.Framebuffer.Width = 400;
    state.Framebuffer.Height = 240;
    state.Framebuffer.ColorFormat = 0; // RGBA8
    state.Framebuffer.DepthFormat = 2; // D24S8

    // Record 20 consecutive draws to the same target
    for (uint32_t i = 0; i < 20; ++i) {
        scheduler.RecordDraw(state, i + 1);
    }

    std::vector<PendingUpload> uploads;
    std::vector<ScheduledRenderPass> passes;
    scheduler.FlushFrame(uploads, passes);

    // Must be consolidated into exactly 1 render pass
    ASSERT_EQ(passes.size(), 1U);
    ASSERT_EQ(passes[0].TotalDrawCount(), 20U);
    ASSERT_EQ(passes[0].ColorLoadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
    ASSERT_EQ(passes[0].ColorStoreOp, VK_ATTACHMENT_STORE_OP_STORE);

    auto metrics = scheduler.GetMetrics();
    ASSERT_EQ(metrics.TotalDrawCalls, 20U);
    ASSERT_EQ(metrics.TotalRenderPassesCreated, 1U);
    ASSERT_EQ(metrics.DrawCallsGrouped, 19U);
    ASSERT_EQ(metrics.ColorLoadsAvoided, 19U);
    ASSERT_GT(metrics.EstimatedDramBandwidthSavedBytes, 0U);

    std::cout << "       -> Passed (" << metrics.DrawCallsGrouped << " draws grouped, 1 render pass)!" << std::endl;
}

void TestMultiTargetPassSplitting() {
    std::cout << "[TEST] 2. Multi-Target Framebuffer Pass Splitting..." << std::endl;

    PicaRenderPassScheduler scheduler;

    // Target 1: Top Screen (400x240)
    PicaDrawState topScreen;
    topScreen.Framebuffer.ColorAddress = 0x20000000U;
    topScreen.Framebuffer.DepthAddress = 0x20040000U;
    topScreen.Framebuffer.Width = 400;
    topScreen.Framebuffer.Height = 240;

    // Target 2: Shadow Map (128x128)
    PicaDrawState shadowMap;
    shadowMap.Framebuffer.ColorAddress = 0x20100000U;
    shadowMap.Framebuffer.DepthAddress = 0x20120000U;
    shadowMap.Framebuffer.Width = 128;
    shadowMap.Framebuffer.Height = 128;

    // Target 3: Bottom Screen (320x240)
    PicaDrawState bottomScreen;
    bottomScreen.Framebuffer.ColorAddress = 0x20200000U;
    bottomScreen.Framebuffer.DepthAddress = 0x20240000U;
    bottomScreen.Framebuffer.Width = 320;
    bottomScreen.Framebuffer.Height = 240;

    // Record: 10 on Top, 5 on Shadow, 8 on Bottom
    for (int i = 0; i < 10; ++i) scheduler.RecordDraw(topScreen);
    for (int i = 0; i < 5; ++i) scheduler.RecordDraw(shadowMap);
    for (int i = 0; i < 8; ++i) scheduler.RecordDraw(bottomScreen);

    std::vector<PendingUpload> uploads;
    std::vector<ScheduledRenderPass> passes;
    scheduler.FlushFrame(uploads, passes);

    // Must split cleanly into 3 passes
    ASSERT_EQ(passes.size(), 3U);
    ASSERT_EQ(passes[0].Target.ColorAddress, 0x20000000U);
    ASSERT_EQ(passes[0].TotalDrawCount(), 10U);

    ASSERT_EQ(passes[1].Target.ColorAddress, 0x20100000U);
    ASSERT_EQ(passes[1].TotalDrawCount(), 5U);

    ASSERT_EQ(passes[2].Target.ColorAddress, 0x20200000U);
    ASSERT_EQ(passes[2].TotalDrawCount(), 8U);

    std::cout << "       -> Passed (3 distinct render targets cleanly separated)!" << std::endl;
}

void TestDepthStoreDontCarePolicy() {
    std::cout << "[TEST] 3. Depth Store DONT_CARE Policy (Adreno GMEM Optimization)..." << std::endl;

    PicaRenderPassScheduler scheduler;

    PicaDrawState state;
    state.Framebuffer.ColorAddress = 0x20000000U;
    state.Framebuffer.DepthAddress = 0x20040000U;
    state.Framebuffer.Width = 400;
    state.Framebuffer.Height = 240;

    scheduler.RecordDraw(state);

    std::vector<PendingUpload> uploads;
    std::vector<ScheduledRenderPass> passes;
    scheduler.FlushFrame(uploads, passes);

    ASSERT_EQ(passes.size(), 1U);
    // Crucial rule for Qualcomm Adreno: DepthStoreOp must be DONT_CARE to avoid tile memory writeback
    ASSERT_EQ(passes[0].DepthStoreOp, VK_ATTACHMENT_STORE_OP_DONT_CARE);
    ASSERT_EQ(passes[0].StencilStoreOp, VK_ATTACHMENT_STORE_OP_DONT_CARE);

    auto metrics = scheduler.GetMetrics();
    ASSERT_EQ(metrics.DepthStoresAvoided, 1U);
    ASSERT_EQ(metrics.StencilStoresAvoided, 1U);

    std::cout << "       -> Passed (DepthStoreOp = DONT_CARE confirmed)!" << std::endl;
}

void TestSubpassInputAttachmentForLocalRead() {
    std::cout << "[TEST] 4. Local Tile Memory Feedback (Subpass Input Attachment)..." << std::endl;

    PicaRenderPassScheduler scheduler;

    PicaDrawState baseDraw;
    baseDraw.Framebuffer.ColorAddress = 0x20000000U;
    baseDraw.Framebuffer.Width = 400;
    baseDraw.Framebuffer.Height = 240;

    // Draw 1 & 2: Base scene rendering
    scheduler.RecordDraw(baseDraw);
    scheduler.RecordDraw(baseDraw);

    // Draw 3: Post-effect / Fog compositing reading from the active framebuffer!
    PicaDrawState feedbackDraw = baseDraw;
    feedbackDraw.Textures[0].Enabled = true;
    feedbackDraw.Textures[0].PhysicalAddress = 0x20000000U; // Reads current render target
    scheduler.RecordDraw(feedbackDraw);

    std::vector<PendingUpload> uploads;
    std::vector<ScheduledRenderPass> passes;
    scheduler.FlushFrame(uploads, passes);

    // Must still be 1 single render pass (NOT broken into 2 separate passes with DRAM spills!)
    ASSERT_EQ(passes.size(), 1U);
    // Must be split into 2 subpasses: Subpass 0 (writes color), Subpass 1 (reads input attachment)
    ASSERT_EQ(passes[0].Subpasses.size(), 2U);
    ASSERT_FALSE(passes[0].Subpasses[0].UsesInputAttachment);
    ASSERT_TRUE(passes[0].Subpasses[1].UsesInputAttachment);

    auto metrics = scheduler.GetMetrics();
    ASSERT_EQ(metrics.SubpassesUtilized, 1U);
    ASSERT_EQ(metrics.ExternalMidPassReadViolations, 0U); // Strict Quality Gate requirement!

    std::cout << "       -> Passed (Subpass input attachment generated, 0 mid-pass read violations)!" << std::endl;
}

void TestUploadBatchingBeforePass() {
    std::cout << "[TEST] 5. Texture Upload Batching Strictly Before Render Passes..." << std::endl;

    PicaRenderPassScheduler scheduler;

    // Queue 3 texture uploads
    scheduler.QueueUpload(0x10000000U, 1024 * 512 * 4, true);
    scheduler.QueueUpload(0x10200000U, 256 * 256 * 4, true);
    scheduler.QueueUpload(0x10300000U, 64 * 64 * 4, true);

    PicaDrawState drawState;
    drawState.Framebuffer.ColorAddress = 0x20000000U;
    drawState.Framebuffer.Width = 400;
    drawState.Framebuffer.Height = 240;
    scheduler.RecordDraw(drawState);

    std::vector<PendingUpload> uploads;
    std::vector<ScheduledRenderPass> passes;
    scheduler.FlushFrame(uploads, passes);

    // Uploads must be isolated and flushed into outUploads before passes
    ASSERT_EQ(uploads.size(), 3U);
    ASSERT_EQ(uploads[0].DestinationAddress, 0x10000000U);
    ASSERT_EQ(passes.size(), 1U);

    auto metrics = scheduler.GetMetrics();
    ASSERT_EQ(metrics.UploadsBatchedBeforePass, 3U);

    std::cout << "       -> Passed (3 texture transfers isolated outside render pass scope)!" << std::endl;
}

void TestVulkanRenderPassInfoBuilding() {
    std::cout << "[TEST] 6. VkRenderPassCreateInfo Generation and Dependencies..." << std::endl;

    ScheduledRenderPass pass;
    pass.PassId = 1;
    pass.Target.ColorAddress = 0x20000000U;
    pass.Target.Width = 400;
    pass.Target.Height = 240;
    pass.ColorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    pass.ColorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    pass.DepthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    pass.DepthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    ScheduledSubpass sp0;
    sp0.SubpassIndex = 0;
    sp0.UsesInputAttachment = false;
    pass.Subpasses.push_back(sp0);

    ScheduledSubpass sp1;
    sp1.SubpassIndex = 1;
    sp1.UsesInputAttachment = true;
    pass.Subpasses.push_back(sp1);

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkSubpassDescription> subpasses;
    std::vector<VkSubpassDependency> dependencies;

    VkRenderPassCreateInfo ci = PicaRenderPassScheduler::BuildVulkanRenderPassInfo(
        pass, attachments, subpasses, dependencies);

    ASSERT_EQ(ci.attachmentCount, 2U);
    ASSERT_EQ(ci.subpassCount, 2U);
    ASSERT_EQ(ci.dependencyCount, 1U);

    // Validate Adreno tile-dependency flags
    ASSERT_EQ(dependencies[0].srcSubpass, 0U);
    ASSERT_EQ(dependencies[0].dstSubpass, 1U);
    ASSERT_EQ(dependencies[0].dependencyFlags, static_cast<VkDependencyFlags>(VK_DEPENDENCY_BY_REGION_BIT));
    ASSERT_EQ(dependencies[0].srcAccessMask, static_cast<VkAccessFlags>(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT));
    ASSERT_EQ(dependencies[0].dstAccessMask, static_cast<VkAccessFlags>(VK_ACCESS_INPUT_ATTACHMENT_READ_BIT));

    std::cout << "       -> Passed (VkRenderPassCreateInfo validated with VK_DEPENDENCY_BY_REGION_BIT)!" << std::endl;
}

} // namespace

int main() {
    std::cout << "========================================================================" << std::endl;
    std::cout << " Running Phase 6 Tests: Adreno TBDR Render Passes & GMEM Optimization" << std::endl;
    std::cout << "========================================================================" << std::endl;

    TestDrawCallBatchingAndConsolidation();
    TestMultiTargetPassSplitting();
    TestDepthStoreDontCarePolicy();
    TestSubpassInputAttachmentForLocalRead();
    TestUploadBatchingBeforePass();
    TestVulkanRenderPassInfoBuilding();

    std::cout << "========================================================================" << std::endl;
    std::cout << " All Phase 6 Adreno render pass tests passed successfully (0 violations)!" << std::endl;
    std::cout << "========================================================================" << std::endl;
    return 0;
}
