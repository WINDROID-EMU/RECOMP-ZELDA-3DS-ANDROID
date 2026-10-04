#include "three_ds_recomp/pica/pica_raster_pipeline.h"

#include <cassert>
#include <cmath>
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

#define ASSERT_NEAR(a, b, eps) \
    do { \
        if (std::fabs((a) - (b)) > (eps)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": |" #a " - " #b "| > " #eps \
                      << " (" << (a) << " vs " << (b) << ")" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

namespace {

using namespace ThreeDsRecomp::Pica;

void TestVulkanStateTranslation() {
    std::cout << "[TEST] 1. Vulkan State Translation (CompareOp, Blend, LogicOp, Viewport, Scissor)..." << std::endl;

    // Compare Ops
    ASSERT_EQ(TranslateCompareOp(CompareFunc::Never), VK_COMPARE_OP_NEVER);
    ASSERT_EQ(TranslateCompareOp(CompareFunc::Always), VK_COMPARE_OP_ALWAYS);
    ASSERT_EQ(TranslateCompareOp(CompareFunc::Equal), VK_COMPARE_OP_EQUAL);
    ASSERT_EQ(TranslateCompareOp(CompareFunc::NotEqual), VK_COMPARE_OP_NOT_EQUAL);
    ASSERT_EQ(TranslateCompareOp(CompareFunc::Less), VK_COMPARE_OP_LESS);
    ASSERT_EQ(TranslateCompareOp(CompareFunc::LessOrEqual), VK_COMPARE_OP_LESS_OR_EQUAL);
    ASSERT_EQ(TranslateCompareOp(CompareFunc::Greater), VK_COMPARE_OP_GREATER);
    ASSERT_EQ(TranslateCompareOp(CompareFunc::GreaterOrEqual), VK_COMPARE_OP_GREATER_OR_EQUAL);

    // Stencil Ops
    ASSERT_EQ(TranslateStencilOp(StencilOp::Keep), VK_STENCIL_OP_KEEP);
    ASSERT_EQ(TranslateStencilOp(StencilOp::Zero), VK_STENCIL_OP_ZERO);
    ASSERT_EQ(TranslateStencilOp(StencilOp::Replace), VK_STENCIL_OP_REPLACE);
    ASSERT_EQ(TranslateStencilOp(StencilOp::Increment), VK_STENCIL_OP_INCREMENT_AND_CLAMP);
    ASSERT_EQ(TranslateStencilOp(StencilOp::Decrement), VK_STENCIL_OP_DECREMENT_AND_CLAMP);
    ASSERT_EQ(TranslateStencilOp(StencilOp::Invert), VK_STENCIL_OP_INVERT);
    ASSERT_EQ(TranslateStencilOp(StencilOp::IncrementWrap), VK_STENCIL_OP_INCREMENT_AND_WRAP);
    ASSERT_EQ(TranslateStencilOp(StencilOp::DecrementWrap), VK_STENCIL_OP_DECREMENT_AND_WRAP);

    // Blend Equations & Factors
    ASSERT_EQ(TranslateBlendOp(BlendEq::Add), VK_BLEND_OP_ADD);
    ASSERT_EQ(TranslateBlendOp(BlendEq::Subtract), VK_BLEND_OP_SUBTRACT);
    ASSERT_EQ(TranslateBlendOp(BlendEq::ReverseSubtract), VK_BLEND_OP_REVERSE_SUBTRACT);
    ASSERT_EQ(TranslateBlendOp(BlendEq::Min), VK_BLEND_OP_MIN);
    ASSERT_EQ(TranslateBlendOp(BlendEq::Max), VK_BLEND_OP_MAX);

    ASSERT_EQ(TranslateBlendFactor(BlendFactor::Zero), VK_BLEND_FACTOR_ZERO);
    ASSERT_EQ(TranslateBlendFactor(BlendFactor::One), VK_BLEND_FACTOR_ONE);
    ASSERT_EQ(TranslateBlendFactor(BlendFactor::SourceAlpha), VK_BLEND_FACTOR_SRC_ALPHA);
    ASSERT_EQ(TranslateBlendFactor(BlendFactor::OneMinusSourceAlpha), VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);

    // Logic Ops
    ASSERT_EQ(TranslateLogicOp(LogicOp::Clear), VK_LOGIC_OP_CLEAR);
    ASSERT_EQ(TranslateLogicOp(LogicOp::Xor), VK_LOGIC_OP_XOR);
    ASSERT_EQ(TranslateLogicOp(LogicOp::And), VK_LOGIC_OP_AND);
    ASSERT_EQ(TranslateLogicOp(LogicOp::Or), VK_LOGIC_OP_OR);

    // Viewport
    ViewportState vp;
    vp.CornerX = 200;
    vp.CornerY = 120;
    vp.HalfWidth = 200.0f;
    vp.HalfHeight = 120.0f;
    vp.NearPlane = 0.0f;
    vp.DepthRange = 1.0f;

    VkViewport vkVp = TranslateViewport(vp, 400, 240, true);
    ASSERT_NEAR(vkVp.x, 0.0f, 1e-4f);
    ASSERT_NEAR(vkVp.width, 400.0f, 1e-4f);
    ASSERT_NEAR(vkVp.y, 240.0f, 1e-4f);
    ASSERT_NEAR(vkVp.height, -240.0f, 1e-4f); // Negative height for Adreno/Vulkan Y-flip
    ASSERT_NEAR(vkVp.minDepth, 0.0f, 1e-4f);
    ASSERT_NEAR(vkVp.maxDepth, 1.0f, 1e-4f);

    // Scissor
    ScissorState sc;
    sc.Mode = 3; // Include
    sc.X1 = 50;
    sc.Y1 = 30;
    sc.X2 = 250;
    sc.Y2 = 180;
    VkRect2D vkSc = TranslateScissor(sc, 400, 240);
    ASSERT_EQ(vkSc.offset.x, 50);
    ASSERT_EQ(vkSc.offset.y, 30);
    ASSERT_EQ(vkSc.extent.width, 200U);
    ASSERT_EQ(vkSc.extent.height, 150U);

    std::cout << "       -> Passed!" << std::endl;
}

void TestDepthTestingAndWriting() {
    std::cout << "[TEST] 2. Depth Testing and Depth Write Control..." << std::endl;

    RasterSurface surface(16, 16);
    surface.Clear(0x00000000U, 0x00FFFFFFU, 0); // Clear color = black, depth = 1.0

    PicaDrawState state;
    state.Framebuffer.ColorWrite = true;
    state.Framebuffer.ColorWriteMask = 0xF;
    state.DepthStencil.DepthTest = true;
    state.DepthStencil.DepthWrite = true;
    state.DepthStencil.DepthCompare = CompareFunc::Less;

    // Draw red quad at z = 0.5
    size_t count1 = PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.5f, { 1.0f, 0.0f, 0.0f, 1.0f }, state, surface);
    ASSERT_EQ(count1, 256U);
    ASSERT_EQ(surface.GetColor(8, 8), 0xFF0000FFU); // Red (RGBA8 little-endian: 0xAABBGGRR)
    ASSERT_NEAR(static_cast<float>(surface.GetDepth24(8, 8)) / 16777215.0f, 0.5f, 1e-5f);

    // Try to draw blue quad at z = 0.8 with Less (Should FAIL depth test)
    size_t count2 = PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.8f, { 0.0f, 0.0f, 1.0f, 1.0f }, state, surface);
    ASSERT_EQ(count2, 0U);
    ASSERT_EQ(surface.GetColor(8, 8), 0xFF0000FFU); // Still Red!

    // Draw green quad at z = 0.2 with DepthWrite = FALSE (Should write color, but preserve depth 0.5)
    state.DepthStencil.DepthWrite = false;
    size_t count3 = PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.2f, { 0.0f, 1.0f, 0.0f, 1.0f }, state, surface);
    ASSERT_EQ(count3, 256U);
    ASSERT_EQ(surface.GetColor(8, 8), 0xFF00FF00U); // Green!
    ASSERT_NEAR(static_cast<float>(surface.GetDepth24(8, 8)) / 16777215.0f, 0.5f, 1e-5f); // Depth still 0.5!

    std::cout << "       -> Passed!" << std::endl;
}

