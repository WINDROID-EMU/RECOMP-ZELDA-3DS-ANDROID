#include "three_ds_recomp/pica/pica_hle_bridge.h"

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
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #a " != " #b << std::endl; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #a " == " #b << std::endl; \
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

void TestHleStateConversion() {
    std::cout << "[TEST] 1. HLE Mesh Batch to Canonical Level 2 State Conversion..." << std::endl;

    HleMeshBatch batch;
    batch.TargetWidth = 400;
    batch.TargetHeight = 240;
    batch.DepthTest = true;
    batch.DepthWrite = true;
    batch.DepthFunc = CompareFunc::Less;
    batch.DiffuseColor = { 0.2f, 0.4f, 0.8f, 1.0f };

    PicaDrawState state = PicaHleBridge::ConvertHleBatchToLevel2State(batch);

    ASSERT_EQ(state.Framebuffer.Width, 400);
    ASSERT_EQ(state.Framebuffer.Height, 240);
    ASSERT_TRUE(state.DepthStencil.DepthTest);
    ASSERT_TRUE(state.DepthStencil.DepthWrite);
    ASSERT_EQ(state.DepthStencil.DepthCompare, CompareFunc::Less);
    ASSERT_NE(state.StateHash, 0ULL);

    std::cout << "       -> Passed!" << std::endl;
}

void TestDirectHleDispatch() {
    std::cout << "[TEST] 2. Direct HLE Draw Dispatch (Register Bypass)..." << std::endl;

    PicaHleBridge bridge;
    RasterSurface surface(400, 240);
    surface.Clear(0x00000000U, 0x00FFFFFFU, 0);

    HleMeshBatch batch;
    batch.ScreenRect[0] = 50;
    batch.ScreenRect[1] = 50;
    batch.ScreenRect[2] = 150;
    batch.ScreenRect[3] = 150;
    batch.DepthZ = 0.5f;
    batch.DiffuseColor = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red

    bool ok = bridge.DispatchHleBatch(batch, surface);
    ASSERT_TRUE(ok);

    // Verify center pixel is Red
    ASSERT_EQ(surface.GetColor(100, 100), 0xFF0000FFU);
    // Verify outside pixel is still Clear (black)
    ASSERT_EQ(surface.GetColor(10, 10), 0x00000000U);

    auto metrics = bridge.GetMetrics();
    ASSERT_EQ(metrics.DirectHleDrawCalls, 1U);
    ASSERT_EQ(metrics.RegisterPacketsBypassed, 16U);

    std::cout << "       -> Passed (16 register packets bypassed)!" << std::endl;
}

void TestLevel2VsHleFrameParity() {
    std::cout << "[TEST] 3. Frame Parity Oracle: HLE Path vs Level 2 Canonical Path..." << std::endl;

    PicaHleBridge bridge;
    RasterSurface surfaceHle(400, 240);
    RasterSurface surfaceLevel2(400, 240);

    HleMeshBatch batch;
    batch.ScreenRect[0] = 20;
    batch.ScreenRect[1] = 20;
    batch.ScreenRect[2] = 380;
    batch.ScreenRect[3] = 220;
    batch.DepthZ = 0.45f;
    batch.DiffuseColor = { 0.35f, 0.65f, 0.85f, 1.0f };

    float maxDiff = 1.0f;
    bool identical = bridge.ValidateFrameParity(batch, surfaceHle, surfaceLevel2, &maxDiff);

    // Strict Quality Gate 8 requirement: Frame must be identical (diff <= 1/255)
    ASSERT_TRUE(identical);
    ASSERT_NEAR(maxDiff, 0.0f, 1e-4f);

    std::cout << "       -> Passed (Frame parity 100% identical, max pixel diff: " << maxDiff << ")!" << std::endl;
}

void TestWidescreenExpansion() {
    std::cout << "[TEST] 4. Widescreen 16:9 Aspect Ratio Expansion..." << std::endl;

    PicaHleBridge bridge;
    bridge.SetWidescreenEnabled(true);
    ASSERT_TRUE(bridge.IsWidescreenEnabled());

    RasterSurface surface(427, 240); // 240 * (16/9) ~ 427
    surface.Clear(0x00000000U, 0x00FFFFFFU, 0);

    HleMeshBatch batch;
    batch.TargetHeight = 240;
    batch.ScreenRect[0] = 0;
    batch.ScreenRect[1] = 0;
    batch.ScreenRect[2] = 427;
    batch.ScreenRect[3] = 240;
    batch.DiffuseColor = { 0.0f, 1.0f, 0.0f, 1.0f };

    bool ok = bridge.DispatchHleBatch(batch, surface);
    ASSERT_TRUE(ok);

    auto metrics = bridge.GetMetrics();
    ASSERT_EQ(metrics.WidescreenDrawsEmitted, 1U);

    std::cout << "       -> Passed (16:9 Widescreen draw emitted successfully)!" << std::endl;
}

void TestLevel2FallbackMechanism() {
    std::cout << "[TEST] 5. Level 2 Dynamic Fallback Mechanism (Rule R22)..." << std::endl;

    PicaHleBridge bridge;
    RasterSurface surface(400, 240);
    surface.Clear(0x00000000U, 0x00FFFFFFU, 0);

    HleMeshBatch batch;
    batch.ScreenRect[0] = 10;
    batch.ScreenRect[1] = 10;
    batch.ScreenRect[2] = 50;
    batch.ScreenRect[3] = 50;
    batch.DiffuseColor = { 0.5f, 0.5f, 0.0f, 1.0f };

    bool ok = bridge.DispatchLevel2Batch(batch, surface);
    ASSERT_TRUE(ok);

    auto metrics = bridge.GetMetrics();
    ASSERT_EQ(metrics.Level2FallbackDrawCalls, 1U);

    std::cout << "       -> Passed (Fallback safely recorded and executed)!" << std::endl;
}

} // namespace

int main() {
    std::cout << "========================================================================" << std::endl;
    std::cout << " Running Phase 8 Tests: Graphics HLE Bridge & Level 2 Parity Oracle" << std::endl;
    std::cout << "========================================================================" << std::endl;

    TestHleStateConversion();
    TestDirectHleDispatch();
    TestLevel2VsHleFrameParity();
    TestWidescreenExpansion();
    TestLevel2FallbackMechanism();

    std::cout << "========================================================================" << std::endl;
    std::cout << " All Phase 8 graphics HLE bridge tests passed successfully (0 diff)!" << std::endl;
    std::cout << "========================================================================" << std::endl;
    return 0;
}
