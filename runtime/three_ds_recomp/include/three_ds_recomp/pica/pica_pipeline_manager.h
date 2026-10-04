#pragma once

#include "three_ds_recomp/pica/pica_state.h"

#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ThreeDsRecomp::Pica {

inline constexpr uint32_t kPicaPipelineCacheMagic = 0x50494341U; // 'PICA'
inline constexpr uint32_t kPicaPipelineCacheVersion = 1U;
inline constexpr size_t kDefaultPipelineCacheCeilingBytes = 64U * 1024U * 1024U; // 64 MB (Rule R14)

/**
 * Hardware and driver identification block to safely invalidate on driver update.
 */
struct DriverIdentity {
    uint32_t VendorId = 0x5143U; // 0x5143 = Qualcomm
    uint32_t DeviceId = 0x0001U;
    uint32_t DriverVersion = 1000U;
    std::array<uint8_t, VK_UUID_SIZE> PipelineCacheUuid{};

    [[nodiscard]] bool operator==(const DriverIdentity& other) const noexcept {
        return VendorId == other.VendorId &&
               DeviceId == other.DeviceId &&
               DriverVersion == other.DriverVersion &&
               PipelineCacheUuid == other.PipelineCacheUuid;
    }
};

/**
 * On-disk header for persistent pipeline cache file.
 */
struct PipelineCacheFileHeader {
    uint32_t Magic = kPicaPipelineCacheMagic;
    uint32_t CacheVersion = kPicaPipelineCacheVersion;
    DriverIdentity Driver;
    uint32_t DataSizeBytes = 0;
    uint32_t Checksum = 0; // Integrity checksum for corruption tolerance (Rule R15)
};

/**
 * Unique 64-bit key identifying a compiled graphics pipeline.
 * Formed from state hash, vertex shader hash, and fragment shader hash.
 */
struct PipelineLookupKey {
    uint64_t StateHash = 0;
    uint64_t VertexShaderHash = 0;
    uint64_t FragmentShaderHash = 0;

    [[nodiscard]] bool operator==(const PipelineLookupKey& other) const noexcept {
        return StateHash == other.StateHash &&
               VertexShaderHash == other.VertexShaderHash &&
               FragmentShaderHash == other.FragmentShaderHash;
    }
};

} // namespace ThreeDsRecomp::Pica

namespace std {
template <>
struct hash<ThreeDsRecomp::Pica::PipelineLookupKey> {
    size_t operator()(const ThreeDsRecomp::Pica::PipelineLookupKey& k) const noexcept {
        size_t h = k.StateHash;
        h ^= (k.VertexShaderHash + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        h ^= (k.FragmentShaderHash + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        return h;
    }
};
} // namespace std

namespace ThreeDsRecomp::Pica {

/**
 * Representation of a ready or compiling pipeline handle.
 */
struct CachedPipeline {
    VkPipeline Handle = VK_NULL_HANDLE;
    bool IsUbershaderFallback = false;
    bool CompilationComplete = false;
    uint64_t CreationTimeNs = 0;
};

/**
 * Metrics tracking runtime stutter, hits, misses, and Adreno-friendly compilation (Quality Gate 7).
 */
struct StutterMetrics {
    size_t CacheHits = 0;
    size_t CacheMisses = 0;
    size_t UbershaderFallbacksUsed = 0;
    size_t AsyncCompilationsCompleted = 0;
    size_t HitchesExceedingThreshold = 0; // Hitches > 33.3 ms (Strict requirement: MUST BE 0!)
    double MaxFrameTimeMs = 0.0;
};

/**
 * Manages persistent on-disk pipeline cache, warm-up, and asynchronous compilation
 * with ubershader fallback to prevent runtime frame stutter on Adreno TBDR.
 */
class PicaPipelineManager {
public:
    explicit PicaPipelineManager(
        DriverIdentity driver = DriverIdentity{},
        size_t maxMemoryBytes = kDefaultPipelineCacheCeilingBytes);

    ~PicaPipelineManager();

    // Prevent copy, allow move
    PicaPipelineManager(const PicaPipelineManager&) = delete;
    PicaPipelineManager& operator=(const PicaPipelineManager&) = delete;
    PicaPipelineManager(PicaPipelineManager&&) noexcept;
    PicaPipelineManager& operator=(PicaPipelineManager&&) noexcept;

    /**
     * Loads persistent cache from disk. If corrupted, truncated, or from a different driver,
     * it gracefully discards and starts fresh without aborting (Rules R12, R15).
     */
    bool LoadFromDisk(const std::filesystem::path& cacheFilePath);

    /**
     * Saves cache data to disk with integrity checksum.
     */
    bool SaveToDisk(const std::filesystem::path& cacheFilePath);

    /**
     * Warms up known corpus pipelines before scene or gameplay starts (Phase 7.2).
     */
    size_t WarmupCorpus(const std::vector<PipelineLookupKey>& corpusKeys);

    /**
     * Retrieves an existing pipeline or dispatches async compilation.
     * If the specialized pipeline is still compiling, immediately returns the
     * synchronous Ubershader fallback without stalling the render thread (Rules R16, R17).
     */
    CachedPipeline GetOrCompilePipeline(
        const PipelineLookupKey& key,
        std::function<VkPipeline()> specializedCompileFunc,
        std::function<VkPipeline()> ubershaderCompileFunc);

    /**
     * Simulates draw dispatch with render-thread frame time measurement.
     * Records any stutter hitch exceeding the 33.3 ms (30 FPS budget) limit.
     */
    void RecordDrawExecution(double executionDurationMs);

    [[nodiscard]] StutterMetrics GetMetrics() const noexcept;
    void ResetMetrics() noexcept;

    [[nodiscard]] size_t GetCachedPipelineCount() const noexcept;
    [[nodiscard]] size_t GetCurrentMemoryUsage() const noexcept;
    [[nodiscard]] size_t GetMemoryCeiling() const noexcept { return mMaxMemoryBytes; }

    void SetMemoryCeiling(size_t bytes) noexcept { mMaxMemoryBytes = bytes; }

private:
    DriverIdentity mDriver;
    size_t mMaxMemoryBytes = kDefaultPipelineCacheCeilingBytes;
    std::vector<uint8_t> mCacheBlob;

    mutable std::mutex mMutex;
    std::unordered_map<PipelineLookupKey, CachedPipeline> mPipelineMap;
    StutterMetrics mMetrics{};
};

} // namespace ThreeDsRecomp::Pica