void TestPolygonOffsetAndZFightingDecals() {
    std::cout << "[TEST] 3. Polygon Offset & Decal Z-Fighting Prevention..." << std::endl;

    RasterSurface surface(16, 16);
    surface.Clear(0x00000000U, 0x00FFFFFFU, 0);

    // 1. Draw base terrain at z = 0.6
    PicaDrawState terrainState;
    terrainState.Framebuffer.ColorWrite = true;
    terrainState.Framebuffer.ColorWriteMask = 0xF;
    terrainState.DepthStencil.DepthTest = true;
    terrainState.DepthStencil.DepthWrite = true;
    terrainState.DepthStencil.DepthCompare = CompareFunc::Less;

    PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.6f, { 0.2f, 0.8f, 0.2f, 1.0f }, terrainState, surface); // Grass green

    // 2. Draw coplanar decal (footprint/shadow) at identical z = 0.6 with Less
    // Without PolygonOffset, Less will reject coplanar fragments (z == z_buffer).
    PicaDrawState decalStateWithoutOffset = terrainState;
    size_t rejectedDecal = PicaRasterOracle::RenderQuad(4, 4, 12, 12, 0.6f, { 0.1f, 0.1f, 0.1f, 1.0f }, decalStateWithoutOffset, surface);
    ASSERT_EQ(rejectedDecal, 0U); // Rejected due to z-fighting!

    // 3. Draw coplanar decal WITH PolygonOffset enabled (unit = -10.0f pulls towards near plane)
    PicaDrawState decalStateWithOffset = terrainState;
    decalStateWithOffset.PolygonOffset.Enabled = true;
    decalStateWithOffset.PolygonOffset.Unit = -10.0f;
    decalStateWithOffset.PolygonOffset.Factor = -1.0f;

    size_t acceptedDecal = PicaRasterOracle::RenderQuad(4, 4, 12, 12, 0.6f, { 0.1f, 0.1f, 0.1f, 1.0f }, decalStateWithOffset, surface);
    ASSERT_EQ(acceptedDecal, 64U); // Successfully accepted without z-fighting!

    // Verify decal color at center
    uint32_t decalColor = surface.GetColor(8, 8);
    ASSERT_EQ(decalColor & 0xFFU, 26U); // ~0.1 * 255 = 26

    std::cout << "       -> Passed!" << std::endl;
}

