#pragma once

#include "three_ds_recomp/pica/pica_state.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Pica {

struct TextureCacheKey {
    uint32_t PhysicalAddress = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    TextureFormat Format = TextureFormat::Rgba8;
    uint64_t ContentHash = 0;

    bool operator<(const TextureCacheKey& other) const noexcept {
        if (PhysicalAddress != other.PhysicalAddress) return PhysicalAddress < other.PhysicalAddress;
        if (Width != other.Width) return Width < other.Width;
        if (Height != other.Height) return Height < other.Height;
        if (Format != other.Format) return static_cast<uint8_t>(Format) < static_cast<uint8_t>(other.Format);
        return ContentHash < other.ContentHash;
    }

    bool operator==(const TextureCacheKey& other) const noexcept {
        return PhysicalAddress == other.PhysicalAddress &&
               Width == other.Width &&
               Height == other.Height &&
               Format == other.Format &&
               ContentHash == other.ContentHash;
    }
};

struct DecodedTexture {
    uint16_t Width = 0;
    uint16_t Height = 0;
    bool IsNativeEtc2 = false; // When true, passes directly to Vulkan ETC2 hardware sampler
    std::vector<uint8_t> Data; // RGBA8 bytes or ETC2 payload bytes
    size_t ByteSize = 0;
};

class PicaTextureCache {
public:
    // Default ceiling: 128 MB for target Adreno device (Rule R14)
    explicit PicaTextureCache(size_t maxMemoryBytes = 128U * 1024U * 1024U);

    void SetMemoryCeiling(size_t maxMemoryBytes) noexcept {
        mMaxMemoryBytes = maxMemoryBytes;
    }

    [[nodiscard]] size_t GetMemoryCeiling() const noexcept {
        return mMaxMemoryBytes;
    }

    [[nodiscard]] size_t GetCurrentMemoryUsage() const noexcept {
        return mCurrentMemoryUsage;
    }

    [[nodiscard]] size_t GetEntryCount() const noexcept {
        return mEntries.size();
    }

    void Clear();

    // Decode and cache texture, returning decoded texture reference
    std::shared_ptr<const DecodedTexture> AcquireTexture(
        const TextureCacheKey& key,
        std::span<const uint8_t> rawTiledBytes,
        bool preferNativeEtc2 = true);

    // Standalone Morton 8x8 deswizzler and format decoders
    static std::vector<uint8_t> DeswizzleMorton(
        std::span<const uint8_t> tiledBytes, uint32_t width, uint32_t height, uint32_t bytesPerPixel);

    static std::vector<uint8_t> DecodeToRgba8(
        TextureFormat format, uint16_t width, uint16_t height, std::span<const uint8_t> tiledBytes);

private:
    void EvictIfNecessary(size_t requiredBytes);

    size_t mMaxMemoryBytes = 0;
    size_t mCurrentMemoryUsage = 0;
    std::map<TextureCacheKey, std::shared_ptr<DecodedTexture>> mEntries;
};

} // namespace ThreeDsRecomp::Pica
