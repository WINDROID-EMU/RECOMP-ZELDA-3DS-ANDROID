#include "three_ds_recomp/pica/pica_texture_cache.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ThreeDsRecomp::Pica {
namespace {

inline uint8_t Expand4To8(uint8_t val) noexcept {
    return static_cast<uint8_t>((val << 4U) | val);
}

inline uint8_t Expand5To8(uint8_t val) noexcept {
    return static_cast<uint8_t>((val << 3U) | (val >> 2U));
}

inline uint8_t Expand6To8(uint8_t val) noexcept {
    return static_cast<uint8_t>((val << 2U) | (val >> 4U));
}

inline uint32_t Morton8(uint32_t x, uint32_t y) noexcept {
    return ((x & 0x1U) << 0U) | ((y & 0x1U) << 1U) |
           ((x & 0x2U) << 1U) | ((y & 0x2U) << 2U) |
           ((x & 0x4U) << 2U) | ((y & 0x4U) << 3U);
}

constexpr std::array<std::array<int, 2>, 8> kEtc1Modifiers{{
    {2, 8}, {5, 17}, {9, 29}, {13, 42},
    {18, 60}, {24, 80}, {33, 106}, {47, 183}
}};

inline int SignExtend3(uint32_t val) noexcept {
    return (val & 0x4U) ? static_cast<int>(val | 0xFFFFFFF8U) : static_cast<int>(val);
}

inline uint8_t ClampU8(int val) noexcept {
    return static_cast<uint8_t>(std::clamp(val, 0, 255));
}

std::array<uint8_t, 3> SampleEtc1Texel(uint64_t block, uint32_t x, uint32_t y) noexcept {
    const uint32_t texel = 4U * x + y;
    const bool flip = ((block >> 32U) & 1U) != 0U;
    const bool diff = ((block >> 33U) & 1U) != 0U;
    const uint32_t table2 = static_cast<uint32_t>((block >> 34U) & 0x7U);
    const uint32_t table1 = static_cast<uint32_t>((block >> 37U) & 0x7U);
    const uint32_t lookupX = flip ? y : x;

    int r = 0, g = 0, b = 0;
    if (diff) {
        int baseR = static_cast<int>((block >> 59U) & 0x1FU);
        int baseG = static_cast<int>((block >> 51U) & 0x1FU);
        int baseB = static_cast<int>((block >> 43U) & 0x1FU);
        if (lookupX >= 2U) {
            baseR += SignExtend3(static_cast<uint32_t>((block >> 56U) & 0x7U));
            baseG += SignExtend3(static_cast<uint32_t>((block >> 48U) & 0x7U));
            baseB += SignExtend3(static_cast<uint32_t>((block >> 40U) & 0x7U));
        }
        r = Expand5To8(static_cast<uint8_t>(baseR & 0x1F));
        g = Expand5To8(static_cast<uint8_t>(baseG & 0x1F));
        b = Expand5To8(static_cast<uint8_t>(baseB & 0x1F));
    } else {
        if (lookupX < 2U) {
            r = Expand4To8(static_cast<uint8_t>((block >> 60U) & 0x0FU));
            g = Expand4To8(static_cast<uint8_t>((block >> 52U) & 0x0FU));
            b = Expand4To8(static_cast<uint8_t>((block >> 44U) & 0x0FU));
        } else {
            r = Expand4To8(static_cast<uint8_t>((block >> 56U) & 0x0FU));
            g = Expand4To8(static_cast<uint8_t>((block >> 48U) & 0x0FU));
            b = Expand4To8(static_cast<uint8_t>((block >> 40U) & 0x0FU));
        }
    }

    const uint32_t table = lookupX < 2U ? table1 : table2;
    const uint32_t subIndex = static_cast<uint32_t>((block >> texel) & 1U);
    int modifier = kEtc1Modifiers[table][subIndex];
    if (((block >> (16U + texel)) & 1U) != 0U) {
        modifier = -modifier;
    }
    return { ClampU8(r + modifier), ClampU8(g + modifier), ClampU8(b + modifier) };
}

} // namespace

PicaTextureCache::PicaTextureCache(size_t maxMemoryBytes)
    : mMaxMemoryBytes(maxMemoryBytes) {}

void PicaTextureCache::Clear() {
    mEntries.clear();
    mCurrentMemoryUsage = 0;
}

void PicaTextureCache::EvictIfNecessary(size_t requiredBytes) {
    while (mCurrentMemoryUsage + requiredBytes > mMaxMemoryBytes && !mEntries.empty()) {
        auto it = mEntries.begin();
        mCurrentMemoryUsage -= it->second->ByteSize;
        mEntries.erase(it);
    }
}