void TestStencilTestingAndShadowVolume() {
    std::cout << "[TEST] 4. Stencil Shadow Testing (Increment, Ref, Compare)..." << std::endl;

    RasterSurface surface(16, 16);
    surface.Clear(0x00000000U, 0x00FFFFFFU, 0); // Stencil = 0

    // Render opaque floor at z = 0.7
    PicaDrawState floorState;
    floorState.Framebuffer.ColorWrite = true;
    floorState.DepthStencil.DepthTest = true;
    floorState.DepthStencil.DepthWrite = true;
    floorState.DepthStencil.DepthCompare = CompareFunc::Less;
    PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.7f, { 0.5f, 0.5f, 0.5f, 1.0f }, floorState, surface);

    // Stencil Pass: Mark shadow region in center (4..12) by incrementing stencil
    PicaDrawState shadowVolumeState;
    shadowVolumeState.Framebuffer.ColorWrite = false; // No color write
    shadowVolumeState.DepthStencil.DepthTest = true;
    shadowVolumeState.DepthStencil.DepthWrite = false;
    shadowVolumeState.DepthStencil.DepthCompare = CompareFunc::Less;
    shadowVolumeState.DepthStencil.StencilTest = true;
    shadowVolumeState.DepthStencil.StencilCompare = CompareFunc::Always;
    shadowVolumeState.DepthStencil.StencilPass = StencilOp::Increment;
    shadowVolumeState.DepthStencil.StencilWriteMask = 0xFF;

    PicaRasterOracle::RenderQuad(4, 4, 12, 12, 0.4f, { 0.0f, 0.0f, 0.0f, 0.0f }, shadowVolumeState, surface);

    // Check stencil value in center vs border
    ASSERT_EQ(surface.GetStencil(8, 8), 1U);
    ASSERT_EQ(surface.GetStencil(1, 1), 0U);

    // Apply shadow darkening quad fullscreen where stencil == 1
    PicaDrawState shadowDrawState;
    shadowDrawState.Framebuffer.ColorWrite = true;
    shadowDrawState.DepthStencil.DepthTest = false; // Fullscreen overlay
    shadowDrawState.DepthStencil.StencilTest = true;
    shadowDrawState.DepthStencil.StencilCompare = CompareFunc::Equal;
    shadowDrawState.DepthStencil.StencilRef = 1;
    shadowDrawState.DepthStencil.StencilCompareMask = 0xFF;

    // Darken by replacing color with black
    size_t shadowedPixels = PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.0f, { 0.0f, 0.0f, 0.0f, 1.0f }, shadowDrawState, surface);
    ASSERT_EQ(shadowedPixels, 64U); // Only 8x8 shadow region modified!

    // Verify center is shadowed, border is still grey
    ASSERT_EQ(surface.GetColor(8, 8), 0xFF000000U); // Black
    ASSERT_NE(surface.GetColor(1, 1), 0xFF000000U); // Grey floor

    std::cout << "       -> Passed!" << std::endl;
}

