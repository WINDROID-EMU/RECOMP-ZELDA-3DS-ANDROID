#include "three_ds_recomp/pica/pica_surface_cache.h"
#include "three_ds_recomp/pica/pica_texture_cache.h"

#include <algorithm>
#include <cstring>

namespace ThreeDsRecomp::Pica {

PicaSurfaceCache::PicaSurfaceCache(size_t maxMemoryBytes)
    : mMaxMemoryBytes(maxMemoryBytes) {}

void PicaSurfaceCache::Clear() {
    mSurfaces.clear();
    mCurrentMemoryUsage = 0;
}

void PicaSurfaceCache::EvictIfNecessary(size_t requiredBytes) {
    while (mCurrentMemoryUsage + requiredBytes > mMaxMemoryBytes && !mSurfaces.empty()) {
        auto it = mSurfaces.begin();
        mCurrentMemoryUsage -= it->second->Pixels.size();
        mSurfaces.erase(it);
    }
}

std::shared_ptr<RenderSurface> PicaSurfaceCache::GetOrCreateSurface(
    uint32_t physicalAddress, uint16_t width, uint16_t height, uint8_t format, bool isDepth) {
    SurfaceKey key{ physicalAddress, width, height, format };
    auto found = mSurfaces.find(key);
    if (found != mSurfaces.end()) {
        return found->second;
    }

    auto surface = std::make_shared<RenderSurface>();
    surface->PhysicalAddress = physicalAddress;
    surface->Width = width;
    surface->Height = height;
    surface->Format = format;
    surface->IsDepth = isDepth;

    // Bytes per pixel: typically 4 for RGBA8 / D24S8, 2 for RGB565 / D16
    const size_t bpp = (format == 0 || format == 1 || isDepth) ? 4U : 2U;
    const size_t byteSize = static_cast<size_t>(width) * height * bpp;
    surface->Pixels.resize(byteSize, 0U);

    EvictIfNecessary(byteSize);
    mSurfaces[key] = surface;
    mCurrentMemoryUsage += byteSize;
    return surface;
}

std::vector<uint8_t> PicaSurfaceCache::PerformDisplayTransfer(
    const RenderSurface& source,
    DisplayTransferRotation rotation,
    bool flipVertical) {
    const uint32_t w = source.Width;
    const uint32_t h = source.Height;
    const size_t bpp = 4U; // Standard RGBA8 output
    if (w == 0 || h == 0 || source.Pixels.empty()) {
        return {};
    }

    // First detile from Morton order
    std::vector<uint8_t> detiled = PicaTextureCache::DeswizzleMorton(source.Pixels, w, h, bpp);

    // If no transformation needed, return detiled directly
    if (rotation == DisplayTransferRotation::None && !flipVertical) {
        return detiled;
    }

    // Output dimensions after rotation
    const bool is90or270 = (rotation == DisplayTransferRotation::Rotate90 ||
                            rotation == DisplayTransferRotation::Rotate270);
    const uint32_t outW = is90or270 ? h : w;
    const uint32_t outH = is90or270 ? w : h;
    std::vector<uint8_t> transformed(static_cast<size_t>(outW) * outH * bpp);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t dstX = x;
            uint32_t dstY = flipVertical ? (h - 1U - y) : y;

            if (rotation == DisplayTransferRotation::Rotate90) {
                const uint32_t origX = dstX;
                dstX = h - 1U - dstY;
                dstY = origX;
            } else if (rotation == DisplayTransferRotation::Rotate180) {
                dstX = w - 1U - dstX;
                dstY = h - 1U - dstY;
            } else if (rotation == DisplayTransferRotation::Rotate270) {
                const uint32_t origX = dstX;
                dstX = dstY;
                dstY = w - 1U - origX;
            }

            const size_t srcIdx = (static_cast<size_t>(y) * w + x) * bpp;
            const size_t dstIdx = (static_cast<size_t>(dstY) * outW + dstX) * bpp;
            if (dstIdx + bpp <= transformed.size() && srcIdx + bpp <= detiled.size()) {
                std::memcpy(&transformed[dstIdx], &detiled[srcIdx], bpp);
            }
        }
    }

    return transformed;
}

bool PicaSurfaceCache::ReinterpretDepthAsColor(
    const RenderSurface& depthSource,
    std::vector<uint8_t>& outColorRgba8) {
    const size_t pixelCount = static_cast<size_t>(depthSource.Width) * depthSource.Height;
    if (pixelCount == 0 || depthSource.Pixels.empty()) {
        // Safe fallback: default depth buffer representation without crashing (Rule R12)
        const size_t fallbackPixels = pixelCount > 0 ? pixelCount : 1U;
        outColorRgba8.assign(fallbackPixels * 4U, 0xFFU);
        return false;
    }

    outColorRgba8.resize(pixelCount * 4U, 0xFFU);
    const size_t copyBytes = std::min(outColorRgba8.size(), depthSource.Pixels.size());
    std::memcpy(outColorRgba8.data(), depthSource.Pixels.data(), copyBytes);
    return true;
}

} // namespace ThreeDsRecomp::Pica
