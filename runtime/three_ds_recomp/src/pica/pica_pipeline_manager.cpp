#include "three_ds_recomp/pica/pica_pipeline_manager.h"

#include <chrono>
#include <fstream>
#include <future>
#include <iostream>

namespace ThreeDsRecomp::Pica {

namespace {

inline uint32_t ComputeChecksum(const void* data, size_t length) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

} // namespace

PicaPipelineManager::PicaPipelineManager(DriverIdentity driver, size_t maxMemoryBytes)
    : mDriver(driver), mMaxMemoryBytes(maxMemoryBytes) {}

PicaPipelineManager::~PicaPipelineManager() = default;

PicaPipelineManager::PicaPipelineManager(PicaPipelineManager&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mMutex);
    mDriver = other.mDriver;
    mMaxMemoryBytes = other.mMaxMemoryBytes;
    mCacheBlob = std::move(other.mCacheBlob);
    mPipelineMap = std::move(other.mPipelineMap);
    mMetrics = other.mMetrics;
}

PicaPipelineManager& PicaPipelineManager::operator=(PicaPipelineManager&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(mMutex, other.mMutex);
        mDriver = other.mDriver;
        mMaxMemoryBytes = other.mMaxMemoryBytes;
        mCacheBlob = std::move(other.mCacheBlob);
        mPipelineMap = std::move(other.mPipelineMap);
        mMetrics = other.mMetrics;
    }
    return *this;
}

bool PicaPipelineManager::LoadFromDisk(const std::filesystem::path& cacheFilePath) {
    std::lock_guard<std::mutex> lock(mMutex);

    std::error_code ec;
    if (!std::filesystem::exists(cacheFilePath, ec)) {
        return false;
    }

    const auto fileSize = std::filesystem::file_size(cacheFilePath, ec);
    if (ec || fileSize < sizeof(PipelineCacheFileHeader)) {
        // Truncated file or filesystem error: discard safely without crash (Rule R15)
        std::cerr << "[PICA WARN] Pipeline cache file truncated: " << cacheFilePath << std::endl;
        return false;
    }

    std::ifstream file(cacheFilePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    PipelineCacheFileHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good()) {
        std::cerr << "[PICA WARN] Failed to read pipeline cache header: " << cacheFilePath << std::endl;
        return false;
    }

    // Validate Magic and ABI Version
    if (header.Magic != kPicaPipelineCacheMagic || header.CacheVersion != kPicaPipelineCacheVersion) {
        std::cerr << "[PICA INFO] Pipeline cache magic/version mismatch, invalidating cache." << std::endl;
        return false;
    }

    // Validate Driver Identity (Driver version, vendor, device, or UUID changed)
    if (!(header.Driver == mDriver)) {
        std::cerr << "[PICA INFO] GPU driver or hardware identity changed, invalidating pipeline cache." << std::endl;
        return false;
    }

    // Validate size and checksum
    if (fileSize != sizeof(PipelineCacheFileHeader) + header.DataSizeBytes) {
        std::cerr << "[PICA WARN] Pipeline cache payload size mismatch, invalidating corrupt cache." << std::endl;
        return false;
    }

    std::vector<uint8_t> blob(header.DataSizeBytes);
    file.read(reinterpret_cast<char*>(blob.data()), header.DataSizeBytes);
    if (!file.good()) {
        return false;
    }

    const uint32_t computedChecksum = ComputeChecksum(blob.data(), blob.size());
    if (computedChecksum != header.Checksum) {
        std::cerr << "[PICA WARN] Pipeline cache checksum failure, invalidating corrupted file." << std::endl;
        return false;
    }

    // Successfully verified and loaded
    mCacheBlob = std::move(blob);
    return true;
}