void TestColorBlendingEquationsAndFactors() {
    std::cout << "[TEST] 5. Color Blending (Alpha Blend, Additive, ReverseSubtract, Constant Color)..." << std::endl;

    RasterSurface surface(16, 16);

    // 1. Classic Alpha Blending: Red background + 50% Green foreground
    surface.Clear(0xFF0000FFU, 0x00FFFFFFU, 0); // Full red (R=255, G=0, B=0, A=255)

    PicaDrawState alphaBlendState;
    alphaBlendState.Framebuffer.ColorWrite = true;
    alphaBlendState.Blend.Enabled = true;
    alphaBlendState.Blend.EquationRgb = BlendEq::Add;
    alphaBlendState.Blend.EquationAlpha = BlendEq::Add;
    alphaBlendState.Blend.SrcRgb = BlendFactor::SourceAlpha;
    alphaBlendState.Blend.DestRgb = BlendFactor::OneMinusSourceAlpha;
    alphaBlendState.Blend.SrcAlpha = BlendFactor::One;
    alphaBlendState.Blend.DestAlpha = BlendFactor::Zero;

    PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.5f, { 0.0f, 1.0f, 0.0f, 0.5f }, alphaBlendState, surface);

    uint32_t blended = surface.GetColor(8, 8);
    uint8_t r = blended & 0xFFU;
    uint8_t g = (blended >> 8U) & 0xFFU;
    uint8_t b = (blended >> 16U) & 0xFFU;
    // Expected: R = 255 * 0.5 = 128, G = 255 * 0.5 = 128, B = 0
    ASSERT_NEAR(static_cast<float>(r), 128.0f, 2.0f);
    ASSERT_NEAR(static_cast<float>(g), 128.0f, 2.0f);
    ASSERT_EQ(b, 0U);

    // 2. Additive Blending: (0.2, 0.2, 0.2) + (0.3, 0.4, 0.5)
    surface.Clear(0xFF333333U, 0x00FFFFFFU, 0); // ~0.2 in each channel
    PicaDrawState additiveState;
    additiveState.Framebuffer.ColorWrite = true;
    additiveState.Blend.Enabled = true;
    additiveState.Blend.EquationRgb = BlendEq::Add;
    additiveState.Blend.SrcRgb = BlendFactor::One;
    additiveState.Blend.DestRgb = BlendFactor::One;

    PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.5f, { 0.3f, 0.4f, 0.5f, 1.0f }, additiveState, surface);
    uint32_t addCol = surface.GetColor(8, 8);
    // Expected: R = 0.2 + 0.3 = 0.5 (~128), G = 0.2 + 0.4 = 0.6 (~153), B = 0.2 + 0.5 = 0.7 (~179)
    ASSERT_NEAR(static_cast<float>(addCol & 0xFFU), 128.0f, 3.0f);
    ASSERT_NEAR(static_cast<float>((addCol >> 8U) & 0xFFU), 153.0f, 3.0f);
    ASSERT_NEAR(static_cast<float>((addCol >> 16U) & 0xFFU), 179.0f, 3.0f);

    std::cout << "       -> Passed!" << std::endl;
}

void TestLogicOperations() {
    std::cout << "[TEST] 6. Logic Operations (XOR, AND, Invert)..." << std::endl;

    RasterSurface surface(16, 16);
    surface.Clear(0xFF00FF00U, 0x00FFFFFFU, 0); // Green (0x00, 0xFF, 0x00, 0xFF)

    PicaDrawState logicState;
    logicState.Framebuffer.ColorWrite = true;
    logicState.Blend.Enabled = false;
    logicState.Blend.LogicOpEnabled = true;
    logicState.Blend.LogicOperation = LogicOp::Xor;

    // XOR with Yellow (0xFF, 0xFF, 0x00, 0x00)
    PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.5f, { 1.0f, 1.0f, 0.0f, 0.0f }, logicState, surface);

    uint32_t xorCol = surface.GetColor(8, 8);
    // R: 0x00 ^ 0xFF = 0xFF
    // G: 0xFF ^ 0xFF = 0x00
    // B: 0x00 ^ 0x00 = 0x00
    // A: 0xFF ^ 0x00 = 0xFF
    ASSERT_EQ(xorCol & 0xFFU, 0xFFU);
    ASSERT_EQ((xorCol >> 8U) & 0xFFU, 0x00U);
    ASSERT_EQ((xorCol >> 16U) & 0xFFU, 0x00U);
    ASSERT_EQ((xorCol >> 24U) & 0xFFU, 0xFFU);

    std::cout << "       -> Passed!" << std::endl;
}