std::vector<uint8_t> PicaTextureCache::DeswizzleMorton(
    std::span<const uint8_t> tiledBytes, uint32_t width, uint32_t height, uint32_t bytesPerPixel) {
    const uint32_t alignedWidth = (width + 7U) & ~7U;
    std::vector<uint8_t> linear(static_cast<size_t>(width) * height * bytesPerPixel);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t tileIndex = (y / 8U) * (alignedWidth / 8U) + (x / 8U);
            const uint32_t tileOffset = Morton8(x % 8U, y % 8U);
            const size_t srcIdx = static_cast<size_t>(tileIndex * 64U + tileOffset) * bytesPerPixel;
            const size_t dstIdx = static_cast<size_t>(y * width + x) * bytesPerPixel;

            if (srcIdx + bytesPerPixel <= tiledBytes.size()) {
                std::memcpy(&linear[dstIdx], &tiledBytes[srcIdx], bytesPerPixel);
            }
        }
    }
    return linear;
}

std::vector<uint8_t> PicaTextureCache::DecodeToRgba8(
    TextureFormat format, uint16_t width, uint16_t height, std::span<const uint8_t> tiledBytes) {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba(pixelCount * 4U, 0xFFU);

    if (width == 0 || height == 0 || tiledBytes.empty()) {
        return rgba;
    }

    switch (format) {
        case TextureFormat::Rgba8: {
            rgba = DeswizzleMorton(tiledBytes, width, height, 4U);
            break;
        }

        case TextureFormat::Rgb8: {
            auto rgbLinear = DeswizzleMorton(tiledBytes, width, height, 3U);
            for (size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4U + 0U] = rgbLinear[i * 3U + 0U];
                rgba[i * 4U + 1U] = rgbLinear[i * 3U + 1U];
                rgba[i * 4U + 2U] = rgbLinear[i * 3U + 2U];
                rgba[i * 4U + 3U] = 0xFFU;
            }
            break;
        }

        case TextureFormat::Rgb565: {
            auto raw16 = DeswizzleMorton(tiledBytes, width, height, 2U);
            for (size_t i = 0; i < pixelCount; ++i) {
                uint16_t val = static_cast<uint16_t>(raw16[i * 2U]) |
                               (static_cast<uint16_t>(raw16[i * 2U + 1U]) << 8U);
                rgba[i * 4U + 0U] = Expand5To8(static_cast<uint8_t>((val >> 11U) & 0x1FU));
                rgba[i * 4U + 1U] = Expand6To8(static_cast<uint8_t>((val >> 5U) & 0x3FU));
                rgba[i * 4U + 2U] = Expand5To8(static_cast<uint8_t>(val & 0x1FU));
                rgba[i * 4U + 3U] = 0xFFU;
            }
            break;
        }

        case TextureFormat::Rgba4: {
            auto raw16 = DeswizzleMorton(tiledBytes, width, height, 2U);
            for (size_t i = 0; i < pixelCount; ++i) {
                uint16_t val = static_cast<uint16_t>(raw16[i * 2U]) |
                               (static_cast<uint16_t>(raw16[i * 2U + 1U]) << 8U);
                rgba[i * 4U + 0U] = Expand4To8(static_cast<uint8_t>((val >> 12U) & 0x0FU));
                rgba[i * 4U + 1U] = Expand4To8(static_cast<uint8_t>((val >> 8U) & 0x0FU));
                rgba[i * 4U + 2U] = Expand4To8(static_cast<uint8_t>((val >> 4U) & 0x0FU));
                rgba[i * 4U + 3U] = Expand4To8(static_cast<uint8_t>(val & 0x0FU));
            }
            break;
        }

        case TextureFormat::Rgb5a1: {
            auto raw16 = DeswizzleMorton(tiledBytes, width, height, 2U);
            for (size_t i = 0; i < pixelCount; ++i) {
                uint16_t val = static_cast<uint16_t>(raw16[i * 2U]) |
                               (static_cast<uint16_t>(raw16[i * 2U + 1U]) << 8U);
                rgba[i * 4U + 0U] = Expand5To8(static_cast<uint8_t>((val >> 11U) & 0x1FU));
                rgba[i * 4U + 1U] = Expand5To8(static_cast<uint8_t>((val >> 6U) & 0x1FU));
                rgba[i * 4U + 2U] = Expand5To8(static_cast<uint8_t>((val >> 1U) & 0x1FU));
                rgba[i * 4U + 3U] = (val & 1U) ? 0xFFU : 0x00U;
            }
            break;
        }

        case TextureFormat::Ia8: {
            auto raw16 = DeswizzleMorton(tiledBytes, width, height, 2U);
            for (size_t i = 0; i < pixelCount; ++i) {
                const uint8_t alpha = raw16[i * 2U];
                const uint8_t lum = raw16[i * 2U + 1U];
                rgba[i * 4U + 0U] = lum;
                rgba[i * 4U + 1U] = lum;
                rgba[i * 4U + 2U] = lum;
                rgba[i * 4U + 3U] = alpha;
            }
            break;
        }

        case TextureFormat::I8: {
            auto raw8 = DeswizzleMorton(tiledBytes, width, height, 1U);
            for (size_t i = 0; i < pixelCount; ++i) {
                const uint8_t lum = raw8[i];
                rgba[i * 4U + 0U] = lum;
                rgba[i * 4U + 1U] = lum;
                rgba[i * 4U + 2U] = lum;
                rgba[i * 4U + 3U] = 0xFFU;
            }
            break;
        }

        case TextureFormat::A8: {
            auto raw8 = DeswizzleMorton(tiledBytes, width, height, 1U);
            for (size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4U + 0U] = 0xFFU;
                rgba[i * 4U + 1U] = 0xFFU;
                rgba[i * 4U + 2U] = 0xFFU;
                rgba[i * 4U + 3U] = raw8[i];
            }
            break;
        }

        case TextureFormat::Etc1:
        case TextureFormat::Etc1a4: {
            const bool hasAlpha = (format == TextureFormat::Etc1a4);
            const uint32_t subtileSize = hasAlpha ? 16U : 8U;
            const uint32_t tileSize = subtileSize * 4U;
            const uint32_t tilesX = std::max<uint32_t>(1U, (width + 7U) / 8U);

            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint32_t fineX = x % 8U;
                    const uint32_t fineY = y % 8U;
                    const uint32_t tileBase = ((y / 8U) * tilesX + (x / 8U)) * tileSize;
                    const uint32_t subtileIndex = fineX / 4U + 2U * (fineY / 4U);
                    const uint32_t subtileBase = tileBase + subtileIndex * subtileSize;
                    const uint32_t localX = fineX % 4U;
                    const uint32_t localY = fineY % 4U;

                    uint8_t alpha = 255U;
                    uint32_t etcBase = subtileBase;
                    if (hasAlpha && subtileBase + 8U <= tiledBytes.size()) {
                        uint64_t packedAlpha = 0;
                        for (size_t b = 0; b < 8U; ++b) {
                            packedAlpha |= static_cast<uint64_t>(tiledBytes[subtileBase + b]) << (b * 8U);
                        }
                        const uint32_t alphaNibble = static_cast<uint32_t>(
                            (packedAlpha >> (4U * (localX * 4U + localY))) & 0x0FU);
                        alpha = Expand4To8(static_cast<uint8_t>(alphaNibble));
                        etcBase += 8U;
                    }

                    if (etcBase + 8U <= tiledBytes.size()) {
                        uint64_t etcBlock = 0;
                        for (size_t b = 0; b < 8U; ++b) {
                            etcBlock |= static_cast<uint64_t>(tiledBytes[etcBase + b]) << (b * 8U);
                        }
                        const auto rgb = SampleEtc1Texel(etcBlock, localX, localY);
                        const size_t dst = (static_cast<size_t>(y) * width + x) * 4U;
                        rgba[dst + 0U] = rgb[0];
                        rgba[dst + 1U] = rgb[1];
                        rgba[dst + 2U] = rgb[2];
                        rgba[dst + 3U] = alpha;
                    }
                }
            }
            break;
        }

        default:
            // Fallback: white texture
            std::fill(rgba.begin(), rgba.end(), 0xFFU);
            break;
    }

    return rgba;
}

std::shared_ptr<const DecodedTexture> PicaTextureCache::AcquireTexture(
    const TextureCacheKey& key,
    std::span<const uint8_t> rawTiledBytes,
    bool preferNativeEtc2) {
    auto found = mEntries.find(key);
    if (found != mEntries.end()) {
        return found->second;
    }

    auto tex = std::make_shared<DecodedTexture>();
    tex->Width = key.Width;
    tex->Height = key.Height;

    if (preferNativeEtc2 && key.Format == TextureFormat::Etc1) {
        // Direct ETC1 block passthrough to native Adreno ETC2 sampler (WARN-05)
        tex->IsNativeEtc2 = true;
        tex->Data.assign(rawTiledBytes.begin(), rawTiledBytes.end());
    } else {
        tex->IsNativeEtc2 = false;
        tex->Data = DecodeToRgba8(key.Format, key.Width, key.Height, rawTiledBytes);
    }
    tex->ByteSize = tex->Data.size();

    EvictIfNecessary(tex->ByteSize);
    mEntries[key] = tex;
    mCurrentMemoryUsage += tex->ByteSize;
    return tex;
}

} // namespace ThreeDsRecomp::Pica