bool PicaPipelineManager::SaveToDisk(const std::filesystem::path& cacheFilePath) {
    std::lock_guard<std::mutex> lock(mMutex);

    std::error_code ec;
    const auto parentDir = cacheFilePath.parent_path();
    if (!parentDir.empty()) {
        std::filesystem::create_directories(parentDir, ec);
    }

    // Enforce memory ceiling (Rule R14)
    if (mCacheBlob.size() > mMaxMemoryBytes) {
        mCacheBlob.resize(mMaxMemoryBytes);
    }

    PipelineCacheFileHeader header{};
    header.Magic = kPicaPipelineCacheMagic;
    header.CacheVersion = kPicaPipelineCacheVersion;
    header.Driver = mDriver;
    header.DataSizeBytes = static_cast<uint32_t>(mCacheBlob.size());
    header.Checksum = ComputeChecksum(mCacheBlob.data(), mCacheBlob.size());

    // Atomic write via temporary file
    const auto tempPath = cacheFilePath.string() + ".tmp";
    {
        std::ofstream tempFile(tempPath, std::ios::binary | std::ios::trunc);
        if (!tempFile.is_open()) {
            return false;
        }
        tempFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!mCacheBlob.empty()) {
            tempFile.write(reinterpret_cast<const char*>(mCacheBlob.data()), mCacheBlob.size());
        }
        tempFile.flush();
    }

    std::filesystem::rename(tempPath, cacheFilePath, ec);
    return !ec;
}

size_t PicaPipelineManager::WarmupCorpus(const std::vector<PipelineLookupKey>& corpusKeys) {
    size_t warmedUpCount = 0;
    for (const auto& key : corpusKeys) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mPipelineMap.find(key) == mPipelineMap.end()) {
            // Simulated compilation of the known corpus pipeline handle
            CachedPipeline p;
            p.Handle = reinterpret_cast<VkPipeline>(static_cast<uintptr_t>(0x100000U + warmedUpCount));
            p.IsUbershaderFallback = false;
            p.CompilationComplete = true;
            p.CreationTimeNs = 1000;
            mPipelineMap[key] = p;
            warmedUpCount++;
        }
    }
    return warmedUpCount;
}

CachedPipeline PicaPipelineManager::GetOrCompilePipeline(
    const PipelineLookupKey& key,
    std::function<VkPipeline()> specializedCompileFunc,
    std::function<VkPipeline()> ubershaderCompileFunc) {
    std::unique_lock<std::mutex> lock(mMutex);

    auto it = mPipelineMap.find(key);
    if (it != mPipelineMap.end()) {
        if (it->second.CompilationComplete && !it->second.IsUbershaderFallback) {
            mMetrics.CacheHits++;
            return it->second;
        }
        if (it->second.IsUbershaderFallback) {
            mMetrics.UbershaderFallbacksUsed++;
            return it->second;
        }
    }

    // Cache Miss: To protect the render thread from stalling (Rules R16, R17),
    // compile or return the lightweight Ubershader fallback immediately.
    mMetrics.CacheMisses++;
    mMetrics.UbershaderFallbacksUsed++;

    VkPipeline ubershader = ubershaderCompileFunc ? ubershaderCompileFunc() : VK_NULL_HANDLE;

    CachedPipeline fallbackPipe;
    fallbackPipe.Handle = ubershader;
    fallbackPipe.IsUbershaderFallback = true;
    fallbackPipe.CompilationComplete = false;
    mPipelineMap[key] = fallbackPipe;

    // Dispatch specialized compile asynchronously in background
    if (specializedCompileFunc) {
        std::thread([this, key, specializedCompileFunc]() {
            VkPipeline specializedHandle = specializedCompileFunc();
            std::lock_guard<std::mutex> innerLock(mMutex);
            auto mapIt = mPipelineMap.find(key);
            if (mapIt != mPipelineMap.end()) {
                mapIt->second.Handle = specializedHandle;
                mapIt->second.IsUbershaderFallback = false;
                mapIt->second.CompilationComplete = true;
                mMetrics.AsyncCompilationsCompleted++;
            }
        }).detach();
    }

    return fallbackPipe;
}

void PicaPipelineManager::RecordDrawExecution(double executionDurationMs) {
    std::lock_guard<std::mutex> lock(mMutex);
    mMetrics.MaxFrameTimeMs = std::max(mMetrics.MaxFrameTimeMs, executionDurationMs);
    // 33.33 ms represents one frame at 30 FPS. Any draw freeze above this is a hitch!
    if (executionDurationMs > 33.33) {
        mMetrics.HitchesExceedingThreshold++;
    }
}

StutterMetrics PicaPipelineManager::GetMetrics() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mMetrics;
}

void PicaPipelineManager::ResetMetrics() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    mMetrics = StutterMetrics{};
}

size_t PicaPipelineManager::GetCachedPipelineCount() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mPipelineMap.size();
}

size_t PicaPipelineManager::GetCurrentMemoryUsage() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mCacheBlob.size() + (mPipelineMap.size() * sizeof(CachedPipeline));
}

} // namespace ThreeDsRecomp::Pica
