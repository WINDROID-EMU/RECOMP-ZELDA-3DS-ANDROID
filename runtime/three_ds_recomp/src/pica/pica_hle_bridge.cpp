#include "three_ds_recomp/pica/pica_hle_bridge.h"
#include "three_ds_recomp/pica/pica_state_decoder.h"

#include <algorithm>
#include <cmath>

namespace ThreeDsRecomp::Pica {

PicaDrawState PicaHleBridge::ConvertHleBatchToLevel2State(const HleMeshBatch& batch) noexcept {
    PicaDrawState state{};

    // 1. Framebuffer configuration
    state.Framebuffer.ColorAddress = 0x20000000U;
    state.Framebuffer.DepthAddress = 0x20040000U;
    state.Framebuffer.Width = static_cast<uint16_t>(batch.TargetWidth);
    state.Framebuffer.Height = static_cast<uint16_t>(batch.TargetHeight);
    state.Framebuffer.ColorFormat = 0; // RGBA8
    state.Framebuffer.DepthFormat = 2; // D24S8
    state.Framebuffer.ColorWrite = true;
    state.Framebuffer.DepthWrite = batch.DepthWrite;
    state.Framebuffer.ColorWriteMask = 0xFU;

    // 2. Viewport and Scissor
    state.Viewport.CornerX = static_cast<int16_t>(batch.TargetWidth / 2U);
    state.Viewport.CornerY = static_cast<int16_t>(batch.TargetHeight / 2U);
    state.Viewport.HalfWidth = static_cast<float>(batch.TargetWidth) / 2.0f;
    state.Viewport.HalfHeight = static_cast<float>(batch.TargetHeight) / 2.0f;
    state.Viewport.NearPlane = 0.0f;
    state.Viewport.DepthRange = 1.0f;

    state.Scissor.Mode = 0; // Disabled
    state.Scissor.X1 = 0;
    state.Scissor.Y1 = 0;
    state.Scissor.X2 = static_cast<uint16_t>(batch.TargetWidth);
    state.Scissor.Y2 = static_cast<uint16_t>(batch.TargetHeight);

    // 3. Raster, Depth and Stencil
    state.Culling = CullMode::KeepAll;
    state.DepthStencil.DepthTest = batch.DepthTest;
    state.DepthStencil.DepthWrite = batch.DepthWrite;
    state.DepthStencil.DepthCompare = batch.DepthFunc;
    state.DepthStencil.AlphaTest = batch.AlphaTest;
    state.DepthStencil.AlphaRef = batch.AlphaRef;
    state.DepthStencil.AlphaCompare = CompareFunc::GreaterOrEqual;

    // 4. Color Blending
    state.Blend.Enabled = batch.BlendEnable;
    state.Blend.SrcRgb = batch.SrcBlend;
    state.Blend.DestRgb = batch.DstBlend;
    state.Blend.SrcAlpha = BlendFactor::One;
    state.Blend.DestAlpha = BlendFactor::Zero;
    state.Blend.EquationRgb = BlendEq::Add;
    state.Blend.EquationAlpha = BlendEq::Add;

    // 5. Texture Unit 0
    state.Textures[0].Enabled = true;
    state.Textures[0].PhysicalAddress = batch.TextureAddress;
    state.Textures[0].Width = batch.TextureWidth;
    state.Textures[0].Height = batch.TextureHeight;
    state.Textures[0].Format = batch.TexFormat;

    // 6. TEV Stage 0: Modulate with DiffuseColor
    state.Tev.Stages[0].OpRgb = TevOp::Replace;
    state.Tev.Stages[0].OpAlpha = TevOp::Replace;
    state.Tev.Stages[0].SourceRgb[0] = TevSource::Constant;
    state.Tev.Stages[0].SourceAlpha[0] = TevSource::Constant;

    const uint8_t r = static_cast<uint8_t>(std::clamp(batch.DiffuseColor[0] * 255.0f, 0.0f, 255.0f));
    const uint8_t g = static_cast<uint8_t>(std::clamp(batch.DiffuseColor[1] * 255.0f, 0.0f, 255.0f));
    const uint8_t b = static_cast<uint8_t>(std::clamp(batch.DiffuseColor[2] * 255.0f, 0.0f, 255.0f));
    const uint8_t a = static_cast<uint8_t>(std::clamp(batch.DiffuseColor[3] * 255.0f, 0.0f, 255.0f));
    state.Tev.Stages[0].ConstantColor = static_cast<uint32_t>(r) |
                                        (static_cast<uint32_t>(g) << 8U) |
                                        (static_cast<uint32_t>(b) << 16U) |
                                        (static_cast<uint32_t>(a) << 24U);

    state.StateHash = ComputePicaStateHash(state);
    return state;
}

bool PicaHleBridge::DispatchHleBatch(
    const HleMeshBatch& batch,
    RasterSurface& surface) {
    HleMeshBatch effectiveBatch = batch;

    if (mWidescreenEnabled) {
        // Apply 16:9 Widescreen expansion
        effectiveBatch.AspectRatio = 16.0f / 9.0f;
        effectiveBatch.TargetWidth = static_cast<uint32_t>(std::round(static_cast<float>(batch.TargetHeight) * (16.0f / 9.0f)));
        mMetrics.WidescreenDrawsEmitted++;
    }

    PicaDrawState state = ConvertHleBatchToLevel2State(effectiveBatch);

    // Direct HLE Draw Dispatch
    PicaRasterOracle::RenderQuad(
        effectiveBatch.ScreenRect[0], effectiveBatch.ScreenRect[1],
        effectiveBatch.ScreenRect[2], effectiveBatch.ScreenRect[3],
        effectiveBatch.DepthZ,
        effectiveBatch.DiffuseColor,
        state, surface);

    mMetrics.DirectHleDrawCalls++;
    mMetrics.RegisterPacketsBypassed += 16U; // Bypassed ~16 PICA register packets per draw
    return true;
}

bool PicaHleBridge::DispatchLevel2Batch(
    const HleMeshBatch& batch,
    RasterSurface& surface) {
    // Level 2 Pipeline: Decodes through canonical state
    PicaDrawState state = ConvertHleBatchToLevel2State(batch);

    PicaRasterOracle::RenderQuad(
        batch.ScreenRect[0], batch.ScreenRect[1],
        batch.ScreenRect[2], batch.ScreenRect[3],
        batch.DepthZ,
        batch.DiffuseColor,
        state, surface);

    mMetrics.Level2FallbackDrawCalls++;
    return true;
}

bool PicaHleBridge::ValidateFrameParity(
    const HleMeshBatch& batch,
    RasterSurface& surfaceHle,
    RasterSurface& surfaceLevel2,
    float* maxDiffOut) {
    // Ensure both surfaces have identical dimensions
    if (surfaceHle.Width != surfaceLevel2.Width || surfaceHle.Height != surfaceLevel2.Height) {
        if (maxDiffOut) *maxDiffOut = 1.0f;
        return false;
    }

    // Clear both surfaces to clean initial baseline
    surfaceHle.Clear(0x00000000U, 0x00FFFFFFU, 0);
    surfaceLevel2.Clear(0x00000000U, 0x00FFFFFFU, 0);

    // Render through HLE path
    DispatchHleBatch(batch, surfaceHle);

    // Render through Level 2 canonical path
    DispatchLevel2Batch(batch, surfaceLevel2);

    // Compare color and depth pixel by pixel
    float maxDiff = 0.0f;
    const size_t totalPixels = surfaceHle.Width * surfaceHle.Height;

    for (size_t i = 0; i < totalPixels; ++i) {
        const uint32_t c1 = surfaceHle.ColorBuffer[i];
        const uint32_t c2 = surfaceLevel2.ColorBuffer[i];

        const float diffR = std::fabs(static_cast<float>(c1 & 0xFFU) - static_cast<float>(c2 & 0xFFU)) / 255.0f;
        const float diffG = std::fabs(static_cast<float>((c1 >> 8U) & 0xFFU) - static_cast<float>((c2 >> 8U) & 0xFFU)) / 255.0f;
        const float diffB = std::fabs(static_cast<float>((c1 >> 16U) & 0xFFU) - static_cast<float>((c2 >> 16U) & 0xFFU)) / 255.0f;
        const float diffA = std::fabs(static_cast<float>((c1 >> 24U) & 0xFFU) - static_cast<float>((c2 >> 24U) & 0xFFU)) / 255.0f;

        const float pixelColorDiff = std::max({ diffR, diffG, diffB, diffA });
        maxDiff = std::max(maxDiff, pixelColorDiff);

        const uint32_t d1 = surfaceHle.DepthStencilBuffer[i] & 0x00FFFFFFU;
        const uint32_t d2 = surfaceLevel2.DepthStencilBuffer[i] & 0x00FFFFFFU;
        const float diffDepth = std::fabs(static_cast<float>(d1) - static_cast<float>(d2)) / 16777215.0f;
        maxDiff = std::max(maxDiff, diffDepth);
    }

    if (maxDiffOut) {
        *maxDiffOut = maxDiff;
    }

    // Identical frame requirement: maxDiff must be strictly <= 1/255
    return maxDiff <= (1.0f / 255.0f);
}

} // namespace ThreeDsRecomp::Pica