void TestAlphaTestFoliageDiscard() {
    std::cout << "[TEST] 7. Alpha Test & Foliage/Grass Discard..." << std::endl;

    RasterSurface surface(16, 16);
    surface.Clear(0xFF112233U, 0x00FFFFFFU, 0); // Initial background color

    PicaDrawState foliageState;
    foliageState.Framebuffer.ColorWrite = true;
    foliageState.DepthStencil.DepthTest = true;
    foliageState.DepthStencil.DepthWrite = true;
    foliageState.DepthStencil.AlphaTest = true;
    foliageState.DepthStencil.AlphaCompare = CompareFunc::GreaterOrEqual;
    foliageState.DepthStencil.AlphaRef = 128; // 0.5 threshold

    // Fragment 1: Transparent foliage pixel (alpha = 0.2 < 0.5)
    PicaRasterOracle::FragmentInput fragTransparent;
    fragTransparent.X = 5;
    fragTransparent.Y = 5;
    fragTransparent.Z = 0.4f;
    fragTransparent.Color = { 0.0f, 1.0f, 0.0f, 0.2f };

    auto res1 = PicaRasterOracle::ProcessFragment(fragTransparent, foliageState, surface);
    ASSERT_FALSE(res1.PassedAlpha);
    ASSERT_FALSE(res1.WroteColor);
    ASSERT_FALSE(res1.WroteDepth);
    ASSERT_EQ(surface.GetColor(5, 5), 0xFF112233U); // Untouched!

    // Fragment 2: Solid foliage pixel (alpha = 0.8 >= 0.5)
    PicaRasterOracle::FragmentInput fragSolid;
    fragSolid.X = 5;
    fragSolid.Y = 5;
    fragSolid.Z = 0.4f;
    fragSolid.Color = { 0.0f, 1.0f, 0.0f, 0.8f };

    auto res2 = PicaRasterOracle::ProcessFragment(fragSolid, foliageState, surface);
    ASSERT_TRUE(res2.PassedAlpha);
    ASSERT_TRUE(res2.WroteColor);
    ASSERT_TRUE(res2.WroteDepth);
    ASSERT_EQ(surface.GetColor(5, 5), 0xCC00FF00U); // Updated with foliage green!
    ASSERT_NEAR(static_cast<float>(surface.GetDepth24(5, 5)) / 16777215.0f, 0.4f, 1e-5f);

    std::cout << "       -> Passed!" << std::endl;
}

void TestColorWriteMask() {
    std::cout << "[TEST] 8. Color Write Masking (Selective R, G, B, A Write)..." << std::endl;

    RasterSurface surface(16, 16);
    surface.Clear(0x11223344U, 0x00FFFFFFU, 0); // R=0x44, G=0x33, B=0x22, A=0x11

    PicaDrawState maskState;
    maskState.Framebuffer.ColorWrite = true;
    maskState.Framebuffer.ColorWriteMask = 0x5; // Write only Red (bit 0) and Blue (bit 2)

    PicaRasterOracle::RenderQuad(0, 0, 16, 16, 0.5f, { 1.0f, 1.0f, 1.0f, 1.0f }, maskState, surface);

    uint32_t maskedPixel = surface.GetColor(8, 8);
    // Red and Blue updated to 255 (0xFF), Green and Alpha remain 0x33 and 0x11
    ASSERT_EQ(maskedPixel & 0xFFU, 0xFFU);
    ASSERT_EQ((maskedPixel >> 8U) & 0xFFU, 0x33U);
    ASSERT_EQ((maskedPixel >> 16U) & 0xFFU, 0xFFU);
    ASSERT_EQ((maskedPixel >> 24U) & 0xFFU, 0x11U);

    std::cout << "       -> Passed!" << std::endl;
}

} // namespace

int main() {
    std::cout << "========================================================================" << std::endl;
    std::cout << " Running Phase 5 Tests: PICA Rasterization, Depth, Stencil & Blending" << std::endl;
    std::cout << "========================================================================" << std::endl;

    TestVulkanStateTranslation();
    TestDepthTestingAndWriting();
    TestPolygonOffsetAndZFightingDecals();
    TestStencilTestingAndShadowVolume();
    TestColorBlendingEquationsAndFactors();
    TestLogicOperations();
    TestAlphaTestFoliageDiscard();
    TestColorWriteMask();

    std::cout << "========================================================================" << std::endl;
    std::cout << " All Phase 5 raster, depth, stencil, and blending tests passed!" << std::endl;
    std::cout << "========================================================================" << std::endl;
    return 0;
}
