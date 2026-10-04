#pragma once

#include "three_ds_recomp/pica/pica_raster_pipeline.h"
#include "three_ds_recomp/pica/pica_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Pica {

/**
 * High-Level Emulation (HLE) Mesh Batch.
 * Direct representation of game engine geometry, materials, and transform matrices,
 * bypassing 3DS PICA register packet encoding/decoding (Phase 8).
 */
struct HleMeshBatch {
    // Spatial transform
    std::array<float, 16> ModelViewMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::array<float, 16> ProjectionMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    // Material and Texture
    uint32_t TextureAddress = 0x10000000U;
    uint16_t TextureWidth = 64;
    uint16_t TextureHeight = 64;
    TextureFormat TexFormat = TextureFormat::Rgba8;
    std::array<float, 4> DiffuseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, 4> AmbientColor{ 0.2f, 0.2f, 0.2f, 1.0f };

    // Raster & Depth/Blend states
    bool DepthTest = true;
    bool DepthWrite = true;
    CompareFunc DepthFunc = CompareFunc::Less;
    bool AlphaTest = false;
    uint8_t AlphaRef = 128;
    bool BlendEnable = false;
    BlendFactor SrcBlend = BlendFactor::SourceAlpha;
    BlendFactor DstBlend = BlendFactor::OneMinusSourceAlpha;

    // Viewport & Resolution
    uint32_t TargetWidth = 400;
    uint32_t TargetHeight = 240;
    float AspectRatio = 5.0f / 3.0f; // 400:240 native 3DS, or 16:9 widescreen
    float ResolutionScale = 1.0f;    // 1x, 2x, 4x internal resolution

    // Geometry rect coordinates in screen space [x0, y0, x1, y1] and depth Z
    int ScreenRect[4] = { 0, 0, 400, 240 };
    float DepthZ = 0.5f;
};

/**
 * Metrics tracking HLE efficiency and Level 2 fallbacks (Rules R26, R32).
 */
struct HleMetrics {
    size_t DirectHleDrawCalls = 0;
    size_t Level2FallbackDrawCalls = 0;
    size_t RegisterPacketsBypassed = 0;
    size_t WidescreenDrawsEmitted = 0;
};

/**
 * Phase 8 Graphics HLE Bridge:
 * Direct connection between recomp game SDK draw functions and native Vulkan pipeline,
 * maintaining Level 2 (PICA state decoder) as validation oracle (Quality Gate 8).
 */
class PicaHleBridge {
public:
    PicaHleBridge() = default;

    /**
     * Converts a high-level game mesh batch into a canonical Level 2 PicaDrawState.
     */
    static PicaDrawState ConvertHleBatchToLevel2State(const HleMeshBatch& batch) noexcept;

    /**
     * Executes the draw call through the Direct HLE path.
     */
    bool DispatchHleBatch(
        const HleMeshBatch& batch,
        RasterSurface& surface);

    /**
     * Executes the draw call through the Level 2 PICA register state path.
     */
    bool DispatchLevel2Batch(
        const HleMeshBatch& batch,
        RasterSurface& surface);

    /**
     * Validates frame parity between the Direct HLE path and the Level 2 path.
     * Criterion for Quality Gate 8: Frame must be identical (diff == 0 / within rounding tolerance).
     */
    bool ValidateFrameParity(
        const HleMeshBatch& batch,
        RasterSurface& surfaceHle,
        RasterSurface& surfaceLevel2,
        float* maxDiffOut = nullptr);

    [[nodiscard]] HleMetrics GetMetrics() const noexcept { return mMetrics; }
    void ResetMetrics() noexcept { mMetrics = HleMetrics{}; }

    [[nodiscard]] bool IsWidescreenEnabled() const noexcept { return mWidescreenEnabled; }
    void SetWidescreenEnabled(bool enabled) noexcept { mWidescreenEnabled = enabled; }

private:
    bool mWidescreenEnabled = false;
    HleMetrics mMetrics{};
};

} // namespace ThreeDsRecomp::Pica
