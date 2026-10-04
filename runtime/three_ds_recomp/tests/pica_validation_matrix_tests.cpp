#include "three_ds_recomp/pica/pica_validation_matrix.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace ThreeDsRecomp::Pica;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

int main() {
    std::cout << "========================================================================\n";
    std::cout << " Running Phase 9 Tests: Continuous Validation, Golden Frames & Drivers\n";
    std::cout << "========================================================================\n";

    // -------------------------------------------------------------------------
    // Test 1: Canonical Golden Frames Generation and Self-Comparison (0 Diff)
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 1. Generating Canonical Golden Frames (5 Key Scenes)...\n";
    auto goldenFrames = PicaValidationMatrix::CreateCanonicalGoldenFrames();
    TEST_ASSERT(goldenFrames.size() == 5, "Expected 5 canonical golden frames");

    for (const auto& gf : goldenFrames) {
        TEST_ASSERT(gf.width == 64 && gf.height == 64, "Golden frame dimensions mismatch");
        TEST_ASSERT(gf.rgbaReference.size() == 64 * 64 * 4, "Golden frame buffer size mismatch");

        // Self-comparison must be bit-exact
        auto metrics = PicaValidationMatrix::CompareBuffers(
            gf.rgbaReference, gf.rgbaReference, gf.width, gf.height, 1.0f / 255.0f);
        TEST_ASSERT(metrics.passed, "Self-comparison must pass");
        TEST_ASSERT(metrics.mismatchedPixels == 0, "Self-comparison must have 0 mismatches");
        TEST_ASSERT(metrics.maxDiff == 0.0f, "Self-comparison maxDiff must be 0.0f");
        TEST_ASSERT(metrics.psnrDb >= 90.0f, "Self-comparison PSNR must be >= 90 dB");
    }
    std::cout << "       -> Passed (5 golden scenes generated and verified with 0 diff)!\n";

    // -------------------------------------------------------------------------
    // Test 2: Buffer Comparison Metrics and Tolerance Thresholding
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 2. Pixel Buffer Comparison & Tolerance Enforcement...\n";
    {
        std::vector<uint8_t> ref(16 * 16 * 4, 128);
        std::vector<uint8_t> actWithinTol = ref;
        // Perturb 1 channel of 1 pixel by 1 unit (1/255 = 0.00392f <= tolerance)
        actWithinTol[0] = 129;

        auto metWithin = PicaValidationMatrix::CompareBuffers(ref, actWithinTol, 16, 16, 1.0f / 255.0f);
        TEST_ASSERT(metWithin.passed, "Diff of 1/255 should pass within tolerance");
        TEST_ASSERT(metWithin.mismatchedPixels == 0, "No pixel exceeds tolerance threshold");
        TEST_ASSERT(std::abs(metWithin.maxDiff - (1.0f / 255.0f)) < 1e-5f, "Max diff accurately measured");

        // Perturb above tolerance
        std::vector<uint8_t> actAboveTol = ref;
        actAboveTol[0] = 135; // Diff = 7/255 > 1/255
        auto metAbove = PicaValidationMatrix::CompareBuffers(ref, actAboveTol, 16, 16, 1.0f / 255.0f);
        TEST_ASSERT(!metAbove.passed, "Diff of 7/255 must fail tolerance threshold");
        TEST_ASSERT(metAbove.mismatchedPixels == 1, "Expected exactly 1 mismatched pixel");
    }
    std::cout << "       -> Passed!\n";

    // -------------------------------------------------------------------------
    // Test 3: Feature Checklist (TEV, Lighting, Fog, Shadow/Decal, R17)
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 3. Comprehensive Feature Checklist Execution...\n";
    auto baselineProfile = DriverProfile::CreateQualcommStockA800();
    auto checklist = PicaValidationMatrix::RunFeatureChecklist(baselineProfile);

    size_t tevCount = 0;
    size_t lightingCount = 0;
    size_t fogCount = 0;
    size_t shadowDecalCount = 0;
    size_t r17Count = 0;

    for (const auto& item : checklist) {
        TEST_ASSERT(item.tested, "All features in checklist must be tested");
        TEST_ASSERT(item.passed, ("Feature failed: " + item.featureItem).c_str());

        if (item.featureCategory == "TEV") tevCount++;
        else if (item.featureCategory == "Lighting") lightingCount++;
        else if (item.featureCategory == "Fog") fogCount++;
        else if (item.featureCategory == "Shadow_Decal") shadowDecalCount++;
        else if (item.featureCategory == "Architecture_Rule_R17") r17Count++;
    }

    TEST_ASSERT(tevCount == 10, "Expected 10 TEV operations tested");
    TEST_ASSERT(lightingCount == 8, "Expected 8 lighting configurations tested");
    TEST_ASSERT(fogCount >= 1, "Expected fog test executed");
    TEST_ASSERT(shadowDecalCount >= 2, "Expected stencil shadow and decal tested");
    TEST_ASSERT(r17Count >= 2, "Expected R17 architecture checks tested");
    std::cout << "       -> Passed (" << checklist.size() << " feature checks verified successfully)!\n";

    // -------------------------------------------------------------------------
    // Test 4: Driver Matrix (Qualcomm Stock A600/700/800 & Mesa Turnip A600/700/800)
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 4. Multi-Driver Matrix Regression (Adreno Generations)...\n";
    std::vector<DriverProfile> driverProfiles = {
        DriverProfile::CreateQualcommStockA600(),
        DriverProfile::CreateQualcommStockA700(),
        DriverProfile::CreateQualcommStockA800(),
        DriverProfile::CreateMesaTurnipA600_A700(),
        DriverProfile::CreateMesaTurnipA800()
    };

    auto reports = PicaValidationMatrix::RunAllDriversMatrix(driverProfiles);
    TEST_ASSERT(reports.size() == 5, "Expected 5 driver profile reports");

    for (const auto& rep : reports) {
        TEST_ASSERT(rep.allPassed, ("Driver report failed: " + rep.profile.deviceName).c_str());
        TEST_ASSERT(rep.goldenFrameResults.size() == 5, "Expected 5 golden frame results per driver");
        for (const auto& [sceneId, metrics] : rep.goldenFrameResults) {
            TEST_ASSERT(metrics.passed, ("Scene " + sceneId + " failed under " + rep.profile.deviceName).c_str());
            TEST_ASSERT(metrics.maxDiff <= rep.profile.maxAllowedTolerance, "Max diff exceeds profile tolerance");
        }
    }
    std::cout << "       -> Passed (All 5 driver profiles passed 100% of scenes with diff <= 1/255)!\n";

    // -------------------------------------------------------------------------
    // Test 5: Markdown Report Generation
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 5. Markdown Report Synthesis for Continuous Integration...\n";
    std::string reportMd = PicaValidationMatrix::GenerateCombinedReport(reports);
    TEST_ASSERT(!reportMd.empty(), "Report must not be empty");
    TEST_ASSERT(reportMd.find("Qualcomm Adreno 830") != std::string::npos, "Report must mention Adreno 830");
    TEST_ASSERT(reportMd.find("Mesa Turnip") != std::string::npos, "Report must mention Mesa Turnip");
    TEST_ASSERT(reportMd.find("TEV_Op_Replace") != std::string::npos, "Report must list TEV features");
    std::cout << "       -> Passed (Report generated with " << reportMd.size() << " bytes)!\n";

    std::cout << "========================================================================\n";
    std::cout << " All Phase 9 validation matrix tests passed successfully (0 diff)!\n";
    std::cout << "========================================================================\n";
    return 0;
}
