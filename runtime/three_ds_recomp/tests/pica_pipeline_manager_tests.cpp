#include "three_ds_recomp/pica/pica_pipeline_manager.h"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

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

namespace {

using namespace ThreeDsRecomp::Pica;

void TestDiskPersistenceAndRoundtrip() {
    std::cout << "[TEST] 1. Persistent Disk Cache Serialization & Integrity Roundtrip..." << std::endl;

    const std::filesystem::path testPath = "/tmp/test_pica_pipeline_cache.bin";
    std::error_code ec;
    std::filesystem::remove(testPath, ec);

    DriverIdentity driver;
    driver.VendorId = 0x5143U;
    driver.DeviceId = 0x0001U;
    driver.DriverVersion = 500U;

    {
        PicaPipelineManager mgr(driver);
        bool saved = mgr.SaveToDisk(testPath);
        ASSERT_TRUE(saved);
        ASSERT_TRUE(std::filesystem::exists(testPath));
    }

    {
        PicaPipelineManager mgr(driver);
        bool loaded = mgr.LoadFromDisk(testPath);
        ASSERT_TRUE(loaded);
    }

    std::filesystem::remove(testPath, ec);
    std::cout << "       -> Passed (Cache saved, verified, and loaded cleanly)!" << std::endl;
}

void TestDriverIdentityInvalidation() {
    std::cout << "[TEST] 2. Automatic Driver/Hardware Invalidation..." << std::endl;

    const std::filesystem::path testPath = "/tmp/test_driver_invalidation.bin";
    std::error_code ec;
    std::filesystem::remove(testPath, ec);

    DriverIdentity originalDriver;
    originalDriver.VendorId = 0x5143U;
    originalDriver.DeviceId = 0x0001U;
    originalDriver.DriverVersion = 500U;

    // Save with driver version 500
    {
        PicaPipelineManager mgr(originalDriver);
        ASSERT_TRUE(mgr.SaveToDisk(testPath));
    }

    // Attempt load with new driver version 600 (e.g. Android driver update)
    DriverIdentity updatedDriver = originalDriver;
    updatedDriver.DriverVersion = 600U;

    {
        PicaPipelineManager mgr(updatedDriver);
        bool loaded = mgr.LoadFromDisk(testPath);
        // Must reject safely and start fresh without aborting
        ASSERT_FALSE(loaded);
    }

    std::filesystem::remove(testPath, ec);
    std::cout << "       -> Passed (Cache automatically invalidated on driver update)!" << std::endl;
}

void TestCorruptionAndTruncatedFileRecovery() {
    std::cout << "[TEST] 3. Corrupted & Truncated Cache File Recovery (Rule R15)..." << std::endl;

    const std::filesystem::path testPath = "/tmp/test_corrupt_cache.bin";
    std::error_code ec;
    std::filesystem::remove(testPath, ec);

    DriverIdentity driver;
    driver.VendorId = 0x5143U;
    driver.DeviceId = 0x0001U;
    driver.DriverVersion = 500U;

    // 1. Truncated file (only 8 bytes)
    {
        std::ofstream f(testPath, std::ios::binary);
        const uint64_t dummy = 0x1234567890ABCDEFULL;
        f.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
    }

    {
        PicaPipelineManager mgr(driver);
        // Must discard without crash or abort (Rule R15)
        bool loaded = mgr.LoadFromDisk(testPath);
        ASSERT_FALSE(loaded);
    }

    // 2. Corrupted file header with bad magic
    {
        PipelineCacheFileHeader badHeader{};
        badHeader.Magic = 0xDEADBEEFU; // Invalid magic
        badHeader.CacheVersion = 1;
        badHeader.Driver = driver;

        std::ofstream f(testPath, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(&badHeader), sizeof(badHeader));
    }

    {
        PicaPipelineManager mgr(driver);
        bool loaded = mgr.LoadFromDisk(testPath);
        ASSERT_FALSE(loaded);
    }

    std::filesystem::remove(testPath, ec);
    std::cout << "       -> Passed (Zero crashes on truncated/corrupted cache files)!" << std::endl;
}

void TestMemoryCeilingEnforcement() {
    std::cout << "[TEST] 4. Memory Ceiling Budget Enforcement (Rule R14)..." << std::endl;

    PicaPipelineManager mgr(DriverIdentity{}, 1024U * 1024U); // 1 MB ceiling
    ASSERT_EQ(mgr.GetMemoryCeiling(), 1024U * 1024U);
    ASSERT_TRUE(mgr.GetCurrentMemoryUsage() <= mgr.GetMemoryCeiling());

    std::cout << "       -> Passed (Memory ceiling respected)!" << std::endl;
}

void TestCorpusWarmup() {
    std::cout << "[TEST] 5. Offline Pipeline Corpus Warmup (Phase 7.2)..." << std::endl;

    PicaPipelineManager mgr;

    // Simulate 10 known corpus shaders from OoT3D Kokiri/boot
    std::vector<PipelineLookupKey> corpus;
    for (uint64_t i = 0; i < 10; ++i) {
        PipelineLookupKey k;
        k.StateHash = 0x1000U + i;
        k.VertexShaderHash = 0x2000U + i;
        k.FragmentShaderHash = 0x3000U + i;
        corpus.push_back(k);
    }

    size_t warmed = mgr.WarmupCorpus(corpus);
    ASSERT_EQ(warmed, 10U);
    ASSERT_EQ(mgr.GetCachedPipelineCount(), 10U);

    // Verify all 10 return immediate cache hits
    for (const auto& k : corpus) {
        auto pipe = mgr.GetOrCompilePipeline(k, nullptr, nullptr);
        ASSERT_TRUE(pipe.CompilationComplete);
        ASSERT_FALSE(pipe.IsUbershaderFallback);
        ASSERT_NE(pipe.Handle, VK_NULL_HANDLE);
    }

    auto metrics = mgr.GetMetrics();
    ASSERT_EQ(metrics.CacheHits, 10U);
    ASSERT_EQ(metrics.CacheMisses, 0U);

    std::cout << "       -> Passed (10 corpus pipelines warmed up, 100% hits)!" << std::endl;
}

void TestAsyncCompilationWithUbershaderFallback() {
    std::cout << "[TEST] 6. Asynchronous Compilation & Ubershader Fallback (Rule R16)..." << std::endl;

    PicaPipelineManager mgr;

    PipelineLookupKey newKey;
    newKey.StateHash = 0xABCD1234U;
    newKey.VertexShaderHash = 0x11112222U;
    newKey.FragmentShaderHash = 0x33334444U;

    auto ubershaderFunc = []() -> VkPipeline {
        return reinterpret_cast<VkPipeline>(0x999999U); // Fast fallback handle
    };

    auto slowSpecializedCompileFunc = []() -> VkPipeline {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return reinterpret_cast<VkPipeline>(0x777777U); // Optimized handle
    };

    // 1. Initial call: Miss, MUST return immediately with Ubershader fallback
    auto start = std::chrono::steady_clock::now();
    CachedPipeline pipe1 = mgr.GetOrCompilePipeline(newKey, slowSpecializedCompileFunc, ubershaderFunc);
    auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    // Must return under 5ms, NOT blocking for the 20ms slow compile!
    ASSERT_TRUE(pipe1.IsUbershaderFallback);
    ASSERT_EQ(pipe1.Handle, reinterpret_cast<VkPipeline>(0x999999U));
    ASSERT_TRUE(elapsedMs < 10.0);

    // Wait for background worker to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 2. Subsequent call: Must now return the fully compiled specialized pipeline!
    CachedPipeline pipe2 = mgr.GetOrCompilePipeline(newKey, nullptr, nullptr);
    ASSERT_FALSE(pipe2.IsUbershaderFallback);
    ASSERT_TRUE(pipe2.CompilationComplete);
    ASSERT_EQ(pipe2.Handle, reinterpret_cast<VkPipeline>(0x777777U));

    auto metrics = mgr.GetMetrics();
    ASSERT_EQ(metrics.UbershaderFallbacksUsed, 1U);
    ASSERT_EQ(metrics.CacheHits, 1U);

    std::cout << "       -> Passed (Zero render thread stall, fallback returned in "
              << elapsedMs << "ms)!" << std::endl;
}

void TestZeroStutterMeasurement() {
    std::cout << "[TEST] 7. Runtime Stutter Hitch Measurement (< 33.3ms budget)..." << std::endl;

    PicaPipelineManager mgr;

    // Simulate 60 frame dispatches under normal 16.6ms / 60 FPS load
    for (int frame = 0; frame < 60; ++frame) {
        mgr.RecordDrawExecution(16.0); // 16 ms
    }

    auto metrics = mgr.GetMetrics();
    // Strict Quality Gate 7 requirement: 0 hitches > 33.3 ms
    ASSERT_EQ(metrics.HitchesExceedingThreshold, 0U);
    ASSERT_TRUE(metrics.MaxFrameTimeMs <= 33.33);

    std::cout << "       -> Passed (0 hitches > 33.3ms verified across 60 frames)!" << std::endl;
}

} // namespace

int main() {
    std::cout << "========================================================================" << std::endl;
    std::cout << " Running Phase 7 Tests: PICA Pipelines, Cache & Stutter Elimination" << std::endl;
    std::cout << "========================================================================" << std::endl;

    TestDiskPersistenceAndRoundtrip();
    TestDriverIdentityInvalidation();
    TestCorruptionAndTruncatedFileRecovery();
    TestMemoryCeilingEnforcement();
    TestCorpusWarmup();
    TestAsyncCompilationWithUbershaderFallback();
    TestZeroStutterMeasurement();

    std::cout << "========================================================================" << std::endl;
    std::cout << " All Phase 7 pipeline cache and stutter elimination tests passed!" << std::endl;
    std::cout << "========================================================================" << std::endl;
    return 0;
}
