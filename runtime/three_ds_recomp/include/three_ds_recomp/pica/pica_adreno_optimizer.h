#pragma once

#include "three_ds_recomp/pica/pica_state.h"
#include "three_ds_recomp/pica/pica_shader.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Pica {

enum class AdrenoResolutionScale : uint8_t {
    Scale1x = 1, // 400x240 (Native 3DS)
    Scale2x = 2, // 800x480 (720p Mobile equivalent)
    Scale3x = 3, // 1200x720 (High Definition)
    Scale4x = 4  // 1600x960 (Ultra High Definition / 4K equivalent)
};

enum class AdrenoThermalStatus : uint8_t {
    Nominal  = 0,
    Light    = 1,
    Moderate = 2,
    Severe   = 3,
    Critical = 4
};

struct AdrenoOptimizationProfile {
    AdrenoResolutionScale resolutionScale = AdrenoResolutionScale::Scale2x;
    bool enableWidescreen = true;
    bool fastMulDefault = true;                  // Fast IEEE mul by default (Phase 10 specification)
    bool mediumpPrecisionTev = true;             // FP16 mediump RelaxedPrecision (Rule R28)
    bool hardwareEtc2Compression = true;         // Zero-copy ETC2 HW decompression (Rule R28)
    bool gmemDontCareDepthStore = true;          // DepthStoreOp = DONT_CARE (Phase 6 & 10)
    uint32_t targetFps = 60;                     // 60 FPS target
    bool thermalThrottlingMitigation = true;     // ADPF thermal mitigation (Rule R30)
};

struct AdrenoScaledDimensions {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t alignedWidth = 0;                   // Aligned to Adreno tile granularity (32px)
    uint32_t alignedHeight = 0;                  // Aligned to Adreno tile granularity (16px)
    float scaleFactor = 1.0f;
    float aspectRatio = 5.0f / 3.0f;
};

struct AdrenoPerformanceMetrics {
    // Memory and Bandwidth metrics (Bytes per frame)
    uint64_t baselineDramBandwidthBytes = 0;
    uint64_t optimizedDramBandwidthBytes = 0;
    float bandwidthReductionPercent = 0.0f;

    // ALU Instruction count per vertex transformation
    uint32_t baselineAluOpsPerVertex = 0;
    uint32_t optimizedAluOpsPerVertex = 0;
    float aluSavingsPercent = 0.0f;

    // CPU Draw Call Dispatch overhead (microseconds per draw)
    float baselineCpuDrawOverheadUs = 0.0f;
    float optimizedCpuDrawOverheadUs = 0.0f;
    float cpuOverheadSavingsPercent = 0.0f;

    // Estimated Frame Times (milliseconds)
    float estimatedCpuTimeMs = 0.0f;
    float estimatedGpuTimeMs = 0.0f;
    float targetFrameBudgetMs = 16.666f;         // 60 FPS budget
    bool sustains60Fps = false;
};

struct NativityIndexReport {
    float percentDrawsNative = 100.0f;           // R32: % of draws in native path
    uint32_t activeNativeFallbacks = 0;          // R26: Count of active fallbacks in hot path
    float cpuFrameTimeMs = 0.0f;
    float gpuFrameTimeMs = 0.0f;
    std::string hardwareFeaturesInUse;
    std::string missingForFullNativity;

    [[nodiscard]] std::string GenerateReportString() const;
};

class PicaAdrenoOptimizer {
public:
    /// Computes scaled render target dimensions according to resolution scale,
    /// widescreen setting, and Adreno hardware tile granularity (32x16).
    static AdrenoScaledDimensions ComputeScaledDimensions(
        uint32_t baseWidth,
        uint32_t baseHeight,
        AdrenoResolutionScale scale,
        bool widescreen);

    /// Configures vertex shader compiler configuration matching Phase 10 optimization profile:
    /// Fast native multiplication by default, with exact non-IEEE fallback only when scene tags require it.
    static PicaShaderConfig ConfigureShaderOptimization(
        const AdrenoOptimizationProfile& profile,
        bool sceneRequiresExactMul = false);

    /// Evaluates performance gains (Bandwidth, ALU, CPU overhead and Frametime)
    /// comparing unoptimized baseline (Citra/Level 1 register emulation) against Phase 10 native Adreno tuning.
    static AdrenoPerformanceMetrics MeasurePerformanceGains(
        const AdrenoOptimizationProfile& profile,
        uint32_t estimatedDrawCount = 120,
        uint32_t estimatedVertexCount = 45000);

    /// Adjusts resolution scale and quality settings preventively according to Android ADPF thermal status
    /// before hardware thermal throttling degrades performance (Rules R30, R31).
    static bool AdjustForThermalState(
        AdrenoThermalStatus thermalStatus,
        AdrenoOptimizationProfile& inOutProfile);

    /// Generates the mandatory Nativity Index (Rule R32) summarizing full native execution.
    static NativityIndexReport GenerateNativityIndex(
        const AdrenoPerformanceMetrics& metrics,
        uint32_t activeFallbacks = 0);
};

} // namespace ThreeDsRecomp::Pica
