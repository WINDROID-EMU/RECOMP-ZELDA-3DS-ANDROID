#pragma once

#include "pica_state.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Pica {

enum class AdrenoDriverFamily : uint8_t {
    QualcommStock = 0,
    MesaTurnip    = 1
};

enum class AdrenoGpuGeneration : uint8_t {
    Adreno600 = 0, // e.g. Snapdragon 865 / Adreno 650
    Adreno700 = 1, // e.g. Snapdragon 8 Gen 1/2 / Adreno 730/740
    Adreno800 = 2  // e.g. Snapdragon 8 Elite / Adreno 830 (Galaxy S25)
};

struct DriverProfile {
    std::string deviceName;
    AdrenoDriverFamily driverFamily = AdrenoDriverFamily::QualcommStock;
    AdrenoGpuGeneration generation = AdrenoGpuGeneration::Adreno700;
    uint32_t driverVersion = 0;
    bool supportsTileShading = false;              // VK_QCOM_tile_shading
    bool supportsRelaxedPrecision = true;          // VK_KHR_shader_relaxed_precision (FP16 mediump)
    bool supportsCustomBorderColor = true;         // VK_EXT_custom_border_color
    bool supportsInputAttachmentFeedback = true;   // Local subpass color feedback
    bool supportsTextureCompressionETC2 = true;    // Hardware ETC2 sampler decompression
    float maxAllowedTolerance = 1.0f / 255.0f;     // Absolute channel diff threshold

    static DriverProfile CreateQualcommStockA600();
    static DriverProfile CreateQualcommStockA700();
    static DriverProfile CreateQualcommStockA800();
    static DriverProfile CreateMesaTurnipA600_A700();
    static DriverProfile CreateMesaTurnipA800();
};

struct GoldenFrame {
    std::string sceneId;
    std::string description;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgbaReference; // RGBA8 (4 bytes per pixel)
    std::vector<std::string> featureTags;
};

struct PixelDiffMetrics {
    uint32_t totalPixels = 0;
    uint32_t matchedPixels = 0;
    uint32_t mismatchedPixels = 0;
    float maxDiff = 0.0f;   // 0.0f .. 1.0f
    float meanDiff = 0.0f;  // 0.0f .. 1.0f
    float psnrDb = 0.0f;    // Peak Signal-to-Noise Ratio (dB)
    bool passed = false;
};

struct FeatureValidationEntry {
    std::string featureCategory;
    std::string featureItem;
    bool tested = false;
    bool passed = false;
    float maxDiff = 0.0f;
    std::string details;
};

struct ValidationMatrixReport {
    DriverProfile profile;
    std::vector<std::pair<std::string, PixelDiffMetrics>> goldenFrameResults;
    std::vector<FeatureValidationEntry> featureChecklist;
    bool allPassed = false;

    [[nodiscard]] std::string GenerateMarkdownSummary() const;
};

class PicaValidationMatrix {
public:
    /// Generates canonical golden frames representing the 5 key test scenarios
    /// documented in Phase 0 (boot_title, kokiri_cutscene, kokiri_gameplay, scene_ydan, effects_particles).
    static std::vector<GoldenFrame> CreateCanonicalGoldenFrames();

    /// Renders a frame using the canonical PICA software oracle under the specified scene ID.
    static std::vector<uint8_t> RenderCanonicalScene(const std::string& sceneId, uint32_t width, uint32_t height, const DriverProfile& profile);

    /// Compares two RGBA8 framebuffers with per-pixel tolerance and produces complete metrics (max diff, mean diff, PSNR).
    static PixelDiffMetrics CompareBuffers(
        const std::vector<uint8_t>& reference,
        const std::vector<uint8_t>& actual,
        uint32_t width,
        uint32_t height,
        float tolerance = 1.0f / 255.0f);

    /// Executes the complete feature checklist (TEV 10 functions, lighting configs 0-7, fog, shadows/stencil, decals, GS absence).
    static std::vector<FeatureValidationEntry> RunFeatureChecklist(const DriverProfile& profile);

    /// Executes the full validation matrix for a single driver profile against a set of golden frames.
    static ValidationMatrixReport RunMatrixForDriver(
        const DriverProfile& profile,
        const std::vector<GoldenFrame>& goldenFrames);

    /// Executes validation across all supported Adreno driver profiles (Stock A600/700/800 and Turnip A600/700/800).
    static std::vector<ValidationMatrixReport> RunAllDriversMatrix(
        const std::vector<DriverProfile>& profiles);

    /// Combines all driver reports into a single comprehensive Markdown matrix report.
    static std::string GenerateCombinedReport(const std::vector<ValidationMatrixReport>& reports);
};

} // namespace ThreeDsRecomp::Pica
